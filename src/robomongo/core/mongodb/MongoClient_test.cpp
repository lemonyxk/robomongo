#include "gtest/gtest.h"

#include "robomongo/core/mongodb/MongoClient.h"

#include "robomongo/core/mongodb/MongoConnection.h"
#include "robomongo/core/bson/Bson.h"

namespace
{
    // Exercise the public client methods without opening a database connection.
    class WriteCommandConnection : public mongo::DBClientBase
    {
    public:
        mongo::BSONObj response = BSON("ok" << 1 << "n" << 1);
        std::vector<mongo::BSONObj> commands;
        std::string database;

        bool runCommand(const std::string &db, const mongo::BSONObj &command,
                        mongo::BSONObj &result, int = 0) override
        {
            commands.push_back(command.getOwned());
            database = db;
            result = response;
            return response.getBoolField("ok");
        }
    };

    class MongoClientWrites : public testing::Test
    {
    protected:
        WriteCommandConnection connection;
        Robomongo::MongoClient client{&connection};
        Robomongo::MongoNamespace ns{"test_database", "documents.with.dots"};

        void expectInsertError(const std::string &message)
        {
            try {
                client.insertDocument(BSON("_id" << 1), ns);
                FAIL() << "Expected the failed write to throw";
            } catch (const std::runtime_error &error) {
                EXPECT_NE(std::string::npos, std::string(error.what()).find(message));
            }
            ASSERT_EQ(1U, connection.commands.size());
        }
    };
}

TEST_F(MongoClientWrites, InsertUsesServerWriteConcernWithoutGetLastError)
{
    mongo::BSONObj const document = BSON("_id" << 1 << "value" << "new");
    client.insertDocument(document, ns);

    ASSERT_EQ(1U, connection.commands.size());
    mongo::BSONObj const &command = connection.commands.front();
    EXPECT_EQ("test_database", connection.database);
    EXPECT_EQ("documents.with.dots", std::string(command.getStringField("insert")));
    EXPECT_FALSE(command.hasField("writeConcern"));
    EXPECT_TRUE(command.getBoolField("ordered"));
    auto const documents = command.getField("documents").Array();
    ASSERT_EQ(1U, documents.size());
    EXPECT_EQ(0, document.woCompare(documents.front().Obj()));
}

TEST_F(MongoClientWrites, SaveReplacesOneDocumentAndAllowsUpsert)
{
    mongo::BSONObj const document = BSON("_id" << 5 << "value" << "updated");
    client.saveDocument(document, ns);

    ASSERT_EQ(1U, connection.commands.size());
    mongo::BSONObj const &command = connection.commands.front();
    EXPECT_EQ("documents.with.dots", std::string(command.getStringField("update")));
    EXPECT_FALSE(command.hasField("writeConcern"));
    auto const updates = command.getField("updates").Array();
    ASSERT_EQ(1U, updates.size());
    mongo::BSONObj const update = updates.front().Obj();
    EXPECT_EQ(0, BSON("_id" << 5).woCompare(update.getObjectField("q")));
    EXPECT_EQ(0, document.woCompare(update.getObjectField("u")));
    EXPECT_TRUE(update.getBoolField("upsert"));
    EXPECT_FALSE(update.getBoolField("multi"));
}

TEST_F(MongoClientWrites, SaveWithoutIdInsertsInsteadOfReplacingAnArbitraryDocument)
{
    client.saveDocument(BSON("value" << "new"), ns);

    ASSERT_EQ(1U, connection.commands.size());
    EXPECT_TRUE(connection.commands.front().hasField("insert"));
    EXPECT_FALSE(connection.commands.front().hasField("update"));
    EXPECT_FALSE(connection.commands.front().hasField("writeConcern"));
}

