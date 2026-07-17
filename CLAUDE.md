# cpp-roster — Development Guide

C++17 CLI roster manager. SQLite3 for storage (C API, thin RAII wrapper),
Protocol Buffers **only** as the import/export file format (never for DB
storage), CMake + Ninja, hand-rolled argv parsing. Keep it dependency-light —
no ORM, no CLI library, no test framework unless asked.

## Build (this machine)

The only toolchain on this Windows box is **MSYS2 UCRT64** (`C:\msys64`) —
no MSVC, no WSL. From PowerShell, run build commands through the UCRT64
environment:

```powershell
$env:MSYSTEM='UCRT64'
C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/cpmcg/Documents/cpp-roster && cmake -S . -B build -G Ninja && cmake --build build"
```

- Output: `build/bin/roster.exe`. It links MinGW runtime DLLs, so it **only
  runs inside the UCRT64 environment** (or via the bash wrapper above).
- `cmake --install build --prefix dist` (from the UCRT64 env) bundles the exe
  plus all non-system DLLs and the `web/` UI into `dist/` — that copy runs
  from plain PowerShell/Explorer. Re-run it after rebuilding if `dist` matters.
- Rule of thumb (the user runs from plain PowerShell): **UCRT64 shell →
  `build/bin`, anywhere else → `dist/bin`**. Telling the user to run
  `build\bin\roster.exe` from PowerShell will fail with a missing-DLL error.
- CMake finds SQLite3 and Protobuf as MSYS2 system packages. The FetchContent
  fallbacks in CMakeLists.txt (SQLite amalgamation download; protobuf v25.3
  from source) exist for machines without them — don't exercise the protobuf
  fallback here, it's a very slow from-source build.

## Architecture (who owns what)

```
src/main.cpp             thin entry point → runCli
src/cli.h/.cpp           argv parsing, dispatch, table output, exception→exit-code mapping
src/roster_manager.h/.cpp façade + validation (empty names, email sanity) + import/export orchestration
src/database.h/.cpp      RAII sqlite3 wrapper — ALL SQL lives here, prepared statements only
src/proto_io.h/.cpp      the ONLY code touching generated protobuf types; converts to/from Person
src/http_server.h/.cpp   `roster serve` HTTP server + JSON REST API (httplib + nlohmann/json)
src/person.h             plain struct shared everywhere
web/                     static frontend (index.html, style.css, app.js) served by `serve`
proto/roster.proto       source of truth for the interchange format
```

Layering rules: `cli` never touches SQL or protobuf; `database` never does
validation or I/O formatting; generated `roster.pb.h` is included only by
`proto_io.cpp`. Generated sources go to `build/generated` — never commit them.
`http_server` is a boundary like `cli`: it goes through `RosterManager` and
`proto_io` only (no SQL, no generated protobuf types), translating exceptions
into JSON error responses so a bad request never crashes the server.

## Conventions & invariants

- **Exit codes:** 0 success, 1 usage/validation error, 2 database error
  (including duplicate email and unknown id), 3 proto I/O error. Keep the
  README table in sync if these change.
- **Global options** `--db <path>` and `--verbose` are stripped from the token
  stream *before* command dispatch, so they're valid anywhere on the command
  line (there's a fixed bug in history from getting this wrong — see 24b9df6).
- Empty email is stored as SQL `NULL`, not `""` — the `UNIQUE` constraint
  would otherwise allow only one email-less person. Reads map NULL → `""`.
- Import never trusts ids from the file; `--merge` matches existing rows by
  email (email-less people therefore duplicate on re-import — known/accepted).
- Errors: exceptions carry a clean `what()` plus raw `detail()`; detail prints
  only with `--verbose`.
- All sqlite binds use `SQLITE_TRANSIENT`; bind indexes are 1-based, column
  indexes 0-based.

### Web server (`roster serve`)

- **Thread-safety invariant:** httplib dispatches requests across a thread
  pool, but the `RosterManager` / sqlite3 handle is **not** thread-safe. A
  single `std::mutex` in `runServer` guards every `RosterManager` access; each
  handler takes `std::lock_guard` for the whole read-modify-write. Never touch
  `mgr` outside that lock.
- **Static-file resolution order** (`resolveWebRoot` in `http_server.cpp`):
  (1) `web/` relative to the current working directory (dev, run from repo
  root), (2) `web/` next to the executable, (3) `../web` relative to the
  executable — this is how both the build tree (`build/bin` + `build/web`) and
  the dist layout (`bin/roster.exe` + `web/`) resolve. On Windows the exe path
  comes from `GetModuleFileNameW` (guarded by `#ifdef _WIN32`; POSIX reads
  `/proc/self/exe`). If none exist, it logs a warning and serves the API only.
- A POST_BUILD step mirrors `web/` into `build/web`, and
  `install(DIRECTORY web DESTINATION .)` ships it into `dist/`. The build-tree
  copy refreshes only when the exe relinks — when live-editing `web/` files,
  run the server from the repo root so the cwd lookup picks up changes without
  a rebuild.
- Server binds to `127.0.0.1` only. The API JSON uses short keys
  (`first`/`last`), distinct from the struct's `first_name`/`last_name`.
- `proto_io` and `RosterManager` have **stream overloads**
  (`saveRosterToProto`/`loadRosterFromProto` over `std::ostream`/`std::istream`;
  `exportToStream`/`importFromStream`) so export/import reuse the exact CLI
  serialization; the path versions delegate to them — do not duplicate.

### Dependencies (this machine)

- **SQLite3, Protobuf:** MSYS2 system packages (see above); FetchContent
  fallbacks exist.
- **nlohmann/json:** installed via `pacman`
  (`mingw-w64-ucrt-x86_64-nlohmann-json`), found by `find_package(nlohmann_json
  CONFIG)`; FetchContent release fallback otherwise.
- **cpp-httplib:** NOT in MSYS2 — normally pulled by **FetchContent** (git tag
  `v0.18.7`, `GIT_SHALLOW`). All optional backends (OpenSSL/zlib/brotli) are
  forced OFF so nothing extra is dragged in. Links `httplib::httplib`,
  `nlohmann_json::nlohmann_json`, and (Windows) `ws2_32`.

## Verifying changes

Never test against a real `roster.db` — always pass `--db` pointing at a
scratch file in a temp dir. A full end-to-end pass should cover: add (with and
without email), duplicate-email rejection (exit 2), missing-arg usage error
(exit 1), list/`--active-only`, update, remove (existing and unknown id),
export, import `--replace` and `--merge` into a second scratch DB, import of a
nonexistent file (exit 3), and `--verbose` output. Check `$?`/`$LASTEXITCODE`
explicitly — the exit-code contract is part of the interface.

For `serve`/web changes, additionally: start the server on a scratch `--db`
and non-default port, curl the API happy paths **and** error mappings (400
validation, 409 duplicate email, 404 unknown id, 400 garbage import), do a
binary export→import round-trip, load the UI in the browser preview (check
console for errors, exercise add/edit/search/filter), and confirm no
horizontal scroll at 375px. Kill the server when done (`taskkill /IM
roster.exe /F` from PowerShell if needed).

`*.db` and `*.pb` are gitignored; keep it that way.
