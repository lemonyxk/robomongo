#include "robomongo/core/mongodb/MongoClient.h"

#include "robomongo/core/mongodb/MongoConnection.h"
#include "robomongo/core/mongodb/MongoConnection.h"
#include "robomongo/core/bson/Bson.h"
#include "robomongo/core/bson/Bson.h"

#include "robomongo/core/domain/MongoDocument.h"
#include "robomongo/core/utils/BsonUtils.h"
#include "robomongo/core/bson/Bson.h"

namespace
{
    std::string writeErrorMessage(const mongo::BSONObj &error)
    {
        std::string const message = error.getStringField("errmsg");
        return message.empty() ? error.toString() : message;
    }

    void runWriteCommand(mongo::DBClientBase *client, const std::string &database,
                         const mongo::BSONObj &command)
    {
        // Let the server apply its default write concern. Forcing w:1 would
        // weaken a cluster's majority default, allowing an immediate majority
        // read after a successful save to return the previous document.
        mongo::BSONObj result;
        if (!client->runCommand(database, command, result))
            throw std::runtime_error(writeErrorMessage(result));

        // An ok:1 response can still contain failed writes or a write concern
        // failure. In either case the caller must not report a successful save.
        std::string errors;
        mongo::BSONElement const writeErrors = result.getField("writeErrors");
        if (!writeErrors.eoo()) {
            for (auto const &error : writeErrors.Array()) {
                if (!errors.empty())
                    errors += "\n";
                errors += writeErrorMessage(error.Obj());
            }
        }

        mongo::BSONElement const concernError = result.getField("writeConcernError");
        if (!concernError.eoo()) {
            if (!errors.empty())
                errors += "\n";
            errors += "Write concern failed: " + writeErrorMessage(concernError.Obj());
        }

        if (!errors.empty())
            throw std::runtime_error(errors);
    }

    Robomongo::IndexInfo makeIndexInfoFromBsonObj(
        const Robomongo::MongoCollectionInfo &collection,
        const mongo::BSONObj &obj)
    {
        using namespace Robomongo::BsonUtils;
        Robomongo::IndexInfo info(collection);
        info._name = obj.getStringField("name");
        mongo::BSONObj keyObj = obj.getObjectField("key");
        if (keyObj.isValid()) 
            info._keys = jsonString(keyObj, mongo::TenGen, 1, Robomongo::DefaultEncoding, Robomongo::Utc);

        info._unique = obj.getBoolField("unique");
        info._backGround = obj.getBoolField("background");
        info._sparse = obj.getBoolField("sparse");
        info._ttl = obj.getIntField("expireAfterSeconds");
        info._defaultLanguage = obj.getStringField("default_language");
        info._languageOverride = obj.getStringField("language_override");
        mongo::BSONObj weightsObj = obj.getObjectField("weights");
        if (weightsObj.isValid()) 
            info._textWeights = jsonString(weightsObj, mongo::TenGen, 1, Robomongo::DefaultEncoding, 
                                           Robomongo::Utc);

        return info;
    }
}

namespace Robomongo
{
    MongoClient::MongoClient(mongo::DBClientBase *const dbclient) :
        _dbclient(dbclient) { }

    std::vector<std::string> MongoClient::getCollectionNamesWithDbname(const std::string &dbname) const
    {
        std::list<mongo::BSONObj> collList = _dbclient->getCollectionInfos(dbname);

        std::vector<std::string> collNames;	
        for (auto const& coll : collList)
            collNames.push_back(dbname + '.' + coll.getStringField("name")); // todo: verify

        std::sort(collNames.begin(), collNames.end());
        return collNames;
    }

    // Warning: 
    // Use string version dbVersionStr(), version number is corrupted after conversion to float
    // Todo: Remove this function
    float MongoClient::getVersion() const
    {
        float result = 0.0f;
        mongo::BSONObj resultObj;
        _dbclient->runCommand("db", BSON("buildInfo" << "1"), resultObj);
        std::string resultStr = BsonUtils::getField<mongo::String>(resultObj, "version");
        result = atof(resultStr.c_str());
        return result;
    }

