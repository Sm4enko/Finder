#include "http_server.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <boost/locale.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <pqxx/pqxx>
#include <include/iconv.h>
#include <boost/regex.hpp>
#include "easylogging++.h"
#include<codecvt>

using boost::asio::ip::tcp;
using namespace std::this_thread; // sleep_for, sleep_until
using namespace std::chrono; // nanoseconds, system_clock, seconds

int threadNumber = -1;
std::string con_data = ""; // data will be input in start_http_server(data) from main.cpp 
std::mutex g_lock1;
boost::asio::io_service service;
typedef boost::shared_ptr<tcp::socket> socket_ptr;

std::unordered_map<std::string, std::string > readConfig(const std::string& filename) {
	std::unordered_map<std::string, std::string > settings;
	std::fstream file(filename);
	if (!file.is_open()) {
		throw std::runtime_error("Failed to open " + filename);
	}
	std::string  line;
	while (std::getline(file, line)) {
		size_t delimiterPos = line.find('=');
		if (delimiterPos != std::string::npos) {
			std::string  key = line.substr(0, delimiterPos);
			std::string  value = line.substr(delimiterPos + 1);
			key.erase(0, key.find_first_not_of(' '));
			key.erase(key.find_last_not_of(' ') + 1);
			value.erase(0, value.find_first_not_of(' '));
			value.erase(value.find_last_not_of(' ') + 1);
			settings[key] = value;
		}
	}
	file.close();
	if (settings.empty()) {
		throw std::runtime_error("Error (NOT READ) " + filename + " or file is empty.");
	}
	return settings;
}

static std::vector<std::string> parse_search_query(std::string& query) {
	size_t start;
	size_t end = 0;
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	std::vector<std::string> searchWords;
	boost::regex spec("( +)"); // удалим из запроса символы, которых точно нет в базе
	query = boost::regex_replace(query, spec, " ");
	std::wstring wstr2 = L"";
	std::wstring wstr = converter.from_bytes(query);
	for (auto c : wstr) {// кирилица В.,н.регистр	латиница В.регистр	  латиница н.регистр		ё				Ё			пробел			цифры
		bool condition = (c > 1039 && c < 1104) || (c > 64 && c < 91) || (c > 96 && c < 123) || (c == 1105) || (c == 1105) || (c == 32) || (c > 47 && c < 58);
		if (condition) {
			if ((c > 1039 && c < 1072) || (c > 64 && c < 91)) { // LOWERCASE
				c += 32;
			}
			wstr2 += c;
		}
	}
	query = converter.to_bytes(wstr2);
	while ((start = query.find_first_not_of(" ", end)) != std::string::npos)
	{
		end = query.find(" ", start);
		searchWords.push_back(query.substr(start, end - start));
	}
	return searchWords;
}

static std::string make_response(const std::string request) {
	std::string response;
	std::string query = request;
	std::vector<std::string> searchWords = parse_search_query(query);

	std::string result_html = "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"UTF-8\"><title>Search Results</title></head><body>";
	result_html += "<h1>Search Results</h1>";
	result_html += "<form id = \"searchForm\" action = \"/\" method = \"POST\" onSubmit = \"SendForm()\" enctype = \"multipart/form-data\" accept-charset = \"UTF-8\" >";
	result_html += "<input type=\"text\" name=\"query\" id=\"searchQuery\" value=\"" + query + "\">";
	result_html += "<button type=\"submit\">Search</button>";
	result_html += "</form> <div id=\"pleaseWait\" style=\"display: none; \">Please Wait</div>";
	result_html += "<div id=\"results\">";

	pqxx::connection conn(con_data); //con_data is initialiaze in start_http_server(data) calling from main.cpp before call make_response(rus_words)
	pqxx::work txn(conn);
	std::string queryPG = "SELECT SUM(wo.frequency) AS sm, p.url, string_agg(w.word, ', ') AS st FROM word_occurrences wo JOIN words w ON wo.word_id = w.id JOIN pages p ON wo.page_id = p.id WHERE w.word = '";
	for (auto& word : searchWords) {
		queryPG += word;
		if (word != searchWords[searchWords.size() - 1]) {
			queryPG += "' OR w.word = '";
		}
	}
	queryPG += "' GROUP by p.url ORDER BY sm DESC";
	pqxx::result result = txn.exec(queryPG);
	if (result.empty()) {
		result_html += "<p>No results found</p>";
	}
	else {
		result_html += "<table border=\"1\"><tr><th>Word</th><th>URL</th><th>Frequency</th></tr>";
		for (const auto& row : result) {
			result_html += "<tr>";
			result_html += "<td>" + std::string(row["st"].c_str()) + "</td>";
			result_html += "<td><a href=\"" + std::string(row["url"].c_str()) + "\">" + std::string(row["url"].c_str()) + "</a></td>";
			result_html += "<td>" + std::to_string(row["sm"].as<int>()) + "</td>";
			result_html += "</tr>";
		}
		result_html += "</table>";
		result_html += "<div id=\"pleaseWait\" style=\"display: none;\">Please Wait</div>";
	}
	result_html += "</div><script>const form = document.getElementById('searchForm');form.addEventListener('submit', function(event) {event.preventDefault();}); async function SendForm(e) {\ndocument.getElementById('pleaseWait').style.display='block';form.submit();\n};</script></body></html>";

	std::string content_length = std::to_string(result_html.length());

	response = "HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=UTF-8\r\n"
		"Content-Length: " + content_length + "\r\n"
		"Connection: close\r\n\r\n" + result_html;

	return response;
}

