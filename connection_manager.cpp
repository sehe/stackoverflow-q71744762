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
