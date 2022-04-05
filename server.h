#pragma once

#include <boost/asio.hpp>
#include <string>
#include "connection.h"
#include "connection_manager.h"

class Server {
  public:
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Construct the server to listen on the specified TCP address and port,
    /// and serve up files from the given directory.
    explicit Server(const std::string& address, const std::string& port);

    /// Run the server's io_service loop.
    void run();

    bool deliver(const std::string& buffer);

  private:
    void do_accept();
    void do_await_signal();

    boost::asio::io_context      io_context_;
    boost::asio::any_io_executor strand_{io_context_.get_executor()};
    boost::asio::signal_set      signals_{strand_};
    tcp::acceptor                acceptor_{strand_};
    ConnectionManager            connection_manager_;
};
