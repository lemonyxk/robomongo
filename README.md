# Robo 3T — modern ARM build

A Qt desktop client for MongoDB, updated to run natively on Apple Silicon.
The embedded MongoDB 4.2 / SpiderMonkey shell has been replaced with a persistent
Node.js process using the official mongosh libraries. Database operations use the
public MongoDB C Driver; BSON values use libbson with lossless Extended JSON.

## Build on macOS

Requirements: Apple Silicon, macOS 13.5 or later, Xcode Command Line Tools,
Python 3.10+, and CMake 3.22+. Run from a native ARM terminal:

```sh
bash scripts/build-macos.sh
```

The script downloads pinned dependencies into `build/deps`, compiles the app,
runs unit tests, and installs a self-contained application at:

```text
build/arm64/install/Robo 3T.app
```

Open it with:

```sh
open "build/arm64/install/Robo 3T.app"
```

Once dependencies are installed, an incremental build is:

```sh
cmake --build build/arm64 --parallel 6
ctest --test-dir build/arm64 --output-on-failure
cmake --install build/arm64
```

The bundled Node runtime and Qt frameworks are included in the application;
Rosetta, Homebrew libraries, and a separately installed mongosh are not needed
at runtime. Builds are locally signed; this repository does not supply an Apple
Developer ID or notarization. Other platforms can use the CMake targets with
native dependency paths, but this migration has been validated on macOS ARM64.

## Dependencies

Latest stable versions verified on 2026-09-16:

| Component | Version |
| --- | --- |
| Qt | 6.11.2 |
| QScintilla | 2.14.1 |
| MongoDB C Driver / libbson | 2.5.3 |
| mongosh release family | 2.11.1 |
| Node.js | 26.8.2 |
| OpenSSL | 4.0.2 |
| libssh2 | 1.11.1 |
| GoogleTest (tests only) | 1.18.0 |

Exact mongosh package versions and transitive dependencies are fixed in
`src/robomongo/runtime/package-lock.json`; they have independent version numbers.
Download locations and SHA-256 hashes are in `scripts/dependencies-macos.json`.
The old MongoDB server sources, SpiderMonkey, Qt 5, QtWebEngine, QJson, Esprima,
and vendored obsolete library versions are no longer part of the build.

## Behavior and migration

- Queries and writes use supported commands; pagination does not send
  `ntoreturn`, and writes do not issue `getlasterror`.
- Shell variables persist between executions. Cursor pages retain the original
  cursor, including aggregation results, without replaying scripts or writes.
- Interrupts/timeouts reset the shell worker, reconnect, and explicitly report
  that variables and cursors were reset. Re-run a query to obtain a new cursor.
- BSON int64, Decimal128, dates, UUIDs and binary values retain their types when
  viewed or edited. Projected, mapped, and aggregated results are read-only.
- Existing 1.4.4 connections can be imported into the 2.0.0 profile. Existing
  encrypted passwords remain readable. Old user profiles are preserved.
- New connections default to SCRAM-SHA-256. SCRAM-SHA-1 remains available for
  existing accounts; removed MONGODB-CR authentication is no longer offered.
- `ROBOMONGO_PROFILE_DIR=/absolute/path` provides an isolated profile and shell
  startup directory for testing or a portable installation.

The application upgrade does not upgrade or modify the connected MongoDB server.
Historical `mongo` shell commands should be updated to their mongosh equivalents;
server-removed operations are not emulated.

## Tests

`ctest --test-dir build/arm64 --output-on-failure` runs the unit suite without a
MongoDB server. See [integration test instructions](src/robomongo-unit-tests/README.md)
for the optional disposable local replica-set tests.

## License

Robo 3T is distributed under the GNU GPL v3. See [LICENSE](LICENSE) and
[third-party notices](THIRD_PARTY_NOTICES.md). Project history remains in CHANGELOG.