TEST_F(MongoClientWrites, UpdateFieldSetsOneNestedValueWithoutReplacingOrUpserting)
{
    const mongo::BSONObj id = BSON("_id" << 5);
    const mongo::BSONObj value = BSON("value" << static_cast<long long>(7));
    client.updateField(id, "items.0.count", value, ns);

    ASSERT_EQ(1U, connection.commands.size());
    const mongo::BSONObj &command = connection.commands.front();
    EXPECT_EQ("test_database", connection.database);
    EXPECT_EQ("documents.with.dots", std::string(command.getStringField("update")));
    EXPECT_FALSE(command.hasField("writeConcern"));
    const auto updates = command.getField("updates").Array();
    ASSERT_EQ(1U, updates.size());
    const mongo::BSONObj update = updates.front().Obj();
    EXPECT_EQ(0, id.woCompare(update.getObjectField("q")));
    EXPECT_FALSE(update.getBoolField("upsert"));
    EXPECT_FALSE(update.getBoolField("multi"));
    const mongo::BSONObj changes = update.getObjectField("u");
    ASSERT_EQ(1, changes.nFields());
    const mongo::BSONObj fields = changes.getObjectField("$set");
    ASSERT_EQ(1, fields.nFields());
    EXPECT_EQ(mongo::NumberLong, fields.getField("items.0.count").type());
    EXPECT_EQ(7, fields.getField("items.0.count").numberLong());
}

TEST_F(MongoClientWrites, UpdateFieldRejectsMissingIdsAndMultipleValuesBeforeWriting)
{
    const mongo::BSONObj value = BSON("value" << 7);
    EXPECT_THROW(client.updateField(mongo::BSONObj(), "field", value, ns), std::runtime_error);
    EXPECT_THROW(client.updateField(BSON("other" << 1), "field", value, ns), std::runtime_error);
    EXPECT_THROW(client.updateField(BSON("_id" << 1 << "other" << 2), "field", value, ns), std::runtime_error);
    EXPECT_THROW(client.updateField(BSON("_id" << 1), "field", mongo::BSONObj(), ns), std::runtime_error);
    EXPECT_THROW(client.updateField(BSON("_id" << 1), "field", BSON("a" << 1 << "b" << 2), ns), std::runtime_error);
    EXPECT_TRUE(connection.commands.empty());
}

TEST_F(MongoClientWrites, UpdateFieldRejectsImmutableAndOperatorPathsBeforeWriting)
{
    const std::vector<std::string> paths = {
        "", "_id", "_id.child", ".field", "field.", "field..child", "$field",
        "items.$.value", "items.$[].value", "items.$[item].value", std::string("a\0b", 3)
    };
    for (const auto &path : paths) {
        SCOPED_TRACE(path);
        EXPECT_THROW(client.updateField(BSON("_id" << 1), path, BSON("value" << 7), ns), std::runtime_error);
    }
    EXPECT_TRUE(connection.commands.empty());
}

TEST_F(MongoClientWrites, UpdateFieldReportsMissingDocumentWithoutInserting)
{
    connection.response = BSON("ok" << 1 << "n" << 0 << "nModified" << 0);
    EXPECT_THROW(client.updateField(BSON("_id" << 1), "field", BSON("value" << 7), ns), std::runtime_error);
    ASSERT_EQ(1U, connection.commands.size());
    EXPECT_FALSE(connection.commands.front().hasField("insert"));
}

TEST_F(MongoClientWrites, UpdateFieldAcceptsAnUnchangedButMatchedValue)
{
    connection.response = BSON("ok" << 1 << "n" << 1 << "nModified" << 0);
    EXPECT_NO_THROW(client.updateField(BSON("_id" << 1), "field", BSON("value" << 7), ns));
}

TEST_F(MongoClientWrites, UpdateFieldRequiresAMatchedCount)
{
    connection.response = BSON("ok" << 1);
    EXPECT_THROW(client.updateField(BSON("_id" << 1), "field", BSON("value" << 7), ns), std::runtime_error);
}

TEST_F(MongoClientWrites, UpdateFieldReportsWriteAndWriteConcernErrors)
{
    connection.response = BSON("ok" << 1 << "n" << 1 << "writeConcernError"
        << BSON("code" << 64 << "errmsg" << "replication acknowledgement failed"));
    EXPECT_THROW(client.updateField(BSON("_id" << 1), "field", BSON("value" << 7), ns), std::runtime_error);
    connection.response = BSON("ok" << 1 << "n" << 0 << "writeErrors"
        << BSON_ARRAY(BSON("index" << 0 << "code" << 121 << "errmsg" << "validation failed")));
    EXPECT_THROW(client.updateField(BSON("_id" << 1), "field", BSON("value" << 7), ns), std::runtime_error);
}

