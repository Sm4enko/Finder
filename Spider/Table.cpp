#include "Table.h"
#include <pqxx/pqxx>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include "easylogging++.h"

std::unordered_map<std::string, std::string > readConfig(const std::string& filename) {
	std::unordered_map<std::string, std::string > settings;
	std::fstream file(filename);
	if (!file.is_open()) {
		throw std::runtime_error("Failed to open " + filename);
	}
	std::string  line;
	while (std::getline(file, line)) {
		size_t delimiterPos = line.find('=');
		if (delimiterPos != std::string::npos) {
			std::string  key = line.substr(0, delimiterPos);
			std::string  value = line.substr(delimiterPos + 1);
			key.erase(0, key.find_first_not_of(' '));
			key.erase(key.find_last_not_of(' ') + 1);
			value.erase(0, value.find_first_not_of(' '));
			value.erase(value.find_last_not_of(' ') + 1);
			settings[key] = value;
		}
	}
	file.close();
	if (settings.empty()) {
		throw std::runtime_error("Error (NOT READ) " + filename + " or file is empty.");
	}
	return settings;
}

void create_table(std::string conn_data) {
	try {
		pqxx::connection conn(conn_data);
		pqxx::work txn(conn);
		pqxx::result result;
		result = txn.exec("SELECT COUNT(*) FROM pg_class WHERE relname = 'words'");
		int i = result[0][0].as<int>();
		if (i!=1) {
			txn.exec("CREATE TABLE IF NOT EXISTS words (id SERIAL PRIMARY KEY, word TEXT UNIQUE)");
			txn.exec("CREATE TABLE IF NOT EXISTS pages (id SERIAL PRIMARY KEY, url TEXT UNIQUE)");
			txn.exec("CREATE TABLE IF NOT EXISTS word_occurrences (word_id INT REFERENCES words(id), page_id INT REFERENCES pages(id), frequency INT, PRIMARY KEY (word_id, page_id))");
			txn.commit();
			conn.close();
			LOG(INFO) << "Tables created successfully.";
		}
		else {
			LOG(INFO) << "Tables allready exist.";
		}
	}
	catch (const std::exception& e) {
		LOG(ERROR) << "Error: " << e.what();
	}
}

