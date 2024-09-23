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
#include "Table.h"
#include "http_server.h"

void run_server() {
    start_http_server();
}

int main() {
    std::string data;
    setlocale(LC_ALL, "Russian");
    try {
        std::unordered_map<std::string, std::string> settings = readConfig("D:/Finder/C++/SearchMachine/config.ini");
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
        std::cout << "Проверка локали.. "  << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    std::thread server_thread(run_server);
    server_thread.detach();
    std::cout << "Welcome http://localhost:8080" << std::endl;

    while (true) {

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
