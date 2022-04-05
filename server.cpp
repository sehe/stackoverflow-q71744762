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
