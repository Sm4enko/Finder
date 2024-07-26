
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
#include <pqxx/pqxx>
#include "Table.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

std::set<std::string> visitedUrls;

std::string remove_html_tags(const std::string& input) {
    std::string output;
    boost::regex tags("<[^>]*>");
    output = boost::regex_replace(input, tags, "");
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
    std::locale::global(gen("en_US.UTF-8"));
}

void crawl_page(const std::string& url, const std::vector<std::string>& searchWords, int depth, std::string data) {
    try {
        initialize_locale();

        if (depth <= 0) {
            return;
        }

        if (visitedUrls.find(url) != visitedUrls.end()) {
            return;
        }
        visitedUrls.insert(url);

        net::io_context ioc;
        ssl::context ctx{ssl::context::sslv23_client};

        tcp::resolver resolver(ioc);
        beast::ssl_stream<tcp::socket> stream(ioc, ctx);

        std::string host;
        std::string target;

        if (url.find("https://") == 0) {
            host = url.substr(8);
        } else {
            std::cerr << "Please use format https://www.site.com/path" << std::endl;
            return;
        }

        auto pos = host.find('/');
        target = (pos == std::string::npos) ? "/" : host.substr(pos);
        host = (pos == std::string::npos) ? host : host.substr(0, pos);

        if (host.empty() || target.empty()) {
            std::cerr << "Please use format like https://www.site.com/path" << std::endl;
            return;
        }

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        auto const results = resolver.resolve(host, "https");
        net::connect(stream.next_layer(), results.begin(), results.end());
        stream.handshake(ssl::stream_base::client);

        http::request<http::empty_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(stream, buffer, res);

        std::string responseBody = boost::beast::buffers_to_string(res.body().data());

        std::string filename = host + ".html";

        std::ofstream htmlFile(filename, std::ios::binary);
        if (htmlFile.is_open()) {
            htmlFile << responseBody;
            htmlFile.close();
            std::cout << "Download content saved to " << filename << std::endl;
        } else {
            std::cerr << "Failed to save main HTML content to " << filename << std::endl;
        }

        std::string plainText = remove_html_tags(responseBody);
        plainText = remove_punctuation(plainText);

        std::ofstream textFile(host + ".txt");
        if (textFile.is_open()) {
            textFile << plainText;
            textFile.close();
            std::cout << "Main plain text content saved to " << host + ".txt" << std::endl;
        } else {
            std::cerr << "Failed to save main plain text content to " << host + ".txt" << std::endl;
        }

        std::map<std::string, int> wordFrequency;
        std::stringstream ss(plainText);
        std::string word;
        while (ss >> word) {
            ++wordFrequency[word];
        }

        pqxx::connection conn(data);
        pqxx::work txn(conn);

               txn.exec_params(
            "INSERT INTO pages (url) VALUES ($1) ON CONFLICT (url) DO NOTHING",
            url
        );

        pqxx::result pageResult = txn.exec_params("SELECT id FROM pages WHERE url = $1", url);
        int pageId = pageResult[0][0].as<int>();

        for (const auto& searchWord : searchWords) {
            auto it = wordFrequency.find(searchWord);
            if (it != wordFrequency.end()) {
                pqxx::result wordResult = txn.exec_params("SELECT id FROM words WHERE word = $1", searchWord);
                int wordId;
                if (wordResult.empty()) {
                    wordResult = txn.exec_params("INSERT INTO words (word) VALUES ($1) RETURNING id", searchWord);
                    wordId = wordResult[0][0].as<int>();
                } else {
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
            } else if (link.find("/") == 0) {
                child_urls.push_back("https://" + host + link);
            }
        }

        for (const std::string& child_url : child_urls) {
            crawl_page(child_url, searchWords, depth - 1, data);
        }

        beast::error_code ec;
        stream.shutdown(ec);
        if (ec == net::error::eof) {
            ec = {};
        }
        if (ec) {
            throw beast::system_error{ec};
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
