
#ifndef SPIDER_H
#define SPIDER_H

#include <string>
#include <vector>

void crawl_page(const std::string& url, const std::vector<std::string>& searchWords, int depth, std::string data);

#endif
