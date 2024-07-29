#include <iostream>
#include <string>
#include <unordered_map>
#include <thread>
#include "http_server.h"
#include "Table.h"
#include "Spider.h"

int main() {
    std::string data;
    try {
        std::unordered_map<std::string, std::string> settings = readConfig("K:/Finder/config.ini");
        std::string host = settings["Host"];
        int port = std::stoi(settings["Port"]);
        std::string database = settings["Database"];
        std::string username = settings["Username"];
        std::string password = settings["Password"];
        std::string startPage = settings["StartPage"];
        int recursionDepth = std::stoi(settings["RecursionDepth"]);
        data += "dbname=" + database + " user=" + username + " password=" + password + " host=" + host + " port=" + std::to_string(port);
        mySettings(data);
        std::cout << "Settings loaded successfully:" << std::endl;
        std::cout << "Host: " << host << std::endl;
        std::cout << "Port: " << port << std::endl;
        std::cout << "Database: " << database << std::endl;
        std::cout << "Username: " << username << std::endl;
        std::cout << "StartPage: " << startPage << std::endl;
        std::cout << "RecursionDepth: " << recursionDepth << std::endl;

     
        std::thread server_thread(start_http_server);
        server_thread.detach();

   
        crawl_page(startPage, {}, recursionDepth, data);

      
        std::this_thread::sleep_for(std::chrono::minutes(10));

    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