TEST_F(MongoClientWrites, DeletePreservesSingleAndMultipleDocumentSemantics)
{
    mongo::BSONObj const filter = BSON("value" << "remove");
    client.removeDocuments(ns, mongo::Query(filter), true);
    client.removeDocuments(ns, mongo::Query(filter), false);

    ASSERT_EQ(2U, connection.commands.size());
    for (std::size_t i = 0; i < connection.commands.size(); ++i) {
        mongo::BSONObj const &command = connection.commands[i];
        EXPECT_EQ("documents.with.dots", std::string(command.getStringField("delete")));
        EXPECT_FALSE(command.hasField("writeConcern"));
        auto const deletes = command.getField("deletes").Array();
        ASSERT_EQ(1U, deletes.size());
        mongo::BSONObj const deletion = deletes.front().Obj();
        EXPECT_EQ(0, filter.woCompare(deletion.getObjectField("q")));
        EXPECT_EQ(i == 0 ? 1 : 0, deletion.getIntField("limit"));
    }
}

TEST_F(MongoClientWrites, DeletePreservesAFieldNamedQueryInTheFilter)
{
    std::vector<mongo::BSONObj> const filters = {
        BSON("query" << 4), BSON("query" << BSON("value" << 1)),
        BSON("$query" << BSON("value" << 1))
    };
    for (auto const &filter : filters)
        client.removeDocuments(ns, mongo::Query(filter), false);

    ASSERT_EQ(filters.size(), connection.commands.size());
    for (std::size_t i = 0; i < filters.size(); ++i) {
        auto const deletes = connection.commands[i].getField("deletes").Array();
        ASSERT_EQ(1U, deletes.size());
        EXPECT_EQ(0, filters[i].woCompare(deletes.front().Obj().getObjectField("q")));
    }
}

TEST_F(MongoClientWrites, CommandFailureIsReported)
{
    connection.response = BSON("ok" << 0 << "code" << 13 << "errmsg" << "not authorized");
    expectInsertError("not authorized");
}

TEST_F(MongoClientWrites, WriteErrorIsReportedEvenWhenCommandSucceeded)
{
    connection.response = BSON("ok" << 1 << "n" << 0 << "writeErrors"
        << BSON_ARRAY(BSON("index" << 0 << "code" << 11000 << "errmsg" << "duplicate key")));
    expectInsertError("duplicate key");
}

TEST_F(MongoClientWrites, WriteConcernErrorIsReportedEvenWhenCommandSucceeded)
{
    connection.response = BSON("ok" << 1 << "n" << 1 << "writeConcernError"
        << BSON("code" << 64 << "errmsg" << "replication acknowledgement failed"));
    expectInsertError("replication acknowledgement failed");
}

TEST_F(MongoClientWrites, BothWriteAndWriteConcernErrorsAreReported)
{
    connection.response = BSON("ok" << 1 << "n" << 0 << "writeErrors"
        << BSON_ARRAY(BSON("index" << 0 << "code" << 121 << "errmsg" << "validation failed"))
        << "writeConcernError" << BSON("code" << 64 << "errmsg" << "replication failed"));
    try {
        client.insertDocument(BSON("_id" << 1), ns);
        FAIL() << "Expected the failed write to throw";
    } catch (const std::runtime_error &error) {
        std::string const message = error.what();
        EXPECT_NE(std::string::npos, message.find("validation failed"));
        EXPECT_NE(std::string::npos, message.find("replication failed"));
    }
}

TEST_F(MongoClientWrites, ErrorWithoutMessageStillFails)
{
    connection.response = BSON("ok" << 1 << "n" << 0 << "writeErrors"
        << BSON_ARRAY(BSON("index" << 0 << "code" << 11000)));
    expectInsertError("11000");
}

TEST_F(MongoClientWrites, UpdateAndDeleteAlsoReportWriteErrors)
{
    connection.response = BSON("ok" << 1 << "n" << 0 << "writeErrors"
        << BSON_ARRAY(BSON("index" << 0 << "code" << 121 << "errmsg" << "validation failed")));
    EXPECT_THROW(client.saveDocument(BSON("_id" << 1), ns), std::runtime_error);
    EXPECT_THROW(client.removeDocuments(ns, mongo::Query(), false), std::runtime_error);
}
