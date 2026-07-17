#pragma once

#include <string>

class RosterManager;

// Runs the web UI HTTP server, blocking until the process is interrupted
// (Ctrl+C). Serves a small REST API backed by `mgr` plus the static frontend in
// the web/ directory (see runServer's web-root resolution). `port` must be a
// valid TCP port; `dbPath` is used only for the startup banner. Returns a
// process exit code (0 on a clean shutdown, non-zero if the port could not be
// bound).
//
// THREAD SAFETY: httplib dispatches requests across a thread pool, but the
// RosterManager / underlying sqlite3 handle is NOT thread-safe. Every access to
// `mgr` inside the request handlers is serialized by a single std::mutex owned
// by runServer.
int runServer(RosterManager& mgr, int port, const std::string& dbPath);
