#include "roster_manager.h"

#include <cstddef>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "database.h"
#include "person.h"
#include "proto_io.h"

namespace {

// Trim leading/trailing ASCII whitespace.
std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const std::size_t start = s.find_first_not_of(ws);
    if (start == std::string::npos) {
        return std::string();
    }
    const std::size_t end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

// Very light email sanity check: exactly the spec's requirement plus guards
// against a leading/trailing '@'. Not a full RFC validator by design.
bool looksLikeEmail(const std::string& email) {
    const std::size_t at = email.find('@');
    if (at == std::string::npos) {
        return false;
    }
    // '@' must not be first or last, and there must be exactly one.
    if (at == 0 || at == email.size() - 1) {
        return false;
    }
    if (email.find('@', at + 1) != std::string::npos) {
        return false;
    }
    return true;
}

}  // namespace

RosterManager::RosterManager(const std::string& dbPath) : db_(dbPath) {}

void RosterManager::validate(const Person& person) {
    if (trim(person.first_name).empty()) {
        throw ValidationException("First name must not be empty.");
    }
    if (trim(person.last_name).empty()) {
        throw ValidationException("Last name must not be empty.");
    }
    if (!person.email.empty() && !looksLikeEmail(person.email)) {
        throw ValidationException(
            "Email '" + person.email + "' does not look valid (must contain a "
            "single '@' with text on both sides).");
    }
}

Person RosterManager::addPerson(const Person& person) {
    validate(person);
    Person toInsert = person;
    toInsert.id = 0;  // The DB assigns the id.
    const int id = db_.insertPerson(toInsert);
    toInsert.id = id;
    return toInsert;
}

bool RosterManager::removePerson(int id) {
    return db_.deletePerson(id);
}

bool RosterManager::updatePerson(const Person& person) {
    validate(person);
    return db_.updatePerson(person);
}

std::optional<Person> RosterManager::getPerson(int id) const {
    return db_.getPerson(id);
}

std::vector<Person> RosterManager::listPeople(bool activeOnly) const {
    return db_.listPeople(activeOnly);
}

void RosterManager::exportToFile(const std::string& path) const {
    // Export everyone, including inactive members.
    const std::vector<Person> people = db_.listPeople(/*activeOnly=*/false);
    saveRosterToProto(people, path);
}

void RosterManager::exportToStream(std::ostream& out) const {
    // Same rule as exportToFile: export everyone, including inactive members.
    const std::vector<Person> people = db_.listPeople(/*activeOnly=*/false);
    saveRosterToProto(people, out);
}

int RosterManager::importFromFile(const std::string& path, ImportMode mode) {
    return applyImport(loadRosterFromProto(path), mode);
}

int RosterManager::importFromStream(std::istream& in, ImportMode mode) {
    return applyImport(loadRosterFromProto(in), mode);
}

int RosterManager::applyImport(std::vector<Person> incoming, ImportMode mode) {
    int affected = 0;

    if (mode == ImportMode::Replace) {
        db_.clearAll();
        for (Person p : incoming) {
            validate(p);
            p.id = 0;  // Never trust imported ids.
            db_.insertPerson(p);
            ++affected;
        }
        return affected;
    }

    // Merge: for each incoming person, if it has an email that already exists in
    // the DB, update that row; otherwise insert a new row. Imported ids are not
    // trusted. To match by email we scan the current roster once and build the
    // lookup in memory (roster sizes here are small).
    const std::vector<Person> existing = db_.listPeople(/*activeOnly=*/false);

    for (Person p : incoming) {
        validate(p);
        p.id = 0;

        int matchId = 0;
        if (!p.email.empty()) {
            for (const Person& e : existing) {
                if (!e.email.empty() && e.email == p.email) {
                    matchId = e.id;
                    break;
                }
            }
        }

        if (matchId != 0) {
            p.id = matchId;
            db_.updatePerson(p);
        } else {
            db_.insertPerson(p);
        }
        ++affected;
    }

    return affected;
}
