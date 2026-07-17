#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "person.h"

// Forward-declare the opaque SQLite handle types so this header does not need
// to pull in <sqlite3.h>. The definitions live in database.cpp.
struct sqlite3;
struct sqlite3_stmt;

// Thrown for any SQLite-level failure. `what()` returns a clean, user-facing
// message; `detail()` returns the raw underlying sqlite3_errmsg text (shown only
// under --verbose). When constructed with a single argument the detail equals
// the message.
class DatabaseException : public std::runtime_error {
public:
    explicit DatabaseException(const std::string& message)
        : std::runtime_error(message), detail_(message) {}

    DatabaseException(const std::string& message, const std::string& detail)
        : std::runtime_error(message), detail_(detail) {}

    const std::string& detail() const noexcept { return detail_; }

private:
    std::string detail_;
};

// RAII wrapper around a single sqlite3 connection. Non-copyable (owns a handle),
// movable so it can be returned/relocated. All SQL in the program lives here.
class Database {
public:
    // Opens (creating if needed) the database at `path` and ensures the schema
    // exists. Throws DatabaseException on failure.
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    // Inserts a new person. Returns the row id assigned by SQLite. The input's
    // id field is ignored. Throws DatabaseException (with a clear message on a
    // duplicate-email UNIQUE violation).
    int insertPerson(const Person& person);

    // Updates every column of the row matching person.id. Returns true if a row
    // was actually changed, false if no row with that id exists.
    bool updatePerson(const Person& person);

    // Deletes the row with the given id. Returns true if a row was removed.
    bool deletePerson(int id);

    // Fetches a single person by id, or std::nullopt if not found.
    std::optional<Person> getPerson(int id) const;

    // Returns all people (optionally only active ones), ordered by last then
    // first name.
    std::vector<Person> listPeople(bool activeOnly) const;

    // Removes every row from the table (used by import --replace).
    void clearAll();

private:
    void ensureSchema();

    // Small helpers kept private; they centralize error handling.
    [[noreturn]] void fail(const std::string& context) const;

    sqlite3* db_ = nullptr;
};
