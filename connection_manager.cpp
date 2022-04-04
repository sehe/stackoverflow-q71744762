#include "connection_manager.h"

ConnectionManager::ConnectionManager()
{
}

void ConnectionManager::start(connection_ptr c)
{
  connections_.insert(c);
  c->start();
}

void ConnectionManager::stop(connection_ptr c)
{
    connections_.erase(c);
    c->stop();
}

void ConnectionManager::stop_all()
{
  for (auto c: connections_)
    c->stop();

  connections_.clear();
}

/// this function is used to keep clients up to date with the changes, not used during syncing phase.
void ConnectionManager::sendAllConnections(const std::string& buffer)
{
  for (auto c: connections_)
      c->do_write(buffer);
}
