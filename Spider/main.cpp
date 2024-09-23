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
#include "Spider.cpp"


void main() {
	std::string data;
	setlocale(LC_ALL, "Russian_Russia.UTF-8");//setlocale(LC_ALL, "");ru_RU.UTF-8  Russian  Russian_Russia.1251 ru_RU.cp1251 Russian_Russia.866

	/*boost::locale::generator gen;
	std::locale loc = gen("Russian_Russia.1251");
	std::locale::global(loc);*/
	
	std::unordered_map<std::string, std::string> settings = readConfig("D:/Finder/C++/SearchMachine/config.ini");
	std::string host = settings["Host"];
	int port = std::stoi(settings["Port"]);
	std::string database = settings["Database"];
	std::string username = settings["Username"];
	std::string password = settings["Password"];
	std::string startPage = settings["StartPage"];
	int recursionDepth = std::stoi(settings["RecursionDepth"]);
	
	
	std::cout << "Settings loaded successfully:" << std::endl;
	std::cout << "Host: " << host << std::endl;
	std::cout << "Port: " << port << std::endl;
	std::cout << "Database: " << database << std::endl;
	std::cout << "Username: " << username << std::endl;
	std::cout << "StartPage: " << startPage << std::endl;
	std::cout << "RecursionDepth: " << recursionDepth << std::endl;
	std::wcout << L"Проверка локали: " << std::endl;
	data += "dbname=" + database + " user=" + username + " password=" + password + " host=" + host + " port=" + std::to_string(port);
	mySettings(data);
	create_table();

	Spider spider;
	spider.guard.addThread(std::thread([&spider, &startPage, &recursionDepth, &data] {
			spider.crawl_page(startPage, recursionDepth, data, 0); 
		}),startPage);
	spider.guard.runTask(spider.ids);
	spider.guard.stop(); // Останавливаем функцию runTask
	std::cout << "Сканирование дерева сайтов завершено" << std::endl;
	exit(0);
}
