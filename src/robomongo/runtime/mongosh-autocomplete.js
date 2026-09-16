'use strict';

// Deliberately independent of mongosh's TypeScript engine. Every keystroke uses
// a bounded lexer and cached metadata; database work only runs in the background.
const LIMITS = Object.freeze({ input: 32768, documents: 20, fields: 512, depth: 6,
  arrayItems: 3, nodes: 2048, entries: 64, suggestions: 200, fieldLength: 256 });
const { isProxy } = require('node:util').types;
const words = (s) => s.split(' ');
const DATABASE = words('getCollection getCollectionNames getCollectionInfos getSiblingDB getName getMongo createCollection dropDatabase runCommand adminCommand aggregate stats version serverStatus currentOp killOp watch');
const COLLECTION = words('find findOne aggregate countDocuments estimatedDocumentCount distinct insertOne insertMany updateOne updateMany replaceOne deleteOne deleteMany findOneAndUpdate findOneAndReplace findOneAndDelete bulkWrite createIndex createIndexes getIndexes dropIndex dropIndexes drop stats renameCollection watch explain validate');
const CURSOR = words('sort limit skip projection hint maxTimeMS batchSize count toArray forEach map explain hasNext next close collation readConcern readPref comment allowDiskUse');
const methodCandidates = (names) => names.map((name) => ({ name, callable: true }));
const DATABASE_METHODS = methodCandidates(DATABASE);
const COLLECTION_METHODS = methodCandidates(COLLECTION);
const CURSOR_METHODS = methodCandidates(CURSOR);
const GLOBALS = words('db rs sh ObjectId ISODate Date NumberInt NumberLong NumberDecimal Decimal128 UUID BinData BSONRegExp Timestamp MinKey MaxKey DBRef EJSON JSON Math print printjson load use show help');
const LOGICAL = words('$and $or $nor $expr $text $where $jsonSchema $comment');
const FILTER = words('$eq $ne $gt $gte $lt $lte $in $nin $exists $type $regex $options $all $elemMatch $size $not $mod $bitsAllClear $bitsAllSet $bitsAnyClear $bitsAnySet $geoWithin $geoIntersects $near $nearSphere');
const UPDATE = words('$set $unset $inc $mul $min $max $rename $setOnInsert $currentDate $addToSet $pop $pull $push $pullAll $bit');
const STAGES = words('$match $project $group $sort $limit $skip $unwind $lookup $addFields $set $unset $count $facet $replaceRoot $replaceWith $sample $sortByCount $bucket $bucketAuto $unionWith $setWindowFields $out $merge');
const EXPRESSION = words('$sum $avg $min $max $first $last $push $addToSet $count $add $subtract $multiply $divide $mod $round $abs $cond $ifNull $switch $eq $ne $gt $gte $lt $lte $and $or $not $in $filter $map $reduce $size $arrayElemAt $concatArrays $concat $toString $toInt $toLong $toDouble $toDate $convert $dateToString $dateFromString $year $month $dayOfMonth $literal $let $mergeObjects $objectToArray $arrayToObject');
const OPTIONS = words('upsert arrayFilters collation hint sort projection returnDocument maxTimeMS writeConcern bypassDocumentValidation');
const keyIdentifier = /^[\p{L}_$][\p{L}\p{N}_$]*$/u;
const identifierCharacter = /[\p{L}\p{N}_$]/u;
const tokenCharacter = /[\p{L}\p{N}_$.]/u;
const metadataKey = (database, collection = '') => JSON.stringify([database, collection]);

