# cpp-roster

A small, teaching-quality command-line application for managing a roster of
people. Data is persisted in **SQLite** (via the C API with a thin RAII
wrapper — no ORM). **Protocol Buffers** are used purely as the import/export
file format, not for database storage.

- **Language:** C++17
- **Build:** CMake >= 3.16
- **Storage:** SQLite3 (prepared statements everywhere)
- **Interchange:** Protobuf binary files (`.pb`)
- **CLI:** hand-rolled argv parsing (no CLI library)

---

## Building

The project needs a C++17 compiler, CMake, and (ideally) system-provided
SQLite3 and Protobuf. Three supported paths, easiest first.

### (a) MSYS2 UCRT64 (recommended on Windows)

From the **MSYS2 UCRT64** shell:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-protobuf \
  mingw-w64-ucrt-x86_64-sqlite3

cmake -S . -B build -G Ninja
cmake --build build
./build/bin/roster.exe help
```

The exe in `build/bin` links against MinGW runtime DLLs (libgcc, libstdc++,
libprotobuf, ...), so it only runs where those are on PATH — i.e. inside the
UCRT64 shell. To get a **self-contained folder that runs anywhere on the
machine** (double-click, plain PowerShell, another directory), run the install
step from the UCRT64 shell:

```bash
cmake --install build --prefix dist
```

This copies `roster.exe` plus every non-system DLL it needs into `dist/bin`.
Note that `roster` is a console program: double-clicking it just flashes a
window; run it from a terminal (`.\dist\bin\roster.exe help` works from plain
PowerShell or cmd).

### (b) Visual Studio 2022 Build Tools + vcpkg

Install the "Desktop development with C++" workload, then use vcpkg for the
dependencies:

```powershell
vcpkg install protobuf sqlite3

cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
.\build\bin\Release\roster.exe help
```

### (c) Zero-install fallback (slow)

If neither SQLite3 nor Protobuf is found, CMake falls back to **FetchContent**:
it downloads the SQLite amalgamation and **compiles Protobuf (and Abseil) from
source**. This works with just a compiler + CMake, but the first configure and
build are slow. You only need a compiler and CMake:

```bash
cmake -S . -B build
cmake --build build
```

CMake prints a `STATUS`/`WARNING` line telling you which path it took for each
dependency.

---

## Usage

The default database is `roster.db` in the current directory. Use `--db <path>`
to point at a scratch database, and `--verbose` to see raw error detail.

```bash
roster add --first John --last Doe --role Engineer --email j@x.com
roster list
roster list --active-only
roster update --id 3 --role "Senior Engineer" --active false
roster remove --id 3
roster export --file team.pb
roster import --file team.pb --merge      # default
roster import --file team.pb --replace
roster help
```

### Command reference

| Command  | Options                                                                                  | Notes |
|----------|------------------------------------------------------------------------------------------|-------|
| `add`    | `--first <F> --last <L> [--role <R>] [--email <E>]`                                       | first/last required |
| `remove` | `--id <N>`                                                                                | reports if id missing |
| `list`   | `[--active-only]`                                                                         | aligned text table |
| `update` | `--id <N> [--first <F>] [--last <L>] [--role <R>] [--email <E>] [--active true\|false]`   | only given fields change |
| `export` | `--file <path.pb>`                                                                        | writes binary protobuf, stamps `exported_at` |
| `import` | `--file <path.pb> [--merge \| --replace]`                                                 | default `--merge` |
| `help`   |                                                                                          | usage summary |

**Global options** (valid on any command): `--db <path>` (default `roster.db`),
`--verbose`.

**Import semantics:**
- `--replace` clears the table, then inserts everyone from the file.
- `--merge` inserts new people and updates existing rows matched **by email**.
  People *without* an email can't be matched, so re-importing a file over a
  database that already contains them creates duplicate rows.
- Imported ids are **never trusted** — the database assigns ids on insert.

### Exit codes

| Code | Meaning          |
|------|------------------|
| 0    | success          |
| 1    | usage error (bad arguments, validation failure) |
| 2    | database error (duplicate email, unknown id, SQLite failure) |
| 3    | proto I/O error (missing/corrupt `.pb` file) |

---

## Layout

```
CMakeLists.txt
proto/roster.proto
src/
  main.cpp            entry point
  cli.h / cli.cpp     argv parsing + dispatch + table output
  database.h/.cpp     RAII SQLite wrapper (all SQL lives here)
  roster_manager.h/.cpp  business logic + validation
  person.h            in-memory struct
  proto_io.h/.cpp     protobuf load/save
tests/                placeholder (unit tests optional)
```