    std::string MongoClient::dbVersionStr() const
    {
        mongo::BSONObj resultObj;
        _dbclient->runCommand("db", BSON("buildInfo" << "1"), resultObj);
        std::string const resultStr = BsonUtils::getField<mongo::String>(resultObj, "version");
        return resultStr;
    }

    std::string MongoClient::getStorageEngineType() const
    {
        mongo::BSONObj resultObj;
        _dbclient->runCommand("db", BSON("serverStatus" << "1"), resultObj);
        return resultObj.getObjectField("storageEngine").getStringField("name");
    }

    std::vector<std::string> MongoClient::getDatabaseNames() const
    {
        std::list<std::string> const& dbs = _dbclient->getDatabaseNames();
        std::vector<std::string> dbNames = {dbs.begin(), dbs.end()};
        std::sort(dbNames.begin(), dbNames.end());
        return dbNames;
    }

    std::vector<MongoUser> MongoClient::getUsers(const std::string &dbName)
    {
        mongo::BSONObjBuilder cmd;
        cmd.append("usersInfo", 1);

        mongo::BSONObj result;
        if (!_dbclient->runCommand(dbName, cmd.done(), result)) {
            std::string errStr = result.getStringField("errmsg");
            if (errStr.empty())
                errStr = "Failed to get error message.";

            throw std::runtime_error(errStr);
        }

        std::vector<MongoUser> users;
        for (auto const& usr : result.getField("users").Array())
            users.push_back(MongoUser(getVersion(), usr.embeddedObject()));

        return users;
    }

    void MongoClient::createUser(const std::string &dbName, const MongoUser &user)
    {
        mongo::BSONObjBuilder cmd;
        cmd.append("createUser", user.name());
        cmd.append("pwd", user.password());
        
        mongo::BSONArrayBuilder roles;
        auto const& rolesStrs = user.roles();
        for (auto const& roleStr : rolesStrs) {
            mongo::BSONObjBuilder role;
            role.append("role", roleStr).append("db", user.userSource());
            roles.append(role.done());
        }
        cmd.appendArray("roles", roles.done());

        mongo::BSONObj result;
        if (!_dbclient->runCommand(dbName, cmd.done(), result)) {
            std::string errStr = result.getStringField("errmsg");
            if (errStr.empty())
                errStr = "Failed to get error message.";
   
            throw std::runtime_error(errStr);
        }       
    }

    void MongoClient::dropUser(const std::string &dbName, const std::string &user)
    {
        mongo::BSONObjBuilder cmd;
        cmd.append("dropUser", user);

        mongo::BSONObj result;
        if (!_dbclient->runCommand(dbName, cmd.done(), result)) {
            std::string errStr = result.getStringField("errmsg");
            if (errStr.empty())
                errStr = "Failed to get error message.";

            throw std::runtime_error(errStr);
        }
    }

    std::vector<MongoFunction> MongoClient::getFunctions(const std::string &dbName) const
    {
        std::vector<MongoFunction> functions;

        std::unique_ptr<mongo::DBClientCursor> cursor(
            _dbclient->query(mongo::NamespaceString(dbName, "system.js"), mongo::Query().sort("_id")));

        // Cursor may be NULL, it means we have connectivity problem
        if (!cursor)
            throw std::runtime_error("Network error while attempting to load list of functions.");

        while (cursor->more()) {
            mongo::BSONObj bsonObj = cursor->next();
            try {
                MongoFunction func(bsonObj);
                functions.push_back(func);
            } catch (const std::exception &) {
                // skip invalid docs
            }
        }
        return functions;
    }

