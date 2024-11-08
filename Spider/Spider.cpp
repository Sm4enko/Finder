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
#include<boost/algorithm/string.hpp>
#include <fstream>
#include <iostream>
#include<codecvt>
#include <chrono>
#include "Table.h"
#include <boost/asio/io_service.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <include/iconv.h>
#include <locale>
#include "easylogging++.h"

using namespace boost::beast;
using namespace boost::asio;
using namespace std::this_thread; // sleep_for, sleep_until
using namespace std::chrono; // nanoseconds, system_clock, seconds
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

Spider::Spider() { run_worker = true; }

void Spider::worker() {
	pqxx::result result;
	std::string query = "";
	std::string out_str;
	//получим данные для краула, адрес и глубину, удалим задание из очереди 
	auto it = task_list.begin();
	std::string url = it->first;
	int depth = it->second;
	task_list.erase(url);
	// заполним словарь уникальных адресов из БД
	pqxx::connection conn(connection_data);
	pqxx::work transaction(conn);
	query = "SELECT url FROM pages";
	result = transaction.exec(query);
	transaction.commit();
	//добавим в уникальные адреса полученные из БД
	for (int row_num = 0; row_num < result.size(); ++row_num) {
		const auto row = result[row_num];
		url_list[row[0].c_str()] = 0;
	}

	//запустим в основном потоке первый краул для заполнения очереди воркера
	crawl(url, depth);
	visited_urls.emplace_back(url);
	// основной цикл спайдера
	while (run_worker) {
		// обработка очереди сайтов
		if (task_list.size()) {
			{
				std::lock_guard<std::mutex> lock_task(mutex_task);
				it = task_list.begin();
				url = it->first;
				depth = it->second;
				task_list.erase(url);
			}
			if (std::find(visited_urls.begin(), visited_urls.end(), url) == visited_urls.end()) {
				visited_urls.emplace_back(url);
				if (depth > 0) {
					try {	// создаём поток краулера
						std::thread crawlerThread([this, &url, &depth] {crawl(url, depth); });
						crawlerThread.detach();
						threads_ids_list.push_back(std::move(crawlerThread));
					}
					catch (const std::exception& e) {
						printMsg(e.what(), 1);
					}
				}
			}
		}
		// запись в БД
		try {
			pqxx::connection conn(connection_data);
			if (sites.size() > 0) {
				std::string url;
				std::map<std::string, int> words_ocs;
				{	// mutex space
					std::lock_guard<std::mutex> lock_sites(mutex_sites);
					auto it = sites.begin();
					url = it->first;
					words_ocs = it->second;
					sites.erase(sites.begin());
				}

				if (words_ocs.size() > 0) {
					// пишем в таблицу адресов
					query = "INSERT INTO pages (url) VALUES ('" + url + "') ON CONFLICT (url) DO NOTHING RETURNING id";
					pqxx::work transaction(conn);
					result = transaction.exec(query);
					transaction.commit();
					if (!result.empty()) {
						int page_id = result[0][0].as<int>();
						for (auto w{ words_ocs.begin() }; w != words_ocs.end(); w++) {
							std::string word = w->first;
							// проверяем есть ли слово в БД, запросим ид
							query = "SELECT id FROM words WHERE word = '" + word + "'";
							try {	
								pqxx::work transaction(conn);
								result = transaction.exec(query);
								transaction.commit();
							}
							catch (pqxx::sql_error const& e) {
								out_str = "DB Error \n";
								out_str += e.what();
								printMsg(out_str, 1);
							}
							// если нет, то добавим, получим ид
							if (result.empty()) { 
								query = "INSERT INTO words (word) VALUES ('" + word + "') RETURNING id";
								try {	// пишем в таблицу слов
									pqxx::work transaction(conn);
									result = transaction.exec(query);
									transaction.commit();
								}
								catch (pqxx::sql_error const& e) {
									out_str = "DB Error \n";
									out_str += e.what();
									printMsg(out_str, 1);
								}
								catch (...) {
									printMsg("Unknown DB Error", 1);
								}
							}
							int word_id = result[0][0].as<int>();
							int freq = w->second;
							query = "INSERT INTO word_occurrences (word_id, page_id, frequency) VALUES ($1 ,$2, $3)";
							try {		// пишем в таблицу соответствий
								pqxx::work transaction(conn);
								result = transaction.exec_params(query, word_id, page_id, freq);
								transaction.commit();
							}
							catch (pqxx::sql_error const& e) {
								out_str = "DB Error \n";
								out_str += e.what();
								printMsg(out_str, 1);
							}
						}
					}
				}
			}
			conn.close();
		}
		catch (pqxx::broken_connection const& e) {
			out_str = "DB Error \n";
			out_str += e.what();
			printMsg(out_str, 1);;
		}
		catch (pqxx::sql_error const& e) {
			out_str = "DB Error \n";
			out_str += e.what();
			printMsg(out_str, 1);
		}
		catch (std::exception const& e) {
			out_str = "DB Error \n ";
			out_str += e.what();
			printMsg(out_str, 1);
		}
		catch (...) {
			printMsg("Unknown DB Error", 1);
		}
		// контроль завершения программы
		if ((pre_ended_threads_id_list.size() == threads_ids_list.size() + 1) && sites.size() == 0 && task_list.size() == 0) { run_worker = false; }
	}
	LOG(INFO) << "THE END";
}

