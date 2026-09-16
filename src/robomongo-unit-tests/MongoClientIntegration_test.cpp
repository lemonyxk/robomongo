#include <cstdlib>
#include <iostream>
#include <memory>

#include <gtest/gtest.h>
#include "robomongo/core/mongodb/MongoConnection.h"

#include "robomongo/core/domain/MongoDocument.h"
#include "robomongo/core/mongodb/MongoClient.h"

namespace {
int testPort;

// This executable is opt-in, connects only to loopback, and uses a new database
// for each test. Run it against a disposable single-node replica set named
// codexCompatibility; it never uses a saved Robo 3T connection.
class MongoClientIntegration : public testing::TestWithParam<bool> {
protected:
    void SetUp() override {
        const std::string uri = "mongodb://127.0.0.1:" + std::to_string(testPort) +
            (GetParam() ? "/?replicaSet=codexCompatibility" : "/?directConnection=true") +
            "&serverSelectionTimeoutMS=1000";
        connection = std::make_unique<mongo::DBClientBase>(uri);
        ASSERT_NO_THROW(connection->ping());
        auto callbacks=mongoc_apm_callbacks_new();
        mongoc_apm_set_command_started_cb(callbacks,[](const mongoc_apm_command_started_t *event) {
            auto self=static_cast<MongoClientIntegration *>(mongoc_apm_command_started_get_context(event));
            self->observedCommands.emplace_back(mongoc_apm_command_started_get_command(event));
        });
        ASSERT_TRUE(mongoc_client_set_apm_callbacks(connection->raw(),callbacks,this));
        mongoc_apm_callbacks_destroy(callbacks);
        client.reset(new Robomongo::MongoClient(connection.get()));
        database = "robo_compat_" + mongo::OID::gen().toString();
        ns = Robomongo::MongoNamespace(database, "documents");
    }

    void TearDown() override {
        if (connection && !database.empty()) {
            mongo::BSONObj result;
            EXPECT_TRUE(connection->runCommand(database, BSON("dropDatabase" << 1), result))
                << result.toString();
        }
    }

    mongo::BSONObj command(const mongo::BSONObj &cmd) {
        mongo::BSONObj result;
        if (!connection->runCommand(database, cmd, result))
            throw std::runtime_error(result.toString());
        return result.getOwned();
    }

    void seed(int count = 123) {
        mongo::BSONArrayBuilder documents;
        for (int i = 0; i < count; ++i)
            documents.append(BSON("_id" << i << "value" << i << "query" << i << "extra" << true));
        // Seed data must follow the same server defaults as the subsequent reads.
        auto result = command(BSON("insert" << "documents" << "documents" << documents.arr()));
        ASSERT_FALSE(result.hasField("writeErrors")) << result.toString();
    }

    std::vector<Robomongo::MongoDocumentPtr> page(int limit, int skip = 0, int batch = 7,
                                               mongo::BSONObj filter = mongo::BSONObj(),
                                               mongo::BSONObj fields = mongo::BSONObj(),
                                               bool special = false) {
        return client->query(Robomongo::MongoQueryInfo(
            Robomongo::CollectionInfo("127.0.0.1", database, "documents"),
            filter, fields, limit, skip, batch, 0, special));
    }

    long long count(mongo::BSONObj filter = mongo::BSONObj()) {
        return command(BSON("count" << "documents" << "query" << filter))["n"].numberLong();
    }

