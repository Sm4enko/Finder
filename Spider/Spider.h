
#ifndef SPIDER_H
#define SPIDER_H
#endif
#include <string>
#include <vector>
#include <shared_mutex>
#include <set>
#include <map>
#include <pqxx/pqxx>

class Spider {
public:
	std::string connection_data;
	// словарь задач (ОЧЕРЕДЬ), пар url:depth,  <string url,int depth>
	std::map<std::string, int> task_list;
	// словарь уникальных адресов, в том числе из БД <string url,int depth> depth - не используется, 0
	std::map<std::string, int> url_list;
	//	словарь объектов для заполнения БД. каждый объект - пара url:словарь частоты слов
	std::map<std::string, std::map<std::string, int>> sites;
	//	вектор адресов на которых запущен поток <string url>
	std::vector<std::string> visited_urls;
	// вектор id потоков, которые выполнили return по условию, или завершились в конце,"завершенные"
	std::vector<std::thread::id> pre_ended_threads_id_list;
	// Вектор, потоков, на которых запущен поток, условно "посещенные"
	std::vector<std::thread> threads_ids_list;
	//	мьютексы для распределения общего доступа к ресурсам
	std::mutex mutex_ids;
	std::mutex mutex_log;
	std::mutex mutex_visit;
	std::mutex mutex_url;
	std::mutex mutex_task;
	std::mutex mutex_connect;
	std::mutex mutex_sites;
	//	Флаг для работы функции worker
	bool run_worker;

	//	инициализатор экземпляра
	Spider();

	//	менеджер краулинга, потоков, и БД
	void worker();
	// вывод сообщения в лог, помечаем поток завершенным
	void pre_end_thread(std::string msg, std::thread::id thread_id, int msg_type);
	//	Краулер. url - адрес страницы, depth - глубина сканирования, data 
	void crawl(const std::string url, int depth);//, std::string data, bool isThread
	std::string html_parser(const std::string& input);
	//	Сообщение в логгер, msg_type: 0 - INFO, 1 - ERROR, 2 - DEBUG
	void printMsg(std::string msg, int msg_type);
	//	конвертер кодировок iConv
	char* convert(const char* s, const char* from_cp, const char* to_cp);
};


