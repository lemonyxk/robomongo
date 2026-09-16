#!/usr/bin/env node
'use strict';

// JSON-RPC 2.0 over newline-delimited stdin/stdout. Shell code runs in a worker,
// keeping both asynchronous and CPU-bound evaluations interruptible.
const { Worker, isMainThread, parentPort } = require('node:worker_threads');
const fs = require('node:fs');
const path = require('node:path');
const { createRequire } = require('node:module');

function errorObject(error, extra = {}) {
  return {
    code: Number.isInteger(error.code) ? error.code : -32000,
    message: error.message || String(error),
    data: { name: error.name || 'Error', ...extra, ...(error.data || {}) }
  };
}

if (isMainThread) {
  const readline = require('node:readline');
  const pending = new Map();
  let worker;
  let connection;
  let closing = false;
  let resetting;
  let generation = 0;
  const send = (message) => process.stdout.write(JSON.stringify({ jsonrpc: '2.0', ...message }) + '\n');

  function failWorker(current, error) {
    if (worker !== current || closing) return;
    worker = undefined;
    for (const [id, entry] of pending) {
      clearTimeout(entry.timer);
      const failure = errorObject(error, { contextReset: true, workerFailed: true });
      if (entry.internal) entry.reject(failure);
      else send({ id, error: failure });
    }
    pending.clear();
    void current.terminate();
  }

  function startWorker() {
    const current = new Worker(__filename, { stdout: true, stderr: true });
    worker = current;
    // Even scripts using require('process').stdout cannot corrupt JSON framing.
    current.stdout.on('data', (data) => process.stderr.write(data));
    current.stderr.on('data', (data) => process.stderr.write(data));
    current.on('message', (message) => {
      if (worker !== current) return;
      const entry = pending.get(message.id);
      if (!entry) return;
      pending.delete(message.id);
      clearTimeout(entry.timer);
      if (!message.error && entry.method === 'connect') connection = entry.params;
      if (!message.error && entry.method === 'use' && connection)
        connection = { ...connection, database: message.result.database };
      if (!message.error && message.result?.database && connection)
        connection = { ...connection, database: message.result.database };
      if (entry.internal) {
        if (message.error) entry.reject(message.error);
        else entry.resolve(message.result);
      } else send(message);
    });
    current.on('error', (error) => {
      failWorker(current, error);
    });
    current.on('exit', (code) => {
      failWorker(current, new Error(`Shell worker exited with code ${code}`));
    });
    return current;
  }

  function dispatch(request, internal = false) {
    return new Promise((resolve, reject) => {
      if (pending.has(request.id)) {
        const error = { code: -32600, message: 'Request id is already in use' };
        if (!internal) send({ id: request.id, error });
        resolve();
        return;
      }
      const entry = { ...request, internal, resolve, reject };
      const timeout = Number(request.params?.timeoutMS);
      if (timeout > 0) {
        entry.timer = setTimeout(() => {
          if (internal) {
            pending.delete(request.id);
            const old = worker;
            worker = undefined;
            if (old) void old.terminate();
            reject({ code: -32800, message: 'Shell reconnection timed out',
              data: { contextReset: true, timedOut: true } });
          } else void resetWorker(`Shell execution exceeded ${timeout} ms`, true);
        }, timeout);
      }
      pending.set(request.id, entry);
      (worker || startWorker()).postMessage(request);
      if (!internal) resolve();
    });
  }

  function resetWorker(reason, timedOut = false) {
    if (resetting) return resetting;
    resetting = (async () => {
      const old = worker;
      worker = undefined;
      const error = {
        code: -32800,
        message: `${reason}. Shell variables and open cursors have been reset.`,
        data: { contextReset: true, timedOut }
      };
      for (const [id, entry] of pending) {
        clearTimeout(entry.timer);
        if (entry.internal) entry.reject(error);
        else send({ id, error });
      }
      pending.clear();
      if (old) await old.terminate();
      if (closing) return { contextReset: true, connected: false };
      startWorker();
      if (connection) {
        try {
          await dispatch({ id: `__reconnect_${++generation}`, method: 'connect', params: connection }, true);
        } catch (failure) {
          return { contextReset: true, connected: false, error: failure };
        }
      }
      return { contextReset: true, connected: !!connection };
    })().finally(() => { resetting = undefined; });
    return resetting;
  }

  const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
  input.on('line', (line) => {
    void (async () => {
      let request;
      try {
        request = JSON.parse(line);
        if (request.jsonrpc !== '2.0' || !['string', 'number'].includes(typeof request.id) ||
            typeof request.method !== 'string')
          throw Object.assign(new Error('Expected a JSON-RPC 2.0 request with id and method'), { code: -32600 });
        request.params ||= {};
        if (request.method === 'interrupt') {
          const result = await resetWorker('Shell execution was interrupted');
          send({ id: request.id, result });
        } else if (request.method === 'close') {
          closing = true;
          if (worker) await worker.terminate();
          send({ id: request.id, result: { closed: true } });
          process.exitCode = 0;
          input.close();
          process.stdin.destroy();
        } else {
          if (resetting) await resetting;
          await dispatch(request);
        }
      } catch (error) {
        if (!request && error instanceof SyntaxError) error.code = -32700;
        send({ id: request?.id ?? null, error: errorObject(error) });
      }
    })();
  });
  input.on('close', () => {
    if (closing) return;
    closing = true;
    if (worker) void worker.terminate();
  });
} else {
  void runShellWorker();
}

