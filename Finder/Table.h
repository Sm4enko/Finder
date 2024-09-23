
#ifndef TABLE_H
#define TABLE_H

#include <string>
#include <unordered_map>

std::unordered_map<std::string, std::string> readConfig(const std::string& filename);
std::string getConnectionString();
void create_table();
void retrieve_data();
void mySettings(const std::string mySettings);

#endif
