#pragma once

#include <set>
#include "connection.h"

/// Manages open connections so that they may be cleanly stopped when the server
/// needs to shut down.
class ConnectionManager
{
public:
  ConnectionManager(const ConnectionManager&) = delete;
  ConnectionManager& operator=(const ConnectionManager&) = delete;

  /// Construct a connection manager.
  ConnectionManager();

  /// Add the specified connection to the manager and start it.
  void start(connection_ptr c);

  /// Stop the specified connection.
  void stop(connection_ptr c);

  /// Stop all connections.
  void stop_all();

  void sendAllConnections(const std::string& buffer);

private:
  /// The managed connections.
  std::set<connection_ptr> connections_;
};
