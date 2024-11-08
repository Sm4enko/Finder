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
#include "http_server.h"
#include "easylogging++.h"
INITIALIZE_EASYLOGGINGPP

std::unordered_map<std::string, std::string> sets;
std::string data = "";
static void run_server() {
	start_http_server(data);
}

int main() {
	setlocale(LC_ALL, "ru_RU.UTF-8");
	std::unordered_map<std::string, std::string> sets;
	try {
		sets = readConfig("C:/Finder-main/config.ini");
	}
	catch (const std::exception& e) {
		LOG(ERROR) << "Ошибка при чтении конфигурации: " << std::string(e.what());
		return 1;
	}
	LOG(INFO) << "Settings loaded successfully:";
	LOG(INFO) << "Host: " << sets["Host"];
	LOG(INFO) << "Port: " << sets["Port"];
	LOG(INFO) << "Database: " << sets["Database"];
	LOG(INFO) << "Username: " << sets["Username"];
	LOG(INFO) << "Username: " << sets["Password"];
	LOG(INFO) << "StartPage: " << sets["StartPage"];
	LOG(INFO) << "RecursionDepth: " << sets["RecursionDepth"];
	data += "dbname=" + sets["Database"] + " user=" + sets["Username"] + " password=" + sets["Password"] + " host=" + sets["Host"] + " port=" + sets["Port"];

	std::thread server_thread(run_server);
	server_thread.detach();
	LOG(INFO) << "Welcome http://localhost:8080";

	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	return 0;
}
