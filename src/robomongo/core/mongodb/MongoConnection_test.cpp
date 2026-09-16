#include "robomongo/core/mongodb/MongoConnection.h"
#include "robomongo/core/settings/ConnectionSettings.h"
#include "robomongo/core/settings/CredentialSettings.h"
#include "robomongo/core/settings/ReplicaSetSettings.h"
#include "robomongo/core/settings/SslSettings.h"
#include <gtest/gtest.h>

TEST(MongoConnectionSettings, PercentEncodedCredentialsRoundTripThroughModernDriver) {
    Robomongo::ConnectionSettings settings(true);
    auto credential=new Robomongo::CredentialSettings();
    const std::string username="user @:/?%&中文";
    const std::string password="p@ss:#[]/?%& +中文";
    credential->setEnabled(true);
    credential->setUserName(username);
    credential->setUserPassword(password);
    credential->setDatabaseName("auth-db");
    credential->setMechanism("SCRAM-SHA-256");
    settings.addCredential(credential);
    settings.setDefaultDatabase("work-db");
    const auto uri=Robomongo::makeConnectionUri(settings);
    EXPECT_EQ(std::string::npos,uri.find(password));
    const auto parsed=mongo::MongoURI::parse(uri);
    ASSERT_TRUE(parsed.isOK()) << parsed.getStatus().toString();
    EXPECT_EQ(username,parsed.getValue().getUser());
    EXPECT_EQ(password,parsed.getValue().getPassword());
    EXPECT_EQ("auth-db",parsed.getValue().getAuthenticationDatabase());
    EXPECT_EQ("work-db",parsed.getValue().getDatabase());
    EXPECT_EQ("SCRAM-SHA-256",parsed.getValue().getOption("authMechanism").get_value_or(""));
}

TEST(MongoConnectionSettings, DirectIpv6ConnectionHasOneBracketPairAndRequestedPort) {
    Robomongo::ConnectionSettings settings(true);
    settings.setServerHost("::1");
    settings.setServerPort(37027);
    auto uri=Robomongo::makeConnectionUri(settings);
    EXPECT_NE(std::string::npos,uri.find("mongodb://[::1]:37027/"));
    auto parsed=mongo::MongoURI::parse(uri);
    ASSERT_TRUE(parsed.isOK()) << parsed.getStatus().toString();
    ASSERT_EQ(1U,parsed.getValue().getServers().size());
    EXPECT_EQ("::1",parsed.getValue().getServers().front().host());
    EXPECT_EQ(37027,parsed.getValue().getServers().front().port());
    EXPECT_EQ("true",parsed.getValue().getOption("directConnection").get_value_or(""));
}

TEST(MongoConnectionSettings, ReplicaSetRetainsEverySeedAndReadPreference) {
    Robomongo::ConnectionSettings settings(true);
    settings.setReplicaSet(true);
    settings.replicaSetSettings()->setSetNameUserEntered("rs-production");
    settings.replicaSetSettings()->setMembers(std::vector<std::string>{"db1.example:27018","[::1]:27019","db3.example"});
    settings.replicaSetSettings()->setReadPreference(Robomongo::ReplicaSetSettings::ReadPreference::PRIMARY_PREFERRED);
    auto parsed=mongo::MongoURI::parse(Robomongo::makeConnectionUri(settings));
    ASSERT_TRUE(parsed.isOK()) << parsed.getStatus().toString();
    const auto hosts=parsed.getValue().getServers();
    ASSERT_EQ(3U,hosts.size());
    EXPECT_EQ("db1.example:27018",hosts[0].toString());
    EXPECT_EQ("[::1]:27019",hosts[1].toString());
    EXPECT_EQ("db3.example:27017",hosts[2].toString());
    EXPECT_EQ("rs-production",parsed.getValue().getSetName());
    EXPECT_EQ("primaryPreferred",parsed.getValue().getOption("readPreference").get_value_or(""));
    EXPECT_FALSE(parsed.getValue().getOption("directConnection").value_or("false")=="true");
}

TEST(MongoConnectionSettings, CachedReplicaSetNameAndStandaloneHostAreUsableSeeds) {
    Robomongo::ConnectionSettings settings(true);
    settings.setReplicaSet(true);
    settings.setServerHost("seed.example");
    settings.setServerPort(27020);
    settings.replicaSetSettings()->setCachedSetName("discovered-set");
    auto parsed=mongo::MongoURI::parse(Robomongo::makeConnectionUri(settings));
    ASSERT_TRUE(parsed.isOK()) << parsed.getStatus().toString();
    EXPECT_EQ("discovered-set",parsed.getValue().getSetName());
    EXPECT_EQ("seed.example:27020",parsed.getValue().getServers().front().toString());
}