    std::vector<IndexInfo> MongoClient::getIndexes(const MongoCollectionInfo &collection) const
    {
        std::vector<IndexInfo> result;
        std::list<mongo::BSONObj> indexes = _dbclient->getIndexSpecs(collection.ns().toString());

        for (std::list<mongo::BSONObj>::iterator it = indexes.begin(); it != indexes.end(); ++it) {
            mongo::BSONObj bsonObj = *it;
            result.push_back(makeIndexInfoFromBsonObj(collection, bsonObj));
        }

        return result;
    }

    void MongoClient::addEditIndex(const IndexInfo &oldInfo, const IndexInfo &newInfo) const
    {   
        bool const editIndex = !oldInfo._name.empty();

        // 1.Step: Drop Index (if this is an Edit Index action).
        // MongoDB docs: To modify an existing index, you need to drop and recreate the index.
        std::string const ns = newInfo._collection.ns().toString();
        if (editIndex)
            _dbclient->dropIndex(ns, oldInfo._name);

        // 2.Step: Add/Edit Index
        auto const createIndexSpec = [](IndexInfo const& indexInfo) {
            mongo::IndexSpec indexSpec;
            indexSpec.name(indexInfo._name);
            indexSpec.addKeys(mongo::Robomongo::fromjson(indexInfo._keys));

            mongo::BSONObjBuilder optionsBuilder;

            auto const addIfTrue = [&](auto const& keyValuePair) {
                if (keyValuePair.second)
                    optionsBuilder.appendBool(keyValuePair.first, true);
            };

            addIfTrue(std::pair{ "unique", indexInfo._unique });
            addIfTrue(std::pair{ "background", indexInfo._backGround });
            addIfTrue(std::pair{ "sparse", indexInfo._sparse });

            if (!indexInfo._defaultLanguage.empty())
                optionsBuilder.append("default_language", indexInfo._defaultLanguage);

            if (!indexInfo._languageOverride.empty())
                optionsBuilder.append("language_override", indexInfo._languageOverride);

            if (!indexInfo._textWeights.empty() && !mongo::Robomongo::fromjson(indexInfo._textWeights).isEmpty())
                optionsBuilder.append("weights", mongo::Robomongo::fromjson(indexInfo._textWeights));

            if (indexInfo._ttl >= 0)
                optionsBuilder.append("expireAfterSeconds", indexInfo._ttl);

            indexSpec.addOptions(optionsBuilder.obj());
            return indexSpec;
        };

        auto const createIndex = [&](IndexInfo const& indexInfo) {
            runWriteCommand(_dbclient, newInfo._collection.ns().databaseName(),
                BSON("createIndexes" << newInfo._collection.ns().collectionName()
                     << "indexes" << BSON_ARRAY(createIndexSpec(indexInfo).toBSON())));
        };

        try {
            createIndex(newInfo);
        } 
        catch (std::exception const& /*ex*/) { // Logging of "ex" is done in upper scope
            if (editIndex) {
                // If we are here, index that is being edited, must have already been dropped and 
                // creation of new index failed. So, we try to at least recover the dropped (old) index
                createIndex(oldInfo);
            }
            throw;
        }
    }

    void MongoClient::renameIndexFromCollection(const MongoCollectionInfo &collection, const std::string &oldIndexName, const std::string &newIndexName) const
    {
        for (const auto& oldInfo : getIndexes(collection)) {
            if (oldInfo._name != oldIndexName) continue;
            auto renamed = oldInfo;
            renamed._name = newIndexName;
            addEditIndex(oldInfo, renamed);
            return;
        }
        throw std::runtime_error("Index does not exist: " + oldIndexName);
    }

    void MongoClient::dropIndexFromCollection(const MongoCollectionInfo &collection, const std::string &indexName) const
    {
        _dbclient->dropIndex(collection.ns().toString(), indexName);
    }

