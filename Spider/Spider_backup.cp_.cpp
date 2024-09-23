
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

std::mutex g_lock;

using namespace boost::beast;
using namespace boost::asio;
using namespace std::this_thread; // sleep_for, sleep_until
using namespace std::chrono; // nanoseconds, system_clock, seconds
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

std::set<std::string> visitedUrls;
std::set<std::string> findedWords;
int completed_threads = 0;
int started_threads = 0;


DWORD GetNumberOfThreads(DWORD processId)
{
	HANDLE hSnapshot;
	THREADENTRY32 te32;
	DWORD dwThreadCount = 0;

	// Take a snapshot of the process
	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE)
	{
		return 0;
	}

	// Initialize the THREADENTRY32 structure
	te32.dwSize = sizeof(te32);

	// Iterate through the threads
	if (Thread32First(hSnapshot, &te32))
	{
		do
		{
			if (te32.th32OwnerProcessID == processId)
			{
				dwThreadCount++;
			}
		} while (Thread32Next(hSnapshot, &te32));
	}

	CloseHandle(hSnapshot);
	return dwThreadCount;
}


std::string remove_html_tags(const std::string& input) {
	std::string output;
	boost::regex td("(<td>|<li>)");
	boost::regex styles("(<style>.*?</style>)");
	boost::regex tags("<[^>]+>");// "(<[^>]*)>"
	boost::regex head("(<head.*?</head>)");
	boost::regex scripts("(<script.*?</script>)");
	boost::regex space("\\s+|\\r|\\n|\\t");
	boost::regex nbsp(R"(\&nbsp;|&#160;|&#xA0;|&#x00A0;)");
	/// <summary>
	/// ВАЖЕН ПОРЯДОК!
	/// </summary>
	output = boost::regex_replace(input, head, "");
	output = boost::regex_replace(output, td, " ");
	output = boost::regex_replace(output, scripts, "");
	output = boost::regex_replace(output, styles, "");
	output = boost::regex_replace(output, tags, "");
	output = boost::regex_replace(output, nbsp, "");
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

void initialize_locale() {
	boost::locale::generator gen;
	std::locale::global(gen("ru_RU.UTF-8"));
}

static int threadNum = 0;
int crawl_page(const std::string& url, int depth, std::string data, bool isThread, int threadNumber) {
	std::string u = url;
	std::string out_str = "New CRAWL in url: " + url + " thread# " + std::to_string(threadNumber) + "\n";
	g_lock.lock();
	std::cout << out_str;
	g_lock.unlock();
	pqxx::result pages;
	try {
		std::lock_guard<std::mutex> lock(g_lock);
		pqxx::connection connection(data);
		pqxx::work transaction(connection);
		std::string query = "SELECT id FROM pages WHERE url = '" + url + "'";
		pages = transaction.exec(query);
		transaction.commit();
		connection.close();
	}
	catch (const std::exception& e) {
		std::lock_guard<std::mutex> lock(g_lock);
		std::cout << "Broken db connection\n";
		out_str = "Stop thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
		std::cerr << "Error: " << e.what() << std::endl;
        std::cout << out_str;
		completed_threads++;
		return 0;
	}

	int page_id = -1;
	try { page_id = pages[0][0].as<int>(); }
	catch (const std::exception& e) {}
	if (page_id != -1) {
		completed_threads++;
		std::lock_guard<std::mutex> lock(g_lock);
		std::cout << " Page " + url + " already exists in database\n";
		out_str = "Stop thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
		std::cout << out_str;
		return 0;
	}

	try {
		initialize_locale();
		if (depth <= 0) {
			completed_threads++;
			out_str = "Stop depth 0 thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::lock_guard<std::mutex> lock(g_lock);
			std::cout << out_str;
			return 0;
		}
		net::io_context ioc;
		net::io_service io_service;
		ssl::context ctx{ ssl::context::sslv23_client };
		tcp::resolver resolver(ioc);
		beast::ssl_stream<tcp::socket> stream(ioc, ctx);
		std::string host;
		std::string target;

		if (url.find("https://") == 0) {
			host = url.substr(8);
		}
		else {
			out_str = "Stop (https:// not start url) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::lock_guard<std::mutex> lock(g_lock);
			std::cout << out_str;
			
			completed_threads++;
			return 0;
		}

		auto pos = host.find('/');
		target = (pos == std::string::npos) ? "/" : host.substr(pos);
		host = (pos == std::string::npos) ? host : host.substr(0, pos);

		if (host.empty() || target.empty()) {
			out_str = "Stop (empty target or host) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::lock_guard<std::mutex> lock(g_lock);
			std::cout << out_str;
			
			completed_threads++;
			return 0;
		}

		if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
			beast::error_code ec{ static_cast<int>(::ERR_get_error()), net::error::get_ssl_category() };
			completed_threads++;
			out_str = "Stop (ssl error) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::lock_guard<std::mutex> lock(g_lock);
			std::cout << out_str;
			return 0;
			//throw beast::system_error{ ec };
		}

		auto const results = resolver.resolve(host, "https");

		net::deadline_timer timer(io_service);
		timer.expires_from_now(boost::posix_time::seconds(2));
		timer.wait();
		try {// Выполняем соединение
			net::connect(stream.next_layer(), results.begin(), results.end());
		}
		catch (const std::exception& e) {
			out_str = "Stop (connect timeout) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::lock_guard<std::mutex> lock(g_lock);
			std::cerr << "Error: " << e.what() << std::endl;
			std::cout << out_str;
			
			completed_threads++;
			return 0;
		}
		try {// Пытаемся выполнить квитирование
			stream.handshake(ssl::stream_base::client);
		}
		catch (const std::exception& e) {
			out_str = "Stop (handshake_ssl timeuot) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::lock_guard<std::mutex> lock(g_lock);
			std::cerr << "Error: " << e.what() << std::endl;
			std::cout << out_str;
			completed_threads++;
			return 0;
		}


		http::request<http::empty_body> req{ http::verb::get, target, 11 };
		req.set(http::field::host, host);
		req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
		try {
			http::write(stream, req);
		}
		
		catch (const std::exception& e) {
			out_str = "Stop (sreamwrite error) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::lock_guard<std::mutex> lock(g_lock);
			std::cerr << "Error: " << e.what() << std::endl;
			std::cout << out_str;
			completed_threads++;
			return 0;
		}
		beast::flat_buffer buffer;
		http::response<http::dynamic_body> res;
		http::read(stream, buffer, res);

		std::string responseBody = boost::beast::buffers_to_string(res.body().data());

		std::string plainText = remove_html_tags(responseBody);
		plainText = remove_punctuation(plainText);

		std::map<std::string, int> wordFrequency;
		std::stringstream ss(plainText);
		std::string word;
		while (ss >> word) {
			++wordFrequency[word];
		}
		try {
			std::lock_guard<std::mutex> lock(g_lock);
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
			std::lock_guard<std::mutex> lock(g_lock);
			std::cout << "Broken db connection\n";
			std::cerr << e.what() << std::endl;
			out_str = "Stop thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::cout << out_str;
			
			completed_threads++;
			return 0;
		}

		if (depth == 1) {
			out_str = "Stop (depth 1, db complete) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::lock_guard<std::mutex> lock(g_lock);
			std::cout << out_str;
			
			completed_threads++;
			return 1;
		}

		boost::regex link_regex("<a\\s+(?:[^>]*?\\s+)?href=(\"([^\"]*)\"|'([^']*)'|([^\\s>]+))");
		boost::sregex_iterator links_begin(responseBody.begin(), responseBody.end(), link_regex);
		boost::sregex_iterator links_end;

		std::vector<std::string> child_urls;
		for (boost::sregex_iterator i = links_begin; i != links_end; ++i) {
			std::string link = (*i)[1].str();
			if (link[0] == '"' || link[0] == '\'') {
				link = link.substr(1, link.size() - 2);
			}
			if (link.find("http://") == 0 || link.find("https://") == 0) {
				child_urls.push_back(link);

			}
			else if (link.find("/") == 0) {
				child_urls.push_back("https://" + host + link);
			}
		}

		for (const std::string& child_url : child_urls) {
			threadNum++;
			std::thread crawl_thread(crawl_page, child_url, depth - 1, data, 1, threadNum);
			crawl_thread.detach();
			started_threads++;
			sleep_for(400ns);
		}
		if (isThread) {
			out_str = "Stop ( depth > 1, thread complete) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			completed_threads++;
			std::lock_guard<std::mutex> lock(g_lock);
			std::cout << out_str;
			
			return 1;
		}
		else {
			out_str = "Stop (main thread complete) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::lock_guard<std::mutex> lock(g_lock);
			std::cout << out_str;
			
		}
		completed_threads++;
		
		



	//	std::cout << "Количество потоков: " << threadCount << std::endl;
		while (started_threads - 1 > completed_threads) {
			DWORD processId = GetCurrentProcessId(); // Replace with the desired process ID
			DWORD threadCount = GetNumberOfThreads(processId);
			std::cout << std::to_string(threadCount) << std::endl;
			sleep_for(5s);
		}
		std::lock_guard<std::mutex> lock(g_lock);
		std::cout << "All threads complete \n\n";
		
		return 1;
	}
	catch (const std::exception& e) {
		completed_threads++;
		std::lock_guard<std::mutex> lock(g_lock);
		std::cerr << "Error: " << e.what() << std::endl;
		return 0;
	}
}
