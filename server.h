#pragma once

#include <boost/asio.hpp>
#include <string>
#include "connection.h"
#include "connection_manager.h"

class Server
{
public:
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  /// Construct the server to listen on the specified TCP address and port, and
  /// serve up files from the given directory.
  explicit Server(const std::string& address, const std::string& port);

  /// Run the server's io_service loop.
  void run();

  void deliver(const std::string& buffer);

private:
  /// Perform an asynchronous accept operation.
  void do_accept();

  /// Wait for a request to stop the server.
  void do_await_stop();

  /// The io_service used to perform asynchronous operations.
  boost::asio::io_service io_service_;

  /// The signal_set is used to register for process termination notifications.
  boost::asio::signal_set signals_;

  /// Acceptor used to listen for incoming connections.
  boost::asio::ip::tcp::acceptor acceptor_;

  /// The connection manager which owns all live connections.
  ConnectionManager connection_manager_;

  /// The *NEXT* socket to be accepted.
  boost::asio::ip::tcp::socket socket_;
};