    void MongoClient::createFunction(const std::string &dbName, const MongoFunction &fun, 
                                     const std::string &existingFunctionName /* = QString() */)
    {
        MongoNamespace ns(dbName, "system.js");
        mongo::BSONObj obj = fun.toBson();

        if (existingFunctionName.empty()) { // create new function
            insertDocument(obj, ns);
        } else { // this is update

            std::string name = fun.name();

            if (existingFunctionName == name) { // update existing function code
                saveDocument(obj, ns);
            } else {    // update function name (remove & insert)
                insertDocument(obj, ns);
                removeDocuments(ns, mongo::Query(BSON("_id" << existingFunctionName)), true);
            }
        }
    }

    void MongoClient::dropFunction(const std::string &dbName, const std::string &name)
    {
        MongoNamespace ns(dbName, "system.js");

        mongo::BSONObjBuilder builder;
        builder.append("_id", name);
        mongo::BSONObj bsonQuery = builder.obj();
        mongo::Query query(bsonQuery);

        removeDocuments(ns, query, true);
    }

    void MongoClient::createDatabase(const std::string &dbName)
    {
        /*
        *  Here we are going to insert temp document to "<dbName>.temp" collection.
        *  This will create <dbName> database for us.
        *  Finally we are dropping just created temporary collection.
        */
        MongoNamespace ns(dbName, "temp");

        // If <dbName>.temp already exists, stop.
        if (_dbclient->exists(ns.toString()))
            throw std::runtime_error(dbName + ".temp already exists.");

        // Building { _id : "temp" } document
        mongo::BSONObjBuilder builder;
        builder.append("_id", "temp");
        mongo::BSONObj obj = builder.obj();

        // Insert this document
        insertDocument(obj, ns);

        // Drop temp collection
        _dbclient->dropCollection(ns.toString());
    }

    void MongoClient::dropDatabase(const std::string &dbName)
    {
        mongo::BSONObj info;
        if (!_dbclient->dropDatabase(dbName, mongo::WriteConcernOptions(), &info)) { // todo: do we catch errorStr via info - test it??
            std::string errStr = info.toString();
            if (errStr.empty())
                errStr = "Failed to get error message.";

            throw std::runtime_error(errStr);
        }
    }

    void MongoClient::createCollection(const std::string& ns, long long size, bool capped, int max, 
                                       const mongo::BSONObj& extraOptions, mongo::BSONObj* info)
    {
        if (capped && !size) throw std::invalid_argument("Capped collections require a size");
        mongo::BSONObj o;
        if (info == 0)
            info = &o;
        mongo::BSONObjBuilder b;
        std::string db = mongo::nsToDatabase(ns);
        b.append("create", ns.c_str() + db.length() + 1);
        if (size) {
            b.append("size", size);
        }
        if (capped) {
            b.append("capped", true);
        }
        if (max) {
            b.append("max", max);
        }
        b.appendElements(extraOptions);

        if (!_dbclient->exists(ns)) {
            mongo::BSONObj result;
            if (!_dbclient->runCommand(db.c_str(), b.done(), result)) {
                std::string errStr = result.getStringField("errmsg");
                if (errStr.empty())
                    errStr = "Failed to get error message.";

                throw std::runtime_error(errStr);
            }
        }
        else {
            throw std::runtime_error("Collection with same name already exists.");
        }
    }

    void MongoClient::renameCollection(const MongoNamespace &ns, const std::string &newCollectionName)
    {
        MongoNamespace from(ns);
        MongoNamespace to(ns.databaseName(), newCollectionName);

        // Building { renameCollection: <source-namespace>, to: <target-namespace> }
        mongo::BSONObjBuilder command; // { collStats: "db.collection", scale : 1 }
        command.append("renameCollection", from.toString());
        command.append("to", to.toString());

        mongo::BSONObj result;
        if (!_dbclient->runCommand("admin", command.obj(), result)) { // this command should be run against "admin" db
            std::string errStr = result.getStringField("errmsg");
            if (errStr.empty())
                errStr = "Failed to get error message.";

            throw std::runtime_error(errStr);
        }
    }

