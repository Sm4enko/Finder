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
#include <Windows.h>
#include "Table.h"
#include <mutex>
#include <include/iconv.h>

using namespace std::this_thread; // sleep_for, sleep_until
using namespace std::chrono; // nanoseconds, system_clock, seconds
using boost::asio::ip::tcp;
std::mutex g_lock1;

std::vector<std::string> parse_search_query(const std::string& query) {
	size_t start;
	size_t end = 0;
	std::vector<std::string> searchWords;
	while ((start = query.find_first_not_of(" ", end)) != std::string::npos)
	{
		end = query.find(" ", start);
		searchWords.push_back(query.substr(start, end - start));
	}
	return searchWords;
}

std::unordered_map<std::string, std::string> settings;
std::string make_response(const std::string request) {
	std::string response;
	std::string query = request;
	std::vector<std::string> searchWords =  parse_search_query(query);
	std::string data;
	std::string host = settings["Host"];
	int port = std::stoi(settings["Port"]);
	std::string database = settings["Database"];
	std::string username = settings["Username"];
	std::string password = settings["Password"];
	data += "dbname=" + database + " user=" + username + " password=" + password + " host=" + host + " port=" + std::to_string(port);
	create_table();
	std::string result_html = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"Windows-1251\"><title>Search Results</title></head><body>";
	result_html += "<h1>Search Results</h1>";
	result_html += "<form action=\"/search\" method=\"POST\">";
	result_html += "<input type=\"text\" name=\"query\" value=\"" + query + "\">";
	result_html += "<button type=\"submit\">Search</button>";
	result_html += "</form>";
	result_html += "<div id=\"results\">";

	pqxx::connection conn(data);
	pqxx::work txn(conn);
	std::string queryPG = "SELECT SUM(wo.frequency) AS sm, p.url, string_agg(w.word, ', ') AS st FROM word_occurrences wo JOIN words w ON wo.word_id = w.id JOIN pages p ON wo.page_id = p.id WHERE w.word = '";
	for (auto word : searchWords) {
		queryPG += word;
		
		if (word != searchWords[searchWords.size()-1]) {
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
	}
	result_html += "</div></body></html>";
	response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=Windows-1251\r\n\r\n" + result_html;
	return response;
}
int threadNumber = -1;


char* convert(const char* s, const char* from_cp, const char* to_cp)
{
	iconv_t ic = iconv_open(to_cp, from_cp);
	if (ic == (iconv_t)(-1)) {
		fprintf(stderr, "iconv: cannot convert from %s to %s\n", from_cp, to_cp);
		char* ch = nullptr;
		return ch;
	}
	char* out_buf = (char*)calloc(strlen(s) + 1, 1);
	char* out = out_buf;
	char* in = (char*)malloc(strlen(s) + 1);
	strcpy(in, s);
	size_t in_ln = strlen(s);
	size_t out_ln = in_ln;
	size_t k = iconv(ic, &in, &in_ln, &out, &out_ln);
	if (k == (size_t)-1)
		fprintf(stderr, "iconv: %u of %d converted\n", k, strlen(s));
	if (errno != 0)
		fprintf(stderr, "iconv: %s\n", strerror(errno));
	iconv_close(ic);
	out_buf[strlen(out_buf) - 2] = 0;
	return out_buf;
}

void handle_request(tcp::socket &socket) {
	threadNumber++;
	std::string out_str;
	out_str = "New request thread# " + std::to_string(threadNumber) + "\n";
	g_lock1.lock();
	std::cout << out_str;
	g_lock1.unlock();

	boost::asio::streambuf buffer;
	boost::system::error_code error;
	boost::asio::read_until(socket, buffer, "\r\n\r\n", error);
	if (error && error != boost::asio::error::eof) {
		std::cerr << "Read error: " << error.message() << std::endl;
		out_str = "Request thread# " + std::to_string(threadNumber) + " Closed \n";
		g_lock1.lock();
		std::cout << "NO Request found" << "\n";
		std::cout << out_str;
		g_lock1.unlock();
		return;
	}
	std::istream request_stream(&buffer);
	std::string request;
	std::getline(request_stream, request);
	std::string response;
	if (request.find("GET /") != std::string::npos) {
		std::cout << " Request Type GET " << "\n";
		std::ifstream file("D:/Finder/C++/SearchMachine/Finder/index.html");
		if (file) {
			std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=Windows-1251\r\n\r\n" + content;
		}
		else {
			response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain; Windows-1251\r\n\r\nFile not found!";
		}
	}

	if (request.find("POST /search") != std::string::npos) {
		std::cout << " Request Type POST " << "\n";
		std::string const parsed_content (std::istreambuf_iterator<char>{request_stream}, {});
		std::string user_words = parsed_content.substr(parsed_content.find("query") + 10);
		user_words = user_words.substr(0,user_words.find("\r\n")+2);
		std::wstring str;
		const char* c = user_words.c_str();
		c = convert(c, "utf-8", "cp1251");//cp1251
		std::string rus_words = c;
		std::cout << "Parsed search words: " << std::quoted(rus_words) << "\n";
		response = make_response(rus_words);
	}

	boost::asio::write(socket, boost::asio::buffer(response), error);
	if (error) {
		std::cerr << "Write error: " << error.message() << std::endl;
	}
	socket.close();
	out_str = "Request thread# " + std::to_string(threadNumber) + " Closed\n";
	g_lock1.lock();
	std::cout << out_str;
	g_lock1.unlock();
	return;
}

typedef boost::shared_ptr<tcp::socket> socket_ptr;
boost::asio::io_service service;
tcp::endpoint ep(tcp::v4(), 8080); // listen on 2001
tcp::acceptor acc(service, ep);

void handle_accept(boost::shared_ptr<tcp::socket> sock, const boost::system::error_code& err)
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

void start_http_server() {
	settings = readConfig("D:/Finder/C++/SearchMachine/config.ini");
	socket_ptr sock(new tcp::socket(service));
	start_accept(sock);
	service.run();
}


