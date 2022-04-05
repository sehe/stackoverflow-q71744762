#include "connection.h"

#include <boost/algorithm/string.hpp>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

#include "connection_manager.h"
using boost::system::error_code;

Connection::Connection(tcp::socket socket)
    : socket_(std::move(socket)) {}

void Connection::start() { // always assumed on the strand (since connection
                           // just constructed)
    do_read();
}

void Connection::stop() { // public, might not be on the strand
    post(socket_.get_executor(),
         [self = shared_from_this()]() mutable {
             self->do_stop();
         });
}

void Connection::do_stop() { // assumed on the strand
    socket_.cancel(); // trust shared pointer to destruct
}

namespace /*missing code stubs*/ {
    auto split(std::string_view input, char delim) {
        std::vector<std::string_view> result;
        boost::algorithm::split(result, input,
                                boost::algorithm::is_from_range(delim, delim));
        return result;
    }

    std::string getExecutionJsons()   { return std::string(1024,  'E'); }
    std::string getOrdersAsJsons()    { return std::string(13312, 'O'); }
    std::string getPositionsAsJsons() { return std::string(8192,  'P'); }
    std::string createSyncDoneJson()  { return std::string(24576, 'C'); }
} // namespace

void Connection::do_read() {
    auto self(shared_from_this());
    socket_.async_read_some(
        boost::asio::buffer(incoming_),
        [this, self](error_code ec, size_t bytes_transferred) {
            if (!ec) {
                std::string buff_str =
                    std::string(incoming_.data(), bytes_transferred);
                const auto& tokenized_buffer = split(buff_str, ' ');

                if (!tokenized_buffer.empty() &&
                    tokenized_buffer[0] == "sync") {
                    std::cerr << "sync detected on " << socket_.remote_endpoint() << std::endl;
                    /// "syncing connection" sends a specific text
                    /// hence I can separate between sycing and long-lived
                    /// connections here and act accordingly.

                    const auto& exec_json_strs     = getExecutionJsons();
                    const auto& order_json_strs    = getOrdersAsJsons();
                    const auto& position_json_strs = getPositionsAsJsons();
                    const auto& all_json_strs      = exec_json_strs +
                        order_json_strs + position_json_strs +
                        createSyncDoneJson();

                    std::cerr << "All json length: " << all_json_strs.length() << std::endl;
                    /// this is potentially a very large data.
                    do_write(all_json_strs); // already on strand!
                }

                do_read();
            } else {
                std::cerr << "do_read terminating: " << ec.message() << std::endl;
            }
        });
}

void Connection::write(std::string msg) { // public, might not be on the strand
    post(socket_.get_executor(),
         [self = shared_from_this(), msg = std::move(msg)]() mutable {
             self->do_write(std::move(msg));
         });
}

void Connection::do_write(std::string msg) { // assumed on the strand
    outgoing_.push_back(std::move(msg));

    if (outgoing_.size() == 1)
        do_write_loop();
}

void Connection::do_write_loop() {
    if (outgoing_.size() == 0)
        return;

    auto self(shared_from_this());
    async_write( //
        socket_, boost::asio::buffer(outgoing_.front()),
        [this, self](error_code ec, size_t transfer_size) {
            std::cerr << "write completion: " << transfer_size << " bytes ("
                      << ec.message() << ")" << std::endl;

            if (!ec) {
                outgoing_.pop_front();
                do_write_loop();
            } else {
                socket_.cancel();
            }
        });
}
