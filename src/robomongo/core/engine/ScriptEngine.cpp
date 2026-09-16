#include "robomongo/core/engine/ScriptEngine.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <algorithm>
#include <stdexcept>
#include "robomongo/core/mongodb/MongoConnection.h"
#include "robomongo/core/settings/ConnectionSettings.h"

namespace {
class ShellRpcError : public std::runtime_error {
public:
    explicit ShellRpcError(const QJsonObject& error)
        : std::runtime_error(error.value("message").toString().toStdString()),
          timedOut(error.value("data").toObject().value("timedOut").toBool()) {}
    bool timedOut;
};
}
namespace Robomongo {
ScriptEngine::ScriptEngine(ConnectionSettings* connection, int timeoutSec)
    : _connection(connection), _timeoutSec(timeoutSec) {}
ScriptEngine::~ScriptEngine() { stopRuntime(); }

void ScriptEngine::startRuntime() {
    stopRuntime();
    QString runtimeDir = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../Resources/runtime");
    QString node = QDir(runtimeDir).absoluteFilePath("node");
    if (!QFileInfo::exists(node) || !QFileInfo::exists(QDir(runtimeDir).filePath("mongosh-bridge.js"))) {
        runtimeDir = QString::fromUtf8(ROBOMONGO_RUNTIME_DIR);
        node = QString::fromUtf8(ROBOMONGO_NODE_EXECUTABLE);
    }
    QString const bridge = QDir(runtimeDir).absoluteFilePath("mongosh-bridge.js");
    if (!QFileInfo::exists(node) || !QFileInfo::exists(bridge))
        throw std::runtime_error("The bundled mongosh runtime is missing. Rebuild or reinstall Robo 3T.");
    _process = std::make_unique<QProcess>();
    _process->setProcessChannelMode(QProcess::SeparateChannels);
    _process->setProgram(node);
    _process->setArguments({bridge});
    _process->start();
    if (!_process->waitForStarted(10000)) {
        _failedScope = true;
        throw std::runtime_error("Failed to start mongosh: " + _process->errorString().toStdString());
    }
    _readBuffer.clear();
    _diagnostics.clear();
    _interrupted = false;
}
void ScriptEngine::stopRuntime() {
    if (!_process) return;
    if (_process->state() != QProcess::NotRunning) {
        _process->closeWriteChannel();
        if (!_process->waitForFinished(1000)) {
            _process->kill();
            _process->waitForFinished(1000);
        }
    }
    _process.reset();
    _initialized = false;
}
QJsonObject ScriptEngine::exchange(const QString& method, const QJsonObject& params, int timeoutMs) {
    if (!_process || _process->state() == QProcess::NotRunning) {
        _failedScope = true;
        throw std::runtime_error("The mongosh process is not running. Reconnect to start a new shell.");
    }
    qint64 const id = ++_requestId;
    QJsonObject const request {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
    _process->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    if (_process->bytesToWrite() > 0 && !_process->waitForBytesWritten(5000))
        throw std::runtime_error("Failed to send a request to mongosh.");
    QElapsedTimer timer;
    timer.start();
    bool interruptSent = false;
    while (true) {
        if (_interrupted.exchange(false) && !interruptSent) {
            QJsonObject const interruptRequest {{"jsonrpc", "2.0"}, {"id", ++_requestId},
                {"method", "interrupt"}, {"params", QJsonObject()}};
            _process->write(QJsonDocument(interruptRequest).toJson(QJsonDocument::Compact) + '\n');
            interruptSent = true;
        }
        _readBuffer += _process->readAllStandardOutput();
        _diagnostics += _process->readAllStandardError();
        if (_diagnostics.size() > 65536) _diagnostics = _diagnostics.right(65536);
        qsizetype newline;
        while ((newline = _readBuffer.indexOf('\n')) >= 0) {
            QByteArray const line = _readBuffer.left(newline);
            _readBuffer.remove(0, newline + 1);
            QJsonParseError parseError;
            QJsonDocument const document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                throw std::runtime_error("Received an invalid JSON response from mongosh.");
            QJsonObject const response = document.object();
            if (response.value("id").toInteger() != id) continue;
            if (response.contains("error")) {
                QJsonObject const error = response.value("error").toObject();
                if (error.value("data").toObject().value("workerFailed").toBool()) _failedScope = true;
                throw ShellRpcError(error);
            }
            return response.value("result").toObject();
        }
        if (_process->state() == QProcess::NotRunning) {
            _failedScope = true;
            throw std::runtime_error("The mongosh process exited. " + _diagnostics.toStdString());
        }
        // The bridge interrupts at timeoutMs; this watchdog also handles a crashed bridge.
        if (timeoutMs > 0 && timer.elapsed() > timeoutMs + 15000) {
            _failedScope = true;
            stopRuntime();
            throw ShellRpcError(QJsonObject {{"message", "Shell timed out. Variables and cursors have been reset; reconnect to continue."},
                {"data", QJsonObject {{"timedOut", true}, {"contextReset", true}}}});
        }
        _process->waitForReadyRead(50);
    }
}
QJsonObject ScriptEngine::rpc(const QString& method, QJsonObject params, int timeoutMs) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (timeoutMs > 0) params.insert("timeoutMS", timeoutMs);
    QJsonObject const result = exchange(method, params, timeoutMs);
    if (result.contains("database")) _currentDatabase = result.value("database").toString().toStdString();
    if (result.contains("server")) _currentServer = result.value("server").toString().toStdString();
    return result;
}
void ScriptEngine::init(bool isLoadMongoRcJs, const std::string& serverAddr, const std::string& dbName) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    Q_UNUSED(serverAddr);
    try {
        startRuntime();
        QJsonObject const params {{"uri", QString::fromStdString(makeConnectionUri(*_connection))},
            {"options", makeTlsOptions(*_connection)}, {"batchSize", _batchSize},
            {"database", QString::fromStdString(dbName.empty() ? _connection->defaultDatabase() : dbName)}};
        rpc("connect", params, 30000);
        _initialized = true;
        _failedScope = false;
        QString const configuredProfile = qEnvironmentVariable("ROBOMONGO_PROFILE_DIR");
        QDir const profile(configuredProfile.isEmpty() ? QDir::homePath() : configuredProfile);
        if (isLoadMongoRcJs) {
            QString const modernRc = profile.filePath(".mongoshrc.js");
            loadStartupFile(QFileInfo::exists(modernRc) ? modernRc : profile.filePath(".mongorc.js"));
        }
        loadStartupFile(profile.filePath(".robomongorc.js"));
    } catch (...) { _failedScope = true; throw; }
}
void ScriptEngine::loadStartupFile(const QString& path) {
    QFile file(path);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Failed to open shell startup script: " + path.toStdString());
    rpc("eval", QJsonObject {{"code", QString::fromUtf8(file.readAll())}, {"filename", path}},
        std::max(0, _timeoutSec.load()) * 1000);
}
std::vector<MongoDocumentPtr> ScriptEngine::parseDocuments(const QJsonObject& result) const {
    std::vector<MongoDocumentPtr> documents;
    QJsonArray const values = result.value("documents").toArray();
    documents.reserve(values.size());
    for (const QJsonValue& value : values)
        documents.push_back(MongoDocumentPtr(new MongoDocument(mongo::BSONObj::fromJson(value.toString().toStdString()))));
    return documents;
}
MongoShellResult ScriptEngine::parseResult(const QJsonObject& result) const {
    MongoQueryInfo info;
    QJsonObject const query = result.value("queryInfo").toObject();
    if (!query.isEmpty()) {
        info = MongoQueryInfo(CollectionInfo(_currentServer, query.value("database").toString().toStdString(),
            query.value("collection").toString().toStdString()),
            mongo::BSONObj::fromJson(query.value("query").toString("{}").toStdString()),
            mongo::BSONObj::fromJson(query.value("fields").toString("{}").toStdString()),
            query.value("limit").toInt(), query.value("skip").toInt(), query.value("batchSize").toInt(_batchSize),
            query.value("options").toInt(), query.value("special").toBool());
    }
    info.runtimeCursorId = result.value("cursorId").toString().toStdString();
    info.readOnly = query.value("readOnly").toBool();
    // The GUI's only custom renderer is collection statistics. Other mongosh
    // type names must not enable an empty custom view.
    std::string const type = result.value("type").toString() == "collectionStats" ? "collectionStats" : "";
    return MongoShellResult(type, result.value("output").toString().toStdString(),
        parseDocuments(result), info, result.value("statement").toString().toStdString(), result.value("elapsedMS").toInteger());
}
MongoShellExecResult ScriptEngine::exec(const std::string& script, const std::string& dbName, AggrInfo aggrInfo) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    Q_UNUSED(aggrInfo);
    if (!_initialized) return MongoShellExecResult(true, "The mongosh shell has not been initialized.");
    _interrupted = false;
    try {
        use(dbName);
        QJsonObject const response = rpc("eval", QJsonObject {{"code", QString::fromStdString(script)},
            {"batchSize", _batchSize}}, std::max(0, _timeoutSec.load()) * 1000);
        std::vector<MongoShellResult> results;
        for (const QJsonValue& value : response.value("results").toArray()) {
            QJsonObject const item = value.toObject();
            if (!item.value("documents").toArray().isEmpty() && !item.value("output").toString().isEmpty())
                results.emplace_back("", item.value("output").toString().toStdString(),
                    std::vector<MongoDocumentPtr>(), MongoQueryInfo(), item.value("statement").toString().toStdString(), 0);
            if (!item.value("documents").toArray().isEmpty() || !item.value("output").toString().isEmpty() || item.contains("cursorId"))
                results.push_back(parseResult(item));
        }
        return MongoShellExecResult(results, _currentServer, !_currentServer.empty(), _currentDatabase, !_currentDatabase.empty());
    } catch (const ShellRpcError& error) { return MongoShellExecResult(true, error.what(), error.timedOut); }
      catch (const std::exception& error) { return MongoShellExecResult(true, error.what()); }
}
std::vector<MongoDocumentPtr> ScriptEngine::queryPage(const MongoQueryInfo& info) {
    if (info._limit == -1) return {};
    return parseDocuments(rpc("cursorPage", QJsonObject {{"cursorId", QString::fromStdString(info.runtimeCursorId)},
        {"skip", info._skip}, {"batchSize", info._batchSize}}, std::max(0, _timeoutSec.load()) * 1000));
}
void ScriptEngine::interrupt() {
    // Called across threads: only the process-owning RPC loop may touch QProcess.
    _interrupted = true;
}
void ScriptEngine::use(const std::string& dbName) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_initialized && !dbName.empty() && dbName != _currentDatabase)
        rpc("use", QJsonObject {{"database", QString::fromStdString(dbName)}}, 10000);
}
void ScriptEngine::setBatchSize(int batchSize) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _batchSize = std::max(1, batchSize);
    if (_initialized) rpc("setBatchSize", QJsonObject {{"batchSize", _batchSize}}, 5000);
}
void ScriptEngine::ping() { if (_initialized) rpc("ping", QJsonObject(), 5000); }
QStringList ScriptEngine::complete(const std::string& prefix, AutocompletionMode mode) {
    if (mode == AutocompleteNone || !_initialized) return {};
    try {
        QJsonObject const response = rpc("autocomplete", QJsonObject {{"code", QString::fromStdString(prefix)},
            {"includeCollectionNames", mode == AutocompleteAll}, {"tokenOnly", true},
            {"functionCalls", true}, {"quoteKeys", true}}, 5000);
        QStringList completions;
        for (const QJsonValue& value : response.value("completions").toArray()) {
            const QString token = value.toString();
            if (!token.isEmpty() && token.size() <= 512)
                completions.append(token);
            if (completions.size() >= 200)
                break;
        }
        return completions;
    } catch (const std::exception&) { return {}; }
}
void ScriptEngine::invalidateDbCollectionsCache() {
    if (_initialized) rpc("invalidateAutocomplete", QJsonObject(), 5000);
}
}
