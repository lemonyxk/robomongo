#include "robomongo/core/mongodb/MongoWorker.h"

#include <algorithm>
#include <cstdlib>
#include <exception>

#include <QThread>
#include <QTimerEvent>
#include <cstring>
#include <map>


#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/domain/App.h"
#include "robomongo/core/domain/MongoShellResult.h"
#include "robomongo/core/domain/MongoCollectionInfo.h"
#include "robomongo/core/events/MongoEvents.h"
#include "robomongo/core/engine/ScriptEngine.h"
#include "robomongo/core/EventBus.h"
#include "robomongo/core/mongodb/MongoClient.h"
#include "robomongo/core/settings/ConnectionSettings.h"
#include "robomongo/core/settings/ReplicaSetSettings.h"
#include "robomongo/core/settings/CredentialSettings.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/settings/SslSettings.h"
#include "robomongo/core/utils/BsonUtils.h"
#include "robomongo/core/utils/Logger.h"
#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/utils/StringOperations.h"

namespace Robomongo
{
    MongoWorker::MongoWorker(ConnectionSettings *connection, bool isLoadMongoRcJs, int batchSize,
                             double mongoTimeoutSec, int shellTimeoutSec, QObject *parent) 
        : QObject(parent),
        _scriptEngine(nullptr),
        _isLoadMongoRcJs(isLoadMongoRcJs),
        _batchSize(batchSize),
        _timerId(-1),
        _dbAutocompleteCacheTimerId(-1),
        _mongoTimeoutSec(mongoTimeoutSec),
        _shellTimeoutSec(shellTimeoutSec),
        _isQuiting(0),
        _dbclient(nullptr),
        _connSettings(connection)
    {
        // Whitespace removed from the start and the end of host string
        _connSettings->setServerHost(QString::fromStdString(_connSettings->serverHost()).trimmed().toStdString());
        _thread = new QThread();
        moveToThread(_thread);
        VERIFY(connect( _thread, SIGNAL(finished()), _thread, SLOT(deleteLater()) ));
        VERIFY(connect( _thread, SIGNAL(finished()), this, SLOT(deleteLater()) ));
        _thread->start();
    }

    void MongoWorker::timerEvent(QTimerEvent *event)
    {
        if (_timerId == event->timerId()) {
            keepAlive();
            return;
        }

        if (_dbAutocompleteCacheTimerId == event->timerId() && _scriptEngine) {
            _scriptEngine->invalidateDbCollectionsCache();
            return;
        }
    }

    void MongoWorker::keepAlive()
    {
        try {
            if (_dbclient)
                pingDatabase(_dbclient.get());

            if (_scriptEngine)
                _scriptEngine->ping();

        } 
        catch(std::exception &ex) {
            sendLog(this, LogEvent::RBM_WARN, 
                "Failed to ping the server. " + std::string(ex.what()));
        }
    }

    void MongoWorker::init()
    {        
        try {
            if (_scriptEngine)
                return;
            _scriptEngine.reset(new ScriptEngine(_connSettings, _shellTimeoutSec));
            _scriptEngine->init(_isLoadMongoRcJs);
            _scriptEngine->use(_connSettings->defaultDatabase());
            _scriptEngine->setBatchSize(_batchSize);
            constexpr int PING_INTERVAL_MSEC { 60 * 1000 };  // 60 seconds
            _timerId = startTimer(PING_INTERVAL_MSEC);
            _dbAutocompleteCacheTimerId = startTimer(30000);
        } catch (const std::exception &ex) {
            _scriptEngine.reset();
            auto const msg { "Failed to initialize MongoWorker. Reason: "};
            sendLog(this, LogEvent::RBM_ERROR, msg + std::string(ex.what()));
            throw std::runtime_error(msg + std::string(ex.what()));
        }
    }

    void MongoWorker::interrupt() {
        try {
            if (_isQuiting || !_scriptEngine)
                return;

            _scriptEngine->interrupt();
        } catch(const std::exception &ex) {
            sendLog(this, LogEvent::RBM_ERROR, std::string(ex.what()));
        }
    }

    MongoWorker::~MongoWorker()
    {
        if (_timerId != -1)
            killTimer(_timerId);

        if (_dbAutocompleteCacheTimerId != -1)
            killTimer(_dbAutocompleteCacheTimerId);

        delete _connSettings;

        // QThread "_thread" and MongoWorker itself will be deleted later
        // (see MongoWorker() constructor)
    }

