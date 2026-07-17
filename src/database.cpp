#include "database.h"

#include <sqlite3.h>

#include <string>
#include <utility>
#include <vector>

namespace {

// RAII guard for a prepared statement: guarantees sqlite3_finalize is called
// exactly once, even if a step/bind throws. Non-copyable.
class StatementGuard {
public:
    StatementGuard(sqlite3* db, const std::string& sql) {
        const int rc = sqlite3_prepare_v2(db, sql.c_str(),
                                          static_cast<int>(sql.size()) + 1,
                                          &stmt_, nullptr);
        if (rc != SQLITE_OK) {
            const std::string msg = sqlite3_errmsg(db);
            if (stmt_) {
                sqlite3_finalize(stmt_);
                stmt_ = nullptr;
            }
            throw DatabaseException("Failed to prepare a database statement.",
                                    "Failed to prepare statement: " + msg);
        }
    }

    ~StatementGuard() {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    StatementGuard(const StatementGuard&) = delete;
    StatementGuard& operator=(const StatementGuard&) = delete;

    sqlite3_stmt* get() const { return stmt_; }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

// Bind a std::string as text. SQLITE_TRANSIENT tells SQLite to copy the bytes,
// so the source string need not outlive the step call.
void bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(),
                      static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

// Read a text column that may be NULL, returning an empty string for NULL.
std::string columnText(sqlite3_stmt* stmt, int index) {
    const unsigned char* text = sqlite3_column_text(stmt, index);
    if (text == nullptr) {
        return std::string();
    }
    return std::string(reinterpret_cast<const char*>(text));
}

// Materialize a Person from the current row of a SELECT over the standard
// column order: id, first_name, last_name, role, email, active.
Person readPerson(sqlite3_stmt* stmt) {
    Person p;
    p.id = sqlite3_column_int(stmt, 0);
    p.first_name = columnText(stmt, 1);
    p.last_name = columnText(stmt, 2);
    p.role = columnText(stmt, 3);
    p.email = columnText(stmt, 4);
    p.active = sqlite3_column_int(stmt, 5) != 0;
    return p;
}

}  // namespace

Database::Database(const std::string& path) {
    const int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        // sqlite3_open may still allocate a handle even on failure; capture the
        // message then close it so we do not leak.
        const std::string msg = db_ ? sqlite3_errmsg(db_) : "out of memory";
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw DatabaseException("Could not open database '" + path + "'.",
                                "Failed to open database '" + path + "': " + msg);
    }

    // Enforce foreign keys / good defaults; also makes UNIQUE constraints active
    // (they always are, but this documents intent). Ignore failure quietly.
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    ensureSchema();
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

Database::Database(Database&& other) noexcept : db_(other.db_) {
    other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        if (db_) {
            sqlite3_close(db_);
        }
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

void Database::fail(const std::string& context) const {
    const std::string msg = db_ ? sqlite3_errmsg(db_) : "no database handle";
    throw DatabaseException(context + ".", context + ": " + msg);
}

void Database::ensureSchema() {
    const char* kSchema =
        "CREATE TABLE IF NOT EXISTS people ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    first_name TEXT NOT NULL,"
        "    last_name  TEXT NOT NULL,"
        "    role       TEXT,"
        "    email      TEXT UNIQUE,"
        "    active     INTEGER NOT NULL DEFAULT 1"
        ");";

    char* errmsg = nullptr;
    const int rc = sqlite3_exec(db_, kSchema, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        const std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw DatabaseException("Failed to initialize the database schema.",
                                "Failed to create schema: " + msg);
    }
}

int Database::insertPerson(const Person& person) {
    const std::string sql =
        "INSERT INTO people (first_name, last_name, role, email, active) "
        "VALUES (?, ?, ?, ?, ?);";
    StatementGuard guard(db_, sql);
    sqlite3_stmt* stmt = guard.get();

    bindText(stmt, 1, person.first_name);
    bindText(stmt, 2, person.last_name);
    bindText(stmt, 3, person.role);
    // Store an absent email as SQL NULL so the UNIQUE constraint permits
    // multiple people without an email address.
    if (person.email.empty()) {
        sqlite3_bind_null(stmt, 4);
    } else {
        bindText(stmt, 4, person.email);
    }
    sqlite3_bind_int(stmt, 5, person.active ? 1 : 0);

    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        if (rc == SQLITE_CONSTRAINT) {
            throw DatabaseException(
                "A person with email '" + person.email + "' already exists.");
        }
        fail("Failed to insert person");
    }

    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool Database::updatePerson(const Person& person) {
    const std::string sql =
        "UPDATE people SET first_name = ?, last_name = ?, role = ?, "
        "email = ?, active = ? WHERE id = ?;";
    StatementGuard guard(db_, sql);
    sqlite3_stmt* stmt = guard.get();

    bindText(stmt, 1, person.first_name);
    bindText(stmt, 2, person.last_name);
    bindText(stmt, 3, person.role);
    if (person.email.empty()) {
        sqlite3_bind_null(stmt, 4);
    } else {
        bindText(stmt, 4, person.email);
    }
    sqlite3_bind_int(stmt, 5, person.active ? 1 : 0);
    sqlite3_bind_int(stmt, 6, person.id);

    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        if (rc == SQLITE_CONSTRAINT) {
            throw DatabaseException(
                "A person with email '" + person.email + "' already exists.");
        }
        fail("Failed to update person");
    }

    return sqlite3_changes(db_) > 0;
}

bool Database::deletePerson(int id) {
    const std::string sql = "DELETE FROM people WHERE id = ?;";
    StatementGuard guard(db_, sql);
    sqlite3_stmt* stmt = guard.get();

    sqlite3_bind_int(stmt, 1, id);

    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fail("Failed to delete person");
    }

    return sqlite3_changes(db_) > 0;
}

std::optional<Person> Database::getPerson(int id) const {
    const std::string sql =
        "SELECT id, first_name, last_name, role, email, active "
        "FROM people WHERE id = ?;";
    StatementGuard guard(db_, sql);
    sqlite3_stmt* stmt = guard.get();

    sqlite3_bind_int(stmt, 1, id);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        return readPerson(stmt);
    }
    if (rc == SQLITE_DONE) {
        return std::nullopt;
    }
    fail("Failed to fetch person");
}

std::vector<Person> Database::listPeople(bool activeOnly) const {
    std::string sql =
        "SELECT id, first_name, last_name, role, email, active FROM people ";
    if (activeOnly) {
        sql += "WHERE active = 1 ";
    }
    sql += "ORDER BY last_name COLLATE NOCASE, first_name COLLATE NOCASE, id;";

    StatementGuard guard(db_, sql);
    sqlite3_stmt* stmt = guard.get();

    std::vector<Person> people;
    for (;;) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            people.push_back(readPerson(stmt));
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            fail("Failed to list people");
        }
    }
    return people;
}

void Database::clearAll() {
    char* errmsg = nullptr;
    const int rc =
        sqlite3_exec(db_, "DELETE FROM people;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        const std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw DatabaseException("Failed to clear the roster.",
                                "Failed to clear people: " + msg);
    }
}
