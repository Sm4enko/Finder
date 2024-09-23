
#include "Spider.h"
#include <boost/locale.hpp>
#include <boost/regex.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <set>
#include <chrono>
#include <pqxx/pqxx>
#include "Table.h"
#include <boost/asio/io_service.hpp>
#include <mutex>
#include <windows.h>
#include <TlHelp32.h>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <include/iconv.h>
#include <codecvt>>
#include <locale>

using namespace boost::beast;
using namespace boost::asio;
using namespace std::this_thread; // sleep_for, sleep_until
using namespace std::chrono; // nanoseconds, system_clock, seconds
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class Spider {
public:
	std::vector<std::thread::id> ids;

	Spider() : running_(true) {}

	struct ThreadGuard {
		std::vector<std::thread> threads_; // Вектор, содержащий объекты std::thread, представляющие потоки
		std::mutex mtx_; // Мьютекс для синхронизации доступа к вектору и другим ресурсам
		std::shared_mutex smtx_;
		std::condition_variable cv_; // Объект для ожидания завершения потоков
		bool stop_; // Флаг для остановки функции runTask

		ThreadGuard() : stop_(false) {}

		void addThread(std::thread&& thread, const std::string& url) { // Добавление потока в вектор
			std::lock_guard<std::mutex> lock(mtx_); // Устанавливаем блокировку для защиты вектора от одновременного доступа нескольких потоков
			threads_.push_back(std::move(thread)); // Добавляем поток в вектор
		}

		void checkState(std::vector<std::thread::id>& ids) {
			if (ids.size() == threads_.size()) {
				stop_ = true; // Устанавливаем флаг остановки
			}
		}

		void runTask(std::vector<std::thread::id>& ids) { // Функция для запуска задачи
			sleep_for(std::chrono::milliseconds(10000));
			while (true) {
				{
					std::lock_guard<std::mutex> lock(mtx_); // Устанавливаем блокировку для защиты вектора от одновременного доступа нескольких потоков
					if (threads_.empty() || stop_) { // Проверяем, пуст ли вектор или установлен флаг остановки
						break; // Если да, то завершаем функцию
					}
				}
				checkState(ids);
				std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Задержка для экономии ресурсов
			}
		}

		void stop() { // Функция для остановки функции runTask
			std::lock_guard<std::mutex> lock(mtx_); // Устанавливаем блокировку для защиты вектора от одновременного доступа нескольких потоков
			stop_ = true; // Устанавливаем флаг остановки
		}
	} guard;

	void crawl_page(const std::string url, int depth, std::string data, bool isThread) {

		std::thread::id threadIdNum = std::this_thread::get_id();
		std::stringstream ss;
		ss << threadIdNum;
		std::string threadId = ss.str();

		boost::asio::io_context io_context;
		std::string out_str = "New thread# " + threadId + " in url: " + url + "\n";
		printMsg(out_str);
		pqxx::result pages;
		try {
			std::mutex mtx_;
			std::lock_guard<std::mutex> lock(mtx_);
			pqxx::connection connection(data);
			pqxx::work transaction(connection);
			std::string query = "SELECT id FROM pages WHERE url = '" + url + "'";
			pages = transaction.exec(query);
			transaction.commit();
			connection.close();
		}
		catch (const std::system_error& e) {
			out_str = "Stop (connection error) thread#  " + threadId + " in url " + url + "\n" + e.what() + +"\n";
			printMsg(out_str);
			std::lock_guard<std::mutex> lock(ids_lock);
			ids.emplace_back(threadIdNum);
			return;
		}
		catch (const std::exception& e) {
			out_str = "Stop (Broken db connection) thread#  " + threadId + " in url " + url + "\n";
			printMsg(out_str);
			std::lock_guard<std::mutex> lock(ids_lock);
			ids.emplace_back(threadIdNum);
			return;
		}

		if (!pages.empty()) {
			out_str = "Stop (already exists in database) thread# " + threadId + " in url " + url + "\n";
			printMsg(out_str);
			std::lock_guard<std::mutex> lock(ids_lock);
			ids.emplace_back(threadIdNum);
			return;
		}

		try {
			if (depth <= 0) {
				out_str = "Stop depth 0 thread# " + threadId + " in url " + url + "\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}

			std::string host;
			std::string target;

			if (url.find("https://") == 0) {
				host = url.substr(8);
			}
			else {
				out_str = "Stop (no start url) thread# " + threadId + " in url " + url + "\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}

			auto pos = host.find('/');
			target = (pos == std::string::npos) ? "/" : host.substr(pos);
			host = (pos == std::string::npos) ? host : host.substr(0, pos);

			if (host.empty() || target.empty()) {
				out_str = "Stop (empty target or host) thread# " + threadId + " in url " + url + "\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}

			net::io_context ioc;
			net::io_service io_service;
			ssl::context ctx{ ssl::context::sslv23_client };
			tcp::resolver resolver(ioc);
			beast::ssl_stream<tcp::socket> stream(ioc, ctx);

			if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
				beast::error_code ec{ static_cast<int>(::ERR_get_error()), net::error::get_ssl_category() };
				out_str = "Stop (ssl error) thread# " + threadId + " in url " + url + "\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}

			auto const results = resolver.resolve(host, "https");

			try {
				net::connect(stream.next_layer(), results.begin(), results.end());
			}
			catch (const std::exception& e) {
				out_str = "Stop (connect timeout) thread# " + threadId + " in url " + url + "\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}
			try {
				stream.handshake(ssl::stream_base::client);
			}
			catch (const std::exception& e) {
				out_str = "Stop (handshake_ssl timeuot) thread# " + threadId + " in url " + url + "\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}

			http::request<http::empty_body> req{ http::verb::get, target, 11 };
			req.set(http::field::host, host);
			req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
			try {
				http::write(stream, req);
			}

			catch (const std::exception& e) {
				out_str = "Stop (streamwrite error) thread# " + threadId + " in url " + url + "\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}

			boost::asio::deadline_timer timer(io_context);
			timer.expires_from_now(boost::posix_time::seconds(20));

			timer.async_wait([this, & stream, &url, &threadId, &out_str, &threadIdNum](const boost::system::error_code& ec) {
				if (ec) {
					out_str = "Stop (streamwrite error) thread# " + threadId + " in url " + url + "\n";
					printMsg(out_str);
					std::lock_guard<std::mutex> lock(ids_lock);
					ids.emplace_back(threadIdNum);
					return;
				}
				else {
					boost::asio::ip::tcp::socket& underlying_socket = stream.next_layer();
					underlying_socket.close();
				}
				});
			beast::flat_buffer buffer;
			http::response<http::string_body> res;
			http::read(stream, buffer, res);
			bool isUTF = false;
			for (auto& h : res.base()) {
				std::string n = h.name_string();
				std::string v = h.value();
				if (n== "Content-Type") {
					if (h.value() == "text/html; charset=UTF-8") {
						isUTF = true;
					}
				}
			}
			std::string responseBody = res.body().data();
			std::string plainText = remove_html_tags(responseBody);
			plainText = remove_punctuation(plainText);

			std::map<std::string, int> wordFrequency;
			std::stringstream ss(plainText);
			std::string word;
			while (ss >> word) {
				if (isUTF) {
					word  = UTF8_to_CP1251(word);
				}
				++wordFrequency[word];
			}
			try {
				pqxx::connection conn(data);
				pqxx::work txn(conn);
				txn.exec_params(
					"INSERT INTO pages (url) VALUES ($1) ON CONFLICT (url) DO NOTHING",
					url
				);
				pqxx::result pageResult = txn.exec_params("SELECT id FROM pages WHERE url = $1", url);
				int pageId = pageResult[0][0].as<int>();
				for (const auto& wordFrequencyItem : wordFrequency) {
					std::string searchWord = wordFrequencyItem.first;
					std::string q = "SELECT id FROM words WHERE word = '" + searchWord + "'";
					pqxx::result wordResult = txn.exec(q);
					int wordId;
					if (wordResult.empty()) {
						wordResult = txn.exec_params("INSERT INTO words (word) VALUES ($1) RETURNING id", searchWord);
						wordId = wordResult[0][0].as<int>();
					}
					else {
						wordId = wordResult[0][0].as<int>();
					}
					txn.exec_params(
						"INSERT INTO word_occurrences (word_id, page_id, frequency) VALUES ($1, $2, $3) "
						"ON CONFLICT (word_id, page_id) DO UPDATE SET frequency = EXCLUDED.frequency",
						wordId, pageId, wordFrequencyItem.second
					);
				}
				txn.commit();
				conn.close();
			}
			catch (const std::exception& e) {
				out_str = "Stop (SQL Error) thread# " + threadId + " in url " + url + "\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}

			if (depth == 1) {
				out_str = "Stop normally (depth 1, db complete) thread# " + threadId + " in url " + url + "\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}

			boost::regex link_regex("<a\\s+(?:[^>]*?\\s+)?href=(\"([^\"]*)\"|'([^']*)'|([^\\s>]+))");
			boost::sregex_iterator links_begin(responseBody.begin(), responseBody.end(), link_regex);
			boost::sregex_iterator links_end;

			try {
				std::mutex mtx_;
				std::lock_guard<std::mutex> lock(mtx_);
				pqxx::connection connection(data);
				pqxx::work transaction(connection);
				std::string query = "SELECT url FROM pages";
				pages = transaction.exec(query);
				transaction.commit();
				connection.close();
			}
			catch (const std::system_error& e) {
				out_str = "Stop (connection error) thread#  " + threadId + " in url " + url + "\n" + e.what() + +"\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}
			catch (const std::exception& e) {
				out_str = "Stop (Broken db connection) thread#  " + threadId + " in url " + url + "\n";
				printMsg(out_str);
				std::lock_guard<std::mutex> lock(ids_lock);
				ids.emplace_back(threadIdNum);
				return;
			}

			std::vector<std::string> child_urls;
			for (boost::sregex_iterator i = links_begin; i != links_end; ++i) {
				std::string link = (*i)[1].str();
				if (link.find("youtube.com") != std::string::npos) {
					continue;
				}
				if (link[0] == '"' || link[0] == '\'') {
					link = link.substr(1, link.size() - 2);
				}
				for (const auto& row : pages) {
					std::string url = row["url"].as<std::string>();
					if (url != link) {
						if (link.find("http://") == 0 || link.find("https://") == 0) {
							child_urls.push_back(link);
						}
						else if (link.find("/") == 0) {
							child_urls.push_back("https://" + host + link);
						}
						break;
					}
				}
			}
			for (const auto& url : child_urls) {
				guard.addThread(std::thread([this, &url, &depth, &data, &threadIdNum] {
					try {
					crawl_page(url, depth - 1, data, 1);
				}
				catch (...) {
					std::string out_str = "unknown exception in url " + url + " ||||||||||data: " + data + " |||||||||depth: " + std::to_string(depth) + "\n";
					(out_str);
					std::lock_guard<std::mutex> lock(ids_lock);
					ids.emplace_back(threadIdNum);
					return;
				}
					}), url);
			}
			io_context.run();
			out_str = " Stop normally (depth > 1 ) thread# " + threadId + " in url " + url + "\n";
			printMsg(out_str);
			std::lock_guard<std::mutex> lock(ids_lock);
			ids.emplace_back(threadIdNum);
			return;
		}
		catch (const std::exception& e) {
			out_str = "Stop (main try exception) thread# " + threadId + " in url " + url + "\n";
			printMsg(out_str);
			std::lock_guard<std::mutex> lock(ids_lock);
			ids.emplace_back(threadIdNum);
			return;
		}
		catch (...) {
			out_str = "Error: unknown exception occurre\n";
			printMsg(out_str);
			std::lock_guard<std::mutex> lock(ids_lock);
			ids.emplace_back(threadIdNum);
			return;
		}
	}

private:
	std::set<std::string> links;
	std::vector<std::thread::id> threads_;
	std::mutex mutex_;
	std::condition_variable cv_;
	bool running_;
	std::mutex ids_lock;
	std::mutex g_lock;
	//	std::vector<std::thread::id> ids;

	std::string remove_html_tags(const std::string& input) {
		std::string output;
		boost::regex td("((<td.*?>)|(<th.*?>)|(<li.*?>)|<br>|</td>|</li>|<p.*?>|PNG \x1a)");
		boost::regex styles("(<style.*?</style>)");
		boost::regex tags("<[^>]+>");// "(<[^>]*)>"
		boost::regex head("(<head.*?</head>)");
		boost::regex scripts("(<script.*?</script>)");
		boost::regex spec("[!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~]");
		boost::regex space("\\s+|\\r|\\n|\\t");
		boost::regex nbsp(R"(\&nbsp;|&#160;|&#xA0;|&#x00A0;)");

		output = boost::regex_replace(input, head, "");
		output = boost::regex_replace(output, td, " ");
		output = boost::regex_replace(output, scripts, "");
		output = boost::regex_replace(output, styles, "");
		output = boost::regex_replace(output, tags, "");
		output = boost::regex_replace(output, nbsp, "");
		output = boost::regex_replace(output, spec, " ");
		output = boost::regex_replace(output, space, " ");
		return output;
	}

	std::string remove_punctuation(const std::string& input) {
		std::string output;
		std::remove_copy_if(input.begin(), input.end(), std::back_inserter(output), [](unsigned char c) {
			return std::ispunct(c);
			});
		return output;
	}

	std::string UTF8_to_CP1251(std::string const& utf8)
	{
		if (!utf8.empty())
		{
			int wchlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.size(), NULL, 0);
			if (wchlen > 0 && wchlen != 0xFFFD)
			{
				std::vector<wchar_t> wbuf(wchlen);
				MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.size(), &wbuf[0], wchlen);
				std::vector<char> buf(wchlen);
				WideCharToMultiByte(1251, 0, &wbuf[0], wchlen, &buf[0], wchlen, 0, 0);
				return std::string(&buf[0], wchlen);
			}
		}
		return std::string();
	}


	int printMsg(std::string msg) {
		try {
			std::mutex my_mutex;
			std::unique_lock<std::mutex> lock(my_mutex, std::defer_lock);
			if (lock.try_lock()) {
				try {
					std::cout << msg << std::endl;
					return 0;
				}
				catch (const std::exception& e) {
					//std::cerr << "Error: exception occurred in critical section - " << e.what() << std::endl;
					return 1;
				}
				catch (...) {
				//	std::cerr << "Error: unknown exception occurred" << std::endl;
					return 0;
				}
			}
			else {
			//	std::cerr << "Error: unable to lock mutex" << std::endl;
				return 1;
			}
		}
		catch (const std::system_error& e) {
			//std::cerr << "Error: system error occurred - " << e.what() << std::endl;
			return 1;
		}
		catch (const std::exception& e) {
			//std::cerr << "Error: exception occurred - " << e.what() << std::endl;
			return  1;
		}
		catch (...) {
			//std::cerr << "Error: unknown exception occurred" << std::endl;
			return 1;
		}
	}
};


