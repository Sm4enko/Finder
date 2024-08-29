
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
std::mutex g_lock;

using namespace boost::beast;
using namespace boost::asio;
using namespace std::this_thread;
using namespace std::chrono;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

std::set<std::string> visitedUrls;
std::set<std::string> findedWords;
int completed_threads = 0;
int started_threads = 0;

std::string remove_html_tags(const std::string& input) {
	std::string output;
	boost::regex td("(<td>)");
	boost::regex styles("(<style>.*?</style>)");
	boost::regex tags("<[^>]+>");// "(<[^>]*)>"
	boost::regex head("(<head.*?</head>)");
	boost::regex scripts("(<head.*?</head>)");
	boost::regex space("\\s+|\\r|\\n|\\t");

	output = boost::regex_replace(input, head, "");
	output = boost::regex_replace(output, td, " ");
	output = boost::regex_replace(output, scripts, "");
	output = boost::regex_replace(output, styles, "");
	output = boost::regex_replace(output, tags, "");
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
void crawl_page(const std::string& url, const std::vector<std::string>& searchWords, int depth, std::string data, bool isThread, int threadNumber) {

	std::string out_str = "New CRAWL in url: " + url + " thread# " + std::to_string(threadNum) + "\n";
	g_lock.lock();
	std::cout << out_str;
	pqxx::connection connection(data);
	pqxx::work transaction(connection);
	std::string query = "SELECT w.word FROM word_occurrences wo JOIN words w on wo.word_id = w.id JOIN pages p on wo.page_id = p.id WHERE p.url = '" + url + "'";
	pqxx::result wordsresult = transaction.exec(query);
	transaction.commit();
	connection.close();
	g_lock.unlock();
	std::vector<std::string> filteredWords = searchWords;

	for (int i = 0; i < searchWords.size(); i++) {
		for (int j = 0; j < wordsresult.size(); j++) { =
			if (searchWords[i] == wordsresult[j][0].as<std::string>()) {
				auto it = std::find(filteredWords.begin(), filteredWords.end(), wordsresult[j][0].as<std::string>());
				filteredWords.erase(it);
			}
		}
	}

	

	try {
		initialize_locale();
		if (depth <= 0) {
			return;
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
			//::cerr << "Please use format https://www.site.com/path" << std::endl;
			out_str = "Stop (https:// not start url) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			g_lock.lock();
			std::cout << out_str;
			g_lock.unlock();
			completed_threads++;
			return;
		}

		auto pos = host.find('/');
		target = (pos == std::string::npos) ? "/" : host.substr(pos);
		host = (pos == std::string::npos) ? host : host.substr(0, pos);

		if (host.empty() || target.empty()) {
			//std::cerr << "Please use format like https://www.site.com/path" << std::endl;
			out_str = "Stop (empty target or host) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			g_lock.lock();
			std::cout << out_str;
			g_lock.unlock();
			completed_threads++;
			return;
		}

		if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
			beast::error_code ec{ static_cast<int>(::ERR_get_error()), net::error::get_ssl_category() };
			throw beast::system_error{ ec };
		}

		auto const results = resolver.resolve(host, "https");

		net::deadline_timer timer(io_service);
		timer.expires_from_now(boost::posix_time::seconds(2));
		
		try {
			net::connect(stream.next_layer(), results.begin(), results.end());
		}
		catch (system_error& e) {
			out_str = "Stop (connect timeout) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			g_lock.lock();
			std::cout << out_str;
			g_lock.unlock();
			completed_threads++;
			return;
		}
		try {
			stream.handshake(ssl::stream_base::client);
		}
		catch (const boost::system::system_error& e) {
			out_str = "Stop (handshake_ssl timeuot) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			g_lock.lock();
			std::cout << out_str;
			g_lock.unlock();
			completed_threads++;
			return;
		}
		timer.wait();

		http::request<http::empty_body> req{ http::verb::get, target, 11 };
		req.set(http::field::host, host);
		req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

		http::write(stream, req);

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
		pqxx::connection conn(data);
		pqxx::work txn(conn);
		g_lock.lock();
		txn.exec_params(
			"INSERT INTO pages (url) VALUES ($1) ON CONFLICT (url) DO NOTHING",
			url
		);
		pqxx::result pageResult = txn.exec_params("SELECT id FROM pages WHERE url = $1", url);
		int pageId = pageResult[0][0].as<int>();
		for (const auto& searchWord : filteredWords) {
			auto it = wordFrequency.find(searchWord);

			if (it != wordFrequency.end()) {
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
					wordId, pageId, it->second
				);
			}
		}
		txn.commit();
		conn.close();
		g_lock.unlock();

		if (depth == 1) {
			out_str = "Stop (depth 1, db complete) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			std::cout << out_str;
			completed_threads++;
			return;
		}

		boost::regex link_regex("<a\\s+(?:[^>]*?\\s+)?href=(\"([^\"]*)\"|'([^']*)'|([^\\s>]+))");
		boost::sregex_iterator links_begin(responseBody.begin(), responseBody.end(), link_regex);
		boost::sregex_iterator links_end;

		std::vector<std::string> child_urls;
		g_lock.lock();
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
		g_lock.unlock();
		for (const std::string& child_url : child_urls) {
			threadNum++;
			std::thread crawl_thread(crawl_page, child_url, searchWords, depth - 1, data, 1, threadNum);
			crawl_thread.detach();
			started_threads++;
			sleep_for(200ns);
		}
		if (isThread) {
			out_str = "Stop (depth>1'thread complete) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			g_lock.lock();
			std::cout << out_str;
			g_lock.unlock();
			return;
		}
		else {
			out_str = "Stop (main thread complete) thread# " + std::to_string(threadNumber) + " in url " + url + "\n";
			g_lock.lock();
			std::cout << out_str;
			g_lock.unlock();
		}
		completed_threads++;
		while (started_threads - 1 > completed_threads) {}
		g_lock.lock();
		std::cout << "All threads complete, transmite response \n\n";
		g_lock.unlock();
		
	}
	catch (const std::exception& e) {
		g_lock.lock();
		std::cerr << "Error: " << e.what() << std::endl;
		g_lock.unlock();
	}
}