// Inspect only data descriptors, including class prototypes (rs/sh methods are
// non-enumerable). Proxy traps, accessors and user functions must never run while
// typing. Intrinsics are captured once before the shell evaluates user code.
function createGlobalCompletions(context, intrinsics = {}) {
  const isObject = (value) => value !== null && (typeof value === 'object' || typeof value === 'function');
  const publicName = (name) => keyIdentifier.test(name) && !name.startsWith('_') && name !== 'constructor';
  const globalDescriptor = (name) => Object.getOwnPropertyDescriptor(context, name) ||
    Object.getOwnPropertyDescriptor(intrinsics, name)?.value;
  function descriptorOf(value, name) {
    for (let depth = 0; depth < LIMITS.depth && isObject(value); ++depth) {
      if (isProxy(value)) return;
      const descriptor = Object.getOwnPropertyDescriptor(value, name);
      if (descriptor) return descriptor;
      value = Object.getPrototypeOf(value);
    }
  }
  return {
    getGlobals: () => [...new Set([...Object.getOwnPropertyNames(context), ...Object.keys(intrinsics)])]
      .filter((name) => keyIdentifier.test(name)).slice(0, LIMITS.fields),
    isGlobalFunction(name) {
      const descriptor = globalDescriptor(name);
      return !!descriptor && 'value' in descriptor && typeof descriptor.value === 'function';
    },
    getGlobalMembers(path) {
      if (!Array.isArray(path) || !path.length || path.length > LIMITS.depth || !path.every(publicName)) return [];
      let descriptor = globalDescriptor(path[0]);
      for (let index = 1; index < path.length && descriptor && 'value' in descriptor; ++index)
        descriptor = descriptorOf(descriptor.value, path[index]);
      if (!descriptor || !('value' in descriptor)) return [];
      let value = descriptor.value;
      const candidates = [], seen = new Set();
      let remaining = LIMITS.fields;
      for (let depth = 0; depth < LIMITS.depth && isObject(value) && remaining > 0; ++depth) {
        if (isProxy(value)) break;
        const prototype = Object.getPrototypeOf(value);
        // Do not crowd shell helpers with Object.prototype's generic methods.
        if (depth > 0 && prototype === null) break;
        for (const name of Object.getOwnPropertyNames(value)) {
          if (--remaining < 0) break;
          if (seen.has(name)) continue;
          seen.add(name);
          if (!publicName(name)) continue;
          const member = Object.getOwnPropertyDescriptor(value, name);
          candidates.push({ name, callable: !!member && 'value' in member && typeof member.value === 'function' });
        }
        value = prototype;
      }
      return candidates;
    }
  };
}

function collectFields(documents) {
  const fields = new Set();
  let remaining = LIMITS.nodes;
  const seen = new WeakSet();
  function visit(value, prefix, depth) {
    if (!value || typeof value !== 'object' || depth > LIMITS.depth || remaining-- <= 0 ||
        fields.size >= LIMITS.fields || seen.has(value) || value._bsontype || value instanceof Date) return;
    seen.add(value);
    if (Array.isArray(value)) {
      for (let i = 0; i < Math.min(value.length, LIMITS.arrayItems) && remaining > 0; ++i)
        visit(value[i], prefix, depth + 1);
      return;
    }
    // for-in lets us stop early on huge documents instead of allocating all keys.
    for (const key in value) {
      if (remaining <= 0 || fields.size >= LIMITS.fields) break;
      --remaining;
      if (!Object.hasOwn(value, key)) continue;
      const field = prefix ? `${prefix}.${key}` : key;
      if (!key || field.length > LIMITS.fieldLength || /[\x00-\x1f]/.test(field)) continue;
      fields.add(field);
      const descriptor = Object.getOwnPropertyDescriptor(value, key);
      if (descriptor && 'value' in descriptor) visit(descriptor.value, field, depth + 1);
    }
  }
  for (let i = 0; i < Math.min(documents?.length || 0, LIMITS.documents) && remaining > 0; ++i)
    visit(documents[i], '', 0);
  return [...fields].sort();
}

