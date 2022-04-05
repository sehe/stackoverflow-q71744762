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