    void MongoWorker::stopAndDelete()
    {
        _isQuiting = 1;
        _thread->quit();
    }

    void MongoWorker::changeTimeout(int newTimeout)
    {
        _shellTimeoutSec = newTimeout;
        if (_scriptEngine)
            _scriptEngine->changeTimeout(newTimeout);
    }

    /**
     * @brief Initiate connection to MongoDB
     */
    bool MongoWorker::handle(EstablishConnectionRequest *event)
    {
        QMutexLocker lock(&_firstConnectionMutex);
        auto replicaSet = std::make_unique<ReplicaSet>();

        try {
            auto *connection = getConnection().first;
            connection->ping();
            if (_connSettings->isReplicaSet())
                replicaSet = std::make_unique<ReplicaSet>(getReplicaSetInfo());

            // Collection tabs reuse the explorer's descriptive metadata instead
            // of repeating listDatabases, buildInfo and serverStatus before find.
            // Their connection and replica-set checks above are still fresh.
            const auto info = [&]() {
                if (event->connectionType == ConnectionSecondary && event->knownInfo)
                    return *event->knownInfo;
                std::unique_ptr<MongoClient> client(getClient());
                const auto databases = getDatabaseNamesSafe(event);
                const auto version = client->dbVersionStr();
                return ConnectionInfo(_connSettings->getFullAddress(), databases,
                    static_cast<float>(std::atof(version.c_str())), version,
                    client->getStorageEngineType(), event->uuid);
            }();
            init();
            reply(event->sender(), new EstablishConnectionResponse(this, info,
                event->connectionType, *replicaSet));
            return true;
        } catch (const std::exception &ex) {
            if (_connSettings->isReplicaSet())
                replicaSet = std::make_unique<ReplicaSet>(getReplicaSetInfo());
            const auto reason = _connSettings->sslSettings()->sslEnabled()
                ? EstablishConnectionResponse::MongoSslConnection
                : EstablishConnectionResponse::MongoConnection;
            reply(event->sender(), new EstablishConnectionResponse(this,
                EventError(ex.what()), event->connectionType, ConnectionInfo(event->uuid),
                *replicaSet, reason));
            sendLog(this, LogEvent::RBM_ERROR, ex.what());
            return false;
        }
    }

    void MongoWorker::handle(RefreshReplicaSetFolderRequest *event)
    {
        try {
            ReplicaSet const& replicaSetInfo = getReplicaSetInfo();
            // Primary is unreachable, but there might be reachable secondary(ies)
            if (replicaSetInfo.primary.empty()) {
                reply(
                    event->sender(), 
                    new RefreshReplicaSetFolderResponse(
                        this, replicaSetInfo, event->expanded, EventError(replicaSetInfo.errorStr)
                    )
                );
                sendLog(this, LogEvent::RBM_ERROR, replicaSetInfo.errorStr);
                return;
            }
            else { // Primary is reachable
                reply(event->sender(), 
                    new RefreshReplicaSetFolderResponse(this, replicaSetInfo, event->expanded));
            }
        }
        catch (const std::exception &ex) {
            reply(
                event->sender(),
                new RefreshReplicaSetFolderResponse(
                    this, ReplicaSet(), event->expanded, EventError(ex.what())
                )
            );
            sendLog(this, LogEvent::RBM_ERROR, ex.what());
        }
    }

    std::string MongoWorker::getAuthBase() const
    {
        if (_connSettings->hasEnabledPrimaryCredential())
            return _connSettings->primaryCredential()->databaseName();

        return std::string();
    }

