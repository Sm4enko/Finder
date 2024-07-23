#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <thread>
#include <boost/locale.hpp>
#include <boost/asio.hpp>
#include <pqxx/pqxx>
#include <Windows.h>
#include <fstream>
#include "Table.h"
#include "Spider.h"

using boost::asio::ip::tcp;

struct SearchResult {
    std::string url;
    std::string word;
    int frequency;
};

std::string to_utf8(const std::string& input) {
    std::wstring wide_string = boost::locale::conv::to_utf<wchar_t>(input, "UTF-8");
 
    return boost::locale::conv::from_utf(wide_string, "UTF-8");
}

std::vector<SearchResult> sort_results_by_relevance(const std::vector<SearchResult>& results) {
    std::vector<SearchResult> sorted_results = results;
    std::sort(sorted_results.begin(), sorted_results.end(), [](const SearchResult& a, const SearchResult& b) {
        return a.frequency > b.frequency;
    });
    return sorted_results;
}

std::string make_html_response(const std::vector<SearchResult>& results) {
    std::string html_response = "<html><head><title>Search Results</title><meta charset=\"UTF-8\"></head><body><h1>Search Results</h1>";
    html_response += "<style>table {border-collapse: collapse; width: 100%;} th, td {border: 1px solid black; padding: 8px;} th {text-align: left;}</style>";
    html_response += "<table><tr><th>URL</th><th>Frequency</th><th>Word</th></tr>";

    if (results.empty()) {
        html_response += "<tr><td colspan='3'>Результатов не найдено</td></tr>";
    } else {
        size_t count = 0;
        for (const auto& sr : results) {
            if (count >= 10) break;
            html_response += "<tr>";
            html_response += "<td><a href=\"" + sr.url + "\">" + sr.url + "</a></td>";
            html_response += "<td>" + std::to_string(sr.frequency) + "</td>";
            html_response += "<td>" + sr.word + "</td>";
            html_response += "</tr>";
            ++count;
        }
    }

    html_response += "</table></body></html>";
    return html_response;
}

std::string make_response(const std::string& request) {
    std::string response;

    if (request.find("GET /search") == 0) {
        size_t pos = request.find("query=");
        if (pos != std::string::npos) {
            std::string query = request.substr(pos + 6);
            query = query.substr(0, query.find(' '));
            std::vector<std::string> searchWords;
            std::istringstream iss(query);
            std::string word;
            while (iss >> word) {
                searchWords.push_back(word);
            }
            if (searchWords.size() < 1 || searchWords.size() > 32) {
                response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nOnly 1 to 32 words allowed.";
            } else {
                std::unordered_map<std::string, std::string> settings = readConfig("K:/Temp/New/config.ini");
                std::string url = settings["StartPage"];
                int depth = std::stoi(settings["RecursionDepth"]);
                std::string data = "dbname=" + settings["Database"] + " user=" + settings["Username"] +
                                   " password=" + settings["Password"] + " host=" + settings["Host"] +
                                   " port=" + settings["Port"];
                mySettings(data);
                crawl_page(url, searchWords, depth, data);

                response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                pqxx::connection conn(data);
                pqxx::work txn(conn);
                
                std::stringstream sql;
                sql << "SELECT w.word, p.url, wo.frequency "
                    << "FROM word_occurrences wo "
                    << "JOIN words w ON wo.word_id = w.id "
                    << "JOIN pages p ON wo.page_id = p.id "
                    << "WHERE w.word IN (";
                
                for (size_t i = 0; i < searchWords.size(); ++i) {
                    sql << txn.quote(searchWords[i]);
                    if (i < searchWords.size() - 1) {
                        sql << ", ";
                    }
                }
                sql << ")";

                pqxx::result result = txn.exec(sql.str());

                std::vector<SearchResult> results;
                for (const auto& row : result) {
                    SearchResult sr;
                    sr.url = to_utf8(row["url"].as<std::string>());
                    sr.word = to_utf8(row["word"].as<std::string>());
                    sr.frequency = row["frequency"].as<int>();
                    results.push_back(sr);
                }

                results = sort_results_by_relevance(results);

                response += make_html_response(results);
            }
        } else {
            response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nNo query provided.";
        }
    } else {
        response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nUnknown request.";
    }

    return response;
}

void handle_request(tcp::socket& socket) {
    boost::asio::streambuf buffer;
    boost::system::error_code error;

    boost::asio::read_until(socket, buffer, "\r\n\r\n", error);
    if (error && error != boost::asio::error::eof) {
        std::cerr << "Read error: " << error.message() << std::endl;
        return;
    }

    std::istream request_stream(&buffer);
    std::string request;
    std::getline(request_stream, request);

    std::string response = make_response(request);
    boost::asio::write(socket, boost::asio::buffer(response), error);
    if (error) {
        std::cerr << "Write error: " << error.message() << std::endl;
    }
}

void start_http_server() {
    try {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 8080));

        while (true) {
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            handle_request(socket);
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

int main() {
    std::string Page;
    int Deep;
    std::string data;

    try {
        std::unordered_map<std::string, std::string> settings = readConfig("D:/Finder/config.ini");
        std::string host = settings["Host"];
        int port = std::stoi(settings["Port"]);
        std::string database = settings["Database"];
        std::string username = settings["Username"];
        std::string password = settings["Password"];
        Page = settings["StartPage"];
        Deep = std::stoi(settings["RecursionDepth"]);
        data += "dbname=" + database + " user=" + username + " password=" + password + " host=" + host + " port=" + std::to_string(port);
        mySettings(data);
        std::cout << "Settings loaded successfully:" << std::endl;
        std::cout << "Host: " << host << std::endl;
        std::cout << "Port: " << port << std::endl;
        std::cout << "Database: " << database << std::endl;
        std::cout << "Username: " << username << std::endl;
        std::cout << "StartPage: " << Page << std::endl;
        std::cout << "RecursionDepth: " << Deep << std::endl;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    boost::locale::generator gen;
    std::locale::global(gen("en_US.UTF-8"));

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    std::ios_base::sync_with_stdio(false);

    create_table();

    std::thread server_thread(start_http_server);
    server_thread.detach();

    std::string command = "start \"\" \"K:/Temp/New/index.html\"";
    system(command.c_str());

    std::cout << "Server started. Open the HTML file and enter search words." << std::endl;
    std::cout << "Press enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
