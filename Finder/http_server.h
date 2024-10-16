#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <boost/asio.hpp>
using boost::asio::ip::tcp;
void start_http_server(std::string &dt);
void start_accept(boost::shared_ptr<tcp::socket> sock);
#endif // HTTP_SERVER_H
