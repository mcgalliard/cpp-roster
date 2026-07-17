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
  plus all non-system DLLs into `dist/bin` — that copy runs from plain
  PowerShell/Explorer. Re-run it after rebuilding if `dist` matters.
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
src/person.h             plain struct shared everywhere
proto/roster.proto       source of truth for the interchange format
```

Layering rules: `cli` never touches SQL or protobuf; `database` never does
validation or I/O formatting; generated `roster.pb.h` is included only by
`proto_io.cpp`. Generated sources go to `build/generated` — never commit them.

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

## Verifying changes

Never test against a real `roster.db` — always pass `--db` pointing at a
scratch file in a temp dir. A full end-to-end pass should cover: add (with and
without email), duplicate-email rejection (exit 2), missing-arg usage error
(exit 1), list/`--active-only`, update, remove (existing and unknown id),
export, import `--replace` and `--merge` into a second scratch DB, import of a
nonexistent file (exit 3), and `--verbose` output. Check `$?`/`$LASTEXITCODE`
explicitly — the exit-code contract is part of the interface.

`*.db` and `*.pb` are gitignored; keep it that way.