static void handle_request(tcp::socket& socket) {
	threadNumber++;
	std::string out_str;
	out_str = "New request thread# " + std::to_string(threadNumber) + "\n";
	g_lock1.lock();
	LOG(INFO) << out_str;
	g_lock1.unlock();
	boost::asio::streambuf buffer;
	boost::system::error_code error;
	boost::asio::read_until(socket, buffer, "\r\n\r\n", error);
	if (error && error != boost::asio::error::eof) {
		LOG(ERROR) << "Read error: " << error.message() << std::endl;
		out_str = "Request thread# " + std::to_string(threadNumber) + " Closed \n";
		g_lock1.lock();
		LOG(INFO) << "NO Request found\n";
		LOG(INFO) << out_str;
		g_lock1.unlock();
		return;
	}
	std::istream request_stream(&buffer);
	std::string request;
	std::getline(request_stream, request);
	std::string response;
	if (request.find("GET /") != std::string::npos) {
		std::cout << " Request Type GET " << "\n";
		std::ifstream file("C:/Finder-main/Finder/index.html");
		if (file) {
			std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nConnection: close\r\n\r\n" + content;
		}
		else {
			response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain; charset=UTF-8\r\nConnection: close\r\n\r\nFile not found!";
		}
	}

	if (request.find("POST /") != std::string::npos) {
		LOG(INFO) << " Request Type POST ";
		std::string const parsed_content(std::istreambuf_iterator<char>{request_stream}, {});
		std::string user_words = parsed_content.substr(parsed_content.find("query") + 10);
		user_words = user_words.substr(0, user_words.find("\r\n") + 2);
		LOG(INFO) << "Parsed search words: " << std::quoted(user_words);
		response = make_response(user_words);
	}

	boost::asio::write(socket, boost::asio::buffer(response), error);
	if (error) {
		LOG(ERROR) << "Write error: " << error.message() << std::endl;
	}
	socket.close();
	out_str = "Request thread# " + std::to_string(threadNumber) + " Closed";
	g_lock1.lock();
	LOG(INFO) << out_str;
	g_lock1.unlock();
	return;
}

tcp::endpoint ep(tcp::v4(), 8080); 
tcp::acceptor acc(service, ep);

static void handle_accept(boost::shared_ptr<tcp::socket> sock, const boost::system::error_code& err)
{
	if (err) return;
	handle_request(*sock);
	socket_ptr sock1(new tcp::socket(service));
	start_accept(sock1);
}

void start_accept(boost::shared_ptr<tcp::socket> sock)
{
	acc.async_accept(*sock, boost::bind(handle_accept, sock, std::placeholders::_1));
}

void start_http_server(std::string& dt) {
	con_data = dt;
	socket_ptr sock(new tcp::socket(service));
	start_accept(sock);
	service.run();
}