    std::vector<std::string> MongoWorker::getDatabaseNamesSafe(EstablishConnectionRequest* event /*= nullptr*/)
    {
        std::set<std::string> dbNames;
        auto const primaryCredential { _connSettings->primaryCredential() };

        try {
            std::unique_ptr<MongoClient> client(getClient());
            std::vector<std::string> dbNamesFetched { client->getDatabaseNames() };
            dbNames = std::set<std::string> { dbNamesFetched.cbegin(), dbNamesFetched.cend() };
        } 
        catch(const std::exception &ex) {
            const bool informUser = event && event->connectionType == ConnectionPrimary &&
                (!primaryCredential || !primaryCredential->useManuallyVisibleDbs() ||
                 primaryCredential->manuallyVisibleDbs().empty());
            std::string const hint {
                "\n\nHint: If this user has access to a specific database, "
                "please use \"Manually specify visible databases\" option in "
                "Connection Settings window -> Authentication tab."
            };
            sendLog(this, LogEvent::RBM_WARN, ex.what() + hint, informUser);
        }

        if (_connSettings->credentialCount() > 0 &&
            primaryCredential->useManuallyVisibleDbs() &&
            !primaryCredential->manuallyVisibleDbs().empty()
        )
        {        
            QString const manuallyVisibleDbs {
                QString::fromStdString(primaryCredential->manuallyVisibleDbs())
            };

            for (const auto &db : manuallyVisibleDbs.split(',', Qt::SkipEmptyParts)) {
                const auto name = db.trimmed();
                if (!name.isEmpty())
                    dbNames.insert(name.toStdString());
            }
        }

        std::string const authBase = getAuthBase();
        if (!authBase.empty())
            dbNames.insert(authBase);

        return std::vector<std::string> { dbNames.cbegin(), dbNames.cend() };
    }

    /**
     * @brief Load list of all database names
     */
    void MongoWorker::handle(LoadDatabaseNamesRequest *event)
    {
        try {
            // If user not an admin - he doesn't have access to mongodb 'listDatabases' command
            // Non admin user has access only to the single database he specified while performing auth.
            std::vector<std::string> dbNames = getDatabaseNamesSafe();

            // Remove from list of created databases existing databases
            for (std::vector<std::string>::iterator it = dbNames.begin(); it != dbNames.end(); ++it) {
                std::unordered_set<std::string>::const_iterator exists = _createdDbs.find(*it);
                if (exists != _createdDbs.end()) {
                    _createdDbs.erase(*it);
                }
            }

            // Merge with list of created databases
            for (std::unordered_set<std::string>::iterator it = _createdDbs.begin(); it != _createdDbs.end(); ++it) {
                dbNames.push_back(*it);
            }

            reply(event->sender(), new LoadDatabaseNamesResponse(this, dbNames));
        } catch(const std::exception &ex) {
            reply(event->sender(), new LoadDatabaseNamesResponse(this, EventError(ex.what())));
            sendLog(this, LogEvent::RBM_ERROR, ex.what());
        }
    }

