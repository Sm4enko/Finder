
#include "Table.h"
#include <pqxx/pqxx>
#include <iostream>
#include <fstream>
#include <stdexcept>


std::string New;

void mySettings(const std::string  mySettings) {
    New = mySettings;
}
std::unordered_map<std::string, std::string > readConfig(const std::string& filename) {
    std::unordered_map<std::string, std::string > settings;
    std::fstream file(filename);
 
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open " + filename);
    }
    std::string  line;
    while (std::getline(file, line)) {
        size_t delimiterPos = line.find('=');
        if (delimiterPos != std::wstring::npos) {
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

std::string getConnectionString() {
        std::string conn_str = New;
    return conn_str;
}

void create_table() {
    try {

        pqxx::connection conn(getConnectionString());
        pqxx::work txn(conn);
        txn.exec("CREATE TABLE IF NOT EXISTS words (id SERIAL PRIMARY KEY, word TEXT UNIQUE)");
        txn.exec("CREATE TABLE IF NOT EXISTS pages (id SERIAL PRIMARY KEY, url TEXT UNIQUE)");
        txn.exec("CREATE TABLE IF NOT EXISTS word_occurrences (word_id INT REFERENCES words(id), page_id INT REFERENCES pages(id), frequency INT, PRIMARY KEY (word_id, page_id))");
        txn.commit();
        std::cout << "Tables created successfully." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

void retrieve_data() {
    try {;
        pqxx::connection conn(getConnectionString());
        pqxx::work txn(conn);
        pqxx::result result = txn.exec(
            "SELECT w.word, p.url, wo.frequency "
            "FROM word_occurrences wo "
            "JOIN words w ON wo.word_id = w.id "
            "JOIN pages p ON wo.page_id = p.id"
        );
        std::cout << "Retrieved data:" << std::endl;
        for (const auto& row : result) {
            std::cout << "Word: " << row["word"].c_str() 
                      << ", URL: " << row["url"].c_str() 
                      << ", Frequency: " << row["frequency"].as<int>() 
                      << std::endl;
        }
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
