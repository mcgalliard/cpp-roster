#include "cli.h"

// Thin entry point. All parsing, dispatch, and exception-to-exit-code mapping
// live in runCli (the CLI boundary), keeping main trivial.
int main(int argc, char** argv) {
    return runCli(argc, argv);
}
