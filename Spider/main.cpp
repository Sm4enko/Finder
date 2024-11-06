#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <thread>
#include <boost/thread.hpp>
#include <boost/locale.hpp>
#include <boost/asio.hpp>
#include <pqxx/pqxx>
#include <Windows.h>
#include "Table.h"
#include "Spider.h"
#include <fcntl.h>
#include <io.h>
#include "easylogging++.h"
INITIALIZE_EASYLOGGINGPP

int main() {
	std::string data = "";
	setlocale(LC_ALL, "ru_RU.UTF-8");//setlocale(LC_ALL, "");ru_RU.UTF-8  Russian  Russian_Russia.1251 ru_RU.cp1251 Russian_Russia.866
	std::unordered_map<std::string, std::string> settings;
	try {
		settings = readConfig("C:/Finder-main/config.ini");
	}
	catch (const std::exception& e) {
		LOG(ERROR) << "Ошибка при чтении конфигурации: " << std::string(e.what());
		return 1;
	}
	std::string host = settings["Host"];
	int port = std::stoi(settings["Port"]);
	std::string database = settings["Database"];
	std::string username = settings["Username"];
	std::string password = settings["Password"];
	std::string startPage = settings["StartPage"];
	int recursionDepth = std::stoi(settings["RecursionDepth"]);

	LOG(INFO) << "Settings loaded successfully:";
	LOG(INFO) << "Host: " << host.c_str();
	LOG(INFO) << "Port: " << port;
	LOG(INFO) << "Database: " << database.c_str();
	LOG(INFO) << "Username: " << username.c_str();
	LOG(INFO) << "StartPage: " << startPage.c_str();
	LOG(INFO) << "RecursionDepth: " << recursionDepth;

	std::string cs = "dbname=" + database + " user=" + username + " password=" + password + " host=" + host + " port=" + std::to_string(port);
	Spider spider;
	// initialize data for Spider instance
	spider.connection_data = cs;
	spider.task_list[startPage] = recursionDepth; // initialize task_list	

	create_table(spider.connection_data);
	spider.worker();
	LOG(INFO) << "";
	LOG(INFO) << "Programm stop work";
	return 0;
}