TEST(MongoConnectionSettings, DisabledCredentialsAndTlsFlagsDoNotLeakIntoConnection) {
    Robomongo::ConnectionSettings settings(true);
    auto credential=new Robomongo::CredentialSettings();
    credential->setUserName("disabled-user");
    credential->setUserPassword("disabled-password");
    credential->setEnabled(false);
    settings.addCredential(credential);
    settings.sslSettings()->setCaFile("/unused-ca.pem");
    settings.sslSettings()->setAllowInvalidCertificates(true);
    auto parsed=mongo::MongoURI::parse(Robomongo::makeConnectionUri(settings));
    ASSERT_TRUE(parsed.isOK());
    EXPECT_TRUE(parsed.getValue().getUser().empty());
    EXPECT_TRUE(parsed.getValue().getPassword().empty());
    EXPECT_EQ(mongo::transport::ConnectSSLMode::kDisableSSL,parsed.getValue().getSSLMode());
    EXPECT_TRUE(Robomongo::makeTlsOptions(settings).isEmpty());
}

TEST(MongoConnectionSettings, TlsFilePathsAndPasswordsArePassedWithoutUriInterpolation) {
    Robomongo::ConnectionSettings settings(true);
    auto tls=settings.sslSettings();
    tls->enableSSL(true);
    tls->setUsePemFile(true);
    tls->setUseAdvancedOptions(true);
    tls->setPemKeyFile("/tmp/client certificate.pem");
    tls->setCaFile("/tmp/root ca.pem");
    tls->setPemPassPhrase("secret@&?中文");
    tls->setCrlFile("/tmp/revoked certificates.pem");
    tls->setAllowInvalidHostnames(true);
    tls->setAllowInvalidCertificates(false);
    auto parsed=mongo::MongoURI::parse(Robomongo::makeConnectionUri(settings));
    ASSERT_TRUE(parsed.isOK());
    EXPECT_EQ(mongo::transport::ConnectSSLMode::kEnableSSL,parsed.getValue().getSSLMode());
    auto options=Robomongo::makeTlsOptions(settings);
    EXPECT_EQ(QString("/tmp/client certificate.pem"),options["tlsCertificateKeyFile"].toString());
    EXPECT_EQ(QString("/tmp/root ca.pem"),options["tlsCAFile"].toString());
    EXPECT_EQ(QString::fromUtf8("secret@&?中文"),options["tlsCertificateKeyFilePassword"].toString());
    EXPECT_EQ(QString("/tmp/revoked certificates.pem"),options["tlsCRLFile"].toString());
    EXPECT_TRUE(options["tlsAllowInvalidHostnames"].toBool());
    EXPECT_FALSE(options["tlsAllowInvalidCertificates"].toBool());
    EXPECT_EQ(std::string::npos,Robomongo::makeConnectionUri(settings).find("secret"));
}

TEST(MongoConnectionSettings, InvalidHostsAndUrisFailBeforeConnecting) {
    EXPECT_THROW(mongo::HostAndPort("host:70000"),std::exception);
    EXPECT_THROW(mongo::HostAndPort("first:27017,second:27017"),std::exception);
    EXPECT_FALSE(mongo::MongoURI::parse("mongodb://[invalid-ipv6").isOK());
    EXPECT_FALSE(mongo::MongoURI::parse("mongodb://[not-an-ip]:27017/").isOK());
    EXPECT_FALSE(mongo::MongoURI::parse("mongodb://::1:27017/").isOK());
}

TEST(MongoConnectionSettings, DriverClientUsesUriTimeoutAndTlsWithoutConnecting) {
    Robomongo::ConnectionSettings settings(true);
    settings.sslSettings()->enableSSL(true);
    mongo::DBClientBase client(Robomongo::makeConnectionUri(settings,2.75),Robomongo::makeTlsOptions(settings));
    const auto uri=mongoc_client_get_uri(client.raw());
    EXPECT_TRUE(mongoc_uri_get_tls(uri));
    EXPECT_EQ(2750,mongoc_uri_get_option_as_int32(uri,"socketTimeoutMS",-1));
    EXPECT_TRUE(mongoc_uri_get_option_as_bool(uri,"directConnection",false));
}

TEST(MongoConnectionSettings, InvalidReadPreferenceCannotSilentlyFallBackToPrimary) {
    mongo::DBClientBase client("mongodb://127.0.0.1:37027/?directConnection=true");
    const mongo::NamespaceString ns("unused","documents");
    EXPECT_THROW(client.find(ns,{}, {},BSON("mode"<<"not-a-mode")),std::invalid_argument);
    EXPECT_THROW(client.find(ns,{}, {},BSON("mode"<<42)),std::invalid_argument);
    EXPECT_THROW(client.find(ns,{}, {},BSON("mode"<<"secondary"<<"tags"<<BSON("region"<<"east"))),std::invalid_argument);
    EXPECT_THROW(client.find(ns,{}, {},BSON("mode"<<"primary"<<"tags"<<BSON_ARRAY(BSON("region"<<"east")))),std::invalid_argument);
}
