# Tests

Build and run all unit tests (BSON round trips, acknowledged write errors, URI/TLS,
password compatibility and utilities):

```sh
bash scripts/build-macos.sh
# Subsequent runs:
ctest --test-dir build/arm64 --output-on-failure
```

The integration tests require a disposable local MongoDB replica set. They never
use saved application connections and create/drop only uniquely named test databases.
Start a separate server and initialize it once, for example:

```sh
mkdir -p build/mongodb-compat-test/db
mongod --dbpath "$PWD/build/mongodb-compat-test/db" \
  --port 37027 --bind_ip 127.0.0.1 --replSet codexCompatibility \
  --nounixsocket --logpath "$PWD/build/mongodb-compat-test/mongod.log"
# In another terminal, with a separately installed mongosh:
mongosh --port 37027 --eval \
  'rs.initiate({_id:"codexCompatibility",members:[{_id:0,host:"127.0.0.1:37027"}]})'
```

Once the server is primary:

```sh
# Exercise immediate refresh with the same global majority read/write defaults
# used by clusters that require majority-committed reads.
mongosh --port 37027 --eval \
  'db.adminCommand({setDefaultRWConcern:1,defaultReadConcern:{level:"majority"},defaultWriteConcern:{w:"majority"}})'
cmake --build build/arm64 --target robo_mongodb_integration_tests robo_shell_integration_tests --parallel 6
ROBOMONGO_TEST_PORT=37027 build/arm64/src/robomongo-unit-tests/robo_mongodb_integration_tests
build/arm64/src/robomongo-unit-tests/robo_shell_integration_tests
```

The driver suite covers direct and replica-set connections, typed write commands,
write errors, pagination/projection/sort/hints, all getMore batches and command
monitoring proving no `ntoreturn` is sent. The secondary-only test skips the direct
topology, where explicit direct connections intentionally read the selected server.
The immediate-refresh regression verifies that a save inherits the server's global
majority write concern and that the next majority read sees the saved value, without
retries. It skips if those global majority defaults have not been configured; the test
itself only reads the defaults and never changes them.
The shell suite checks the actual QProcess/Node/mongosh integration, BSON precision,
persistent variables, cursor paging, writes, completion, and interruption/timeout
recovery. It isolates shell startup files in a temporary profile.

JavaScript-only bridge tests:

```sh
build/deps/modern/node-v26.8.2-darwin-arm64/bin/node src/robomongo/runtime/mongosh-bridge.test.js
```

Stop the disposable `mongod` with Ctrl-C after testing. Never point these tests at
production databases.
