'use strict';

// Offline acceptance checks. Metadata loaders below are fakes; no database is used.
const assert = require('node:assert/strict');
const { performance } = require('node:perf_hooks');
const { test } = require('node:test');
const { createAutocomplete, createGlobalCompletions, collectFields, LIMITS } = require('./mongosh-autocomplete');

const flush = () => new Promise((resolve) => setImmediate(resolve));
const deferred = () => {
  let resolve;
  let reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
};

function engine(options = {}) {
  return createAutocomplete({
    getDatabase: () => 'app',
    getGlobals: () => ['ObjectId', 'NumberLong', 'ISODate'],
    loadFields: async () => [],
    loadCollections: async () => [],
    ...options
  });
}

function tokens(completer, code, options = {}) {
  const result = completer.complete(code, { ...options, tokenOnly: true });
  assert.ok(result && !result.then, 'completion must return synchronously');
  assert.ok(Array.isArray(result.completions));
  return result.completions;
}

function names(completer, code, options = {}) {
  return tokens(completer, code, options).map((value) => value
    .replace(/^['"]/, '').replace(/['"](?:\s*:\s*)?$/, '').replace(/:\s*$/, ''));
}

function includes(completer, code, expected) {
  const actual = names(completer, code);
  for (const name of expected) assert.ok(actual.includes(name), `${code}: missing ${name}; got ${JSON.stringify(actual)}`);
  return actual;
}

test('global helper members complete with correct replacement boundaries and callable metadata', () => {
  class ReplicaSet {
    status() {}
    conf() {}
    config() {}
  }
  const completer = engine(createGlobalCompletions({ rs: new ReplicaSet(), sh: { status() {} },
    EJSON: { parse() {}, stringify() {} }, helper: { nested: { run() {}, value: 1 } } }));
  const options = { functionCalls: true };
  includes(completer, 'r', ['rs']);
  includes(completer, 's', ['sh']);
  assert.deepEqual(tokens(completer, 'rs.st', options), ['rs.status()']);
  assert.deepEqual(tokens(completer, 'rs.co', options), ['rs.conf()', 'rs.config()']);
  assert.ok(tokens(completer, 'rs.', options).includes('rs.status()'));
  assert.deepEqual(tokens(completer, 'rs.st'), ['rs.status']);
  assert.deepEqual(tokens(completer, 'sh.st', options), ['sh.status()']);
  assert.deepEqual(tokens(completer, 'EJSON.pa', options), ['EJSON.parse()']);
  assert.deepEqual(tokens(completer, 'helper.nested.r', options), ['helper.nested.run()']);
  assert.deepEqual(tokens(completer, 'helper.nested.v', options), ['helper.nested.value']);
  assert.deepEqual(tokens(completer, 'rs .st', options), ['.status()']);
  assert.deepEqual(tokens(completer, 'rs. st', options), ['status()']);
  assert.deepEqual(tokens(completer, 'rs /* comment */\n. st', options), ['status()']);
  assert.deepEqual(tokens(completer, 'helper . nested.r', options), ['nested.run()']);
  assert.deepEqual(completer.complete('const result = rs.st', options),
    { replace: 'rs.st', completions: ['const result = rs.status()'] });
});

test('global members do not override query fields, quoted strings or calls', () => {
  const completer = engine(createGlobalCompletions({ rs: { status() {} } }));
  completer.warm('app', 'users', [{ rs: { state: 1 } }]);
  assert.deepEqual(tokens(completer, 'db.users.find({ rs.st', { quoteKeys: true }), ['"rs.state"']);
  assert.deepEqual(tokens(completer, 'db.users.find({ "rs.st', { quoteKeys: true }), ['"rs.state"']);
  assert.deepEqual(tokens(completer, 'db.users.find({ value: rs.st', { functionCalls: true }), ['rs.status()']);
  for (const code of ['"rs.st', 'db.users.find({ value: "rs.st', '// rs.st', '/* rs.st',
    'rs.status().st', 'unknown().rs.st', '1.rs.st', '`${rs.st'])
    assert.deepEqual(tokens(completer, code), [], code);
});

test('runtime descriptors provide VM intrinsics without invoking getters, proxies or functions', () => {
  const vm = require('node:vm');
  const context = vm.createContext({});
  const intrinsics = vm.runInContext('Object.getOwnPropertyDescriptors(globalThis)', context);
  let effects = 0;
  Object.defineProperty(Object.getPrototypeOf(intrinsics), 'inheritedGetter', {
    get() { ++effects; return { value: { run() {} } }; }
  });
  const proxy = new Proxy({}, {
    ownKeys() { ++effects; throw new Error('ownKeys must not run'); },
    getPrototypeOf() { ++effects; throw new Error('getPrototypeOf must not run'); },
    getOwnPropertyDescriptor() { ++effects; throw new Error('descriptor trap must not run'); }
  });
  Object.assign(context, { helper: {
    run() { ++effects; },
    get getter() { ++effects; return { run() {} }; },
    proxy,
    inheritedProxy: Object.create(proxy)
  }, proxy });
  Object.defineProperty(context, 'getter', { get() { ++effects; return context.helper; } });
  const completer = engine(createGlobalCompletions(context, intrinsics));
  const options = { functionCalls: true };
  assert.deepEqual(tokens(completer, 'JSON.pa', options), ['JSON.parse()']);
  assert.deepEqual(tokens(completer, 'Math.ma', options), ['Math.max()']);
  assert.deepEqual(tokens(completer, 'Math.P', options), ['Math.PI']);
  assert.deepEqual(tokens(completer, 'parseIn', options), ['parseInt()']);
  assert.deepEqual(tokens(completer, 'helper.r', options), ['helper.run()']);
  assert.deepEqual(tokens(completer, 'helper.g', options), ['helper.getter']);
  for (const code of ['getter.', 'inheritedGetter.', 'helper.getter.', 'proxy.', 'helper.proxy.', 'helper.inheritedProxy.',
    'helper.inheritedProxy.nested.', 'helper.run().'])
    assert.deepEqual(tokens(completer, code, options), [], code);
  assert.equal(effects, 0);
  context.JSON = { replacement() {} };
  assert.deepEqual(tokens(completer, 'JSON.', options), ['JSON.replacement()'], 'current values override captured intrinsics');
});

test('runtime member inspection respects shadowed accessors and bounded depth and candidates', () => {
  let effects = 0;
  const value = Object.create({ run() {}, other() {} });
  Object.defineProperty(value, 'run', { get() { ++effects; return function() {}; } });
  const completions = createGlobalCompletions({ helper: value });
  assert.deepEqual(completions.getGlobalMembers(['helper']).filter(({ name }) => name === 'run'),
    [{ name: 'run', callable: false }]);
  assert.equal(effects, 0);
  assert.deepEqual(completions.getGlobalMembers(Array(LIMITS.depth + 1).fill('helper')), []);
  const large = Object.fromEntries(Array.from({ length: 10000 }, (_, index) => [`field${index}`, index]));
  assert.ok(createGlobalCompletions({ large }).getGlobalMembers(['large']).length <= LIMITS.fields);
  const completer = engine(completions);
  const started = performance.now();
  assert.deepEqual(tokens(completer, 'helper .'.repeat(4000) + ' run'), []);
  assert.ok(performance.now() - started < 200, 'long spaced member chains must stay bounded');
});

test('function-call insertion is opt-in and distinguishes method candidates from data names', async () => {
  const completer = engine({ loadCollections: async () => ['stats', 'find', 'count'] });
  const options = { functionCalls: true };
  assert.ok(tokens(completer, 'db.users.fi').includes('db.users.find'));
  assert.ok(!tokens(completer, 'db.users.fi').includes('db.users.find()'));
  assert.ok(tokens(completer, 'db.users.fi', options).includes('db.users.find()'));
  assert.deepEqual(tokens(completer, 'db.users.find({}).lim', options), ['.limit()']);
  tokens(completer, 'db.st', options);
  await flush();
  const collisions = tokens(completer, 'db.st', options);
  assert.ok(collisions.includes('db.stats()'), 'database method needs parentheses');
  assert.ok(collisions.includes('db.stats'), 'same-named collection stays a data candidate');
  assert.deepEqual(tokens(completer, 'db.fi', options), ['db.find']);
  assert.deepEqual(tokens(completer, 'db.getCollection("st', options), ['stats']);
  completer.warm('app', 'users', [{ find: true, count: 1, ObjectId: 'data' }]);
  assert.deepEqual(tokens(completer, 'db.users.find({ fi', options), ['find']);
  assert.deepEqual(tokens(completer, 'db.users.find({ "ObjectI', options), ['ObjectId']);
  assert.deepEqual(tokens(completer, 'db.users.find({ count: { $gt', options), ['$gt', '$gte']);
});

test('quoted keys cover fields and operators while function calls remain executable', () => {
  const completer = engine();
  const options = { quoteKeys: true, functionCalls: true };
  completer.warm('app', 'users', [{ name: 'Ada', age: 30, find: true, profile: { name: 'Ada' } }]);
  const cases = [
    ['db.users.find({ na', ['name']],
    ['db.users.find({ profile: { na', ['name']],
    ['db.users.find({ age: { $gt', ['$gt', '$gte']],
    ['db.users.updateOne({}, { $un', ['$unset']],
    ['db.users.updateOne({}, { $set: { na', ['name']],
    ['db.users.aggregate([{ $gr', ['$group']],
    ['db.users.find({}).sort({ na', ['name']],
    ['db.users.find({ fi', ['find']]
  ];
  for (const [code, expected] of cases)
    assert.deepEqual(tokens(completer, code, options), expected.map((value) => JSON.stringify(value)), code);
  assert.ok(tokens(completer, 'db.users.fi', options).includes('db.users.find()'));
  assert.deepEqual(tokens(completer, 'db.users.find({}).lim', options), ['.limit()']);
  assert.deepEqual(tokens(completer, 'db.users.find({ na'), ['name'], 'legacy caller remains opt-in');
});

test('quoted suggestions replace complete strings and escape literal contents once', async () => {
  const completer = engine({ loadCollections: async () => ['audit-log'] });
  const options = { quoteKeys: true };
  const fieldNames = ['name', 'user-name', 'two words', 'a"b', "a'b", 'a\\b', '名字'];
  completer.warm('app', 'users', [Object.fromEntries(fieldNames.map((name) => [name, true]))]);
  for (const quote of ['"', "'"]) {
    const code = 'db.users.find({ ' + quote;
    const actual = tokens(completer, code, options);
    for (const name of fieldNames) assert.ok(actual.includes(JSON.stringify(name)), name);
    assert.deepEqual(tokens(completer, code + 'user-n', options), ['"user-name"']);
    assert.deepEqual(tokens(completer, code + 'two w', options), ['"two words"']);
    assert.deepEqual(tokens(completer, 'db.users.distinct(' + quote + 'na', options), ['"name"']);
    assert.deepEqual(tokens(completer, 'db.users.aggregate([{ $project: { user: ' + quote + '$na', options), ['"$name"']);
    const prefix = 'db.getCollection(';
    tokens(completer, prefix + quote + 'audit-l', options);
    await flush();
    assert.deepEqual(tokens(completer, prefix + quote + 'audit-l', options), ['"audit-log"']);
  }
});

test('full quoted completions and replacement metadata share the entire literal range', () => {
  const completer = engine();
  completer.warm('app', 'users', [{ 'user-name': 'Ada' }]);
  for (const prefix of ['db.users.find({ ', 'db.users.find({ "na": "literal", ']) {
    for (const quote of ['"', "'"]) {
      const typed = quote + 'user-n';
      const result = completer.complete(prefix + typed, { quoteKeys: true });
      assert.equal(result.replace, typed);
      assert.deepEqual(result.completions, [prefix + '"user-name"']);
    }
  }
});

test('global call parentheses use function identity instead of candidate spelling', () => {
  const values = { ObjectId() {}, NumberLong: 7, customFunction() {}, customValue: {} };
  const completer = engine({ getGlobals: () => Object.keys(values), isGlobalFunction: (name) =>
    typeof Object.getOwnPropertyDescriptor(values, name)?.value === 'function' });
  assert.deepEqual(tokens(completer, 'ObjectI', { functionCalls: true }), ['ObjectId()']);
  assert.deepEqual(tokens(completer, 'NumberL', { functionCalls: true }), ['NumberLong']);
  assert.deepEqual(tokens(completer, 'custom', { functionCalls: true }), ['customFunction()', 'customValue']);
  assert.deepEqual(tokens(completer, 'ObjectI'), ['ObjectId']);
});

test('collection fields, nested paths and array members come from sampled documents', () => {
  const completer = engine();
  completer.warm('app', 'users', [{
    name: 'Ada', age: 30, profile: { name: 'Ada', email: 'a@example.test' },
    purchases: [{ price: 3 }, { quantity: 2 }]
  }]);
  includes(completer, 'db.users.find({ na', ['name']);
  includes(completer, 'db.users.find({ "profile.e', ['profile.email']);
  includes(completer, 'db.getCollection("users").find({ "purchases.p', ['purchases.price']);
  includes(completer, 'db.users.find({ profile: { na', ['name']);
});

test('quoted collection names and fields retain their original query prefix', () => {
  const completer = engine();
  completer.warm('app', 'audit-log', [{ 'user-name': 'Ada', profile: { email: 'a@example.test' } }]);
  const code = "db.getCollection('audit-log').find({ 'user-n";
  const result = completer.complete(code);
  assert.ok(result.completions.some((value) => value.includes('user-name')));
  for (const value of result.completions) assert.ok(value.startsWith("db.getCollection('audit-log').find({ "));
  includes(completer, "db.getCollection('audit-log').find({ 'profile.e", ['profile.email']);
});

test('query operators are distinguished from updates and pipeline stages', () => {
  const completer = engine();
  const query = includes(completer, 'db.users.find({ age: { $g', ['$gt', '$gte']);
  assert.ok(!query.includes('$group'));
  const update = includes(completer, 'db.users.updateOne({}, { $', ['$set', '$unset', '$inc']);
  assert.ok(!update.includes('$match'));
  assert.ok(!update.includes('$gte'));
  const stages = includes(completer, 'db.users.aggregate([{ $', ['$match', '$group', '$project']);
  assert.ok(!stages.includes('$gte'));
});

test('aggregation stages restore their own field and operator context', () => {
  const completer = engine();
  completer.warm('app', 'users', [{ name: 'Ada', age: 30 }]);
  includes(completer, 'db.users.aggregate([{ $match: { na', ['name']);
  includes(completer, 'db.users.aggregate([{ $match: { age: { $g', ['$gt', '$gte']);
  includes(completer, 'db.users.aggregate([{ $group: { total: { $su', ['$sum']);
  includes(completer, 'db.users.aggregate([{ $project: { full: { $con', ['$concat']);
  includes(completer, 'db.users.aggregate([{ $set: { full: { $con', ['$concat']);
  includes(completer, 'db.users.aggregate([{ $match: {} }, { $gr', ['$group']);
});

test('update values and projection/sort keys offer collection fields', () => {
  const completer = engine();
  completer.warm('app', 'users', [{ name: 'Ada', age: 30 }]);
  includes(completer, 'db.users.updateOne({}, { $set: { na', ['name']);
  includes(completer, 'db.users.find({}, { na', ['name']);
  includes(completer, 'db.users.find({}).sort({ ag', ['age']);
});

test('comments and ordinary string values do not offer structural completions', () => {
  const completer = engine();
  for (const code of [
    '// db.users.find({ $',
    'db.users.find({ /* $',
    'db.users.find({ name: "hello',
    'db.users.find({ name: \'hello'
  ]) assert.deepEqual(tokens(completer, code), [], code);
  includes(completer, '// old query\ndb.users.find({ age: { $g', ['$gt']);
});

test('regular expressions after expression operators do not offer globals', () => {
  const completer = engine();
  for (const code of ['const pattern = true ? /Obj', 'x && /Obj', 'x || /Obj', 'x ?? /Obj',
    'x + /Obj', 'x * /Obj', '!/Obj', 'void /Obj', 'x => /Obj', 'return /Obj', '/[Obj'])
    assert.deepEqual(tokens(completer, code), [], code);
  includes(completer, 'x / Obj', ['ObjectId']);
});

test('completion never evaluates user expressions', () => {
  const completer = engine();
  globalThis.__autocompleteSideEffect = 0;
  try {
    tokens(completer, '(globalThis.__autocompleteSideEffect++, db.users).find({ $');
    tokens(completer, 'db.getCollection((globalThis.__autocompleteSideEffect++, "users")).find({ $');
    assert.equal(globalThis.__autocompleteSideEffect, 0);
  } finally {
    delete globalThis.__autocompleteSideEffect;
  }
});

test('cold metadata completion is immediate and concurrent requests are deduplicated', async () => {
  const request = deferred();
  const calls = [];
  const completer = engine({ loadFields: (database, collection, options) => {
    calls.push({ database, collection, options });
    return request.promise;
  } });
  const started = performance.now();
  for (let index = 0; index < 20; index++) tokens(completer, 'db.users.find({ na');
  assert.ok(performance.now() - started < 200, 'typing must not wait for metadata');
  await flush();
  assert.equal(calls.length, 1);
  assert.equal(calls[0].database, 'app');
  assert.equal(calls[0].collection, 'users');
  assert.ok(calls[0].options.signal instanceof AbortSignal);
  assert.ok(calls[0].options.maxTimeMS > 0 && calls[0].options.maxTimeMS <= 1000);
  assert.ok(calls[0].options.limit > 0 && calls[0].options.limit <= 100);
  request.resolve([{ name: 'Ada' }]);
  await flush();
  includes(completer, 'db.users.find({ na', ['name']);
  await flush();
  assert.equal(calls.length, 1, 'cached fields must not re-query on every keystroke');
});

test('collection metadata is cached and opt-out avoids loading it', async () => {
  const calls = [];
  const completer = engine({ loadCollections: async (database) => {
    calls.push(database);
    return ['users', 'usage', 'audit-log'];
  } });
  tokens(completer, 'db.us', { includeCollectionNames: false });
  await flush();
  assert.deepEqual(calls, []);
  tokens(completer, 'db.us');
  await flush();
  includes(completer, 'db.us', ['db.users', 'db.usage']);
  includes(completer, "db.getCollection('au", ['audit-log']);
  await flush();
  assert.deepEqual(calls, ['app']);
});

test('field cache separates databases including getSiblingDB', () => {
  let currentDatabase = 'app';
  const completer = engine({ getDatabase: () => currentDatabase });
  completer.warm('app', 'users', [{ applicationName: 'Ada' }]);
  completer.warm('archive', 'users', [{ archivedName: 'Ada' }]);
  assert.ok(names(completer, 'db.users.find({ app').includes('applicationName'));
  assert.ok(!names(completer, 'db.users.find({ arch').includes('archivedName'));
  includes(completer, "db.getSiblingDB('archive').users.find({ arch", ['archivedName']);
  currentDatabase = 'archive';
  includes(completer, 'db.users.find({ arch', ['archivedName']);
  assert.ok(!names(completer, 'db.users.find({ app').includes('applicationName'));
});

test('method completion preserves dot-chain replacement boundaries', () => {
  const completer = engine();
  includes(completer, 'db.users.fi', ['db.users.find', 'db.users.findOne']);
  includes(completer, "db.getCollection('users').fi", ['.find', '.findOne']);
  includes(completer, 'db.users.find({}).so', ['.sort']);
  const result = completer.complete('db.users.fi', { tokenOnly: true });
  assert.equal(result.replace, 'db.users.fi');
});

test('aggregation field references and array element filters use sampled paths', () => {
  const completer = engine();
  completer.warm('app', 'users', [{ name: 'Ada', purchases: [{ price: 3 }] }]);
  includes(completer, 'db.users.aggregate([{ $group: { _id: "$na', ['$name']);
  includes(completer, 'db.users.aggregate([{ $project: { source: "$$R', ['$$ROOT', '$$REMOVE']);
  includes(completer, 'db.users.find({ purchases: { $elemMatch: { pr', ['price']);
});

test('Unicode names complete inside both quoted and unquoted field keys', () => {
  const completer = engine();
  completer.warm('app', '用户', [{ 姓名: 'Ada', 地址: { 城市: '台北' }, café: true }]);
  includes(completer, 'db.用户.find({ 姓', ['姓名']);
  includes(completer, 'db.用户.find({ "地址.城', ['地址.城市']);
  includes(completer, 'db.用户.find({ caf', ['café']);
});

test('failed metadata loads are negatively cached and remain retryable', async () => {
  let time = 1000;
  let calls = 0;
  const completer = engine({ now: () => time, negativeTTLMS: 100, loadFields: async () => {
    calls++;
    if (calls === 1) throw new Error('metadata permission denied');
    return [{ name: 'Ada' }];
  } });
  tokens(completer, 'db.users.find({ na');
  await flush();
  for (let index = 0; index < 20; index++) tokens(completer, 'db.users.find({ na');
  await flush();
  assert.equal(calls, 1);
  time += 101;
  tokens(completer, 'db.users.find({ na');
  await flush();
  assert.equal(calls, 2);
  includes(completer, 'db.users.find({ na', ['name']);
});

test('expired metadata refreshes without withholding known fields', async () => {
  let time = 1000;
  const request = deferred();
  let calls = 0;
  const completer = engine({ now: () => time, ttlMS: 100, loadFields: () => {
    calls++;
    return request.promise;
  } });
  completer.warm('app', 'users', [{ name: 'Ada' }]);
  time += 101;
  includes(completer, 'db.users.find({ na', ['name']);
  await flush();
  assert.equal(calls, 1);
  request.resolve([{ name: 'Ada', nickname: 'A' }]);
  await flush();
  includes(completer, 'db.users.find({ nick', ['nickname']);
});

test('invalidation prevents an older metadata response repopulating the cache', async () => {
  const request = deferred();
  const completer = engine({ loadFields: () => request.promise });
  tokens(completer, 'db.users.find({ old');
  await flush();
  completer.invalidate();
  completer.warm('app', 'users', [{ newField: true }]);
  request.resolve([{ oldField: true }]);
  await flush();
  includes(completer, 'db.users.find({ new', ['newField']);
  assert.ok(!names(completer, 'db.users.find({ old').includes('oldField'));
});

test('stuck metadata has a deadline and cannot block static operators', async () => {
  let signal;
  const completer = engine({ timeoutMS: 15, loadFields: (_database, _collection, options) => {
    signal = options.signal;
    return new Promise(() => {});
  } });
  tokens(completer, 'db.users.find({ na');
  await flush();
  includes(completer, 'db.users.find({ age: { $g', ['$gt']);
  await new Promise((resolve) => setTimeout(resolve, 40));
  assert.ok(signal.aborted, 'metadata request must be aborted at its deadline');
});

test('many collection contexts cap cache size, queued work and metadata concurrency', async () => {
  const completer = engine({ loadFields: (_database, _collection, { signal }) => new Promise((_resolve, reject) => {
    if (signal.aborted) return reject(new Error('aborted'));
    signal.addEventListener('abort', () => reject(new Error('aborted')), { once: true });
  }) });
  for (let index = 0; index < 500; index++) tokens(completer, `db.collection${index}.find({ fi`);
  const stats = completer.stats();
  assert.ok(stats.entries <= LIMITS.entries);
  assert.ok(stats.queued <= LIMITS.entries);
  assert.ok(stats.running <= 2);
  completer.dispose();
  await flush();
});

test('field discovery is finite for deep, cyclic and wide documents', () => {
  const document = { name: 'Ada', nested: { email: 'a@example.test' }, items: [{ price: 3 }] };
  document.loop = document;
  let deep = document;
  for (let index = 0; index < 1000; index++) deep = deep.child = {};
  for (let index = 0; index < 10000; index++) document[`field${index}`] = index;
  const started = performance.now();
  const fields = collectFields([document]);
  assert.ok(fields.includes('name'));
  assert.ok(fields.includes('nested.email'));
  assert.ok(fields.includes('items.price'));
  assert.equal(new Set(fields).size, fields.length);
  assert.deepEqual(fields, [...fields].sort());
  assert.ok(fields.length <= LIMITS.fields, 'schema discovery must cap fields');
  assert.ok(performance.now() - started < 200, 'schema discovery must bound traversal');
  assert.ok(LIMITS && typeof LIMITS === 'object');
});

test('token-only responses avoid repeating large editor buffers', () => {
  const completer = engine();
  const prefix = '/* previous query */\n'.repeat(500);
  const code = `${prefix}db.users.find({ age: { $g`;
  const tokenResult = completer.complete(code, { tokenOnly: true });
  const fullResult = completer.complete(code);
  assert.ok(tokenResult.completions.includes('$gt'));
  assert.ok(fullResult.completions.includes(`${prefix}db.users.find({ age: { $gt`));
  assert.equal(tokenResult.replace, '$g');
  assert.ok(JSON.stringify(tokenResult).length < 2048);
  assert.ok(JSON.stringify(fullResult).length > prefix.length);
});

test('warm typing stays bounded with many sampled fields and long input', () => {
  const completer = engine();
  const document = {};
  for (let index = 0; index < 10000; index++) document[`field${index}`] = index;
  completer.warm('app', 'users', [document]);
  const started = performance.now();
  for (let index = 0; index < 500; index++) {
    const values = tokens(completer, 'db.users.find({ fi');
    assert.ok(values.length <= LIMITS.suggestions, 'popup candidate count must be bounded');
  }
  const elapsed = performance.now() - started;
  assert.ok(elapsed < 1500, `500 cached completions took ${elapsed.toFixed(1)} ms`);
  const huge = ' '.repeat(2 * 1024 * 1024) + 'db.users.find({ $';
  assert.ok(tokens(completer, huge).length <= LIMITS.suggestions);
});

test('cached field, operator and long-editor latency stays interactive', (context) => {
  const completer = engine();
  completer.warm('app', 'users', [{ name: 'Ada', profile: { email: 'a@example.test' } }]);
  const samples = [
    ['field', 'db.users.find({ na'],
    ['operator', 'db.users.find({ age: { $g'],
    ['long-editor', 'var value=1;\n'.repeat(2400) + 'db.users.find({ na']
  ];
  for (const [label, code] of samples) {
    for (let index = 0; index < 20; index++) tokens(completer, code);
    const durations = [];
    for (let index = 0; index < 250; index++) {
      const started = performance.now();
      tokens(completer, code);
      durations.push(performance.now() - started);
    }
    durations.sort((left, right) => left - right);
    const median = durations[Math.floor(durations.length / 2)];
    const p95 = durations[Math.floor(durations.length * 0.95)];
    context.diagnostic(`${label}: median ${median.toFixed(3)} ms, p95 ${p95.toFixed(3)} ms (${code.length} characters)`);
    assert.ok(p95 < 100, `${label} p95 ${p95.toFixed(1)} ms exceeds interactive latency budget`);
  }
});

test('long identifier followed by punctuation cannot trigger quadratic suffix scanning', () => {
  const completer = engine();
  const inputs = ['a'.repeat(LIMITS.input - 1) + ';', 'a'.repeat(2 * 1024 * 1024) + ';'];
  for (const code of inputs) {
    const started = performance.now();
    tokens(completer, code);
    assert.ok(performance.now() - started < 200, 'trailing-token scan must be bounded even without a token at the end');
  }
});
