#include "proto_io.h"

#include <cstddef>
#include <ctime>
#include <fstream>
#include <ios>
#include <string>
#include <utility>
#include <vector>

#include "roster.pb.h"

namespace {

// Returns the current local time formatted as ISO8601, e.g.
// "2026-07-17T14:03:55". No timezone suffix (kept simple and local).
std::string nowIso8601Local() {
    const std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buffer[32];
    // %F = %Y-%m-%d, %T = %H:%M:%S
    if (std::strftime(buffer, sizeof(buffer), "%FT%T", &tm_buf) == 0) {
        return std::string();
    }
    return std::string(buffer);
}

}  // namespace

void saveRosterToProto(const std::vector<Person>& people,
                       const std::string& path) {
    roster::Roster message;
    message.set_exported_at(nowIso8601Local());

    for (const Person& p : people) {
        roster::Person* out = message.add_people();
        out->set_id(p.id);
        out->set_first_name(p.first_name);
        out->set_last_name(p.last_name);
        out->set_role(p.role);
        out->set_email(p.email);
        out->set_active(p.active);
    }

    std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw ProtoIoException("Could not open file for writing: " + path);
    }

    if (!message.SerializeToOstream(&stream)) {
        throw ProtoIoException("Failed to serialize roster to: " + path);
    }
}

std::vector<Person> loadRosterFromProto(const std::string& path) {
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream) {
        throw ProtoIoException("Could not open file for reading: " + path);
    }

    roster::Roster message;
    if (!message.ParseFromIstream(&stream)) {
        throw ProtoIoException(
            "Failed to parse roster (not a valid roster file?): " + path);
    }

    std::vector<Person> people;
    people.reserve(static_cast<std::size_t>(message.people_size()));
    for (int i = 0; i < message.people_size(); ++i) {
        const roster::Person& in = message.people(i);
        Person p;
        p.id = in.id();
        p.first_name = in.first_name();
        p.last_name = in.last_name();
        p.role = in.role();
        p.email = in.email();
        p.active = in.active();
        people.push_back(std::move(p));
    }
    return people;
}
