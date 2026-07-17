#include "cli.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "database.h"
#include "http_server.h"
#include "person.h"
#include "proto_io.h"
#include "roster_manager.h"

namespace {

// Exit codes (kept in one place; mirrored in cli.h and the README).
constexpr int kOk = 0;
constexpr int kUsage = 1;
constexpr int kDbError = 2;
constexpr int kProtoError = 3;

// Thrown for argument/usage problems. Maps to exit code 1.
class UsageException : public std::runtime_error {
public:
    explicit UsageException(const std::string& message)
        : std::runtime_error(message) {}
};

// ---- Argument parsing helpers ----------------------------------------------

// A parsed set of "--key value" options plus standalone flags. Command tokens
// (like "add") are handled before this by the dispatcher.
struct Options {
    // key -> value for "--key value" style options.
    std::vector<std::pair<std::string, std::string>> values;
    // Set of standalone "--flag" options.
    std::vector<std::string> flags;

    bool hasFlag(const std::string& name) const {
        return std::find(flags.begin(), flags.end(), name) != flags.end();
    }

    std::optional<std::string> get(const std::string& name) const {
        for (const auto& kv : values) {
            if (kv.first == name) {
                return kv.second;
            }
        }
        return std::nullopt;
    }
};

// Options that take no value (standalone flags). Everything else consumes the
// following token as its value.
bool isKnownFlag(const std::string& name) {
    return name == "--verbose" || name == "--active-only" ||
           name == "--merge" || name == "--replace";
}

// Parse the tokens following the command into Options. Throws UsageException on
// a malformed option (e.g. "--first" with no value).
Options parseOptions(const std::vector<std::string>& tokens) {
    Options opts;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& tok = tokens[i];
        if (tok.rfind("--", 0) != 0) {
            throw UsageException("Unexpected argument: '" + tok +
                                 "'. Options must start with '--'.");
        }
        if (isKnownFlag(tok)) {
            opts.flags.push_back(tok);
            continue;
        }
        // Value option: the next token is its value.
        if (i + 1 >= tokens.size()) {
            throw UsageException("Option '" + tok + "' requires a value.");
        }
        opts.values.emplace_back(tok, tokens[i + 1]);
        ++i;
    }
    return opts;
}

// Parse a required integer option. Throws UsageException if missing or invalid.
int requireInt(const Options& opts, const std::string& name) {
    const std::optional<std::string> raw = opts.get(name);
    if (!raw) {
        throw UsageException("Missing required option '" + name + "'.");
    }
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(*raw, &consumed);
        if (consumed != raw->size()) {
            throw std::invalid_argument("trailing characters");
        }
        return value;
    } catch (const std::exception&) {
        throw UsageException("Option '" + name + "' must be an integer, got '" +
                             *raw + "'.");
    }
}

// Parse a boolean option value "true"/"false" (case-insensitive). Throws on
// anything else.
bool parseBool(const std::string& name, const std::string& raw) {
    std::string lowered = raw;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowered == "true" || lowered == "1" || lowered == "yes") {
        return true;
    }
    if (lowered == "false" || lowered == "0" || lowered == "no") {
        return false;
    }
    throw UsageException("Option '" + name +
                         "' must be true or false, got '" + raw + "'.");
}

// ---- Output helpers --------------------------------------------------------

void printHelp() {
    std::cout <<
        "roster - a command-line roster manager\n\n"
        "Usage:\n"
        "  roster <command> [options]\n\n"
        "Global options:\n"
        "  --db <path>       Path to the SQLite database (default: roster.db)\n"
        "  --verbose         Show raw underlying error detail on failure\n\n"
        "Commands:\n"
        "  add     --first <F> --last <L> [--role <R>] [--email <E>]\n"
        "  remove  --id <N>\n"
        "  list    [--active-only]\n"
        "  update  --id <N> [--first <F>] [--last <L>] [--role <R>]\n"
        "                    [--email <E>] [--active true|false]\n"
        "  export  --file <path.pb>\n"
        "  import  --file <path.pb> [--merge | --replace]   (default: merge)\n"
        "  serve   [--port <N>]                             (default: 8090)\n"
        "  help    Show this message\n\n"
        "Exit codes: 0 success, 1 usage error, 2 database error, "
        "3 proto I/O error\n";
}

