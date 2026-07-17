#pragma once

#include <string>

// Plain in-memory representation of a roster member. Used everywhere except the
// two boundary layers (SQLite in database.cpp, protobuf in proto_io.cpp).
struct Person {
    int id = 0;               // DB primary key; 0/unset if not yet persisted.
    std::string first_name;
    std::string last_name;
    std::string role;         // Freeform, optional.
    std::string email;        // Optional; UNIQUE in the DB when present.
    bool active = true;
};
