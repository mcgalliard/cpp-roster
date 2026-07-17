#pragma once

#include <iosfwd>
#include <stdexcept>
#include <string>
#include <vector>

#include "person.h"

// Thrown for any failure while reading or writing a protobuf roster file.
// `what()` is a clean message; `detail()` carries any lower-level detail shown
// only under --verbose (equals the message when constructed with one argument).
class ProtoIoException : public std::runtime_error {
public:
    explicit ProtoIoException(const std::string& message)
        : std::runtime_error(message), detail_(message) {}

    ProtoIoException(const std::string& message, const std::string& detail)
        : std::runtime_error(message), detail_(detail) {}

    const std::string& detail() const noexcept { return detail_; }

private:
    std::string detail_;
};

// Serializes `people` to a binary protobuf Roster message at `path`, stamping
// the message's exported_at with the current local time (ISO8601). Throws
// ProtoIoException on failure.
void saveRosterToProto(const std::vector<Person>& people,
                       const std::string& path);

// Stream overload: serializes `people` to `out` (same wire format and
// exported_at stamping as the path version, which delegates here). Throws
// ProtoIoException on failure. Used by the web export endpoint.
void saveRosterToProto(const std::vector<Person>& people, std::ostream& out);

// Parses a binary protobuf Roster message from `path` into a vector of Person.
// Throws ProtoIoException on failure (missing file, parse error, etc.).
std::vector<Person> loadRosterFromProto(const std::string& path);

// Stream overload: parses a binary protobuf Roster message from `in` (the path
// version delegates here). Throws ProtoIoException on parse failure. Used by the
// web import endpoint.
std::vector<Person> loadRosterFromProto(std::istream& in);
