#include "http_server.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <boost/locale.hpp>
#include <boost/asio.hpp>
#include <pqxx/pqxx>
#include <Windows.h>
#include "Table.h"
#include "Spider.h"

using boost::asio::ip::tcp;

std::vector<std::string> parse_search_query(const std::string& query) {
    std::vector<std::string> searchWords;
    std::istringstream iss(query);
    std::string word;
    while (iss >> word) {
        searchWords.push_back(word);
    }
    std::sort(searchWords.begin(), searchWords.end());
    return searchWords;
}

std::string make_response(const std::string& request) {
    std::string response;

    if (request.find("GET / ") != std::string::npos) {
   
        std::ifstream file("D:/Finder/index.html");
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n" + content;
        } else {
            response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nFile not found!";
        }
    } else if (request.find("POST /search") == 0) {
   
        size_t content_start = request.find("\r\n\r\n");
        std::string body = request.substr(content_start + 4);

        std::unordered_map<std::string, std::string> form_data;
        std::istringstream body_stream(body);
        std::string line;
        while (std::getline(body_stream, line, '&')) {
            size_t equal_pos = line.find('=');
            if (equal_pos != std::string::npos) {
                std::string key = line.substr(0, equal_pos);
                std::string value = line.substr(equal_pos + 1);
                form_data[key] = value;
            }
        }

        std::string query = form_data["query"];
        std::vector<std::string> searchWords = parse_search_query(query);

        std::string data;
        std::unordered_map<std::string, std::string> settings = readConfig("D:/Finder/config.ini");
        std::string host = settings["Host"];
        int port = std::stoi(settings["Port"]);
        std::string database = settings["Database"];
        std::string username = settings["Username"];
        std::string password = settings["Password"];
        data += "dbname=" + database + " user=" + username + " password=" + password + " host=" + host + " port=" + std::to_string(port);

        create_table();
        crawl_page(settings["StartPage"], searchWords, std::stoi(settings["RecursionDepth"]), data);

     
        std::string result_html = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>Search Results</title></head><body>";
        result_html += "<h1>Search Results</h1>";
        result_html += "<form action=\"/search\" method=\"POST\">";
        result_html += "<input type=\"text\" name=\"query\" value=\"" + query + "\">";
        result_html += "<button type=\"submit\">Search</button>";
        result_html += "</form>";
        result_html += "<div id=\"results\">";

        pqxx::connection conn(data);
        pqxx::work txn(conn);
        pqxx::result result = txn.exec(
            "SELECT w.word, p.url, wo.frequency "
            "FROM word_occurrences wo "
            "JOIN words w ON wo.word_id = w.id "
            "JOIN pages p ON wo.page_id = p.id "
            "ORDER BY wo.frequency DESC"
        );

        if (result.empty()) {
            result_html += "<p>No results found</p>";
        } else {
            result_html += "<table border=\"1\"><tr><th>Word</th><th>URL</th><th>Frequency</th></tr>";
            for (const auto& row : result) {
                result_html += "<tr>";
                result_html += "<td>" + std::string(row["word"].c_str()) + "</td>";
                result_html += "<td><a href=\"" + std::string(row["url"].c_str()) + "\">" + std::string(row["url"].c_str()) + "</a></td>";
                result_html += "<td>" + std::to_string(row["frequency"].as<int>()) + "</td>";
                result_html += "</tr>";
            }
            result_html += "</table>";
        }
        result_html += "</div></body></html>";

        response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n" + result_html;
    } else {
        response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nUnknown request!";
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
