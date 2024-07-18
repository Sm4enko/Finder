#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <thread> 
#include <boost/locale.hpp>
#include <boost/asio.hpp>
#include "Table.h"
#include "Spider.h"
#include <pqxx/pqxx>
#include <Windows.h>

using boost::asio::ip::tcp;

std::string make_response(const std::string& request) {
    std::string response;

    if (request.find("GET") == 0) {
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nWelcome, GET request!";
    } else if (request.find("POST") == 0) {
     
        size_t content_start = request.find("\r\n\r\n");
        std::string body = request.substr(content_start + 4);

      
        std::istringstream body_stream(body);
        std::string url;
        std::getline(body_stream, url);
        std::string myStr;

        if (!url.empty()) {
                       std::vector<std::string> searchWords;
            int depth = 2;
            crawl_page(url, searchWords, depth, myStr);

            response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nURL processed: " + url;
        } else {
            response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nNo URL provided!";
        }
    } else {
        response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nUnknown request!";
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

    std::vector<std::string> searchWords;
    std::string inputSentence;
    std::cout << "Enter search words (e.g., 'white bread', 'red hat', etc.): ";
    std::getline(std::cin, inputSentence);

    std::istringstream iss(inputSentence);
    std::string word;
    while (iss >> word) {
        searchWords.push_back(word);
    }

    if (searchWords.size() < 1 || searchWords.size() > 32) {
        std::cout << "Only 1 to 32 words." << std::endl;
        return 1;
    }

    std::thread server_thread(start_http_server);
    server_thread.detach();
   
    crawl_page(Page, searchWords, Deep, data);

    retrieve_data();

    std::cout << "Press enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}