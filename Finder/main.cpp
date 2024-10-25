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

std::unordered_map<std::string, std::string> sets;
std::string data="";
void run_server() {
    start_http_server(data);
}

int main() {   
    setlocale(LC_ALL, "Russian");
    try {
        sets = readConfig("C:/SearchMachine/config.ini");
        std::cout << "Settings loaded successfully:" << std::endl;
        std::cout << "Host: " << sets["Host"] << std::endl;
        std::cout << "Port: " << sets["Port"] << std::endl;
        std::cout << "Database: " << sets["Database"] << std::endl;
        std::cout << "Username: " << sets["Username"] << std::endl;
        std::cout << "Username: " << sets["Password"] << std::endl;
        std::cout << "StartPage: " << sets["StartPage"] << std::endl;
        std::cout << "RecursionDepth: " << sets["RecursionDepth"] << std::endl;
        std::cout << "Проверка локали.. "  << std::endl;
        data += "dbname=" + sets["Database"] + " user=" + sets["Username"] + " password=" + sets["Password"] + " host=" + sets["Host"] + " port=" + sets["Port"];
        mySettings(data);
        create_table();
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
