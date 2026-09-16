'use strict';

// Offline regression checks: this suite never connects to a database.
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const readline = require('node:readline');
const path = require('node:path');
const { test } = require('node:test');

test('persistent mongosh context, BSON, output, autocomplete, interruption and recovery', async () => {
  const child = spawn(process.execPath, [path.join(__dirname, 'mongosh-bridge.js')], {
    stdio: ['pipe', 'pipe', 'pipe'], env: process.env
  });
  let nextId = 0;
  let stderr = '';
  child.stderr.on('data', (data) => { stderr += data; });
  const pending = new Map();
  const lines = readline.createInterface({ input: child.stdout });
  lines.on('line', (line) => {
    const response = JSON.parse(line);
    const listener = pending.get(response.id);
    assert.ok(listener, `Unexpected response: ${line}`);
    pending.delete(response.id);
    listener(response);
  });
  child.on('exit', () => {
    for (const listener of pending.values()) listener({ error: { message: `Worker exited: ${stderr}` } });
    pending.clear();
  });
  const rpc = (method, params = {}) => new Promise((resolve) => {
    const id = ++nextId;
    pending.set(id, resolve);
    child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
  });
  try {
    const connected = await rpc('connect', { nodb: true });
    assert.ifError(connected.error);
    assert.equal(connected.result.versions.architecture, process.arch);
    const initial = await rpc('eval', { code: 'var persistent = 40; persistent + 2; print("hello", 42); ({oid:ObjectId("0123456789abcdef01234567"), n:NumberLong("9223372036854775807"), date:ISODate("2026-01-01T00:00:00Z")})' });
    assert.ifError(initial.error);
    assert.equal(initial.result.results[1].output, '42');
    assert.equal(initial.result.results[2].output, 'hello 42');
    const bson = JSON.parse(initial.result.results[3].documents[0]);
    assert.equal(bson.oid.$oid, '0123456789abcdef01234567');
    assert.equal(bson.n.$numberLong, '9223372036854775807');
    assert.deepEqual(bson.date, { $date: { $numberLong: '1767225600000' } });
    const scalarDate = await rpc('eval', { code: 'ISODate("2026-01-01T00:00:00Z")' });
    assert.ifError(scalarDate.error);
    assert.equal(scalarDate.result.results[0].documents.length, 0);
    assert.match(scalarDate.result.results[0].output, /2026-01-01/);
    const persisted = await rpc('eval', { code: 'persistent += 2; persistent' });
    assert.ifError(persisted.error);
    assert.equal(persisted.result.results.at(-1).output, '42');
    const lexical = await rpc('eval', { code: 'let mutable = 10; const fixed = 2; mutable + fixed' });
    assert.ifError(lexical.error);
    assert.equal(lexical.result.results.at(-1).output, '12');
    assert.equal((await rpc('eval', { code: 'mutable + fixed' })).result.results[0].output, '12');
    const template = await rpc('eval', { code: 'var text = `first\nuse notADatabase\nlast`; text' });
    assert.ifError(template.error);
    assert.equal(template.result.results.at(-1).output, 'first\nuse notADatabase\nlast');
    const completionEngineLoaded = async () => {
      const response = await rpc('eval', { code: 'Object.keys(require.cache).some(p => p.includes("@mongodb-js/mongodb-ts-autocomplete/"))' });
      assert.ifError(response.error);
      return response.result.results[0].output;
    };
    assert.equal(await completionEngineLoaded(), 'false', 'collection queries must not load TypeScript autocomplete');
    assert.ifError((await rpc('invalidateAutocomplete')).error);
    assert.equal(await completionEngineLoaded(), 'false', 'cache invalidation must not load autocomplete');
    const complete = await rpc('autocomplete', { code: 'ObjectI' });
    assert.ifError(complete.error);
    assert.ok(complete.result.completions.some((value) => value.includes('ObjectId')), JSON.stringify(complete));
    assert.equal(await completionEngineLoaded(), 'true');
    const completeWithoutCollections = await rpc('autocomplete', { code: 'NumberL', includeCollectionNames: false });
    assert.ifError(completeWithoutCollections.error);
    assert.ok(completeWithoutCollections.result.completions.some((value) => value.includes('NumberLong')));
    assert.ifError((await rpc('invalidateAutocomplete')).error);
    assert.ok((await rpc('autocomplete', { code: 'ObjectI' })).result.completions.includes('ObjectId'));
    const partial = await rpc('eval', { code: '({ok:1}); print("before"); throw new Error("expected")' });
    assert.equal(partial.error.message, 'expected');
    assert.equal(partial.error.data.results.length, 2);
    const infinite = rpc('eval', { code: 'while (true) {}' });
    const interrupted = await rpc('interrupt');
    assert.equal(interrupted.result.contextReset, true);
    assert.equal((await infinite).error.data.contextReset, true);
    const after = await rpc('eval', { code: 'typeof persistent; 6 * 7' });
    assert.ifError(after.error);
    assert.equal(after.result.results[0].output, 'undefined');
    assert.equal(after.result.results[1].output, '42');
    const timeout = await rpc('eval', { code: 'while (true) {}', timeoutMS: 100 });
    assert.equal(timeout.error.data.timedOut, true);
    const recovered = await rpc('eval', { code: '1 + 1' });
    assert.ifError(recovered.error);
    assert.equal(recovered.result.results[0].output, '2');
    assert.equal((await rpc('close')).result.closed, true);
  } finally {
    child.kill();
    lines.close();
  }
});
