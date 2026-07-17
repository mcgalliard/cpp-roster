#include "http_server.h"

// httplib.h pulls in the platform sockets headers (winsock2 on Windows); it must
// be included before <windows.h> so the winsock2/winsock1 ordering stays sane.
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <ios>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>  // GetModuleFileNameW, for locating the exe's directory.
#endif

#include "person.h"
#include "proto_io.h"
#include "roster_manager.h"

namespace {

using nlohmann::json;

// ---- JSON <-> Person -------------------------------------------------------

// Serialize a Person to the API's JSON shape: {id,first,last,role,email,active}.
json personToJson(const Person& p) {
    return json{
        {"id", p.id},          {"first", p.first_name}, {"last", p.last_name},
        {"role", p.role},      {"email", p.email},      {"active", p.active},
    };
}

// ---- Error handling --------------------------------------------------------

// Write a JSON error body `{ "error": <message> }` with the given status code.
void writeError(httplib::Response& res, int status, const std::string& message) {
    json body;
    body["error"] = message;
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

// A duplicate-email violation surfaces as a DatabaseException whose message
// contains "already exists" (see Database::insertPerson/updatePerson). The REST
// contract maps that specific case to 409 and every other DB error to 500.
bool isDuplicateEmail(const DatabaseException& e) {
    return std::string(e.what()).find("already exists") != std::string::npos;
}

// Translate a caught exception from a handler into the appropriate JSON error
// response. Called from each handler's catch blocks so the mapping lives once.
// Ordering (validation -> db -> proto -> json -> generic) mirrors the CLI's
// exception-to-exit-code mapping.
void writeDbError(httplib::Response& res, const DatabaseException& e) {
    writeError(res, isDuplicateEmail(e) ? 409 : 500, e.what());
}

// ---- Static file (web root) resolution -------------------------------------

// Absolute path to the running executable, or an empty path if it cannot be
// determined. Used to locate a web/ directory shipped alongside the binary.
std::filesystem::path executablePath() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return {};  // Failure or truncation: give up and fall back to cwd only.
    }
    return std::filesystem::path(std::wstring(buffer, length));
#else
    std::error_code ec;
    const std::filesystem::path self =
        std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : self;
#endif
}

// Locate the web/ directory holding the frontend. Resolution order:
//   1. "web" relative to the current working directory (dev: run from repo root)
//   2. "web" next to the executable (build tree: build/bin/web)
//   3. "../web" relative to the executable (dist layout: bin/roster + web/)
// Returns an empty string if none exist, in which case only the API is served.
std::string resolveWebRoot() {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::is_directory("web", ec)) {
        return "web";
    }

    const fs::path exe = executablePath();
    if (!exe.empty()) {
        const fs::path exeDir = exe.parent_path();
        const fs::path candidates[] = {exeDir / "web", exeDir.parent_path() / "web"};
        for (const fs::path& candidate : candidates) {
            if (fs::is_directory(candidate, ec)) {
                return candidate.string();
            }
        }
    }

    return std::string();
}

}  // namespace