function lex(code) {
  const tokens = [];
  let i = 0;
  while (i < code.length) {
    const ch = String.fromCodePoint(code.codePointAt(i));
    if (/\s/.test(ch)) { ++i; continue; }
    if (ch === '/' && code[i + 1] === '/') {
      const end = code.indexOf('\n', i + 2);
      if (end < 0) return { tokens, blocked: true };
      i = end + 1; continue;
    }
    if (ch === '/' && code[i + 1] === '*') {
      const end = code.indexOf('*/', i + 2);
      if (end < 0) return { tokens, blocked: true };
      i = end + 2; continue;
    }
    const start = i;
    if (ch === '"' || ch === "'" || ch === '`') {
      const quote = ch;
      let value = '', valid = true, closed = false;
      ++i;
      while (i < code.length) {
        const current = code[i++];
        if (current === quote) { closed = true; break; }
        if (current !== '\\') { value += current; continue; }
        if (i === code.length) { valid = false; break; }
        const escaped = code[i++];
        if ('\\\'"'.includes(escaped)) value += escaped;
        else if (escaped === 'u' || escaped === 'x') {
          const length = escaped === 'u' ? 4 : 2;
          const digits = code.slice(i, i + length);
          if (digits.length === length && /^[0-9a-f]+$/i.test(digits)) {
            value += String.fromCharCode(parseInt(digits, 16)); i += length;
          } else valid = false;
        } else if (Object.hasOwn({ n: 1, r: 1, t: 1 }, escaped)) {
          value += ({ n: '\n', r: '\r', t: '\t' })[escaped];
        } else valid = false;
      }
      tokens.push({ type: quote === '`' ? 'template' : 'string', value, start, end: i, quote, closed, valid });
      if (!closed) return { tokens, blocked: quote === '`' || !valid };
      continue;
    }
    // Skip regex literals, including escaped slashes and character classes.
    if (ch === '/' && (!tokens.length || /^(?:[([{:,=!;?&|+*%~<>^/-]|return|throw|case|yield|await|void|typeof|delete|in|instanceof)$/.test(tokens.at(-1).value))) {
      ++i;
      let inClass = false, closed = false;
      while (i < code.length) {
        const current = code[i++];
        if (current === '\\') { ++i; continue; }
        if (current === '[') inClass = true;
        if (current === ']') inClass = false;
        if (current === '/' && !inClass) { closed = true; break; }
      }
      if (!closed) return { tokens, blocked: true };
      while (/[a-z]/i.test(code[i] || '') && i < code.length) ++i;
      tokens.push({ type: 'regex', value: '', start, end: i }); continue;
    }
    if (identifierCharacter.test(ch)) {
      while (i < code.length) {
        const character = String.fromCodePoint(code.codePointAt(i));
        if (!identifierCharacter.test(character)) break;
        i += character.length;
      }
      tokens.push({ type: 'id', value: code.slice(start, i), start, end: i }); continue;
    }
    tokens.push({ type: 'punct', value: ch, start, end: ++i });
  }
  return { tokens, blocked: false };
}

function collectionOf(expression) {
  if (expression?.kind === 'collection' || expression?.kind === 'cursor') return expression;
  if (expression?.kind === 'member' && expression.base?.kind === 'db' &&
      !DATABASE.includes(expression.name)) return { ...expression.base, kind: 'collection', collection: expression.name };
}

function argumentRole(call, array = false) {
  if (!call?.target) return 'unknown';
  const method = call.method, argument = call.argument;
  if (method === 'aggregate' && argument === 0) return array ? 'pipeline' : 'stage';
  if (/^(updateOne|updateMany|findOneAndUpdate)$/.test(method) && argument === 1)
    return array ? 'pipeline' : 'update';
  if (/^(replaceOne|findOneAndReplace)$/.test(method) && argument === 1) return 'fields';
  if (/^(insertOne|insertMany)$/.test(method)) return 'fields';
  if (method === 'sort' || method === 'projection' || (['find', 'findOne'].includes(method) && argument === 1)) return 'fields';
  if (method === 'distinct') return argument === 1 ? 'query' : 'unknown';
  if (argument > 0) return 'options';
  return /^(find|findOne|countDocuments|deleteOne|deleteMany|updateOne|updateMany|findOneAndUpdate|findOneAndDelete|replaceOne|findOneAndReplace)$/.test(method) ? 'query' : 'unknown';
}

function childRole(parent, array) {
  if (!parent) return 'unknown';
  if (parent.kind === 'call') return argumentRole(parent, array);
  if (parent.kind === 'array') return parent.role === 'pipeline' ? 'stage' : parent.role;
  if (parent.role === 'stage') {
    if (parent.key === '$match') return 'query';
    if (parent.key === '$group') return 'group';
    if (['$project', '$addFields', '$set'].includes(parent.key)) return 'expressionFields';
    if (parent.key === '$sort' || parent.key === '$unset') return 'fields';
    if (parent.key === '$lookup') return 'lookup';
    if (parent.key === '$facet') return array ? 'pipeline' : 'facets';
    return 'expression';
  }
  if (parent.role === 'facets') return 'pipeline';
  if (parent.role === 'query') {
    if (['$and', '$or', '$nor'].includes(parent.key)) return 'query';
    if (parent.key === '$expr') return 'expression';
    return 'filter';
  }
  if (parent.role === 'filter') return parent.key === '$elemMatch' ? 'query' : 'filter';
  if (parent.role === 'update') return 'updateFields';
  if (parent.role === 'updateFields') return ['$push', '$addToSet'].includes(parent.updateOperator) ? 'push' :
    parent.updateOperator === '$pull' ? 'query' : 'fields';
  if (['expressionFields', 'group', 'expression'].includes(parent.role)) return 'expression';
  return parent.role;
}

function analyze(code, database = '') {
  // Avoid an end-anchored * regex: a long identifier followed by punctuation
  // makes an unanchored search quadratic before it reaches the empty suffix.
  if (code.length > LIMITS.input) return { trailing: '', prefixLength: code.length, database, kind: 'none' };
  let tokenStart = code.length;
  while (tokenStart > 0) {
    const low = code.charCodeAt(tokenStart - 1);
    const width = low >= 0xdc00 && low <= 0xdfff && tokenStart > 1 &&
      code.charCodeAt(tokenStart - 2) >= 0xd800 && code.charCodeAt(tokenStart - 2) <= 0xdbff ? 2 : 1;
    if (!tokenCharacter.test(code.slice(tokenStart - width, tokenStart))) break;
    tokenStart -= width;
  }
  const trailing = code.slice(tokenStart);
  const prefixLength = code.length - trailing.length;
  const output = { trailing, prefixLength, database, kind: 'none' };
  const { tokens, blocked } = lex(code);
  if (blocked) return output;
  const last = tokens.at(-1);
  const openString = last?.type === 'string' && !last.closed ? last : undefined;
  // The token being typed is not yet part of the enclosing object's grammar.
  const active = tokens.filter((token) => token !== openString && token.start < prefixLength);
  const stack = [];
  let expression, member = false;
  for (const token of active) {
    const parent = stack.at(-1);
    if (token.type === 'id' || token.type === 'string' || token.type === 'template' || token.type === 'regex') {
      if (parent?.kind === 'object' && parent.expectKey) {
        parent.key = token.value; parent.expectKey = false; parent.awaitColon = true;
      }
      if (parent?.kind === 'call') {
        ++parent.argumentTokens;
        if (parent.argument === 0 && parent.argumentTokens === 1 && token.type === 'string' && token.valid)
          parent.literal = token.value;
      }
      if (member) {
        // Keep one over-limit segment as a rejection marker, without repeatedly
        // copying an arbitrarily long chain as the lexer advances.
        expression = expression?.kind === 'global' ? { kind: 'global',
          path: expression.path.length > LIMITS.depth ? expression.path : [...expression.path, token.value] } :
          { kind: 'member', base: expression, name: token.value }; member = false;
      } else expression = token.type !== 'id' ? undefined : token.value === 'db' ? { kind: 'db', database } :
        { kind: 'global', path: [token.value] };
      continue;
    }
    if (token.value === '.') { member = true; continue; }
    if (token.value === '(') {
      if (parent?.kind === 'call') ++parent.argumentTokens;
      const target = collectionOf(expression?.base);
      stack.push({ kind: 'call', callee: expression, target, method: expression?.name,
        argument: 0, argumentTokens: 0, literal: undefined });
      expression = undefined; member = false; continue;
    }
    if (token.value === '{' || token.value === '[') {
      if (parent?.kind === 'call') ++parent.argumentTokens;
      const role = childRole(parent, token.value === '[');
      const path = parent?.role === 'query' && parent.key && !parent.key.startsWith('$') ?
        [parent.path, parent.key].filter(Boolean).join('.') : parent?.path;
      stack.push({ kind: token.value === '{' ? 'object' : 'array', role, path,
        updateOperator: parent?.role === 'update' ? parent.key : parent?.updateOperator,
        expectKey: true, target: parent?.target });
      expression = undefined; member = false; continue;
    }
    if (token.value === ')' || token.value === '}' || token.value === ']') {
      const frame = stack.pop();
      expression = undefined; member = false;
      if (token.value === ')' && frame?.kind === 'call') {
        const base = frame.callee?.base;
        if (base?.kind === 'db' && frame.argument === 0 && frame.argumentTokens === 1 && frame.literal !== undefined) {
          if (frame.method === 'getSiblingDB') expression = { kind: 'db', database: frame.literal };
          if (frame.method === 'getCollection') expression = { kind: 'collection', database: base.database, collection: frame.literal };
        } else if (frame.target) expression = { ...frame.target, kind: 'cursor' };
      }
      continue;
    }
    if (token.value === ',') {
      if (parent?.kind === 'object') { parent.expectKey = true; parent.awaitColon = false; parent.key = undefined; }
      if (parent?.kind === 'call') { ++parent.argument; parent.argumentTokens = 0; }
      expression = undefined; member = false; continue;
    }
    if (token.value === ':') {
      if (parent?.kind === 'object') { parent.awaitColon = false; parent.inValue = true; }
      expression = undefined; member = false; continue;
    }
    if (parent?.kind === 'call') ++parent.argumentTokens;
    expression = undefined; member = false;
  }
  const parent = stack.at(-1);
  const call = [...stack].reverse().find((frame) => frame.kind === 'call' && frame.target);
  if (call) Object.assign(output, { database: call.target.database, collection: call.target.collection, method: call.method });
  if (openString) {
    output.quote = openString.quote;
    output.quoteStart = openString.start;
    output.typed = openString.value;
    // Replacement starts at the trailing token, which may follow spaces/hyphens.
    output.stringHead = code.slice(openString.start + 1, prefixLength);
    if (output.stringHead.includes('\\')) return output;
    if (parent?.kind === 'call' && parent.method === 'getCollection' && parent.argument === 0 &&
        parent.callee?.base?.kind === 'db') {
      output.kind = 'collections'; output.database = parent.callee.base.database; return output;
    }
    if (parent?.kind === 'object' && parent.expectKey) {
      output.kind = 'keys'; output.role = parent.role; output.path = parent.path; return output;
    }
    if (parent?.kind === 'call' && parent.method === 'distinct' && parent.argument === 0) {
      output.kind = 'fields'; return output;
    }
    if (['expressionFields', 'group', 'expression'].includes(parent?.role) && openString.value.startsWith('$')) {
      output.kind = 'references'; return output;
    }
    return output;
  }
  // Dot chains without parentheses are wholly in the trailing replacement token.
  if (trailing.startsWith('db.')) {
    const parts = trailing.split('.');
    if (parts.length === 2) return { ...output, kind: 'database', typed: parts[1], tokenHead: 'db.' };
    return { ...output, kind: 'collectionMethods', collection: parts.slice(1, -1).join('.'),
      typed: parts.at(-1), tokenHead: parts.slice(0, -1).join('.') + '.' };
  }
  if (trailing.startsWith('.') && expression && expression.kind !== 'global') {
    const target = collectionOf(expression);
    return { ...output, kind: expression.kind === 'db' ? 'database' : target?.kind === 'cursor' ? 'cursorMethods' :
      target ? 'collectionMethods' : 'none', database: expression.database, collection: target?.collection,
    typed: trailing.slice(1), tokenHead: '.' };
  }
  if (member && expression && expression.kind !== 'global') {
    const target = collectionOf(expression);
    return { ...output, kind: expression.kind === 'db' ? 'database' : target?.kind === 'cursor' ? 'cursorMethods' :
      target ? 'collectionMethods' : 'none', database: expression.database, collection: target?.collection,
    typed: trailing, tokenHead: '' };
  }
  if (parent?.kind === 'object' && parent.expectKey) {
    return { ...output, kind: 'keys', role: parent.role, path: parent.path, typed: trailing };
  }
  if (parent?.awaitColon) return output;
  if (trailing.includes('.') || member) {
    const parts = trailing.split('.');
    const path = parts.slice(0, -1);
    if (trailing.startsWith('.') || member) {
      if (expression?.kind !== 'global') return output;
      if (trailing.startsWith('.')) path.shift();
      path.unshift(...expression.path);
    }
    if (path.length && path.length <= LIMITS.depth && path.every((name) => keyIdentifier.test(name)))
      return { ...output, kind: 'globalMembers', path, typed: parts.at(-1),
        tokenHead: trailing.slice(0, trailing.length - parts.at(-1).length) };
    return output;
  }
  if (!trailing.includes('.')) return { ...output, kind: 'globals', typed: trailing };
  return output;
}

function createAutocomplete({ getDatabase = () => '', getGlobals = () => [], isGlobalFunction = () => false,
  getGlobalMembers = () => [], loadFields,
  loadCollections, now = Date.now, ttlMS = 60000, negativeTTLMS = 10000,
  timeoutMS = 300, maxEntries = LIMITS.entries } = {}) {
  const cache = new Map();
  let generation = 0, running = 0;
  const jobs = [];
  function prune() {
    while (cache.size > Math.min(maxEntries, LIMITS.entries)) {
      const key = cache.keys().next().value;
      const old = cache.get(key);
      cache.delete(key); old.controller?.abort();
    }
  }
  function pump() {
    while (running < 2 && jobs.length) {
      const job = jobs.shift();
      if (cache.get(job.key) !== job.entry || job.generation !== generation) continue;
      ++running;
      const controller = new AbortController();
      job.entry.controller = controller;
      let timer;
      const deadline = new Promise((_, reject) => {
        timer = setTimeout(() => { controller.abort(); reject(new Error('Metadata completion timeout')); }, timeoutMS);
        timer.unref?.();
      });
      Promise.race([deadline, Promise.resolve().then(() => job.load({ signal: controller.signal,
        maxTimeMS: Math.min(200, timeoutMS), limit: LIMITS.documents }))]).then((values) => {
        if (cache.get(job.key) !== job.entry || generation !== job.generation) return;
        const fetched = job.fields ? collectFields(values) : [...new Set((values || [])
          .slice(0, LIMITS.fields).filter((value) => typeof value === 'string' && value.length <= LIMITS.fieldLength))].sort();
        job.entry.values = job.fields ? [...new Set([...job.entry.values, ...fetched])].slice(0, LIMITS.fields).sort() : fetched;
        job.entry.expires = now() + (job.entry.values.length ? ttlMS : negativeTTLMS);
      }, () => {
        if (cache.get(job.key) === job.entry && generation === job.generation) job.entry.expires = now() + negativeTTLMS;
      }).finally(() => {
        clearTimeout(timer); job.entry.pending = false; job.entry.controller = undefined;
        --running; pump();
      });
    }
  }
  function get(key, load, fields) {
    let entry = cache.get(key);
    if (!entry) { entry = { values: [], expires: 0, pending: false }; cache.set(key, entry); prune(); }
    else { cache.delete(key); cache.set(key, entry); }
    if (!entry.pending && entry.expires <= now() && load) {
      entry.pending = true;
      // Keep only jobs whose cache entries are still live when many tabs change.
      if (jobs.length >= LIMITS.entries) {
        for (let i = jobs.length - 1; i >= 0; --i) if (cache.get(jobs[i].key) !== jobs[i].entry) jobs.splice(i, 1);
      }
      jobs.push({ key, entry, load, fields, generation }); pump();
    }
    return entry.values;
  }
  function complete(code, { includeCollectionNames = true, tokenOnly = false, functionCalls = false, quoteKeys = false } = {}) {
    code = String(code || '');
    const info = analyze(code, getDatabase());
    // The editor replaces the entire existing string for quoted suggestions,
    // including its delimiters, rather than adding another pair inside it.
    const replacementStart = quoteKeys && info.quote ? info.quoteStart : info.prefixLength;
    const result = { completions: [], replace: code.slice(replacementStart) };
    if (info.kind === 'none') return result;
    let candidates = [];
    const fields = () => info.collection ? get('f' + metadataKey(info.database, info.collection), loadFields &&
      ((options) => loadFields(info.database, info.collection, options)), true) : [];
    const collections = () => includeCollectionNames ? get('c' + metadataKey(info.database), loadCollections &&
      ((options) => loadCollections(info.database, options)), false) : [];
    // Preserve each candidate's origin: a collection or field can have the same
    // name as a method, but must never receive call parentheses.
    if (info.kind === 'database') candidates = [...DATABASE_METHODS, ...collections().filter((name) => keyIdentifier.test(name))];
    else if (info.kind === 'collections') candidates = collections();
    else if (info.kind === 'collectionMethods') candidates = COLLECTION_METHODS;
    else if (info.kind === 'cursorMethods') candidates = CURSOR_METHODS;
    else if (info.kind === 'fields') candidates = fields();
    else if (info.kind === 'references') candidates = [...fields().map((field) => '$' + field), '$$ROOT', '$$CURRENT', '$$NOW', '$$REMOVE'];
    else if (info.kind === 'globals') candidates = [...GLOBALS, ...getGlobals().slice(0, 512)];
    else if (info.kind === 'globalMembers') candidates = getGlobalMembers(info.path).slice(0, LIMITS.fields);
    else if (info.kind === 'keys') {
      const names = () => info.path ? fields().filter((field) => field.startsWith(info.path + '.')).map((field) => field.slice(info.path.length + 1)) : fields();
      if (info.role === 'query') candidates = [...names(), ...LOGICAL];
      else if (info.role === 'filter') candidates = [...names(), ...FILTER];
      else if (info.role === 'update') candidates = UPDATE;
      else if (info.role === 'stage') candidates = STAGES;
      else if (info.role === 'expression') candidates = EXPRESSION;
      else if (info.role === 'group') candidates = ['_id', ...names()];
      else if (['fields', 'updateFields', 'expressionFields'].includes(info.role)) candidates = names();
      else if (info.role === 'push') candidates = words('$each $slice $position $sort');
      else if (info.role === 'lookup') candidates = words('from localField foreignField as let pipeline');
      else if (info.role === 'options') candidates = OPTIONS;
    }
    const typed = info.typed ?? info.trailing;
    const unique = new Set();
    for (const entry of candidates) {
      const candidate = typeof entry === 'string' ? entry : entry.name;
      if (!candidate.startsWith(typed)) continue;
      let insertion = candidate;
      if (quoteKeys && (info.quote || info.kind === 'keys')) insertion = JSON.stringify(candidate);
      else if (info.quote) {
        insertion = candidate.replace(/\\/g, '\\\\').replace(new RegExp(info.quote, 'g'), '\\' + info.quote);
        if (!insertion.startsWith(info.stringHead || '')) continue;
        insertion = insertion.slice((info.stringHead || '').length);
      } else if (info.kind === 'keys' && !keyIdentifier.test(candidate)) insertion = JSON.stringify(candidate);
      else insertion = (info.tokenHead || '') + candidate;
      if (functionCalls && !info.quote && (entry.callable ||
          (info.kind === 'globals' && isGlobalFunction(candidate)))) insertion += '()';
      if (insertion.length > 512 || unique.has(insertion)) continue;
      unique.add(insertion);
      result.completions.push(tokenOnly ? insertion : code.slice(0, replacementStart) + insertion);
      if (result.completions.length >= LIMITS.suggestions) break;
    }
    return result;
  }
  return { complete,
    warm(database, collection, documents) {
      if (!database || !collection || collection.startsWith('$cmd')) return;
      const key = 'f' + metadataKey(database, collection);
      const values = collectFields(documents);
      if (!values.length) return;
      const existing = cache.get(key);
      const merged = [...new Set([...(existing?.values || []), ...values])].slice(0, LIMITS.fields).sort();
      if (existing) { existing.values = merged; existing.expires = now() + ttlMS; }
      else cache.set(key, { values: merged, expires: now() + ttlMS, pending: false });
      prune();
    },
    invalidate() {
      ++generation;
      for (const job of jobs) job.entry.pending = false;
      jobs.length = 0;
      for (const entry of cache.values()) { entry.expires = 0; entry.controller?.abort(); }
    },
    dispose() { ++generation; for (const entry of cache.values()) entry.controller?.abort(); cache.clear(); jobs.length = 0; },
    stats() { return { entries: cache.size, running, queued: jobs.length }; }
  };
}

module.exports = { createAutocomplete, createGlobalCompletions, analyze, collectFields, LIMITS };