void Spider::pre_end_thread(std::string msg, std::thread::id thread_id, int msg_type) {
	printMsg(msg, msg_type);
	std::lock_guard<std::mutex> lock(mutex_ids);
	pre_ended_threads_id_list.emplace_back(thread_id);
}

void Spider::crawl(const std::string url, int depth) { //, std::string data, bool isThread
	// Получим id потока

	std::thread::id threadIdNum = std::this_thread::get_id();
	std::stringstream ss;
	ss << threadIdNum;
	std::string threadId = ss.str();
	std::string out_str = "New thread# " + threadId + " in url: " + url;
	printMsg(out_str, 0);

	// запросим из базы url страницы, обработаем исключения
	boost::asio::io_context io_context;

	// работа со страницей,запрос, парсинг, подготовка данных к записи в БД
	try {
		std::string host;
		std::string target;

		if (url.find("https://") == 0) {
			host = url.substr(8);
		}
		else {
			out_str = "Stop normally (no start url) thread# " + threadId + " in url " + url;
			pre_end_thread(out_str, threadIdNum, 0);
			return;
		}

		auto pos = host.find('/');
		target = (pos == std::string::npos) ? "/" : host.substr(pos);
		host = (pos == std::string::npos) ? host : host.substr(0, pos);

		if (host.empty() || target.empty()) {
			out_str = "Stop normally (empty target or host) thread# " + threadId + " in url " + url;
			pre_end_thread(out_str, threadIdNum, 0);
			return;
		}

		net::io_context ioc;
		net::io_service io_service;
		ssl::context ctx{ ssl::context::sslv23_client };
		tcp::resolver resolver(ioc);
		beast::ssl_stream<tcp::socket> stream(ioc, ctx);
		if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
			beast::error_code ec{ static_cast<int>(::ERR_get_error()), net::error::get_ssl_category() };
			out_str = "Stop (ssl error) thread# " + threadId + " in url " + url;
			pre_end_thread(out_str, threadIdNum, 1);
			return;
		}

		auto const results = resolver.resolve(host, "https");
		try {
			net::connect(stream.next_layer(), results.begin(), results.end());
		}
		catch (const std::exception& e) {
			out_str = "Stop (connect timeout) thread# " + threadId + " in url " + url + "\n" + e.what();
			pre_end_thread(out_str, threadIdNum, 1);
			return;
		}
		catch (...) {
			out_str = "Stop (Unknown net::connect ecxeption) thread#  " + threadId + " in url " + url;
			pre_end_thread(out_str, threadIdNum, 1);
			return;
		}
		try {
			stream.handshake(ssl::stream_base::client);
		}
		catch (const std::exception& e) {
			out_str = "Stop (handshake_ssl timeuot) thread# " + threadId + " in url " + url + "\n" + e.what();
			pre_end_thread(out_str, threadIdNum, 1);
			return;
		}
		catch (...) {
			out_str = "Stop (Unknown ssl handshake ecxeption) thread#  " + threadId + " in url " + url;
			pre_end_thread(out_str, threadIdNum, 1);
			return;
		}

		http::request<http::empty_body> req{ http::verb::get, target, 11 };
		req.set(http::field::host, host);
		req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
		req.set(http::field::accept, "text/html");
		req.set(http::field::connection, "close");
		try {
			http::write(stream, req);
		}
		catch (const std::exception& e) {
			out_str = "Stop (streamwrite error) thread# " + threadId + " in url " + url + "\n" + e.what();
			pre_end_thread(out_str, threadIdNum, 1);
			return;
		}
		catch (...) {
			out_str = "Stop (Unknown http::write ecxeption) thread#  " + threadId + " in url " + url;
			pre_end_thread(out_str, threadIdNum, 1);
			return;
		}

		boost::asio::deadline_timer timer(io_context);// таймер на таймаут соединения
		timer.expires_from_now(boost::posix_time::seconds(20));
		timer.async_wait([this, &stream, &url, &threadId, &out_str, &threadIdNum](const boost::system::error_code& ec) {
			if (ec) {
				out_str = "Stop (streamwrite error) thread# " + threadId + " in url " + url;
				pre_end_thread(out_str, threadIdNum, 0);
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
		std::string responseBody = res.body().data();

		bool is1251 = false;
		if (responseBody.find("charset=Windows") < std::string::npos) { is1251 = true; }
		if (responseBody.find("charset=windows") < std::string::npos) { is1251 = true; }
		if (is1251) {
			const char* c = responseBody.c_str();
			c = convert(c, "cp1251", "utf-8");
			responseBody = c;
		}

		std::string plainText = html_parser(responseBody);
		std::map<std::string, int> wordFrequency;
		std::stringstream ss(plainText);
		std::string word;
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

		while (ss >> word) {
			std::wstring ws = converter.from_bytes(word);
			++wordFrequency[word];
		}
		{ // пространство mutex lock_sites
			std::lock_guard<std::mutex> lock_sites(mutex_sites);
			sites[url] = wordFrequency;
		}

		if (depth == 1) {
			out_str = "Stop normally (depth 1, db complete) thread# " + threadId + " in url " + url;
			pre_end_thread(out_str, threadIdNum, 0);
			return;
		}
		boost::regex link_regex("<a\\s+(?:[^>]*?\\s+)?href=(\"([^\"]*)\"|'([^']*)'|([^\\s>]+))");
		boost::sregex_iterator links_begin(responseBody.begin(), responseBody.end(), link_regex);
		boost::sregex_iterator links_end;
		std::vector<std::string> child_urls;
		for (boost::sregex_iterator i = links_begin; i != links_end; ++i) {
			std::string link = (*i)[1].str();
			if (link.find("youtube.com") != std::string::npos) {
				continue;
			}
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
		// заполним таск лист и лист уникальных url
		// оставим бОльшую глубину прохода, если сайт ещё не пройден
		for (const auto& url : child_urls) {
			if (url_list.contains(url)) {	//	если адрес есть в уникальных
				if (task_list[url] > 0) {	//	если сайт не пройден 
					if (task_list[url] < depth - 1) {	//	если текущая глубина больше
						std::lock_guard<std::mutex> lock_url(mutex_url);
						url_list[url] = depth - 1;	//	обновим глубину
					}
				}
				continue;	// перейдем к новой итерации
			}
			{	// mutex space
				std::lock_guard<std::mutex> lock_url(mutex_url);
				url_list[url] = depth - 1;
			}
			{	// mutex space
				std::lock_guard<std::mutex> lock_task1(mutex_task);
				task_list[url] = depth - 1;
			}
		}
		io_context.run();
		out_str = " Stop normally (depth > 1 ), new url's added, thread# " + threadId + " in url " + url;
		pre_end_thread(out_str, threadIdNum, 0);
	}
	catch (const std::exception& e) {
		out_str = "Stop (main try exception) thread# " + threadId + " in url " + url + "\n" + e.what();
		pre_end_thread(out_str, threadIdNum, 1);
		return;
	}
	catch (...) {
		out_str = "Stop (main try unknown exception) thread# " + threadId + " in url " + url;
		pre_end_thread(out_str, threadIdNum, 1);
		return;
	}
}

std::string Spider::html_parser(const std::string& input) {
	std::string output;
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	boost::regex styles("(<style.*?</style>)");
	boost::regex tags("<[^<>]+>");// "(<[^>]*)>" "<[^>]+>"
	boost::regex head("(<head.*?</head>)");
	boost::regex scripts("(<script.*?</script>)");
	boost::regex space("( +)");
	boost::regex nbsp("(\&nbsp;)");

	output = boost::regex_replace(input, head, " ");
	output = boost::regex_replace(output, scripts, " ");
	output = boost::regex_replace(output, styles, " ");
	output = boost::regex_replace(output, tags, " ");
	output = boost::regex_replace(output, nbsp, " ");
	output = boost::regex_replace(output, space, " ");
	std::wstring wstr2 = L"";
	std::wstring wstr = converter.from_bytes(output);
	for (auto c : wstr) {// кирилица В.,н.регистр	латиница В.регистр	  латиница н.регистр		ё				Ё			пробел			цифры
		bool condition = (c > 1039 && c < 1104) || (c > 64 && c < 91) || (c > 96 && c < 123) || (c == 1105) || (c == 1105) || (c == 32) || (c > 47 && c < 58);
		if (condition) {
			if ((c > 1039 && c < 1072) || (c > 64 && c < 91)) { // LOWERCASE
				c += 32;
			}
			wstr2 += c;
		}
	}
	output = converter.to_bytes(wstr2);
	return output;
}

void Spider::printMsg(std::string msg, int msg_type) {
	std::lock_guard<std::mutex> lock(mutex_log);
	if (msg_type == 0) {
		LOG(INFO) << msg;
	}
	else if (msg_type == 0) {
		LOG(ERROR) << msg;
	}
	return;
}
char* Spider::convert(const char* s, const char* from_cp, const char* to_cp)
{
	iconv_t ic = iconv_open(to_cp, from_cp);
	if (ic == (iconv_t)(-1)) {
		fprintf(stderr, "iconv: cannot convert from %s to %s\n", from_cp, to_cp);
		char* ch = nullptr;
		return ch;
	}
	char* out_buf = (char*)calloc(strlen(s) + 1, 1);
	char* out = out_buf;
	char* in = (char*)malloc(strlen(s) + 1);
	strcpy(in, s);
	size_t in_ln = strlen(s);
	size_t out_ln = in_ln;
	size_t k = iconv(ic, &in, &in_ln, &out, &out_ln);
	if (k == (size_t)-1)
		fprintf(stderr, "iconv: %u of %d converted\n", k, strlen(s));
	if (errno != 0)
		fprintf(stderr, "iconv: %s\n", strerror(errno));
	iconv_close(ic);
	out_buf[strlen(out_buf) - 2] = 0;
	return out_buf;
}