    /**
     * @brief Load list of all collection names
     */
    void MongoWorker::handle(LoadCollectionNamesRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            auto const& namespaces = client->getCollectionNamesWithDbname(event->databaseName());
            std::vector<MongoCollectionInfo> const& collInfos = client->runCollStatsCommand(namespaces);
            client->done();
            reply(event->sender(), new LoadCollectionNamesResponse(this, event->databaseName(), collInfos));
        } catch(const std::exception &ex) {
            reply(event->sender(), new LoadCollectionNamesResponse(this, EventError(ex.what())));
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(LoadUsersRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            const std::vector<MongoUser> &users = client->getUsers(event->databaseName());
            client->done();

            reply(event->sender(), new LoadUsersResponse(this, event->databaseName(), users));
        } catch(const std::exception &ex) {
            reply(event->sender(), new LoadUsersResponse(this, EventError(ex.what())));
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(LoadCollectionIndexesRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            const std::vector<IndexInfo> &ind = client->getIndexes(event->collection());
            client->done();

            reply(event->sender(), new LoadCollectionIndexesResponse(this, ind));
        } catch(const std::exception &ex) {
            reply(event->sender(), new LoadCollectionIndexesResponse(this, EventError(ex.what())));
            sendLog(this, LogEvent::RBM_ERROR, ex.what());
        }
    }

    void MongoWorker::handle(AddEditIndexRequest *event)
    {
        const IndexInfo &newIndex = event->newInfo();
        const IndexInfo &oldIndex = event->oldInfo();
        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->addEditIndex(oldIndex, newIndex);
            client->done();
            reply(event->sender(), new AddEditIndexResponse(this, oldIndex, newIndex));

            std::vector<IndexInfo> const &indexes = client->getIndexes(newIndex._collection);
            reply(event->sender(), new LoadCollectionIndexesResponse(this, indexes));
        } catch(const std::exception &ex) {
            reply(event->sender(), 
                new AddEditIndexResponse(this, EventError(ex.what()), oldIndex, newIndex)
            );
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(DropCollectionIndexRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->dropIndexFromCollection(event->collection(), event->index());
            client->done();
            reply(event->sender(), 
                new DropCollectionIndexResponse(this, event->collection(), event->index()));
        } catch(const std::exception &ex) {
            reply(event->sender(), 
                new DropCollectionIndexResponse(this, EventError(ex.what()), event->index()));
            // Logging handled in main thread
        }            
    }

    void MongoWorker::handle(LoadFunctionsRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            const std::vector<MongoFunction> &funcs = client->getFunctions(event->databaseName());
            client->done();

            reply(event->sender(), new LoadFunctionsResponse(this, event->databaseName(), funcs));
        } catch(const std::exception &ex) {
            reply(event->sender(), new LoadFunctionsResponse(this, EventError(ex.what())));
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(InsertDocumentRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
    
            if (event->overwrite())
                client->saveDocument(event->obj(), event->ns());
            else
                client->insertDocument(event->obj(), event->ns());

            client->done();
            reply(event->sender(), new InsertDocumentResponse(this));
        } 
        catch(const std::exception &ex) {
            reply(event->sender(), new InsertDocumentResponse(this, EventError(ex.what())));
            sendLog(this, LogEvent::RBM_ERROR, ex.what());
        }
    }

    void MongoWorker::handle(RemoveDocumentRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());

            client->removeDocuments(event->ns(), event->query(), 
                                    event->removeCount() == RemoveDocumentCount::ONE);
            client->done();

            reply(event->sender(), new RemoveDocumentResponse(this, event->removeCount(), event->index()));
        } 
        catch(const std::exception &ex) {
            reply(event->sender(), new RemoveDocumentResponse(this, EventError(ex.what()), 
                event->removeCount(), event->index()));
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(ExecuteQueryRequest *event)
    {
        try {
            const auto &info = event->queryInfo();
            std::vector<MongoDocumentPtr> docs;
            if (!info.runtimeCursorId.empty()) {
                if (!_scriptEngine)
                    throw std::runtime_error("MongoDB Shell was not initialized");
                docs = _scriptEngine->queryPage(info);
            } else {
                std::unique_ptr<MongoClient> client(getClient());
                docs = client->query(info);
                client->done();
            }
            reply(event->sender(),
                new ExecuteQueryResponse(this, event->resultIndex(), event->queryInfo(), docs)
            );
        } catch(const std::exception &ex) {
            reply(event->sender(), new ExecuteQueryResponse(this, EventError(ex.what())));
            sendLog(this, LogEvent::RBM_ERROR, std::string(ex.what()));
        }
    }

    /**
     * @brief Execute javascript
     */
    void MongoWorker::handle(ExecuteScriptRequest *event)
    {
        try {
            if (!_scriptEngine || !_dbclient)
                throw std::runtime_error("MongoDB Shell was not initialized or connection failed");
            if (_scriptEngine->failedScope()) {
                _scriptEngine->init(_isLoadMongoRcJs);
                _scriptEngine->setBatchSize(_batchSize);
            }
            const auto result = _scriptEngine->exec(event->script,
                event->databaseName, event->aggrInfo);
            if (result.error()) {
                reply(event->sender(), new ExecuteScriptResponse(this,
                    EventError(result.errorMessage()), result.timeoutReached()));
            } else {
                reply(event->sender(), new ExecuteScriptResponse(this, result,
                    event->script.empty(), result.timeoutReached()));
            }
        } catch (const std::exception &ex) {
            reply(event->sender(), new ExecuteScriptResponse(this, EventError(ex.what())));
            sendLog(this, LogEvent::RBM_ERROR, ex.what());
        }
    }

    /**
     * @brief Interrupt javascript execution
     */
    void MongoWorker::handle(StopScriptRequest *)
    {
        try {
            if (!_scriptEngine) {
                return;
            }

            _scriptEngine->interrupt();
        } catch(const std::exception &ex) {
            sendLog(this, LogEvent::RBM_ERROR, std::string(ex.what()));
        }
    }

    void MongoWorker::handle(AutocompleteRequest *event)
    {
        try {
            if (!_scriptEngine) {
                reply(event->sender(), 
                    new AutocompleteResponse(this, EventError("MongoDB Shell was not initialized")));
                return;
            }

            QStringList list = _scriptEngine->complete(event->prefix, event->mode);
            reply(event->sender(), new AutocompleteResponse(this, list, event->prefix));
        } catch(const std::exception &ex) {
            reply(event->sender(), new AutocompleteResponse(this, EventError(ex.what())));
            sendLog(this, LogEvent::RBM_ERROR, std::string(ex.what()));            
        }
    }

    void MongoWorker::handle(CreateDatabaseRequest *event)
    {
        std::string dbname = event->database();
        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->createDatabase(dbname);

            // Insert to list of created database. Read docs for this hashset in the header
            _createdDbs.insert(dbname);

            reply(event->sender(), new CreateDatabaseResponse(this, dbname));
        } catch(const std::exception &ex) {
            reply(event->sender(), new CreateDatabaseResponse(this, dbname, 
                EventError(ex.what()))
            );
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(DropDatabaseRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->dropDatabase(event->database);

            // Remove from the list of created database, Read docs for this hashset in the header
            _createdDbs.erase(event->database);

            reply(event->sender(), new DropDatabaseResponse(this, event->database));
        } 
        catch(const std::exception &ex) {
            reply(event->sender(), 
                new DropDatabaseResponse(this, event->database, EventError(ex.what()))
            );
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(CreateCollectionRequest *event)
    {
        std::string const& collection = event->ns().collectionName();

        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->createCollection(event->ns().toString(), event->getSize(), event->getCapped(),
                event->getMaxDocNum(), event->getExtraOptions());
            client->done();

            reply(event->sender(), new CreateCollectionResponse(this, collection));
        } catch(const std::exception &ex) {
            reply(event->sender(), 
                new CreateCollectionResponse(this, collection, EventError(ex.what()))
            );
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(DropCollectionRequest *event)
    {
        std::string const& collection = event->ns().collectionName();

        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->dropCollection(event->ns());
            client->done();

            reply(event->sender(), new DropCollectionResponse(this, collection));
        } catch(const std::exception &ex) {
            reply(event->sender(), 
                new DropCollectionResponse(this, collection, EventError(ex.what()))
            );
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(RenameCollectionRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->renameCollection(event->ns(), event->newCollection());
            client->done();

            reply(event->sender(), new RenameCollectionResponse(this, event->ns().collectionName(),
                                                                event->newCollection()));
        } catch(const std::exception &ex) {
            reply(event->sender(), new RenameCollectionResponse(this, EventError(ex.what())));
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(DuplicateCollectionRequest *event)
    {
        std::string const& sourceCollection = event->ns().collectionName();

        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->duplicateCollection(event->ns(), event->newCollection());
            client->done();

            reply(event->sender(), 
                new DuplicateCollectionResponse(this, sourceCollection, event->newCollection())
            );
        }
        catch (const std::exception &ex) {
            reply(event->sender(), 
                new DuplicateCollectionResponse(this, sourceCollection, EventError(ex.what()))
            );
            // Logging handled in main thread
        }
    }
    
    void MongoWorker::handle(CopyCollectionToDiffServerRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            MongoWorker *cl = event->worker();
            // A mongoc client belongs to one worker thread. Open a local source
            // client instead of sharing the other worker's active connection.
            mongo::DBClientBase source(makeConnectionUri(*cl->_connSettings, cl->_mongoTimeoutSec),
                                       makeTlsOptions(*cl->_connSettings));
            client->copyCollectionToDiffServer(&source, event->from(), event->to());
            client->done();

            reply(event->sender(), new CopyCollectionToDiffServerResponse(this));
        } catch(const std::exception &ex) {
            reply(event->sender(), 
                new CopyCollectionToDiffServerResponse(this, EventError(ex.what()))
            );
            sendLog(this, LogEvent::RBM_ERROR, std::string(ex.what()));
        }
    }

    void MongoWorker::handle(CreateUserRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->createUser(event->database(), event->user());
            client->done();

            reply(event->sender(), new CreateUserResponse(this, event->user().name()));
        } catch(const std::exception &ex) {
            reply(event->sender(), 
                new CreateUserResponse(this, event->user().name(), EventError(ex.what()))
            );
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(DropUserRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->dropUser(event->database(), event->username());
            client->done();

            reply(event->sender(), new DropUserResponse(this, event->username()));
        } catch(const std::exception &ex) {
            reply(event->sender(), 
                new DropUserResponse(this, event->username(), EventError(ex.what()))
            );
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(CreateFunctionRequest *event)
    {
        std::string const& functionName = event->function().name();

        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->createFunction(event->database(), event->function(), event->existingFunctionName());
            client->done();

            reply(event->sender(), new CreateFunctionResponse(this, functionName));
        } catch(const std::exception &ex) {
            reply(event->sender(), 
                new CreateFunctionResponse(this, functionName, EventError(ex.what()))
            );
            // Logging handled in main thread
        }
    }

    void MongoWorker::handle(DropFunctionRequest *event)
    {
        try {
            std::unique_ptr<MongoClient> client(getClient());
            client->dropFunction(event->database(), event->functionName());
            client->done();

            reply(event->sender(), new DropFunctionResponse(this, event->functionName()));
        }
        catch (const std::exception &ex) {
            reply(event->sender(), 
                new DropFunctionResponse(this, event->functionName(), EventError(ex.what()))
            );
            // Logging handled in main thread
        }
    }

    std::pair<mongo::DBClientBase*, std::string> MongoWorker::getConnection(bool mayReturnNull)
    {
        try {
            if (!_dbclient) {
                _dbclient = std::make_unique<mongo::DBClientBase>(
                    makeConnectionUri(*_connSettings, _mongoTimeoutSec), makeTlsOptions(*_connSettings));
                _dbclient->ping();
            }
            return {_dbclient.get(), {}};
        } catch (const std::exception &error) {
            if (mayReturnNull)
                return {nullptr, error.what()};
            throw;
        }
    }

    MongoClient *MongoWorker::getClient()
    {
        return new MongoClient(getConnection().first);
    }

    ReplicaSet MongoWorker::getReplicaSetInfo() const
    {
        std::string setName = _connSettings->replicaSetSettings()->setNameUserEntered();
        if (setName.empty())
            setName = _connSettings->replicaSetSettings()->cachedSetName();
        mongo::HostAndPort primary;
        std::map<std::string, bool> members;
        std::string error;
        for (const auto &member : _connSettings->replicaSetSettings()->members())
            members[member] = false;

        if (_dbclient) {
            // hello and the driver's server descriptions include discovered nodes,
            // even when the primary was not present in the original seed list.
            try {
                const auto hello = _dbclient->hello();
                if (hello.hasField("setName"))
                    setName = hello.getStringField("setName");
                for (const auto &host : hello["hosts"].Array())
                    members.emplace(host.String(), false);
            } catch (const std::exception &ex) {
                error = ex.what();
            }

            size_t count = 0;
            auto **descriptions = mongoc_client_get_server_descriptions(_dbclient->raw(), &count);
            for (size_t i = 0; i < count; ++i) {
                const auto *description = descriptions[i];
                const auto *host = mongoc_server_description_host(description);
                const std::string type = mongoc_server_description_type(description);
                const mongo::HostAndPort address(host->host, host->port);
                members[address.toString()] = type != "Unknown";
                if (type == "RSPrimary")
                    primary = address;
                const bson_t *hello = mongoc_server_description_hello_response(description);
                bson_iter_t field;
                if (hello && bson_iter_init_find(&field, hello, "setName") && BSON_ITER_HOLDS_UTF8(&field))
                    setName = bson_iter_utf8(&field, nullptr);
            }
            mongoc_server_descriptions_destroy_all(descriptions, count);
        }
        if (primary.empty() && error.empty())
            error = "No primary is currently reachable.";
        return ReplicaSet(setName, primary,
            std::vector<std::pair<std::string, bool>>(members.begin(), members.end()), error);
    }

    /**
     * @brief Send event to this MongoWorker
     */
    void MongoWorker::send(Event *event)
    {
        if (_isQuiting)
            return;

        AppRegistry::instance().bus()->send(this, event);
    }

    /**
     * @brief Send reply event to object 'receiver'
     */
    void MongoWorker::reply(QObject *receiver, Event *event)
    {
        if (_isQuiting)
            return;

        AppRegistry::instance().bus()->send(receiver, event);
    }

    void MongoWorker::pingDatabase(mongo::DBClientBase *dbclient) const
    {
        dbclient->ping();
    }
}
