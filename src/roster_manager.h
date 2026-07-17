#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "database.h"
#include "person.h"

// Thrown when input fails business-rule validation (empty name, bad email,
// etc.). Distinct from DatabaseException so the CLI can map it to a usage error.
class ValidationException : public std::runtime_error {
public:
    explicit ValidationException(const std::string& message)
        : std::runtime_error(message) {}
};

// How an import merges with existing data.
enum class ImportMode {
    Merge,    // Insert new rows; update existing rows matched by email.
    Replace,  // Clear the table, then insert everything from the file.
};

// Facade over Database. Owns the connection and enforces validation before any
// data reaches the DB. The CLI talks only to this class (plus proto_io).
class RosterManager {
public:
    explicit RosterManager(const std::string& dbPath);

    // Validates and inserts. Returns the created person with its assigned id.
    Person addPerson(const Person& person);

    // Deletes by id. Returns false if no such person exists.
    bool removePerson(int id);

    // Validates and applies a full update. Returns false if no such id.
    bool updatePerson(const Person& person);

    // Fetches a single person by id.
    std::optional<Person> getPerson(int id) const;

    // Lists people, optionally active only.
    std::vector<Person> listPeople(bool activeOnly) const;

    // Writes the current roster (all people, including inactive) to a proto file.
    void exportToFile(const std::string& path) const;

    // Loads a proto file and applies it per `mode`. Returns the number of people
    // inserted or updated. Imported ids are never trusted.
    int importFromFile(const std::string& path, ImportMode mode);

private:
    // Validates a person for insert/update. Throws ValidationException on error.
    static void validate(const Person& person);

    Database db_;
};
