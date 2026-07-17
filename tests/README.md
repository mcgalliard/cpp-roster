# Tests

Unit tests are not part of v1 and are intentionally omitted.

When adding them later, a lightweight setup that fits this project:

- Add a test executable target in `CMakeLists.txt` guarded behind an option
  (e.g. `option(ROSTER_BUILD_TESTS "Build unit tests" OFF)`).
- Use [Catch2](https://github.com/catchorg/Catch2) or GoogleTest via
  `FetchContent`, mirroring how protobuf is fetched.
- Exercise `RosterManager` against a throwaway database path (e.g. a temp
  file or `:memory:`), never the user's real `roster.db`.
- Round-trip `proto_io` (`saveRosterToProto` then `loadRosterFromProto`) and
  assert the vectors match field-for-field.

Nothing in `src/` needs to change to make the code testable: `Database` and
`RosterManager` both take an explicit path, and `proto_io` functions are free
functions.
