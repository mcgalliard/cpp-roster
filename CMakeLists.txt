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
    ${PROTO_SRC}
)

add_dependencies(roster generate_proto)

target_include_directories(roster
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${GENERATED_DIR}          # for the generated roster.pb.h
)

target_link_libraries(roster
    PRIVATE
        SQLite3::SQLite3
        protobuf::libprotobuf
)

# ===========================================================================
# 5. Warnings and compiler-specific flags.
# ===========================================================================
if(MSVC)
    target_compile_options(roster PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(roster PRIVATE -Wall -Wextra)
endif()
