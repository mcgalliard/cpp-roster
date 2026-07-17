cmake_minimum_required(VERSION 3.16)

project(roster_manager LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard: C++17, required, no compiler extensions.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put binaries in a predictable place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# ===========================================================================
# 1. SQLite3
#    Prefer a system/package-provided SQLite3 (target SQLite3::SQLite3 from the
#    stock FindSQLite3 module). If not found, download the official amalgamation
#    and build a tiny static library exposing the same SQLite3::SQLite3 target.
# ===========================================================================
find_package(SQLite3 QUIET)

if(SQLite3_FOUND)
    message(STATUS "SQLite3: using system package (SQLite3::SQLite3).")
else()
    message(STATUS "SQLite3: not found on the system.")
    message(STATUS "SQLite3: downloading the official amalgamation via "
                   "FetchContent and building it from source.")

    # The amalgamation is a C source (sqlite3.c). The project only enabled CXX,
    # so a C compiler must be turned on before adding a C library target;
    # otherwise CMake errors with "No CMAKE_C_COMPILER could be found".
    enable_language(C)

    FetchContent_Declare(
        sqlite_amalgamation
        # Official SQLite amalgamation (single-file sqlite3.c + sqlite3.h).
        # No URL_HASH is pinned here so the file compiles first-try without a
        # fabricated checksum; add URL_HASH SHA3_256=<hash> for reproducibility.
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
    )
    FetchContent_GetProperties(sqlite_amalgamation)
    if(NOT sqlite_amalgamation_POPULATED)
        FetchContent_Populate(sqlite_amalgamation)
    endif()

    # Locate sqlite3.c regardless of whether the archive was flattened.
    file(GLOB_RECURSE _sqlite3_sources
         "${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c")
    if(NOT _sqlite3_sources)
        message(FATAL_ERROR
            "Could not find sqlite3.c under "
            "${sqlite_amalgamation_SOURCE_DIR}. The amalgamation download may "
            "have failed.")
    endif()
    list(GET _sqlite3_sources 0 _sqlite3_c)
    get_filename_component(_sqlite3_include_dir "${_sqlite3_c}" DIRECTORY)

    add_library(sqlite3_amalgamation STATIC "${_sqlite3_c}")
    target_include_directories(sqlite3_amalgamation
                               PUBLIC "${_sqlite3_include_dir}")

    # POSIX platforms need threads and libdl for SQLite's default build.
    if(NOT WIN32)
        find_package(Threads REQUIRED)
        target_link_libraries(sqlite3_amalgamation
                              PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    endif()

    # Expose the same target name find_package would have provided.
    add_library(SQLite3::SQLite3 ALIAS sqlite3_amalgamation)
    message(STATUS "SQLite3: built from amalgamation at ${_sqlite3_c}")
endif()

# ===========================================================================
# 2. Protobuf
#    Try CONFIG mode first (vcpkg / modern installs), then module mode, then
#    fall back to building protobuf from source via FetchContent.
# ===========================================================================
set(_protobuf_from_source OFF)

find_package(Protobuf CONFIG QUIET)
if(Protobuf_FOUND)
    message(STATUS "Protobuf: found via CONFIG mode.")
else()
    find_package(Protobuf QUIET)  # Module mode (stock FindProtobuf).
    if(Protobuf_FOUND)
        message(STATUS "Protobuf: found via module mode (FindProtobuf).")
    endif()
endif()

if(NOT Protobuf_FOUND)
    set(_protobuf_from_source ON)
    message(STATUS "Protobuf: not found on the system.")
    message(WARNING
        "Protobuf will be COMPILED FROM SOURCE via FetchContent. The first "
        "configure and build will be SLOW (protobuf + Abseil). Install "
        "protobuf via your package manager (MSYS2 pacman or vcpkg) to avoid "
        "this.")

    # Build protobuf statically, no tests, no install, Abseil pulled in by
    # protobuf's own FetchContent dependencies.
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(protobuf_ABSL_PROVIDER "module" CACHE STRING "" FORCE)

    FetchContent_Declare(
        protobuf
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
        GIT_TAG        v25.3   # A stable v25.x release.
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(protobuf)
    message(STATUS "Protobuf: built from source (targets protobuf::libprotobuf "
                   "and protobuf::protoc).")
endif()

# ===========================================================================
# 2b. Web UI dependencies (header-only): nlohmann_json + cpp-httplib.
#     Both are used only by src/http_server.cpp for the `roster serve` command.
#     Prefer a system/package-provided config so offline machines and CI reuse
#     an install; fall back to FetchContent otherwise.
# ===========================================================================

# nlohmann_json — installed here via MSYS2 pacman
# (mingw-w64-ucrt-x86_64-nlohmann-json), which ships a CMake config.
find_package(nlohmann_json 3 CONFIG QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "nlohmann_json: using system package "
                   "(nlohmann_json::nlohmann_json).")
else()
    message(STATUS "nlohmann_json: not found; fetching the single-header "
                   "release via FetchContent (header-only, fast).")
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        nlohmann_json
        # The json.tar.xz release asset carries a CMake config exporting
        # nlohmann_json::nlohmann_json; much smaller/faster than the git repo.
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
    message(STATUS "nlohmann_json: built from FetchContent release v3.12.0.")
endif()

# cpp-httplib — NOT in MSYS2, so FetchContent is the normal path on this machine.
# Try a system config first for other environments. Turn every optional backend
# OFF so no OpenSSL / zlib / brotli dependency is dragged in (plain HTTP is all
# the local UI needs).
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB    OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_BROTLI  OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE    OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE  OFF CACHE BOOL "" FORCE)

find_package(httplib CONFIG QUIET)
if(httplib_FOUND)
    message(STATUS "cpp-httplib: using system package (httplib::httplib).")
else()
    message(STATUS "cpp-httplib: not found; fetching from GitHub via "
                   "FetchContent (header-only).")
    FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.18.7   # A stable v0.18.x release.
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(httplib)
    message(STATUS "cpp-httplib: built from FetchContent (target httplib::httplib).")
endif()

# ===========================================================================
# 3. Protobuf code generation (portable custom command; no protobuf_generate_cpp).
# ===========================================================================
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/roster.proto)
set(PROTO_SRC  ${GENERATED_DIR}/roster.pb.cc)
set(PROTO_HDR  ${GENERATED_DIR}/roster.pb.h)

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND $<TARGET_FILE:protobuf::protoc>
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
            --cpp_out=${GENERATED_DIR}
            ${PROTO_FILE}
    DEPENDS ${PROTO_FILE} protobuf::protoc
    COMMENT "Generating protobuf C++ sources from roster.proto"
    VERBATIM
)

