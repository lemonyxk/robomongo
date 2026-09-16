# Embedded mongosh runtime

This replaces the obsolete MongoDB 4.2/SpiderMonkey shell with official mongosh
components. The bridge runs on Node.js 26.8.2 for macOS arm64. Exact package
versions are recorded in `package.json` and `package-lock.json`.

Install with `npm ci --omit=dev` in this directory. Package `node`,
`mongosh-bridge.js`, `package.json`, and `node_modules` together under the app's
`Contents/Resources/runtime` directory. Development builds can find dependencies
under `build/deps/mongosh-runtime`, or `ROBOMONGO_MONGOSH_PACKAGE_DIR` can specify
the package directory explicitly. Connections and credentials are sent over
stdin, never through process arguments.

The manifest's version-pinned `allowScripts` entries permit the six reviewed
native dependency builds required on macOS. Other dependency install scripts
remain disabled. Review those entries again when upgrading the lockfile; do not
enable install scripts globally. Upstream Kerberos and client-encryption addons
ship universal binaries with arm64 and x86_64 slices; Node runs the arm64 slice.

The implementation uses MongoDB's `ShellInstanceState`, `ShellEvaluator`,
`NodeDriverServiceProvider`, and autocomplete APIs. These are the same underlying
shell APIs used by MongoDB Compass. A persistent VM retains interactive variables;
an isolated Node worker allows interruption even during an infinite JavaScript
loop. Interrupting or timing out terminates the worker, resets variables and
cursors, and reconnects using the existing in-memory connection configuration.
The interrupted request reports `error.data.contextReset: true`. Scripts and
writes are never automatically replayed.

The TypeScript autocomplete engine loads on the first completion request, so
opening a collection does not pay its startup cost. Invalidating completion
caches only discards existing completers; they are rebuilt when next requested.

## JSON-RPC protocol

Send one JSON-RPC 2.0 object per line, with `id`, `method`, and object `params`.
One response line contains the same `id` and either `result` or `error`.
Only protocol responses use stdout; worker output is captured separately.

- `connect`: `{uri, options, database?, batchSize?: 50, nodb?: false}`.
  `options` accepts MongoDB Node driver options, including TLS options. The
  additional `tlsCRLFile` option is read and passed as Node's `crl` PEM option.
- `eval`: `{code, filename?, batchSize?, timeoutMS?}`. Returns `results`, current
  `database`, `server`, and `contextReset`. Each result includes `type`,
  `statement`, `output`, `documents`, and `elapsedMS`. Documents are **canonical
  Extended JSON strings**, preserving BSON types and 64-bit integer precision.
  Queries also include a `cursorId` and `queryInfo` with database, collection,
  filter/projection EJSON, limit, skip, batch size, options, and special flag.
- `cursorPage`: `{cursorId, skip?, batchSize?}`. Returns a result row with
  `documents` and `hasMore`. `skip` is the absolute skip shown by the UI, including
  an original `find().skip(...)`. Paging uses the retained cursor and cached
  prior results; it never re-executes the script or aggregation pipeline.
  At most 64 cursor results are retained per shell; older handles expire.
- `use`: `{database}`.
- `autocomplete`: `{code, includeCollectionNames?: true}`. Returns
  `{completions, replace}` with complete replacement strings.
- `setBatchSize`: `{batchSize}`.
- `invalidateAutocomplete`: `{}`.
- `ping`: `{}`.
- `interrupt`: `{}`. Returns `{contextReset: true, connected}` after recovery.
- `close`: `{}`. Terminates workers and exits the bridge.

An evaluation error contains completed result rows and captured output under
`error.data`. `error.data.timedOut` identifies execution timeouts. `nodb: true`
initializes BSON helpers and JavaScript without opening a database connection.

## Validation

Run `node --test mongosh-bridge.test.js` for offline regression checks covering
variables, multiple results, output, BSON types, lazy autocomplete, infinite-loop
interruption, timeout, and recovery. This test never connects to a database.

Official interfaces:

- <https://github.com/mongodb-js/mongosh/tree/main/packages/shell-evaluator>
- <https://github.com/mongodb-js/mongosh/tree/main/packages/shell-api>
- <https://github.com/mongodb-js/mongosh/tree/main/packages/service-provider-node-driver>
- <https://github.com/mongodb-js/mongosh/tree/main/packages/node-runtime-worker-thread>
