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
    const callable = await rpc('autocomplete', { code: 'ObjectI', tokenOnly: true, functionCalls: true });
    assert.ifError(callable.error);
    assert.deepEqual(callable.result.completions, ['ObjectId()']);
    for (const [code, expected] of [
      ['r', 'rs'], ['s', 'sh'], ['rs.', 'rs.status()'], ['rs.st', 'rs.status()'],
      ['rs.co', 'rs.conf()'], ['rs.co', 'rs.config()'], ['sh.st', 'sh.status()'],
      ['JSON.pa', 'JSON.parse()'], ['Math.ma', 'Math.max()'], ['Math.P', 'Math.PI'],
      ['EJSON.pa', 'EJSON.parse()'], ['printj', 'printjson()'], ['parseIn', 'parseInt()']
    ]) {
      const response = await rpc('autocomplete', { code, tokenOnly: true, functionCalls: true });
      assert.ifError(response.error);
      assert.ok(response.result.completions.includes(expected), `${code}: ${JSON.stringify(response.result)}`);
    }
    // Completion inspects the live shell's prototype methods without invoking
    // rs/sh commands; this worker has no database connection.
    assert.ifError((await rpc('eval', { code: `
      var completionMemberEffects = 0;
      var completionHelper = {
        run() { completionMemberEffects++; },
        get nested() { completionMemberEffects++; return { run() {} }; },
        proxy: new Proxy({}, {
          ownKeys() { completionMemberEffects++; throw new Error('proxy enumerated'); },
          getPrototypeOf() { completionMemberEffects++; throw new Error('proxy inspected'); }
        })
      };
    ` })).error);
    const member = await rpc('autocomplete', { code: 'completionHelper.r', tokenOnly: true, functionCalls: true });
    assert.ifError(member.error);
    assert.deepEqual(member.result.completions, ['completionHelper.run()']);
    for (const code of ['completionHelper.nested.', 'completionHelper.proxy.', 'completionHelper.run().',
      'const pattern = true ? /Obj', 'x && /Obj']) {
      const response = await rpc('autocomplete', { code, tokenOnly: true, functionCalls: true });
      assert.ifError(response.error);
      assert.deepEqual(response.result.completions, [], code);
    }
    const memberEffects = await rpc('eval', { code: 'completionMemberEffects' });
    assert.ifError(memberEffects.error);
    assert.equal(memberEffects.result.results[0].output, '0', 'member completion must not execute shell code');
    // defineProperty returns globalThis; do not serialize the whole shell during setup.
    assert.ifError((await rpc('eval', { code: 'var completionGetterCalls = 0; void Object.defineProperty(globalThis, "completionGetter", { configurable: true, get() { completionGetterCalls++; return function() {}; } });' })).error);
    const accessor = await rpc('autocomplete', { code: 'completionGetter', tokenOnly: true, functionCalls: true });
    assert.ifError(accessor.error);
    assert.ok(accessor.result.completions.includes('completionGetter'));
    assert.ok(!accessor.result.completions.includes('completionGetter()'));
    const getterCalls = await rpc('eval', { code: 'completionGetterCalls' });
    assert.ifError(getterCalls.error);
    assert.equal(getterCalls.result.results[0].output, '0', 'completion must not invoke global getters');
    assert.equal(await completionEngineLoaded(), 'false', 'typing must not load TypeScript autocomplete');
    const completeWithoutCollections = await rpc('autocomplete', { code: 'NumberL', includeCollectionNames: false });
    assert.ifError(completeWithoutCollections.error);
    assert.ok(completeWithoutCollections.result.completions.some((value) => value.includes('NumberLong')));
    const operator = await rpc('autocomplete', { code: 'db.users.find({ age: { $g', tokenOnly: true });
    assert.ifError(operator.error);
    assert.deepEqual(operator.result.completions, ['$gt', '$gte', '$geoWithin', '$geoIntersects']);
    // Metadata and an awaited user query must not block editor completions.
    let evaluationSettled = false;
    const slowEvaluation = rpc('eval', { code: 'new Promise(resolve => setTimeout(() => resolve(7), 150))' });
    slowEvaluation.then(() => { evaluationSettled = true; });
    const whileEvaluating = await rpc('autocomplete', { code: 'db.users.fi', tokenOnly: true });
    assert.ifError(whileEvaluating.error);
    assert.ok(whileEvaluating.result.completions.includes('db.users.find'));
    assert.equal(evaluationSettled, false, 'completion must bypass the asynchronous evaluation queue');
    assert.ifError((await slowEvaluation).error);
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