    void MongoClient::duplicateCollection(const MongoNamespace &ns, const std::string &newCollectionName)
    {
        MongoNamespace const newCollection(ns.databaseName(), newCollectionName);

        if (!_dbclient->exists(newCollection.toString())) {
            mongo::BSONObj result;
            // todo: Issue #1258 : Duplicate Collection should support advanced collection options.
            //       _dbclient->createCollection() should be called with properties of source collection
            //       not with default parameters as below.
            if (!_dbclient->createCollection(newCollection.toString(), 0, false, 0, &result)) {
                std::string errStr = result.getStringField("errmsg");
                if (errStr.empty())
                    errStr = "Failed to get error message.";

                throw std::runtime_error(errStr);
            }
        }
        else {
            throw std::runtime_error("Collection with same name already exists.");
        }

        std::unique_ptr<mongo::DBClientCursor> cursor {
			_dbclient->query(mongo::NamespaceString(ns.databaseName(), ns.collectionName()), mongo::Query()) 
		};

        // Cursor may be NULL, it means we have connectivity problem
        if (!cursor)
            throw std::runtime_error("Network error while attempting to run query");

        while (cursor->more()) {
            mongo::BSONObj bsonObj = cursor->next();
            insertDocument(bsonObj, newCollection);
        }
    }

    void MongoClient::copyCollectionToDiffServer(mongo::DBClientBase *const fromServ, const MongoNamespace &from, 
                                                 const MongoNamespace &to)
    {
        if (!_dbclient->exists(to.toString()))
            _dbclient->createCollection(to.toString());

        std::unique_ptr<mongo::DBClientCursor> cursor{fromServ->query(
            mongo::NamespaceString(from.databaseName(), from.collectionName()),
            mongo::Query()) 
		};

        // Cursor may be NULL, it means we have connectivity problem
        if (!cursor)
            throw std::runtime_error("Network error while attempting to run query");

        while (cursor->more()) {
            mongo::BSONObj bsonObj = cursor->next();
            insertDocument(bsonObj, to);
        }
    }

    void MongoClient::dropCollection(const MongoNamespace &ns)
    {
        if (_dbclient->exists(ns.toString())) {
            mongo::BSONObj info;
            if (!_dbclient->dropCollection(ns.toString(), mongo::WriteConcernOptions(), &info)) { 
                std::string errStr = info.toString();
                if (errStr.empty())
                    errStr = "Failed to get error message.";

                throw std::runtime_error(errStr);
            }
        }
        else {
            throw std::runtime_error("Collection does not exist.");
        }
    }

    void MongoClient::insertDocument(const mongo::BSONObj &obj, const MongoNamespace &ns)
    {
        runWriteCommand(_dbclient, ns.databaseName(),
            BSON("insert" << ns.collectionName() << "documents" << BSON_ARRAY(obj)
                 << "ordered" << true));
    }

    void MongoClient::saveDocument(const mongo::BSONObj &obj, const MongoNamespace &ns)
    {
        mongo::BSONElement id = obj.getField("_id");
        if (id.eoo()) {
            insertDocument(obj, ns);
            return;
        }

        mongo::BSONObjBuilder builder;
        builder.append(id);
        mongo::BSONObj bsonQuery = builder.obj();
        runWriteCommand(_dbclient, ns.databaseName(),
            BSON("update" << ns.collectionName()
                 << "updates" << BSON_ARRAY(BSON("q" << bsonQuery << "u" << obj
                                                   << "upsert" << true << "multi" << false))
                 << "ordered" << true));
    }

    void MongoClient::removeDocuments(const MongoNamespace &ns, mongo::Query query, bool justOne /*= true*/)
    {
        runWriteCommand(_dbclient, ns.databaseName(),
            BSON("delete" << ns.collectionName()
                 << "deletes" << BSON_ARRAY(BSON("q" << query.obj
                                                   << "limit" << (justOne ? 1 : 0)))
                 << "ordered" << true));
    }

