#include "connection.h"

#include <utility>
#include <vector>
#include <iostream>
#include <thread>
#include <boost/algorithm/string.hpp>

#include "connection_manager.h"

Connection::Connection(boost::asio::ip::tcp::socket socket, ConnectionManager& manager)
    : socket_(std::move(socket))
    , connection_manager_(manager)
{
}

void Connection::start()
{
  do_read();
}

void Connection::stop()
{
  socket_.close();
}

Connection::~Connection()
{
}

namespace /*missing code stubs*/ {
    auto split(std::string_view input, char delim) {
        std::vector<std::string_view> result;
        boost::algorithm::split(result, input,
                                boost::algorithm::is_from_range(delim, delim));
        return result;
    }

    std::string getExecutionJsons()   { return ""; }
    std::string getOrdersAsJsons()    { return ""; }
    std::string getPositionsAsJsons() { return ""; }
    std::string createSyncDoneJson()  { return ""; }
}

void Connection::do_read()
{
  auto self(shared_from_this());
  socket_.async_read_some(boost::asio::buffer(buffer_), [this, self](boost::system::error_code ec, std::size_t bytes_transferred) {
        if (!ec) {
            std::string buff_str = std::string(buffer_.data(), bytes_transferred);
            const auto& tokenized_buffer = split(buff_str, ' ');
            
            if(!tokenized_buffer.empty() && tokenized_buffer[0] == "sync") {
                /// "syncing connection" sends a specific text
                /// hence I can separate between sycing and long-lived connections here and act accordingly.

                const auto& exec_json_strs = getExecutionJsons();
                const auto& order_json_strs = getOrdersAsJsons();
                const auto& position_json_strs = getPositionsAsJsons();
                const auto& all_json_strs = exec_json_strs + order_json_strs + position_json_strs + createSyncDoneJson();
                
                /// this is potentially a very large data.
                do_write(all_json_strs);
            }

            do_read();
        } else {
          connection_manager_.stop(shared_from_this());
        }
      });
}

void Connection::do_write(const std::string& write_buffer)
{
  outgoing_buffer_ = write_buffer;

  auto self(shared_from_this());
  boost::asio::async_write(socket_, boost::asio::buffer(outgoing_buffer_, outgoing_buffer_.size()), [/*this,*/ self](boost::system::error_code ec, std::size_t /*transfer_size*/) {
        if (!ec) {
           /// everything is fine.
        } else {
           /// what to do here?
        }
      });
}
