
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
	// ������� ����� (�������), ��� url:depth,  <string url,int depth>
	std::map<std::string, int> task_list;
	// ������� ���������� �������, � ��� ����� �� �� <string url,int depth> depth - �� ������������, 0
	std::map<std::string, int> url_list;
	//	������� �������� ��� ���������� ��. ������ ������ - ���� url:������� ������� ����
	std::map<std::string, std::map<std::string, int>> sites;
	//	������ ������� �� ������� ������� ����� <string url>
	std::vector<std::string> visited_urls;
	// ������ id �������, ������� ��������� return �� �������, ��� ����������� � �����,"�����������"
	std::vector<std::thread::id> pre_ended_threads_id_list;
	// ������, �������, �� ������� ������� �����, ������� "����������"
	std::vector<std::thread> threads_ids_list;
	//	�������� ��� ������������� ������ ������� � ��������
	std::mutex mutex_ids;
	std::mutex mutex_log;
	std::mutex mutex_visit;
	std::mutex mutex_url;
	std::mutex mutex_task;
	std::mutex mutex_connect;
	std::mutex mutex_sites;
	//	���� ��� ������ ������� worker
	bool run_worker;

	//	������������� ����������
	Spider();

	//	�������� ���������, �������, � ��
	void worker();
	// ����� ��������� � ���, �������� ����� �����������
	void pre_end_thread(std::string msg, std::thread::id thread_id, int msg_type);
	//	�������. url - ����� ��������, depth - ������� ������������, data 
	void crawl(const std::string url, int depth);//, std::string data, bool isThread
	std::string html_parser(const std::string& input);
	//	��������� � ������, msg_type: 0 - INFO, 1 - ERROR, 2 - DEBUG
	void printMsg(std::string msg, int msg_type);
	//	��������� ��������� iConv
	char* convert(const char* s, const char* from_cp, const char* to_cp);
};