    std::vector<MongoDocumentPtr> MongoClient::query(const MongoQueryInfo &info)
    {
        std::vector<MongoDocumentPtr> docs;
        if (info._limit == -1) // it means that we do not need to load any documents
            return docs;

        MongoNamespace const ns(info._info._ns);
        mongo::NamespaceString const cursorNs(ns.databaseName(), ns.collectionName());

        mongo::BSONObj filter = info._query;
        mongo::BSONObjBuilder options;
        if (info._skip < 0) throw std::invalid_argument("Query skip must not be negative");
        if (info._skip) options.append("skip", static_cast<long long>(info._skip));
        if (info._limit) options.append("limit", std::abs(static_cast<long long>(info._limit)));
        if (info._batchSize) options.append("batchSize", std::abs(static_cast<long long>(info._batchSize)));
        if (info._limit < 0 || info._batchSize < 0) options.append("singleBatch", true);
        if (!info._fields.isEmpty()) options.append("projection", info._fields);
        mongo::BSONObj readPreference;
        if (info._special) {
            filter = info._query.hasField("$query") ? info._query.getObjectField("$query") : info._query.getObjectField("query");
            const std::pair<const char*, const char*> modifiers[] = {
                {"$orderby", "sort"}, {"orderby", "sort"}, {"$hint", "hint"},
                {"$min", "min"}, {"$max", "max"}, {"$maxTimeMS", "maxTimeMS"},
                {"$comment", "comment"}, {"$returnKey", "returnKey"},
                {"$showDiskLoc", "showRecordId"}, {"readConcern", "readConcern"},
                {"collation", "collation"}
            };
            for (const auto& modifier : modifiers) {
                auto value = info._query[modifier.first];
                if (!value.eoo()) options.appendAs(value, modifier.second);
            }
            readPreference = info._query.getObjectField("$readPreference");
        }
        if ((info._options & 4) && readPreference.isEmpty()) readPreference = BSON("mode" << "secondaryPreferred");
        if (info._options & 2) options.append("tailable", true);
        if (info._options & 16) options.append("noCursorTimeout", true);
        if (info._options & 32) options.append("awaitData", true);
        if (info._options & 128) options.append("allowPartialResults", true);
        if (info._special && info._query.getBoolField("$explain")) {
            auto find = mongo::BSONObjBuilder().append("find", ns.collectionName())
                .append("filter", filter).appendElements(options.obj()).obj();
            docs.emplace_back(new MongoDocument(_dbclient->command(ns.databaseName(),
                BSON("explain" << find << "verbosity" << "allPlansExecution"))));
            return docs;
        }
        auto cursor = _dbclient->find(cursorNs, filter, options.obj(), readPreference);
        while (cursor->more()) docs.emplace_back(new MongoDocument(cursor->next()));

        return docs;
    }

    MongoCollectionInfo MongoClient::runCollStatsCommand(const std::string &ns)
    {
        MongoCollectionInfo info(ns);
        return info;

/*      // Commented for now, to speedup load of collection names
        MongoNamespace mongons(ns);

        mongo::BSONObjBuilder command; // { collStats: "db.collection", scale : 1 }
        command.append("collStats", mongons.collectionName());
        command.append("scale", 1);

        mongo::BSONObj result;
        _dbclient->runCommand(mongons.databaseName(), command.obj(), result);
        std::string isCV = result.toString();
        MongoCollectionInfo newInfo(result);
        return newInfo;
        */
    }

    std::vector<MongoCollectionInfo> MongoClient::runCollStatsCommand(const std::vector<std::string> &namespaces)
    {
        std::vector<MongoCollectionInfo> infos;
        for (auto const& ns : namespaces) {
            MongoCollectionInfo info = runCollStatsCommand(ns);
            if (info.ns().isValid()) 
                infos.push_back(info);
        }
        return infos;
    }

    void MongoClient::done()
    {
        // do nothing here, because we are not using ScopedDbConnection now
        //_scopedConnection->done();
    }

}
