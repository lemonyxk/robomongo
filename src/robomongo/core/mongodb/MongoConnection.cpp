#include "MongoConnection.h"
#include "robomongo/core/settings/ConnectionSettings.h"
#include "robomongo/core/settings/CredentialSettings.h"
#include "robomongo/core/settings/ReplicaSetSettings.h"
#include "robomongo/core/settings/SslSettings.h"
#include <QUrl>
#include <QHostAddress>
#include <algorithm>
#include <mutex>

namespace {
void initializeDriver() { static std::once_flag once; std::call_once(once, [] { mongoc_init(); }); }
std::string stringOrEmpty(const char* value) { return value ? value : ""; }
// libmongoc accepts some malformed bracketed hosts as DNS names. Reject them
// before saving a connection or attempting server selection.
std::string authorityError(const std::string& text) {
    auto scheme = text.find("://");
    if (scheme == std::string::npos) return {};
    auto begin = scheme + 3;
    auto end = text.find_first_of("/?#", begin);
    auto authority = text.substr(begin, end == std::string::npos ? end : end - begin);
    auto user = authority.rfind('@');
    if (user != std::string::npos) authority.erase(0, user + 1);
    std::size_t offset = 0;
    while (offset <= authority.size()) {
        auto separator = authority.find(',', offset);
        auto host = authority.substr(offset, separator == std::string::npos ? separator : separator - offset);
        if (host.find_first_of("[]") != std::string::npos) {
            auto close = host.find(']');
            if (host.empty() || host.front() != '[' || close == std::string::npos ||
                host.find('[', 1) != std::string::npos || host.find(']', close + 1) != std::string::npos ||
                (close + 1 < host.size() && host[close + 1] != ':'))
                return "Invalid brackets in MongoDB host";
            QHostAddress address(QUrl::fromPercentEncoding(QByteArray::fromStdString(host.substr(1, close - 1))));
            if (address.protocol() != QAbstractSocket::IPv6Protocol)
                return "A bracketed MongoDB host must be a valid IPv6 address";
        } else if (std::count(host.begin(), host.end(), ':') > 1) {
            return "IPv6 addresses in a MongoDB URI must be enclosed in brackets";
        }
        if (separator == std::string::npos) break;
        offset = separator + 1;
    }
    return {};
}
mongo::BSONObj copyBson(const bson_t* value) {
    if (!value) return {};
    return mongo::BSONObj(value);
}
std::list<mongo::BSONObj> collect(mongoc_cursor_t* cursor) {
    mongo::DBClientCursor owned(cursor);
    std::list<mongo::BSONObj> documents;
    while (owned.more()) documents.push_back(owned.next());
    return documents;
}
}
namespace mongo {
HostAndPort::HostAndPort(const std::string& address) {
    if (address.empty()) return;
    initializeDriver();
    if (auto reason = authorityError("mongodb://" + address); !reason.empty())
        throw std::invalid_argument(reason);
    bson_error_t error{};
    std::unique_ptr<mongoc_uri_t, decltype(&mongoc_uri_destroy)> uri(mongoc_uri_new_with_error(("mongodb://" + address).c_str(), &error), mongoc_uri_destroy);
    if (!uri) throw std::runtime_error(error.message);
    auto host = mongoc_uri_get_hosts(uri.get());
    if (!host || host->next) throw std::runtime_error("Expected one MongoDB host");
    _host = host->host; _port = host->port;
}
std::string HostAndPort::toString() const {
    if (_host.empty()) return {};
    return (_host.find(':') == std::string::npos ? _host : "[" + _host + "]") + ":" + std::to_string(_port);
}
StatusWith<MongoURI> MongoURI::parse(const std::string& text) {
    if (auto reason = authorityError(text); !reason.empty()) return Status(reason);
    initializeDriver(); bson_error_t error{};
    MongoURI result;
    result._uri = {mongoc_uri_new_with_error(text.c_str(), &error), mongoc_uri_destroy};
    if (!result._uri) return Status(error.message);
    result._text = text;
    return result;
}
ConnectionString::ConnectionType MongoURI::type() const { return !getSetName().empty() || getServers().size() > 1 ? ConnectionString::ConnectionType::SET : ConnectionString::ConnectionType::MASTER; }
std::vector<HostAndPort> MongoURI::getServers() const {
    std::vector<HostAndPort> hosts;
    for (auto host = mongoc_uri_get_hosts(_uri.get()); host; host = host->next) hosts.emplace_back(host->host, host->port);
    if (hosts.empty()) { auto srv = mongoc_uri_get_srv_hostname(_uri.get()); if (srv) hosts.emplace_back(srv, 27017); }
    return hosts;
}
std::string MongoURI::getUser() const { return stringOrEmpty(mongoc_uri_get_username(_uri.get())); }
std::string MongoURI::getPassword() const { return stringOrEmpty(mongoc_uri_get_password(_uri.get())); }
std::string MongoURI::getDatabase() const { return stringOrEmpty(mongoc_uri_get_database(_uri.get())); }
std::string MongoURI::getAuthenticationDatabase() const { return stringOrEmpty(mongoc_uri_get_auth_source(_uri.get())); }
std::string MongoURI::getSetName() const { return stringOrEmpty(mongoc_uri_get_replica_set(_uri.get())); }
Optional<std::string> MongoURI::getOption(const std::string& name) const {
    if (name == "authMechanism") { const char* p = mongoc_uri_get_auth_mechanism(_uri.get()); if (p) return std::string(p); return {}; }
    bson_iter_t it;
    if (!bson_iter_init_find_case(&it, mongoc_uri_get_options(_uri.get()), name.c_str())) return {};
    if (BSON_ITER_HOLDS_UTF8(&it)) return std::string(bson_iter_utf8(&it, nullptr));
    if (BSON_ITER_HOLDS_BOOL(&it)) return std::string(bson_iter_bool(&it) ? "true" : "false");
    if (BSON_ITER_HOLDS_INT32(&it)) return std::to_string(bson_iter_int32(&it));
    return {};
}
BSONObj MongoURI::getOptions() const { return copyBson(mongoc_uri_get_options(_uri.get())); }
transport::ConnectSSLMode MongoURI::getSSLMode() const { return mongoc_uri_get_tls(_uri.get()) ? transport::ConnectSSLMode::kEnableSSL : transport::ConnectSSLMode::kDisableSSL; }
DBClientCursor::DBClientCursor(mongoc_cursor_t* cursor) : _cursor(cursor) { if (!cursor) throw std::runtime_error("Unable to create MongoDB cursor"); }
DBClientCursor::~DBClientCursor() { mongoc_cursor_destroy(_cursor); }
bool DBClientCursor::more() {
    if (_ready) return true;
    _ready = mongoc_cursor_next(_cursor, &_next);
    if (!_ready) { bson_error_t error{}; if (mongoc_cursor_error(_cursor, &error)) throw std::runtime_error(error.message); }
    return _ready;
}
BSONObj DBClientCursor::next() { if (!more()) throw std::runtime_error("Cursor exhausted"); auto result = copyBson(_next); _ready = false; return result; }
DBClientBase::DBClientBase(const std::string& uriText, const QJsonObject& tls) {
    if (auto reason = authorityError(uriText); !reason.empty()) throw std::invalid_argument(reason);
    initializeDriver(); bson_error_t error{};
    std::unique_ptr<mongoc_uri_t, decltype(&mongoc_uri_destroy)> uri(mongoc_uri_new_with_error(uriText.c_str(), &error), mongoc_uri_destroy);
    if (!uri) throw std::runtime_error(error.message);
    _client = mongoc_client_new_from_uri(uri.get());
    if (!_client) throw std::runtime_error("Unable to create MongoDB client");
    mongoc_client_set_error_api(_client, 2);
    if (!mongoc_uri_get_appname(uri.get())) mongoc_client_set_appname(_client, "Robo 3T");
    if (!tls.isEmpty()) {
        const auto pem = tls["tlsCertificateKeyFile"].toString().toUtf8();
        const auto password = tls["tlsCertificateKeyFilePassword"].toString().toUtf8();
        const auto ca = tls["tlsCAFile"].toString().toUtf8();
        const auto crl = tls["tlsCRLFile"].toString().toUtf8();
        mongoc_ssl_opt_t ssl = *mongoc_ssl_opt_get_default();
        ssl.pem_file = pem.isEmpty() ? nullptr : pem.constData();
        ssl.pem_pwd = password.isEmpty() ? nullptr : password.constData();
        ssl.ca_file = ca.isEmpty() ? nullptr : ca.constData();
        ssl.crl_file = crl.isEmpty() ? nullptr : crl.constData();
        ssl.weak_cert_validation = tls["tlsAllowInvalidCertificates"].toBool();
        ssl.allow_invalid_hostname = tls["tlsAllowInvalidHostnames"].toBool();
        mongoc_client_set_ssl_opts(_client, &ssl);
    }
}
DBClientBase::~DBClientBase() { if (_client) mongoc_client_destroy(_client); }
bool DBClientBase::runCommand(const std::string& db, const BSONObj& command, BSONObj& result, int) {
    if (!_client) throw std::runtime_error("MongoDB client is not initialized");
    bson_t reply; bson_error_t error{};
    bool ok = mongoc_client_command_simple(_client, db.c_str(), command.raw(), nullptr, &reply, &error);
    result = copyBson(&reply); bson_destroy(&reply);
    if (!ok && !result.hasField("errmsg")) result = BSON("ok" << 0 << "errmsg" << std::string(error.message) << "code" << static_cast<int>(error.code));
    return ok;
}
BSONObj DBClientBase::command(const std::string& db, const BSONObj& commandObject) {
    BSONObj result;
    if (!runCommand(db, commandObject, result)) throw std::runtime_error(result.getStringField("errmsg"));
    return result;
}
void DBClientBase::ping() { command("admin", BSON("ping" << 1)); }
std::list<std::string> DBClientBase::getDatabaseNames() {
    auto result = command("admin", BSON("listDatabases" << 1 << "nameOnly" << true << "authorizedDatabases" << true));
    std::list<std::string> names; for (const auto& db : result["databases"].Array()) names.push_back(db.Obj().getStringField("name")); return names;
}
std::list<BSONObj> DBClientBase::getCollectionInfos(const std::string& db) {
    auto database = mongoc_client_get_database(_client, db.c_str());
    auto cursor = mongoc_database_find_collections_with_opts(database, nullptr);
    mongoc_database_destroy(database); return collect(cursor);
}
std::list<BSONObj> DBClientBase::getIndexSpecs(const std::string& ns) {
    NamespaceString n(ns); auto collection = mongoc_client_get_collection(_client, n.db().c_str(), n.coll().c_str());
    auto cursor = mongoc_collection_find_indexes_with_opts(collection, nullptr);
    mongoc_collection_destroy(collection); return collect(cursor);
}
bool DBClientBase::exists(const std::string& ns) { NamespaceString n(ns); for (const auto& info : getCollectionInfos(n.db())) if (info.getStringField("name") == n.coll()) return true; return false; }
bool DBClientBase::dropDatabase(const std::string& db, WriteConcernOptions, BSONObj* result) { BSONObj reply; bool ok = runCommand(db, BSON("dropDatabase" << 1), reply); if (result) *result = reply; return ok; }
bool DBClientBase::dropCollection(const std::string& ns, WriteConcernOptions, BSONObj* result) { NamespaceString n(ns); BSONObj reply; bool ok = runCommand(n.db(), BSON("drop" << n.coll()), reply); if (result) *result = reply; return ok; }
bool DBClientBase::createCollection(const std::string& ns, long long size, bool capped, int max, BSONObj* result) {
    NamespaceString n(ns); BSONObjBuilder b; b.append("create", n.coll()); if (capped) b.append("capped", true); if (size) b.append("size", size); if (max) b.append("max", max);
    BSONObj reply; bool ok = runCommand(n.db(), b.obj(), reply); if (result) *result = reply; return ok;
}
void DBClientBase::dropIndex(const std::string& ns, const std::string& name) { NamespaceString n(ns); command(n.db(), BSON("dropIndexes" << n.coll() << "index" << name)); }
std::unique_ptr<DBClientCursor> DBClientBase::query(const NamespaceString& ns, const Query& query) { BSONObj options; if (!query.sortFields.isEmpty()) options = BSON("sort" << query.sortFields); return find(ns, query.obj, options); }
std::unique_ptr<DBClientCursor> DBClientBase::find(const NamespaceString& ns, const BSONObj& filter, const BSONObj& options, const BSONObj& readPreference) {
    auto collection = mongoc_client_get_collection(_client, ns.db().c_str(), ns.coll().c_str());
    std::unique_ptr<mongoc_collection_t, decltype(&mongoc_collection_destroy)> collectionGuard(collection, mongoc_collection_destroy);
    mongoc_read_mode_t mode = MONGOC_READ_PRIMARY;
    if (readPreference.hasField("mode") && readPreference["mode"].type() != String)
        throw std::invalid_argument("Read preference mode must be a string");
    std::string name = readPreference.getStringField("mode");
    if (name == "primaryPreferred") mode = MONGOC_READ_PRIMARY_PREFERRED;
    else if (name == "secondary") mode = MONGOC_READ_SECONDARY;
    else if (name == "secondaryPreferred") mode = MONGOC_READ_SECONDARY_PREFERRED;
    else if (name == "nearest") mode = MONGOC_READ_NEAREST;
    else if (!name.empty() && name != "primary") throw std::invalid_argument("Unknown read preference mode: " + name);
    std::unique_ptr<mongoc_read_prefs_t, decltype(&mongoc_read_prefs_destroy)> prefs(mongoc_read_prefs_new(mode), mongoc_read_prefs_destroy);
    if (readPreference.hasField("tags")) {
        if (readPreference["tags"].type() != Array) throw std::invalid_argument("Read preference tags must be an array");
        mongoc_read_prefs_set_tags(prefs.get(), readPreference.getObjectField("tags").raw());
    }
    if (readPreference.hasField("maxStalenessSeconds")) mongoc_read_prefs_set_max_staleness_seconds(prefs.get(), readPreference["maxStalenessSeconds"].numberLong());
    if (!mongoc_read_prefs_is_valid(prefs.get())) throw std::invalid_argument("Invalid read preference options");
    return std::make_unique<DBClientCursor>(mongoc_collection_find_with_opts(collection, filter.raw(), options.raw(), readPreference.isEmpty() ? nullptr : prefs.get()));
}
BSONObj DBClientBase::hello() { return command("admin", BSON("hello" << 1)); }
std::string DBClientBase::serverAddress() const { auto uri = mongoc_client_get_uri(_client); auto host = mongoc_uri_get_hosts(uri); return host ? HostAndPort(host->host, host->port).toString() : ""; }
}
namespace Robomongo {
std::string makeConnectionUri(const ConnectionSettings& settings, double timeoutSec) {
    auto encode = [](const std::string& s) { return QUrl::toPercentEncoding(QString::fromStdString(s)).toStdString(); };
    std::string uri = "mongodb://";
    const CredentialSettings* auth = settings.hasEnabledPrimaryCredential() ? settings.primaryCredential() : nullptr;
    if (auth) uri += encode(auth->userName()) + ":" + encode(auth->userPassword()) + "@";
    auto rs = settings.replicaSetSettings();
    if (settings.isReplicaSet() && !rs->members().empty()) {
        for (const auto& host : rs->members()) { if (uri.back() != '/' && uri.back() != '@') uri += ','; uri += mongo::HostAndPort(host).toString(); }
    } else uri += settings.hostAndPort().toString();
    uri += "/" + encode(settings.defaultDatabase()) + "?appName=Robo%203T";
    uri += "&connectTimeoutMS=10000&serverSelectionTimeoutMS=10000&socketTimeoutMS=" + std::to_string(static_cast<long long>(std::max(0.0, timeoutSec) * 1000));
    if (auth) uri += "&authSource=" + encode(auth->databaseName()) + "&authMechanism=" + encode(auth->mechanism());
    if (settings.isReplicaSet()) {
        auto name = rs->setNameUserEntered().empty() ? rs->cachedSetName() : rs->setNameUserEntered();
        if (!name.empty()) uri += "&replicaSet=" + encode(name);
        if (rs->readPreference() == ReplicaSetSettings::ReadPreference::PRIMARY_PREFERRED) uri += "&readPreference=primaryPreferred";
    } else uri += "&directConnection=true";
    if (settings.sslSettings()->sslEnabled()) uri += "&tls=true";
    return uri;
}
QJsonObject makeTlsOptions(const ConnectionSettings& settings) {
    auto ssl = settings.sslSettings(); QJsonObject options;
    if (!ssl->sslEnabled()) return options;
    options["tls"] = true;
    options["tlsAllowInvalidCertificates"] = ssl->allowInvalidCertificates();
    if (!ssl->caFile().empty()) options["tlsCAFile"] = QString::fromStdString(ssl->caFile());
    if (ssl->usePemFile()) {
        options["tlsCertificateKeyFile"] = QString::fromStdString(ssl->pemKeyFile());
        if (!ssl->pemPassPhrase().empty()) options["tlsCertificateKeyFilePassword"] = QString::fromStdString(ssl->pemPassPhrase());
    }
    if (ssl->useAdvancedOptions()) {
        options["tlsAllowInvalidHostnames"] = ssl->allowInvalidHostnames();
        if (!ssl->crlFile().empty()) options["tlsCRLFile"] = QString::fromStdString(ssl->crlFile());
    }
    return options;
}
}