# A named target so the generation participates in the build graph cleanly.
add_custom_target(generate_proto DEPENDS ${PROTO_SRC} ${PROTO_HDR})

# ===========================================================================
# 4. Main executable.
# ===========================================================================
add_executable(roster
    src/main.cpp
    src/cli.cpp
    src/database.cpp
    src/roster_manager.cpp
    src/proto_io.cpp
    src/http_server.cpp
    ${PROTO_SRC}
)

add_dependencies(roster generate_proto)

# Mirror the web UI into the build tree so the build-tree exe (build/bin) finds
# it via its "../web" lookup, same as the dist layout. Kept in sync on every
# build; when editing web/ live, run the server from the repo root instead (the
# cwd lookup wins and picks up changes without a rebuild).
add_custom_command(TARGET roster POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/web
    COMMENT "Copying web/ into the build tree")

target_include_directories(roster
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${GENERATED_DIR}          # for the generated roster.pb.h
)

target_link_libraries(roster
    PRIVATE
        SQLite3::SQLite3
        protobuf::libprotobuf
        httplib::httplib
        nlohmann_json::nlohmann_json
)

# cpp-httplib needs the Winsock library. Its imported target usually carries
# ws2_32 already, but link it explicitly on Windows so the build does not depend
# on that (and so a system-config httplib without the link still resolves).
if(WIN32)
    target_link_libraries(roster PRIVATE ws2_32)
endif()

# ===========================================================================
# 5. Warnings and compiler-specific flags.
# ===========================================================================
if(MSVC)
    target_compile_options(roster PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(roster PRIVATE -Wall -Wextra)
endif()

# ===========================================================================
# 6. Install / deploy.
#    The build-tree exe links against the toolchain's shared runtime DLLs
#    (libgcc, libstdc++, libprotobuf, libsqlite3, ...), which are only on PATH
#    inside the MinGW/MSYS2 shell. `cmake --install build --prefix dist` (run
#    from that shell) copies the exe plus every non-system DLL it needs into
#    dist/bin, producing a folder that runs anywhere on the machine.
# ===========================================================================
if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    # The toolchain's bin directory is where MinGW-distributed DLLs live.
    get_filename_component(_toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(TARGETS roster
        RUNTIME_DEPENDENCIES
            PRE_EXCLUDE_REGEXES  "api-ms-" "ext-ms-"
            POST_EXCLUDE_REGEXES ".*[Ss]ystem32.*" ".*[Ww]indows[/\\\\].*"
            DIRECTORIES "${_toolchain_bin}"
        RUNTIME DESTINATION bin)
else()
    install(TARGETS roster RUNTIME DESTINATION bin)
endif()

# Ship the web UI next to the binary. The dist layout is then bin/roster.exe +
# web/, which the server's next-to-executable lookup finds via its "../web"
# candidate (see resolveWebRoot in http_server.cpp).
install(DIRECTORY web DESTINATION .)
