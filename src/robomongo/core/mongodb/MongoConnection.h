#pragma once

// Application adapters around the supported MongoDB C driver. No server internals.
#include "robomongo/core/bson/Bson.h"
#include <mongoc/mongoc.h>
#include <QJsonObject>
#include <list>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace Robomongo { class ConnectionSettings; }
namespace mongo {
class Status {
public:
    Status(std::string reason = {}) : _reason(std::move(reason)) {}
    bool isOK() const { return _reason.empty(); }
    std::string reason() const { return _reason; }
    std::string toString() const { return _reason; }
private: std::string _reason;
};
template<class T> class StatusWith {
public:
    StatusWith(T value) : _value(std::move(value)) {}
    StatusWith(Status status) : _status(std::move(status)) {}
    bool isOK() const { return _status.isOK(); }
    const Status& getStatus() const { return _status; }
    const T& getValue() const { if (!isOK()) throw std::runtime_error(_status.reason()); return *_value; }
private: Status _status; std::optional<T> _value;
};
class HostAndPort {
public:
    HostAndPort() = default;
    HostAndPort(const std::string& address);
    HostAndPort(const char* address) : HostAndPort(std::string(address)) {}
    HostAndPort(StringData address) : HostAndPort(address.toString()) {}
    HostAndPort(std::string host, int port) : _host(std::move(host)), _port(port) {}
    const std::string& host() const { return _host; }
    int port() const { return _port; }
    bool empty() const { return _host.empty(); }
    std::string toString() const;
    bool operator<(const HostAndPort& other) const { return toString() < other.toString(); }
    bool operator==(const HostAndPort& other) const { return _host == other._host && _port == other._port; }
private: std::string _host; int _port = 27017;
};
inline std::ostream& operator<<(std::ostream& out, const HostAndPort& host) { return out << host.toString(); }
struct ConnectionString {
    enum class ConnectionType { MASTER, SET };
    explicit ConnectionString(HostAndPort host) : _host(std::move(host)) {}
    std::string toString() const { return _host.toString(); }
private: HostAndPort _host;
};
namespace transport { enum class ConnectSSLMode { kEnableSSL, kDisableSSL }; }
template<class T> class Optional : public std::optional<T> {
public:
    using std::optional<T>::optional;
    T get_value_or(const T& fallback) const { return this->value_or(fallback); }
};
class MongoURI {
public:
    static StatusWith<MongoURI> parse(const std::string& uri);
    ConnectionString::ConnectionType type() const;
    std::vector<HostAndPort> getServers() const;
    std::string getUser() const;
    std::string getPassword() const;
    std::string getDatabase() const;
    std::string getAuthenticationDatabase() const;
    std::string getSetName() const;
    Optional<std::string> getOption(const std::string& name) const;
    BSONObj getOptions() const;
    transport::ConnectSSLMode getSSLMode() const;
    std::string toString() const { return _text; }
private:
    std::shared_ptr<mongoc_uri_t> _uri;
    std::string _text;
};
class NamespaceString {
public:
    explicit NamespaceString(std::string ns) : _ns(std::move(ns)) {}
    NamespaceString(const std::string& db, const std::string& collection) : _ns(db + "." + collection) {}
    std::string ns() const { return _ns; }
    std::string db() const { return _ns.substr(0, _ns.find('.')); }
    std::string coll() const { auto p = _ns.find('.'); return p == std::string::npos ? "" : _ns.substr(p + 1); }
private: std::string _ns;
};
inline std::string nsToDatabase(const std::string& ns) { return NamespaceString(ns).db(); }
class Query {
public:
    Query() = default;
    Query(BSONObj object) : obj(std::move(object)) {}
    Query& sort(const std::string& field, int direction = 1) { sortFields = BSON(field << direction); return *this; }
    Query& sort(BSONObj fields) { sortFields = fields; return *this; }
    BSONObj obj;
    BSONObj sortFields;
};
struct WriteConcernOptions {};
class IndexSpec {
public:
    void name(const std::string& name) { _name = name; }
    void addKeys(BSONObj keys) { _keys = keys; }
    void addOptions(BSONObj options) { _options = options; }
    BSONObj toBSON() const { return BSONObjBuilder().append("name", _name).append("key", _keys).appendElements(_options).obj(); }
private: std::string _name; BSONObj _keys, _options;
};
class DBClientCursor {
public:
    explicit DBClientCursor(mongoc_cursor_t* cursor);
    ~DBClientCursor();
    bool more();
    BSONObj next();
    BSONObj nextSafe() { return next(); }
private:
    mongoc_cursor_t* _cursor;
    const bson_t* _next = nullptr;
    bool _ready = false;
};
class DBClientBase {
public:
    DBClientBase() = default; // also permits command-only test doubles
    explicit DBClientBase(const std::string& uri, const QJsonObject& tls = {});
    virtual ~DBClientBase();
    DBClientBase(const DBClientBase&) = delete;
    DBClientBase& operator=(const DBClientBase&) = delete;
    virtual bool runCommand(const std::string& db, const BSONObj& command, BSONObj& result, int options = 0);
    BSONObj command(const std::string& db, const BSONObj& command);
    void ping();
    std::list<std::string> getDatabaseNames();
    std::list<BSONObj> getCollectionInfos(const std::string& db);
    std::list<BSONObj> getIndexSpecs(const std::string& ns);
    bool exists(const std::string& ns);
    bool dropDatabase(const std::string& db, WriteConcernOptions = {}, BSONObj* result = nullptr);
    bool dropCollection(const std::string& ns, WriteConcernOptions = {}, BSONObj* result = nullptr);
    bool createCollection(const std::string& ns, long long size = 0, bool capped = false, int max = 0, BSONObj* result = nullptr);
    void dropIndex(const std::string& ns, const std::string& name);
    std::unique_ptr<DBClientCursor> query(const NamespaceString& ns, const Query& query);
    std::unique_ptr<DBClientCursor> find(const NamespaceString& ns, const BSONObj& filter, const BSONObj& options, const BSONObj& readPreference = {});
    BSONObj hello();
    std::string serverAddress() const;
    mongoc_client_t* raw() const { return _client; }
private:
    mongoc_client_t* _client = nullptr;
};
}
namespace Robomongo {
std::string makeConnectionUri(const ConnectionSettings& settings, double timeoutSec = 10);
QJsonObject makeTlsOptions(const ConnectionSettings& settings);
}