// Print a plain-text, left-aligned table of people.
void printPeople(const std::vector<Person>& people) {
    if (people.empty()) {
        std::cout << "No people found.\n";
        return;
    }

    // Column headers.
    const std::string hId = "ID";
    const std::string hName = "NAME";
    const std::string hRole = "ROLE";
    const std::string hEmail = "EMAIL";
    const std::string hActive = "ACTIVE";

    // Compute widths.
    std::size_t wId = hId.size();
    std::size_t wName = hName.size();
    std::size_t wRole = hRole.size();
    std::size_t wEmail = hEmail.size();
    const std::size_t wActive = hActive.size();  // "ACTIVE" fits "yes"/"no".

    std::vector<std::string> names;
    names.reserve(people.size());
    std::vector<std::string> ids;
    ids.reserve(people.size());
    for (const Person& p : people) {
        const std::string name = p.first_name + " " + p.last_name;
        const std::string id = std::to_string(p.id);
        names.push_back(name);
        ids.push_back(id);
        wId = std::max(wId, id.size());
        wName = std::max(wName, name.size());
        wRole = std::max(wRole, p.role.size());
        wEmail = std::max(wEmail, p.email.size());
    }

    auto pad = [](const std::string& s, std::size_t width) {
        std::string out = s;
        if (out.size() < width) {
            out.append(width - out.size(), ' ');
        }
        return out;
    };

    // Header row.
    std::cout << pad(hId, wId) << "  " << pad(hName, wName) << "  "
              << pad(hRole, wRole) << "  " << pad(hEmail, wEmail) << "  "
              << hActive << "\n";

    // Separator row.
    std::cout << std::string(wId, '-') << "  " << std::string(wName, '-')
              << "  " << std::string(wRole, '-') << "  "
              << std::string(wEmail, '-') << "  " << std::string(wActive, '-')
              << "\n";

    // Data rows.
    for (std::size_t i = 0; i < people.size(); ++i) {
        const Person& p = people[i];
        std::cout << pad(ids[i], wId) << "  " << pad(names[i], wName) << "  "
                  << pad(p.role, wRole) << "  " << pad(p.email, wEmail) << "  "
                  << (p.active ? "yes" : "no") << "\n";
    }
}

// ---- Command handlers ------------------------------------------------------

int cmdAdd(RosterManager& mgr, const Options& opts) {
    Person p;
    const std::optional<std::string> first = opts.get("--first");
    const std::optional<std::string> last = opts.get("--last");
    if (!first || !last) {
        throw UsageException("'add' requires --first and --last.");
    }
    p.first_name = *first;
    p.last_name = *last;
    p.role = opts.get("--role").value_or("");
    p.email = opts.get("--email").value_or("");
    p.active = true;

    const Person created = mgr.addPerson(p);
    std::cout << "Added person #" << created.id << ": " << created.first_name
              << " " << created.last_name << "\n";
    return kOk;
}

int cmdRemove(RosterManager& mgr, const Options& opts) {
    const int id = requireInt(opts, "--id");
    if (mgr.removePerson(id)) {
        std::cout << "Removed person #" << id << ".\n";
        return kOk;
    }
    std::cerr << "No person with id " << id << ".\n";
    return kDbError;
}

int cmdList(RosterManager& mgr, const Options& opts) {
    const bool activeOnly = opts.hasFlag("--active-only");
    printPeople(mgr.listPeople(activeOnly));
    return kOk;
}

int cmdUpdate(RosterManager& mgr, const Options& opts) {
    const int id = requireInt(opts, "--id");
    std::optional<Person> existing = mgr.getPerson(id);
    if (!existing) {
        std::cerr << "No person with id " << id << ".\n";
        return kDbError;
    }

    Person p = *existing;
    if (const auto v = opts.get("--first")) p.first_name = *v;
    if (const auto v = opts.get("--last")) p.last_name = *v;
    if (const auto v = opts.get("--role")) p.role = *v;
    if (const auto v = opts.get("--email")) p.email = *v;
    if (const auto v = opts.get("--active")) p.active = parseBool("--active", *v);

    if (mgr.updatePerson(p)) {
        std::cout << "Updated person #" << id << ".\n";
        return kOk;
    }
    // Row existed a moment ago; a false here means it vanished concurrently.
    std::cerr << "No person with id " << id << ".\n";
    return kDbError;
}

int cmdExport(RosterManager& mgr, const Options& opts) {
    const std::optional<std::string> file = opts.get("--file");
    if (!file) {
        throw UsageException("'export' requires --file <path>.");
    }
    mgr.exportToFile(*file);
    std::cout << "Exported roster to " << *file << ".\n";
    return kOk;
}

int cmdImport(RosterManager& mgr, const Options& opts) {
    const std::optional<std::string> file = opts.get("--file");
    if (!file) {
        throw UsageException("'import' requires --file <path>.");
    }
    if (opts.hasFlag("--merge") && opts.hasFlag("--replace")) {
        throw UsageException("Choose only one of --merge or --replace.");
    }
    const ImportMode mode =
        opts.hasFlag("--replace") ? ImportMode::Replace : ImportMode::Merge;

    const int affected = mgr.importFromFile(*file, mode);
    std::cout << "Imported " << affected << " "
              << (affected == 1 ? "person" : "people") << " from " << *file
              << " (" << (mode == ImportMode::Replace ? "replace" : "merge")
              << ").\n";
    return kOk;
}

