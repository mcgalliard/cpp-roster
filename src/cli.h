#pragma once

// Entry point for the command-line interface. Parses argv, dispatches to the
// RosterManager, prints results, and returns a process exit code:
//   0  success
//   1  usage error (bad/missing arguments, validation failure)
//   2  database error
//   3  proto I/O error
//
// Exceptions are caught here / in main so callers never see a raw throw. Raw
// error detail is shown only when --verbose is passed.
int runCli(int argc, char** argv);
