
#ifndef SPIDER_H
#define SPIDER_H
#endif
#include <string>
#include <vector>
#include <shared_mutex>
#include <set>
class Spider {
public:
	std::vector<std::thread::id> ids;
	std::mutex ids_lock;
	std::set<std::string> links;
	std::vector<std::thread::id> threads_;
	std::mutex mutex_;
	std::condition_variable cv_;
	bool running_;
	std::mutex g_lock;
	Spider();
	struct ThreadGuard {
		std::vector<std::thread> threads_; // ������, ���������� ������� std::thread, �������������� ������
		std::mutex mtx_; // ������� ��� ������������� ������� � ������� � ������ ��������
		std::shared_mutex smtx_;
		std::condition_variable cv_; // ������ ��� �������� ���������� �������
		bool stop_; // ���� ��� ��������� ������� runTask
		ThreadGuard();
		void addThread(std::thread&& thread, const std::string& url);
		void runTask(std::vector<std::thread::id>& ids);
		void checkState(std::vector<std::thread::id>& ids);
		void stop();
	}guard;
	void crawl_page(const std::string url, int depth, std::string data, bool isThread);
	std::string remove_html_tags(const std::string& input);
	std::string remove_punctuation(const std::string& input);
	std::string UTF8_to_CP1251(std::string const& utf8);
	int printMsg(std::string msg);
};


