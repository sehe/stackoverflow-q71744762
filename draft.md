Reviewing, adding some missing code bits:

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

Now the things I notice are:

 1. you have a single `io_service`, so a single thread. Okay, so no strands should be required unless you have threads in your other code (`main`, e.g.?).

 1. A particular reason to suspect that threads are at play is that nobody could possibly call `Server::deliver` because `run()` is blocking. This means that whenever you cal `deliver()` now it causes a data race, which leads to [Undefined Behaviour](https://en.wikipedia.org/wiki/Undefined_behavior)

    The casual comment
 
         /// this function is used to keep clients up to date with the changes,
         /// not used during syncing phase.
 
    does not do much to remove this concern. The code needs to defend against misuse. Comments do not get executed. Make it better:
 
         void Server::deliver(const std::string& buffer) {
             post(io_context_,
                  [this, buffer] { connection_manager_.broadcast(std::move(buffer)); });
         }
 
 1. you do not check that previous writes are completed before accepting a "new" one. This means that calling `Connection::do_write` results in [Undefined Behaviour](https://en.wikipedia.org/wiki/Undefined_behavior) for two reasons:
 
     - modifying `outgoing_buffer_` during an ongoing async operation that uses that buffer is UB
 
     - having two overlapped `async_write` on the same IO object is UB (see [docs](https://www.boost.org/doc/libs/1_78_0/doc/html/boost_asio/reference/async_write/overload1.html#:~:text=This%20operation%20is,this%20operation%20complete)
 
    The typical way to fix that is to have a queue of outgoing messages instead.
 
 1. using `async_read_some` is rarely what you want, especially since the reads don't accumulate into a dynamic buffer. This means that if your packets get separated at unexpected boundaries, you may not detect commands at all, or incorrectly.
 
    Instead consider `asio::async_read_until` with a dynamic buffer (e.g.
      -  read directly into `std::string` so you don't have to copy the buffer into a string
      - read into `streambuf` so you can use `std::istream(&sbuf_)` to parse instead of tokenizing

 1. Concatenating `all_json_strs` which clearly *have* to be owning text containers is wasteful. Instead, use a const-buffer-sequence to combine them all without copying.

    Better yet, consider a streaming approach to JSON serialization so not all the JSON needs to be serialized in memory at any given time.

 1. Don't declare empty destructors (`~Connection`). They're pessimizations

 1. Likewise for empty constructors (`ConnectionManager`). If you must, consider

        ConnectionManager::ConnectionManager() = default;

 1. The `getNativeHandle` gives me more questions about other code that may interfere. E.g. it may indicate other libraries doing operations, which again can lead to overlapped reads/writes, or it could be a sign of more code living on threads (as `Server::run()` is by definition blocking)

 1. Connection manager should probably hold `weak_ptr`, so `Connection`s could eventually terminate. Now, the last reference is **by defintion** held in the connection manager, meaning nothing ever gets destructed when the peer disconnects or the session fails for some other reason.

 1. This is not idiomatic:

        // Check whether the server was stopped by a signal before this
        // completion handler had a chance to run.
        if (!acceptor_.is_open()) {
            return;
        }

    If you closed the acceptor, the completion handler is called with `error::operation_aborted` anyways. Simply handle that, e.g. in the final version I'll post later:

        // separate strand for each connection - just in case you ever add threads
        acceptor_.async_accept(
            make_strand(io_context_), [this](error_code ec, tcp::socket sock) {
                if (!ec) {
                    connection_manager_.register_and_start(
                        std::make_shared<Connection>(std::move(sock),
                                                     connection_manager_));
                    do_accept();
                }
            });

 1. I notice this comment:

        // The server is stopped by cancelling all outstanding asynchronous
        // operations. Once all operations have finished the io_service::run()
        // call will exit.

    In fact you never `cancel()` any operation on any IO object in your code. Again, comments aren't executed. It's better to indeed do as you say, and let the destructors close the resources. This prevents spurious errors when objects are used-after-close, and also prevents very annoying race conditions when e.g. you closed the handle, some other thread re-opened a new stream on the same filedescriptor and you had given out the handle to a third party (using `getNativeHandle`)... you see where this leads?


## Reproducing The Problem?

Having reviewed this way, I tried to repro the issue, so I created fake data:

        std::string getExecutionJsons()   { return std::string(1024,  'E'); }
        std::string getOrdersAsJsons()    { return std::string(13312, 'O'); }
        std::string getPositionsAsJsons() { return std::string(8192,  'P'); }
        std::string createSyncDoneJson()  { return std::string(24576, 'C'); }

With some minor tweaks to the Connection class:

        std::string buff_str =
            std::string(buffer_.data(), bytes_transferred);
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

We get the server outputting 

    sync detected on 127.0.0.1:43012
    All json length: 47104
    sync detected on 127.0.0.1:43044
    All json length: 47104

And clients faked with netcat:

    $ netcat localhost 8989 <<< 'sync me' > expected
    ^C
    $ wc -c expected 
    47104 expected

Good. Now let's cause premature disconnect:

    netcat localhost 8989 -w0 <<< 'sync me' > truncated
    $ wc -c truncated 
    0 truncated

So, it does lead to early close, but server still says

    sync detected on 127.0.0.1:44176
    All json length: 47104

Let's instrument `do_write` as well:

        async_write( //
            socket_, boost::asio::buffer(outgoing_buffer_, outgoing_buffer_.size()),
            [/*this,*/ self](error_code ec, size_t transfer_size) {
                std::cerr << "do_write completion: " << transfer_size << " bytes ("
                          << ec.message() << ")" << std::endl;

                if (!ec) {
                    /// everything is fine.
                } else {
                    /// what to do here?
                    // FIXME: probably cancel the read loop so the connection
                    // closes?
                }
            });

Now we see:

    sync detected on 127.0.0.1:44494
    All json length: 47104
    do_write completion: 47104 bytes (Success)
    sync detected on 127.0.0.1:44512
    All json length: 47104
    do_write completion: 32768 bytes (Operation canceled)

For one disconnected and one "okay" connection. 

No sign of crashes/undefined behaviour. Let's check with `-fsanitize=address,undefined`: clean record, even adding a heartbeat:

    int main() {
        Server s("127.0.0.1", "8989");

        std::thread yolo([&s] {
            using namespace std::literals;
            int i = 1;

            do {
                std::this_thread::sleep_for(5s);
            } while (s.deliver("HEARTBEAT DEMO " + std::to_string(i++)));
        });

        s.run();

        yolo.join();
    }

## Conclusion

The only problem highlighted above that weren't addressed were:

 - additional threading issues not shown (perhaps via `getNativeHandle`)

 - the fact that you can have overlapping writes in the Connection `do_write`. Fixing that:

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
    
                        // This would ideally be enough to free the connection, but
                        // since `ConnectionManager` doesn't use `weak_ptr` you need to
                        // force the issue using kind of an "umbellical cord reflux":
                        connection_manager_.stop(self);
                    }
                });
        }
  
As you can see I also split `write`/`do_write` to prevent off-strand invocation. Same with `stop`.

## Full Listing

A full listing with all the remarks/fixes from above:



 * File `connection.h`

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


 * File `connection_manager.h`

        #pragma once
        
        #include <list>
        #include "connection.h"
        
        /// Manages open connections so that they may be cleanly stopped when the server
        /// needs to shut down.
        class ConnectionManager {
          public:
            ConnectionManager(const ConnectionManager&) = delete;
            ConnectionManager& operator=(const ConnectionManager&) = delete;
            ConnectionManager() = default; // could be split across h/cpp if you wanted
        
            void register_and_start(connection_ptr c);
            void stop(connection_ptr c);
            void stop_all();
        
            void broadcast(const std::string& buffer);
        
            // purge defunct connections, returns remaining active connections
            size_t garbage_collect();
        
          private:
            using handle = std::weak_ptr<connection_ptr::element_type>;
            std::list<handle> connections_;
        };


 * File `server.h`

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


 * File `connection.cpp`

        #include "connection.h"
        
        #include <boost/algorithm/string.hpp>
        #include <iostream>
        #include <thread>
        #include <utility>
        #include <vector>
        
        #include "connection_manager.h"
        using boost::system::error_code;
        
        Connection::Connection(tcp::socket socket, ConnectionManager& manager)
            : socket_(std::move(socket))
            , connection_manager_(manager) {}
        
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
                boost::asio::buffer(buffer_),
                [this, self](error_code ec, size_t bytes_transferred) {
                    if (!ec) {
                        std::string buff_str =
                            std::string(buffer_.data(), bytes_transferred);
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
                        connection_manager_.stop(shared_from_this());
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
        
                        // This would ideally be enough to free the connection, but
                        // since `ConnectionManager` doesn't use `weak_ptr` you need to
                        // force the issue using kind of an "umbellical cord reflux":
                        connection_manager_.stop(self);
                    }
                });
        }


 * File `connection_manager.cpp`

        #include "connection_manager.h"
        
        void ConnectionManager::register_and_start(connection_ptr c) {
            connections_.emplace_back(c);
            c->start();
        }
        
        void ConnectionManager::stop(connection_ptr c) {
            c->stop();
        }
        
        void ConnectionManager::stop_all() {
            for (auto h : connections_)
                if (auto c = h.lock())
                    c->stop();
        }
        
        /// this function is used to keep clients up to date with the changes, not used
        /// during syncing phase.
        void ConnectionManager::broadcast(const std::string& buffer) {
            for (auto h : connections_)
                if (auto c = h.lock())
                    c->write(buffer);
        }
        
        size_t ConnectionManager::garbage_collect() {
            connections_.remove_if(std::mem_fn(&handle::expired));
            return connections_.size();
        }


 * File `server.cpp`

        #include "server.h"
        #include <signal.h>
        #include <utility>
        
        using boost::system::error_code;
        
        Server::Server(const std::string& address, const std::string& port)
            : io_context_(1) // THREAD HINT: single threaded
            , connection_manager_()
        {
            // Register to handle the signals that indicate when the server should exit.
            // It is safe to register for the same signal multiple times in a program,
            // provided all registration for the specified signal is made through Asio.
            signals_.add(SIGINT);
            signals_.add(SIGTERM);
        #if defined(SIGQUIT)
            signals_.add(SIGQUIT);
        #endif // defined(SIGQUIT)
        
            do_await_signal();
        
            // Open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
            tcp::resolver resolver(io_context_);
            tcp::endpoint endpoint = *resolver.resolve({address, port});
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();
        
            do_accept();
        }
        
        void Server::run() {
            // The io_service::run() call will block until all asynchronous operations
            // have finished. While the server is running, there is always at least one
            // asynchronous operation outstanding: the asynchronous accept call waiting
            // for new incoming connections.
            io_context_.run();
        }
        
        void Server::do_accept() {
            // separate strand for each connection - just in case you ever add threads
            acceptor_.async_accept(
                make_strand(io_context_), [this](error_code ec, tcp::socket sock) {
                    if (!ec) {
                        connection_manager_.register_and_start(
                            std::make_shared<Connection>(std::move(sock),
                                                         connection_manager_));
                        do_accept();
                    }
                });
        }
        
        void Server::do_await_signal() {
            signals_.async_wait([this](error_code /*ec*/, int /*signo*/) {
                // handler on the strand_ because of the executor on signals_
        
                // The server is stopped by cancelling all outstanding asynchronous
                // operations. Once all operations have finished the io_service::run()
                // call will exit.
                acceptor_.cancel();
                connection_manager_.stop_all();
            });
        }
        
        bool Server::deliver(const std::string& buffer) {
            if (io_context_.stopped()) {
                return false;
            }
            post(io_context_,
                 [this, buffer] { connection_manager_.broadcast(std::move(buffer)); });
            return true;
        }


 * File `test.cpp`

        #include "server.h"
        
        int main() {
            Server s("127.0.0.1", "8989");
        
            std::thread yolo([&s] {
                using namespace std::literals;
                int i = 1;
        
                do {
                    std::this_thread::sleep_for(5s);
                } while (s.deliver("HEARTBEAT DEMO " + std::to_string(i++)));
            });
        
            s.run();
        
            yolo.join();
        }



