#pragma once

#include <array>
#include <memory>
#include <string>
#include <boost/asio.hpp>

class ConnectionManager;

/// Represents a single connection from a client.
class Connection : public std::enable_shared_from_this<Connection>
{
public:
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  /// Construct a connection with the given socket.
  explicit Connection(boost::asio::ip::tcp::socket socket, ConnectionManager& manager);

  /// Start the first asynchronous operation for the connection.
  void start();

  /// Stop all asynchronous operations associated with the connection.
  void stop();

  /// Perform an asynchronous write operation.
  void do_write(const std::string& buffer);

  int getNativeHandle();

  ~Connection();

private:
  /// Perform an asynchronous read operation.
  void do_read();

  /// Socket for the connection.
  boost::asio::ip::tcp::socket socket_;

  /// The manager for this connection.
  ConnectionManager& connection_manager_;

  /// Buffer for incoming data.
  std::array<char, 8192> buffer_;

  std::string outgoing_buffer_;
};

typedef std::shared_ptr<Connection> connection_ptr;