int runServer(RosterManager& mgr, int port, const std::string& dbPath) {
    // The single lock guarding every RosterManager access. httplib serves
    // requests on multiple threads; the sqlite3 handle behind `mgr` is not
    // thread-safe, so each handler takes this lock for the duration of its DB
    // work. Captured by reference into the handler lambdas below.
    std::mutex mgrMutex;

    httplib::Server svr;

    // ---- Static frontend ---------------------------------------------------
    const std::string webRoot = resolveWebRoot();
    if (webRoot.empty()) {
        std::cerr << "Warning: could not find a 'web' directory; serving the "
                     "API only (the UI will not load).\n";
    } else {
        // Mounting at "/" makes httplib serve index.html for a request to "/".
        svr.set_mount_point("/", webRoot);
    }

    // ---- GET /api/people ---------------------------------------------------
    svr.Get("/api/people", [&](const httplib::Request&, httplib::Response& res) {
        try {
            json people = json::array();
            {
                std::lock_guard<std::mutex> lock(mgrMutex);
                for (const Person& p : mgr.listPeople(/*activeOnly=*/false)) {
                    people.push_back(personToJson(p));
                }
            }
            res.set_content(people.dump(), "application/json");
        } catch (const std::exception& e) {
            writeError(res, 500, e.what());
        }
    });

    // ---- POST /api/people --------------------------------------------------
    svr.Post("/api/people", [&](const httplib::Request& req,
                                httplib::Response& res) {
        try {
            const json body = json::parse(req.body);
            Person p;
            p.first_name = body.value("first", std::string());
            p.last_name = body.value("last", std::string());
            p.role = body.value("role", std::string());
            p.email = body.value("email", std::string());
            p.active = body.value("active", true);

            Person created;
            {
                std::lock_guard<std::mutex> lock(mgrMutex);
                created = mgr.addPerson(p);
            }
            res.status = 201;
            res.set_content(personToJson(created).dump(), "application/json");
        } catch (const ValidationException& e) {
            writeError(res, 400, e.what());
        } catch (const DatabaseException& e) {
            writeDbError(res, e);
        } catch (const json::exception&) {
            writeError(res, 400, "Request body must be valid JSON.");
        } catch (const std::exception& e) {
            writeError(res, 500, e.what());
        }
    });

    // ---- PUT /api/people/<id> ----------------------------------------------
    // Accepts any subset of {first,last,role,email,active}; unspecified fields
    // keep their current value. The whole read-modify-write runs under one lock
    // so a concurrent request cannot interleave.
    svr.Put(R"(/api/people/(\d+))", [&](const httplib::Request& req,
                                        httplib::Response& res) {
        try {
            const int id = std::stoi(req.matches[1].str());
            const json body = json::parse(req.body);

            std::lock_guard<std::mutex> lock(mgrMutex);
            const std::optional<Person> existing = mgr.getPerson(id);
            if (!existing) {
                writeError(res, 404, "No person with id " + std::to_string(id) + ".");
                return;
            }

            Person p = *existing;
            if (body.contains("first")) p.first_name = body.at("first").get<std::string>();
            if (body.contains("last")) p.last_name = body.at("last").get<std::string>();
            if (body.contains("role")) p.role = body.at("role").get<std::string>();
            if (body.contains("email")) p.email = body.at("email").get<std::string>();
            if (body.contains("active")) p.active = body.at("active").get<bool>();

            mgr.updatePerson(p);
            const std::optional<Person> updated = mgr.getPerson(id);
            if (!updated) {
                writeError(res, 404, "No person with id " + std::to_string(id) + ".");
                return;
            }
            res.set_content(personToJson(*updated).dump(), "application/json");
        } catch (const ValidationException& e) {
            writeError(res, 400, e.what());
        } catch (const DatabaseException& e) {
            writeDbError(res, e);
        } catch (const json::exception&) {
            writeError(res, 400, "Request body must be valid JSON.");
        } catch (const std::exception& e) {
            writeError(res, 500, e.what());
        }
    });

    // ---- DELETE /api/people/<id> -------------------------------------------
    svr.Delete(R"(/api/people/(\d+))", [&](const httplib::Request& req,
                                           httplib::Response& res) {
        try {
            const int id = std::stoi(req.matches[1].str());
            bool removed = false;
            {
                std::lock_guard<std::mutex> lock(mgrMutex);
                removed = mgr.removePerson(id);
            }
            if (removed) {
                res.status = 204;
            } else {
                writeError(res, 404, "No person with id " + std::to_string(id) + ".");
            }
        } catch (const DatabaseException& e) {
            writeDbError(res, e);
        } catch (const std::exception& e) {
            writeError(res, 500, e.what());
        }
    });

    // ---- GET /api/export ---------------------------------------------------
    // Returns the same protobuf bytes `roster export` would write, as a binary
    // download named roster.pb.
    svr.Get("/api/export", [&](const httplib::Request&, httplib::Response& res) {
        try {
            std::ostringstream out(std::ios::out | std::ios::binary);
            {
                std::lock_guard<std::mutex> lock(mgrMutex);
                mgr.exportToStream(out);
            }
            res.set_header("Content-Disposition",
                           "attachment; filename=\"roster.pb\"");
            res.set_content(out.str(), "application/octet-stream");
        } catch (const ProtoIoException& e) {
            writeError(res, 500, e.what());
        } catch (const std::exception& e) {
            writeError(res, 500, e.what());
        }
    });

    // ---- POST /api/import?mode=merge|replace -------------------------------
    // The raw request body is the .pb file's bytes. `mode` defaults to merge;
    // anything other than "replace" is treated as merge.
    svr.Post("/api/import", [&](const httplib::Request& req,
                                httplib::Response& res) {
        try {
            const std::string modeParam =
                req.has_param("mode") ? req.get_param_value("mode") : "merge";
            const ImportMode mode = (modeParam == "replace") ? ImportMode::Replace
                                                             : ImportMode::Merge;

            std::istringstream in(req.body, std::ios::in | std::ios::binary);
            int imported = 0;
            {
                std::lock_guard<std::mutex> lock(mgrMutex);
                imported = mgr.importFromStream(in, mode);
            }
            json body;
            body["imported"] = imported;
            res.set_content(body.dump(), "application/json");
        } catch (const ProtoIoException& e) {
            writeError(res, 400, e.what());
        } catch (const ValidationException& e) {
            writeError(res, 400, e.what());
        } catch (const DatabaseException& e) {
            writeDbError(res, e);
        } catch (const std::exception& e) {
            writeError(res, 500, e.what());
        }
    });

    // ---- Bind and serve ----------------------------------------------------
    // Bind first so a failure is reported before the banner prints; then block
    // in listen_after_bind until the process is interrupted.
    if (!svr.bind_to_port("127.0.0.1", port)) {
        std::cerr << "Error: could not bind to port " << port
                  << " (is it already in use?).\n";
        return 1;
    }

    std::cout << "Serving roster UI on http://localhost:" << port << " (db: "
              << dbPath << ")\n";
    std::cout.flush();

    svr.listen_after_bind();
    return 0;
}