async function runShellWorker() {
  const vm = require('node:vm');
  const util = require('node:util');
  const { EventEmitter } = require('node:events');
  const dependencyDirectory = process.env.ROBOMONGO_MONGOSH_PACKAGE_DIR ||
    (fs.existsSync(path.join(__dirname, 'node_modules')) ? __dirname :
      path.resolve(__dirname, '../../../build/deps/mongosh-runtime'));
  const dependencyRequire = createRequire(path.join(dependencyDirectory, 'package.json'));
  const packageVersion = (name) => JSON.parse(fs.readFileSync(
    path.join(path.dirname(dependencyRequire.resolve(name)), '../package.json'), 'utf8')).version;
  const { NodeDriverServiceProvider } = dependencyRequire('@mongosh/service-provider-node-driver');
  const { ShellInstanceState, toShellResult, getShellApiType } = dependencyRequire('@mongosh/shell-api');
  const { ShellEvaluator } = dependencyRequire('@mongosh/shell-evaluator');
  const { EJSON } = dependencyRequire('bson');
  const { createAutocomplete, createGlobalCompletions, LIMITS: completionLimits } = require('./mongosh-autocomplete');
  const parser = await import(require('node:url').pathToFileURL(dependencyRequire.resolve('@babel/parser')).href);
  let provider;
  let state;
  let context;
  let evaluator;
  let autocomplete;
  let server = '';
  let displayBatchSize = 50;
  let prints = [];
  let queue = Promise.resolve();
  let cursorSequence = 0;
  const cursorPrefix = require('node:crypto').randomUUID();
  const cursors = new Map();
  const canonical = (value) => EJSON.stringify(value, { relaxed: false });
  const display = (value) => typeof value === 'string' ? value : util.inspect(value, {
    colors: false, depth: 20, maxArrayLength: 1000, breakLength: 100, compact: false
  });
  const databaseName = () => {
    try { return state.currentDb.getName(); } catch { return ''; }
  };
  const checkConnected = () => {
    if (!state) throw new Error('The shell is not initialized. Call connect first.');
  };
  const pageSize = (value) => {
    const size = value === undefined ? displayBatchSize : Number(value);
    if (!Number.isSafeInteger(size) || size < 1 || size > 100000)
      throw new Error('batchSize must be an integer between 1 and 100000');
    return size;
  };

  async function initialize(params) {
    autocomplete?.dispose();
    autocomplete = undefined;
    if (state) await state.close();
    else if (provider) await provider.close();
    cursors.clear();
    const bus = new EventEmitter();
    // mongosh reports optional metadata failures here; keep them out of stdout.
    bus.on('mongosh:error', () => {});
    const driverOptions = { appName: 'Robo3T', ...params.options };
    if (driverOptions.tlsCRLFile) {
      driverOptions.crl = fs.readFileSync(driverOptions.tlsCRLFile, 'utf8');
      delete driverOptions.tlsCRLFile;
    }
    provider = await NodeDriverServiceProvider.connect(
      params.uri || 'mongodb://127.0.0.1:27017/',
      driverOptions, { nodb: !!params.nodb }, bus);
    state = new ShellInstanceState(provider, bus, { nodb: !!params.nodb });
    state.setPreFetchCollectionAndDatabaseNames(false);
    displayBatchSize = pageSize(params.batchSize ?? 50);
    state.setEvaluationListener({
      onPrint(values) { prints.push(values.map((value) => display(value.printable)).join(' ')); },
      getConfig(key) { return key === 'displayBatchSize' ? displayBatchSize : undefined; },
      onPrompt() { throw new Error('Interactive shell prompts are not supported; configure credentials in the connection.'); },
      onExit(code) { throw Object.assign(new Error('Shell requested exit'), { data: { exitRequested: true, exitCode: code || 0 } }); },
      onLoad(filename) {
        const resolvedFilename = path.resolve(filename);
        return { resolvedFilename, evaluate: () => evaluateCode(fs.readFileSync(resolvedFilename, 'utf8'), resolvedFilename) };
      }
    });
    context = vm.createContext({ Buffer, setTimeout, clearTimeout, setInterval, clearInterval,
      TextEncoder, TextDecoder, URL, URLSearchParams, require: dependencyRequire });
    // VM intrinsics such as Math/JSON are not own properties of the external
    // context object. Capture their descriptors before any user code can run.
    const intrinsicDescriptors = vm.runInContext('Object.getOwnPropertyDescriptors(globalThis)', context);
    state.setCtx(context);
    evaluator = new ShellEvaluator(state, (value) => value);
    // Store a credential-free server label for the UI.
    server = params.uri ? params.uri.replace(/\/\/[^/]*@/, '//').split('/').slice(0, 3).join('/') : '';
    if (params.database && !params.nodb) state.setDbFunc(state.currentDb.getSiblingDB(params.database));
    // Capture this connection; background metadata from an old connection must
    // never read through a newly assigned provider after reconnecting.
    const completionProvider = provider;
    autocomplete = createAutocomplete({ getDatabase: databaseName,
      ...createGlobalCompletions(context, intrinsicDescriptors),
      loadFields: params.nodb ? undefined : async (database, collection, options) => {
        const cursor = completionProvider.find(database, collection, {}, {
          limit: options.limit, batchSize: 1, maxTimeMS: options.maxTimeMS,
          timeoutMS: 300, signal: options.signal
        });
        const documents = [];
        let sampleCost = 0;
        try {
          while (documents.length < completionLimits.documents && !options.signal.aborted) {
            const document = await cursor.next();
            if (document === null) break;
            documents.push(document);
            sampleCost += completionDocumentCost(document);
            if (sampleCost >= 256 * 1024) break;
          }
          return documents;
        } finally { await cursor.close().catch(() => {}); }
      },
      loadCollections: params.nodb ? undefined : async (database, options) => {
        // A single bounded batch avoids eagerly collecting a database with
        // thousands of namespaces into memory on the query editor's worker.
        const result = await completionProvider.runCommand(database, {
          listCollections: 1, nameOnly: true, authorizedCollections: true,
          cursor: { batchSize: completionLimits.fields }, maxTimeMS: options.maxTimeMS
        }, { signal: options.signal, timeoutMS: 300 });
        const cursorId = result.cursor?.id;
        if (cursorId && String(cursorId) !== '0') {
          await completionProvider.runCommand(database, {
            killCursors: '$cmd.listCollections', cursors: [cursorId]
          }, { signal: options.signal, timeoutMS: 300 }).catch(() => {});
        }
        return (result.cursor?.firstBatch || []).map((entry) => entry.name);
      }
    });
    return { database: databaseName(), server, connected: !params.nodb, contextReset: false,
      versions: { node: process.version, architecture: process.arch,
        shellApi: packageVersion('@mongosh/shell-api'),
        shellEvaluator: packageVersion('@mongosh/shell-evaluator'),
        serviceProvider: packageVersion('@mongosh/service-provider-node-driver') } };
  }

  function evaluateCode(code, filename = 'Robo3T') {
    return evaluator.customEval((source, scope, file) => new vm.Script(source, {
      filename: file, displayErrors: true
    }).runInContext(scope), code, context, filename);
  }

  // Approximate a bounded sample budget without serializing large BSON values.
  function completionDocumentCost(document) {
    let remaining = completionLimits.nodes, cost = 0;
    function visit(value, depth) {
      if (remaining-- <= 0 || depth > completionLimits.depth || cost >= 256 * 1024) return;
      if (typeof value === 'string') { cost += value.length * 2; return; }
      if (!value || typeof value !== 'object') { cost += 8; return; }
      if (value._bsontype) { cost += value.buffer?.byteLength || 32; return; }
      for (const key in value) {
        if (remaining <= 0 || cost >= 256 * 1024) break;
        if (!Object.hasOwn(value, key)) continue;
        cost += key.length * 2;
        const descriptor = Object.getOwnPropertyDescriptor(value, key);
        if (descriptor && 'value' in descriptor) visit(descriptor.value, depth + 1);
      }
    }
    visit(document, 0);
    return cost;
  }

  function statements(code) {
    const parse = (source) => parser.parse(source, { sourceType: 'script', allowAwaitOutsideFunction: true,
      allowReturnOutsideFunction: false, errorRecovery: false });
    const result = [];
    let javascript = '';
    const flush = () => {
      const ast = parse(javascript);
      result.push(...ast.program.body.map((node) => javascript.slice(node.start, node.end)));
      javascript = '';
    };
    for (const line of code.split('\n')) {
      // Accept direct shell commands on their own lines, including mixed scripts.
      // A preceding incomplete JS fragment means this line is inside a string,
      // comment, template, or block; never rewrite it as a shell command there.
      let direct = /^\s*(?:use|show|help|it|exit|quit|cls)(?:\s+[^;]*)?;?\s*$/.test(line);
      if (direct) {
        try { parse(javascript); } catch { direct = false; }
      }
      if (direct) { flush(); result.push(line); }
      else javascript += line + '\n';
    }
    flush();
    return result;
  }

  function queryInfo(cursor, batchSize) {
    const construction = cursor._constructionOptions;
    if (!construction) return undefined;
    if (construction.method !== 'find') {
      const [database, collection] = construction.args;
      return { database, collection: typeof collection === 'string' ? collection : '$cmd.aggregate',
        query: '{}', fields: '{}', limit: 0, skip: 0, batchSize, options: 0, special: false, readOnly: true };
    }
    const [database, collection, filter = {}, findOptions = {}] = construction.args;
    const options = { ...findOptions };
    for (const chain of cursor._chains || []) {
      if (chain.args.length) options[chain.method] = chain.args[0];
      if (chain.method === 'readConcern') options.readConcern = { level: chain.args[0] };
      if (['returnKey', 'showRecordId', 'noCursorTimeout', 'allowPartialResults'].includes(chain.method))
        options[chain.method] = chain.args[0] ?? true;
      if (chain.method === 'tailable') {
        options.tailable = true;
        options.awaitData = !!chain.args[0]?.awaitData;
      }
    }
    const query = { query: filter };
    for (const [option, modifier] of Object.entries({ sort: 'orderby', hint: '$hint',
      maxTimeMS: '$maxTimeMS', comment: '$comment', min: '$min', max: '$max',
      returnKey: '$returnKey', showRecordId: '$showDiskLoc', collation: 'collation',
      readConcern: 'readConcern' })) {
      if (options[option] !== undefined) query[modifier] = options[option];
    }
    if (options.readPref) {
      const chain = cursor._chains.find((item) => item.method === 'readPref');
      query.$readPreference = { mode: options.readPref, ...(chain?.args[1] ? { tags: chain.args[1] } : {}) };
    } else if (options.readPreference) {
      query.$readPreference = typeof options.readPreference === 'string'
        ? { mode: options.readPreference } : options.readPreference;
    }
    let flags = (options.tailable ? 2 : 0) | (options.noCursorTimeout ? 16 : 0) |
      (options.awaitData ? 32 : 0) | (options.allowPartialResults ? 128 : 0);
    for (const chain of cursor._chains || []) {
      if (chain.method === 'addOption') flags |= Number(chain.args[0]);
    }
    return { database, collection, query: canonical(query), fields: canonical(options.projection || {}),
      limit: Math.abs(options.limit || 0), skip: options.skip || 0, batchSize, options: flags, special: true,
      readOnly: !!cursor._transform || !!options.returnKey || !!Object.keys(options.projection || {}).length };
  }

  async function cursorPage(params) {
    const record = cursors.get(params.cursorId);
    if (!record) throw new Error('This cursor has expired or the shell was reset. Run the query again.');
    const baseSkip = record.queryInfo?.skip || 0;
    const skip = Number(params.skip ?? baseSkip);
    const size = pageSize(params.batchSize);
    if (!Number.isSafeInteger(skip) || skip < 0) throw new Error('skip must be a non-negative integer');
    const offset = Math.max(0, skip - baseSkip);
    const completionSample = [];
    while (!record.exhausted && record.documents.length < offset + size) {
      const document = await record.cursor.tryNext();
      if (document === null) {
        record.exhausted = record.cursor.isExhausted();
        break;
      }
      record.documents.push(canonical(document));
      if (completionSample.length < completionLimits.documents) completionSample.push(document);
    }
    if (record.queryInfo?.collection && !record.queryInfo.readOnly)
      autocomplete?.warm(record.queryInfo.database, record.queryInfo.collection, completionSample);
    return { type: record.type, statement: record.statement, output: '',
      documents: record.documents.slice(offset, offset + size), cursorId: params.cursorId,
      hasMore: !record.exhausted || record.documents.length > offset + size,
      queryInfo: record.queryInfo, skip, batchSize: size };
  }

  async function resultFor(raw, statement, batchSize) {
    const type = getShellApiType(raw);
    if (raw && typeof raw.tryNext === 'function' && typeof raw.isExhausted === 'function') {
      const cursorId = `${cursorPrefix}:${++cursorSequence}`;
      if (cursors.size >= 64) {
        const oldest = cursors.keys().next().value;
        const previous = cursors.get(oldest);
        await previous.cursor.close();
        cursors.delete(oldest);
      }
      cursors.set(cursorId, { cursor: raw, type: type || 'Cursor', statement,
        queryInfo: queryInfo(raw, batchSize), documents: [], exhausted: false });
      return cursorPage({ cursorId, batchSize });
    }
    const shellResult = await toShellResult(raw);
    const printable = shellResult.printable;
    const result = { type: type || typeof raw, statement, output: '', documents: [] };
    if (typeof raw?.ns === 'string' && raw.storageSize !== undefined && raw.nindexes !== undefined)
      result.type = 'collectionStats';
    if (raw === undefined) return result;
    const objects = Array.isArray(printable) ? printable : [printable];
    if (objects.every((object) => object !== null && !object._bsontype &&
        Object.prototype.toString.call(object) === '[object Object]')) {
      try { result.documents = objects.map(canonical); } catch { /* Functions/cycles remain printable. */ }
    }
    if (!result.documents.length) result.output = display(printable);
    return result;
  }

  async function handle(method, params) {
    if (method === 'connect') return initialize(params);
    checkConnected();
    if (method === 'use') {
      if (typeof params.database !== 'string' || !params.database) throw new Error('database is required');
      state.setDbFunc(state.currentDb.getSiblingDB(params.database));
      return { database: databaseName(), server };
    }
    if (method === 'autocomplete') {
      return autocomplete.complete(params.code, { includeCollectionNames: params.includeCollectionNames !== false,
        tokenOnly: params.tokenOnly === true, functionCalls: params.functionCalls === true, quoteKeys: params.quoteKeys === true });
    }
    if (method === 'cursorPage') return cursorPage(params);
    if (method === 'invalidateAutocomplete') {
      autocomplete.invalidate();
      return { invalidated: true };
    }
    if (method === 'ping') {
      await state.currentDb.runCommand({ ping: 1 });
      return { database: databaseName(), server };
    }
    if (method === 'setBatchSize') {
      displayBatchSize = pageSize(params.batchSize);
      return { batchSize: displayBatchSize };
    }
    if (method !== 'eval') throw Object.assign(new Error(`Unknown method: ${method}`), { code: -32601 });
    const results = [];
    const batchSize = pageSize(params.batchSize);
    for (const statement of statements(String(params.code || ''))) {
      prints = [];
      const started = performance.now();
      try {
        const raw = await evaluateCode(statement, params.filename);
        const result = await resultFor(raw, statement, batchSize);
        result.output = [...prints, result.output].filter(Boolean).join('\n');
        result.elapsedMS = Math.round(performance.now() - started);
        results.push(result);
      } catch (error) {
        error.data = { ...(error.data || {}), results, output: prints.join('\n'), statement,
          database: databaseName(), server };
        throw error;
      }
    }
    return { results, database: databaseName(), server, contextReset: false };
  }

  parentPort.on('message', (request) => {
    // Completion reads a cache snapshot only. It must not wait behind a slow
    // evaluation or metadata request in the shell's serialized command queue.
    if (request.method === 'autocomplete' || request.method === 'invalidateAutocomplete') {
      void handle(request.method, request.params || {}).then(
        (result) => parentPort.postMessage({ id: request.id, result }),
        (error) => parentPort.postMessage({ id: request.id, error: errorObject(error) }));
      return;
    }
    queue = queue.then(async () => {
      try { parentPort.postMessage({ id: request.id, result: await handle(request.method, request.params || {}) }); }
      catch (error) { parentPort.postMessage({ id: request.id, error: errorObject(error) }); }
    });
  });
}
