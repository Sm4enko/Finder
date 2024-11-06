
#ifndef TABLE_H
#define TABLE_H

#include <string>
#include <unordered_map>

std::unordered_map<std::string, std::string> readConfig(const std::string& filename);
void create_table(std::string conn_data);
#endif