    std::vector<mongo::BSONObj> observedCommands;
    std::unique_ptr<mongo::DBClientBase> connection;
    std::unique_ptr<Robomongo::MongoClient> client;
    std::string database;
    Robomongo::MongoNamespace ns;
};

TEST_P(MongoClientIntegration, PagesPreserveSortProjectionAndLimitAcrossGetMore) {
    seed();
    const auto query = BSON("$query" << BSON("value" << BSON("$gte" << 0))
                            << "$orderby" << BSON("value" << -1));
    const auto fields = BSON("value" << 1 << "_id" << 0);
    for (int skip : {0, 50, 100, 150}) {
        auto docs = page(50, skip, 7, query, fields, true);
        const int expected = skip < 100 ? 50 : (skip == 100 ? 23 : 0);
        ASSERT_EQ(expected, docs.size());
        for (int i = 0; i < expected; ++i) {
            EXPECT_EQ(122 - skip - i, docs[i]->bsonObj()["value"].numberInt());
            EXPECT_EQ(1, docs[i]->bsonObj().nFields());
        }
    }
}

TEST_P(MongoClientIntegration, UnlimitedQueryWithBatchSizeOneConsumesAllBatches) {
    seed(12);
    EXPECT_EQ(12U, page(0, 0, 1).size());
}

TEST_P(MongoClientIntegration, ZeroBatchSizeUsesServerDefaultsWithoutTruncatingResults) {
    seed(123);
    EXPECT_EQ(123U,page(0,0,0).size());
    EXPECT_EQ(1U,page(1,0,0).size());
}

TEST_P(MongoClientIntegration, NegativeBatchSizeStopsAfterExactlyOneBatch) {
    seed(12);
    observedCommands.clear();
    EXPECT_EQ(3U,page(0,0,-3).size());
    bool findSeen=false;
    for(const auto &command:observedCommands) {
        EXPECT_FALSE(command.hasField("getMore"));
        if(command.hasField("find")) {
            findSeen=true;
            EXPECT_TRUE(command.getBoolField("singleBatch"));
            EXPECT_EQ(3,command["batchSize"].numberInt());
        }
    }
    EXPECT_TRUE(findSeen);
}

TEST_P(MongoClientIntegration, DriverSendsModernFindAndGetMoreWithExactOptions) {
    seed(12);
    observedCommands.clear();
    const auto filter=BSON("query"<<BSON("value"<<BSON("$gte"<<0))
                           <<"orderby"<<BSON("value"<<-1));
    EXPECT_EQ(5U,page(5,2,2,filter,BSON("value"<<1<<"_id"<<0),true).size());
    int finds=0,getMores=0;
    for(const auto &command:observedCommands) {
        EXPECT_FALSE(command.hasField("ntoreturn"));
        if(command.hasField("find")) {
            ++finds;
            EXPECT_EQ(5,command["limit"].numberInt());
            EXPECT_EQ(2,command["skip"].numberInt());
            EXPECT_EQ(2,command["batchSize"].numberInt());
            EXPECT_EQ(-1,command["sort"].Obj()["value"].numberInt());
            EXPECT_EQ(0,command["projection"].Obj()["_id"].numberInt());
        }
        if(command.hasField("getMore"))++getMores;
    }
    EXPECT_EQ(1,finds);
    EXPECT_GE(getMores,2);
}

TEST_P(MongoClientIntegration, ExplicitSecondaryReadPreferenceNeverSilentlyReadsPrimary) {
    if(!GetParam())GTEST_SKIP()<<"A direct connection deliberately pins one server";
    seed(1);
    auto query=BSON("query"<<mongo::BSONObj()<<"$readPreference"<<BSON("mode"<<"secondary"));
    EXPECT_THROW(page(1,0,1,query,mongo::BSONObj(),true),std::exception);
}

TEST_P(MongoClientIntegration, NegativeLimitUsesOneBatchAndMinusOneLoadsNothing) {
    seed(12);
    EXPECT_EQ(5U, page(-5, 0, 0).size());
    EXPECT_TRUE(page(-1).empty());
}

TEST_P(MongoClientIntegration, OrdinaryQueryNamedFieldIsNotUnwrapped) {
    seed(12);
    auto docs = page(10, 0, 2, BSON("query" << 4));
    ASSERT_EQ(1U, docs.size());
    EXPECT_EQ(4, docs.front()->bsonObj()["value"].numberInt());
    command(BSON("insert" << "documents" << "documents"
                 << BSON_ARRAY(BSON("_id" << 20 << "query" << BSON("nested" << true)))));
    docs = page(10, 0, 2, BSON("query" << BSON("nested" << true)));
    ASSERT_EQ(1U, docs.size());
    EXPECT_EQ(20, docs.front()->bsonObj()["_id"].numberInt());
}

TEST_P(MongoClientIntegration, NamedIndexHintPreservesPaging) {
    seed(12);
    const auto query = BSON("query" << mongo::BSONObj() << "$hint" << "_id_"
                            << "orderby" << BSON("_id" << 1));
    auto docs = page(5, 2, 2, query, mongo::BSONObj(), true);
    ASSERT_EQ(5U, docs.size());
    EXPECT_EQ(2, docs.front()->bsonObj()["_id"].numberInt());
    EXPECT_EQ(6, docs.back()->bsonObj()["_id"].numberInt());
}

TEST_P(MongoClientIntegration, InsertAcknowledgesSuccessAndReportsDuplicateKey) {
    ASSERT_NO_THROW(client->insertDocument(BSON("_id" << 1 << "value" << "first"), ns));
    EXPECT_THROW(client->insertDocument(BSON("_id" << 1 << "value" << "duplicate"), ns),
                 std::exception);
    EXPECT_EQ(1, count());
}

TEST_P(MongoClientIntegration, SaveReplacesExistingDocumentAndUpsertsNewId) {
    seed(1);
    ASSERT_NO_THROW(client->saveDocument(BSON("_id" << 0 << "value" << "updated"), ns));
    auto docs = page(10);
    ASSERT_EQ(1U, docs.size());
    EXPECT_STREQ("updated", docs.front()->bsonObj().getStringField("value"));
    EXPECT_FALSE(docs.front()->bsonObj().hasField("extra"));
    ASSERT_NO_THROW(client->saveDocument(BSON("_id" << 99 << "value" << "new"), ns));
    EXPECT_EQ(2, count());
}

TEST_P(MongoClientIntegration, SaveHonorsGlobalMajorityDefaultsOnImmediateRefresh) {
    mongo::BSONObj defaults;
    ASSERT_TRUE(connection->runCommand("admin", BSON("getDefaultRWConcern" << 1), defaults))
        << defaults.toString();
    if (defaults.getObjectField("defaultReadConcern")["level"].String() != "majority" ||
        defaults.getObjectField("defaultWriteConcern")["w"].String() != "majority" ||
        defaults["defaultWriteConcernSource"].String() != "global") {
        GTEST_SKIP() << "Configure global majority read/write defaults on the disposable "
                        "replica set to exercise immediate refresh: " << defaults.toString();
    }

    seed(1);
    observedCommands.clear();
    ASSERT_NO_THROW(client->saveDocument(BSON("_id" << 0 << "value" << "updated"), ns));
    // Read immediately, with no retry or delay to hide an acknowledgement mismatch.
    const auto docs = page(1, 0, 1, BSON("_id" << 0));
    ASSERT_EQ(1U, docs.size());
    EXPECT_STREQ("updated", docs.front()->bsonObj().getStringField("value"));

    int updates = 0, finds = 0;
    for (const auto &command : observedCommands) {
        if (command.hasField("update")) {
            ++updates;
            EXPECT_FALSE(command.hasField("writeConcern")) << command.toString();
        }
        if (command.hasField("find")) {
            ++finds;
            EXPECT_EQ(1, updates);
            EXPECT_FALSE(command.hasField("readConcern")) << command.toString();
        }
    }
    EXPECT_EQ(1, updates);
    EXPECT_EQ(1, finds);
}

TEST_P(MongoClientIntegration, SaveWithoutIdInsertsInsteadOfReplacingAnotherDocument) {
    seed(1);
    ASSERT_NO_THROW(client->saveDocument(BSON("value" << "new"), ns));
    EXPECT_EQ(2, count());
    EXPECT_EQ(1, count(BSON("_id" << 0 << "extra" << true)));
}

TEST_P(MongoClientIntegration, RemoveOneAndRemoveManyRespectTheSelector) {
    seed(12);
    client->removeDocuments(ns, mongo::Query(BSON("value" << BSON("$lt" << 5))), true);
    EXPECT_EQ(11, count());
    client->removeDocuments(ns, mongo::Query(BSON("value" << BSON("$lt" << 5))), false);
    EXPECT_EQ(7, count());
    EXPECT_EQ(0, count(BSON("value" << BSON("$lt" << 5))));
}

TEST_P(MongoClientIntegration, RemovePreservesQueryNamedFieldsInTheSelector) {
    seed(12);
    client->removeDocuments(ns, mongo::Query(BSON("query" << 4)), true);
    EXPECT_EQ(11, count());
    EXPECT_EQ(0, count(BSON("_id" << 4)));
    EXPECT_EQ(1, count(BSON("_id" << 0)));
    client->removeDocuments(ns, mongo::Query(BSON("query" << BSON("$lt" << 5))), false);
    EXPECT_EQ(7, count());
}

TEST_P(MongoClientIntegration, ValidationErrorsDoNotReportASuccessfulSave) {
    command(BSON("create" << "documents" << "validator"
                 << BSON("value" << BSON("$type" << "string"))));
    client->insertDocument(BSON("_id" << 1 << "value" << "valid"), ns);
    EXPECT_THROW(client->saveDocument(BSON("_id" << 1 << "value" << 42), ns), std::exception);
    EXPECT_EQ(1, count(BSON("_id" << 1 << "value" << "valid")));
}

INSTANTIATE_TEST_SUITE_P(DirectAndReplicaSet, MongoClientIntegration, testing::Bool());
} // namespace

int main(int argc, char **argv) {
    const char *port = std::getenv("ROBOMONGO_TEST_PORT");
    if (!port || (testPort = std::atoi(port)) < 1024 || testPort > 65535) {
        std::cerr << "Set ROBOMONGO_TEST_PORT to a disposable local MongoDB replica set port.\n";
        return 2;
    }

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