int cmdServe(RosterManager& mgr, const Options& opts, const std::string& dbPath) {
    int port = 8090;  // Default HTTP port for the web UI.
    if (const std::optional<std::string> raw = opts.get("--port")) {
        // Parse the port ourselves (rather than via requireInt) so we can add a
        // range check with a tailored message; both problems are usage errors.
        try {
            std::size_t consumed = 0;
            port = std::stoi(*raw, &consumed);
            if (consumed != raw->size()) {
                throw std::invalid_argument("trailing characters");
            }
        } catch (const std::exception&) {
            throw UsageException("Option '--port' must be an integer, got '" +
                                 *raw + "'.");
        }
        if (port < 1 || port > 65535) {
            throw UsageException("Option '--port' must be between 1 and 65535, "
                                 "got '" + *raw + "'.");
        }
    }
    // runServer blocks until the process is interrupted; it returns a process
    // exit code (0 on clean shutdown, non-zero if the port could not be bound).
    return runServer(mgr, port, dbPath);
}

// Split the global --db option out of the token stream, returning the db path
// (default "roster.db") and leaving the remaining tokens for the command. The
// --db option may appear anywhere.
std::string extractDbPath(std::vector<std::string>& tokens) {
    std::string dbPath = "roster.db";
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "--db") {
            if (i + 1 >= tokens.size()) {
                throw UsageException("Option '--db' requires a path value.");
            }
            dbPath = tokens[i + 1];
            tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(i),
                         tokens.begin() + static_cast<std::ptrdiff_t>(i) + 2);
            --i;  // Re-check the position now holding the next token.
        }
    }
    return dbPath;
}

// Dispatch one command. Throws typed exceptions on failure; returns exit code
// on success/handled cases.
int dispatch(const std::string& command, RosterManager& mgr,
             const Options& opts, const std::string& dbPath) {
    if (command == "add") return cmdAdd(mgr, opts);
    if (command == "remove") return cmdRemove(mgr, opts);
    if (command == "list") return cmdList(mgr, opts);
    if (command == "update") return cmdUpdate(mgr, opts);
    if (command == "export") return cmdExport(mgr, opts);
    if (command == "import") return cmdImport(mgr, opts);
    if (command == "serve") return cmdServe(mgr, opts, dbPath);
    throw UsageException("Unknown command: '" + command +
                         "'. Run 'roster help' for usage.");
}

}  // namespace

int runCli(int argc, char** argv) {
    // Collect argv (skip program name) into a vector for easier handling.
    std::vector<std::string> tokens;
    tokens.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        tokens.emplace_back(argv[i]);
    }

    // A pre-scan for verbose so error reporting can honor it even if parsing of
    // the rest fails. Being global, it is removed from the stream entirely so
    // it may appear before or after the command.
    const bool verbose =
        std::find(tokens.begin(), tokens.end(), "--verbose") != tokens.end();
    tokens.erase(std::remove(tokens.begin(), tokens.end(), "--verbose"),
                 tokens.end());

    try {
        // Extract the global --db option from wherever it sits (before or
        // after the command) so the command token is whatever remains first.
        const std::string dbPath = extractDbPath(tokens);

        if (tokens.empty()) {
            printHelp();
            return kUsage;  // No command given is a usage error.
        }

        // help / -h / --help before touching the database.
        const std::string& first = tokens.front();
        if (first == "help" || first == "-h" || first == "--help") {
            printHelp();
            return kOk;
        }

        // Pull the command token off the front, then parse the remaining
        // options.
        const std::string command = tokens.front();
        tokens.erase(tokens.begin());

        const Options opts = parseOptions(tokens);

        // Open the database only after argument parsing succeeds.
        RosterManager mgr(dbPath);
        return dispatch(command, mgr, opts, dbPath);

    } catch (const UsageException& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return kUsage;
    } catch (const ValidationException& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return kUsage;
    } catch (const DatabaseException& e) {
        std::cerr << "Database error: " << e.what() << "\n";
        if (verbose) {
            std::cerr << "  detail: " << e.detail() << "\n";
        }
        return kDbError;
    } catch (const ProtoIoException& e) {
        std::cerr << "File error: " << e.what() << "\n";
        if (verbose) {
            std::cerr << "  detail: " << e.detail() << "\n";
        }
        return kProtoError;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << "\n";
        return kUsage;
    }
}
