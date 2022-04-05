#pragma once

#include <boost/asio.hpp>

#include <array>
#include <deque>
#include <memory>
#include <string>
using boost::asio::ip::tcp;

class ConnectionManager;

/// Represents a single connection from a client.
class Connection : public std::enable_shared_from_this<Connection> {
  public:
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    /// Construct a connection with the given socket.
    explicit Connection(tcp::socket socket, ConnectionManager& manager);

    void start();
    void stop();
    void write(std::string msg);

  private:
    void do_stop();
    void do_write(std::string msg);
    void do_write_loop();

    /// Perform an asynchronous read operation.
    void do_read();

    /// Socket for the connection.
    tcp::socket socket_;

    /// The manager for this connection.
    ConnectionManager& connection_manager_;

    /// Buffer for incoming data.
    std::array<char, 8192> buffer_;

    std::deque<std::string> outgoing_;
};

using connection_ptr = std::shared_ptr<Connection>;
