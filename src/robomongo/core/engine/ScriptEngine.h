#pragma once
#include <QObject>
#include <QJsonObject>
#include <QStringList>
#include <QByteArray>
#include <atomic>
#include <memory>
#include <mutex>
#include "robomongo/core/domain/MongoShellResult.h"
#include "robomongo/core/Enums.h"
class QProcess;
namespace Robomongo {
class ConnectionSettings;
class ScriptEngine : public QObject {
    Q_OBJECT
public:
    ScriptEngine(ConnectionSettings* connection, int timeoutSec);
    ~ScriptEngine();
    void init(bool isLoadMongoJs, const std::string& serverAddr = "", const std::string& dbName = "");
    MongoShellExecResult exec(const std::string& script, const std::string& dbName = std::string(), AggrInfo aggrInfo = AggrInfo());
    std::vector<MongoDocumentPtr> queryPage(const MongoQueryInfo& info);
    void interrupt();
    void use(const std::string& dbName);
    void setBatchSize(int batchSize);
    void ping();
    QStringList complete(const std::string& prefix, AutocompletionMode mode);
    void invalidateDbCollectionsCache();
    bool failedScope() const { return _failedScope; }
    void changeTimeout(int newTimeout) { _timeoutSec = newTimeout; }
private:
    void startRuntime();
    void stopRuntime();
    QJsonObject rpc(const QString& method, QJsonObject params = {}, int timeoutMs = 0);
    QJsonObject exchange(const QString& method, const QJsonObject& params, int timeoutMs);
    std::vector<MongoDocumentPtr> parseDocuments(const QJsonObject& result) const;
    MongoShellResult parseResult(const QJsonObject& result) const;
    void loadStartupFile(const QString& path);
    ConnectionSettings* _connection;
    std::unique_ptr<QProcess> _process;
    QByteArray _readBuffer;
    QByteArray _diagnostics;
    std::recursive_mutex _mutex;
    std::atomic<bool> _interrupted {false};
    std::atomic<int> _timeoutSec;
    std::atomic<bool> _failedScope {false};
    bool _initialized = false;
    int _batchSize = 50;
    qint64 _requestId = 0;
    std::string _currentDatabase;
    std::string _currentServer;
};
}
