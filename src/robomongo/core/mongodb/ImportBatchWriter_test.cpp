#include "robomongo/core/mongodb/ImportBatchWriter.h"
#include "robomongo/core/mongodb/DataTransfer.h"
#include "robomongo/core/domain/MongoNamespace.h"
#include <mongoc/mongoc.h>
#include <gtest/gtest.h>
#include <stdexcept>

namespace
{
    using Robomongo::DataTransferResult;
    using Robomongo::MongoNamespace;
    using Robomongo::detail::applyImportBatchReply;
    using Robomongo::detail::importReplacementSelector;

    mongo::BSONObj reply(qint64 inserted, qint64 matched, qint64 upserted)
    {
        return BSON("nInserted" << static_cast<long long>(inserted)
                    << "nMatched" << static_cast<long long>(matched)
                    << "nModified" << 0
                    << "nUpserted" << static_cast<long long>(upserted));
    }

    bson_error_t writeError()
    {
        bson_error_t error{};
        bson_set_error(&error, MONGOC_ERROR_SERVER, 11000, "duplicate key");
        return error;
    }

    mongo::BSONObj duplicateAt(const mongo::BSONObj &counts, int index)
    {
        return mongo::BSONObjBuilder().appendElements(counts)
            .append("writeErrors", BSON_ARRAY(BSON("index" << index << "code" << 11000
                                                  << "errmsg" << "duplicate key"))).obj();
    }
}

TEST(ImportBatchWriter, AppendModeNeverReplacesEvenWithId)
{
    EXPECT_TRUE(importReplacementSelector(BSON("_id" << 7 << "value" << 1), false).isEmpty());
}

TEST(ImportBatchWriter, UpsertWithoutIdUsesInsert)
{
    EXPECT_TRUE(importReplacementSelector(BSON("value" << 1), true).isEmpty());
}

TEST(ImportBatchWriter, ReplacementSelectorPreservesIdTypesAndExcludesOtherFields)
{
    const auto id = mongo::OID("00112233445566778899aabb");
    const auto selector = importReplacementSelector(BSON("_id" << id << "value" << 1), true);
    EXPECT_EQ(1, selector.nFields());
    EXPECT_EQ(0, BSON("_id" << id).woCompare(selector));

    const auto longSelector = importReplacementSelector(BSON("_id" << 9007199254740993LL), true);
    EXPECT_EQ(mongo::NumberLong, longSelector["_id"].type());
    EXPECT_EQ(9007199254740993LL, longSelector["_id"].Long());
}

TEST(ImportBatchWriter, NullIdIsAPresentReplacementId)
{
    const auto document = mongo::BSONObjBuilder().appendNull("_id").append("value", 1).obj();
    const auto selector = importReplacementSelector(document, true);
    EXPECT_FALSE(selector.isEmpty());
    EXPECT_EQ(mongo::jstNULL, selector["_id"].type());
}

TEST(ImportBatchWriter, CountsAllAcknowledgedInserts)
{
    DataTransferResult result;
    result.documents = 12;
    applyImportBatchReply(MongoNamespace("db", "items"), 1, 500, reply(500, 0, 0), true, {}, result);
    EXPECT_EQ(512, result.documents);
}

TEST(ImportBatchWriter, CountsMatchesIncludingUnchangedReplacementsAndUpserts)
{
    DataTransferResult result;
    // nModified is zero: all matched documents were already equal to the input.
    applyImportBatchReply(MongoNamespace("db", "items"), 1, 1000, reply(10, 900, 90), true, {}, result);
    EXPECT_EQ(1000, result.documents);
}

TEST(ImportBatchWriter, OrderedWriteErrorCountsPrefixAndMapsOriginalDocumentNumber)
{
    DataTransferResult result;
    result.documents = 25;
    try {
        applyImportBatchReply(MongoNamespace("db", "items"), 101, 10,
                              duplicateAt(reply(1, 2, 1), 4), false, writeError(), result);
        FAIL() << "Expected the ordered write error";
    } catch (const std::runtime_error &error) {
        EXPECT_EQ("items, document 105: duplicate key", std::string(error.what()));
    }
    EXPECT_EQ(29, result.documents);
}

TEST(ImportBatchWriter, FailureAtFirstDocumentCountsNoWrites)
{
    DataTransferResult result;
    EXPECT_THROW(applyImportBatchReply(MongoNamespace("db", "items"), 101, 10,
        duplicateAt(reply(0, 0, 0), 0), false, writeError(), result), std::runtime_error);
    EXPECT_EQ(0, result.documents);
}

TEST(ImportBatchWriter, WriteConcernFailureDoesNotCountPotentiallyAppliedWrites)
{
    DataTransferResult result;
    result.documents = 12;
    const auto response = mongo::BSONObjBuilder().appendElements(reply(2, 3, 5))
        .append("writeConcernErrors", BSON_ARRAY(BSON("code" << 64 << "errmsg" << "timed out"))).obj();
    bson_error_t error{};
    bson_set_error(&error, MONGOC_ERROR_WRITE_CONCERN, 64, "timed out");
    EXPECT_THROW(applyImportBatchReply(MongoNamespace("db", "items"), 1, 10,
                                      response, false, error, result), std::runtime_error);
    EXPECT_EQ(12, result.documents);
}

TEST(ImportBatchWriter, WriteConcernFailureTakesPriorityOverPartialWriteErrors)
{
    DataTransferResult result;
    const auto response = mongo::BSONObjBuilder().appendElements(duplicateAt(reply(2, 0, 0), 2))
        .append("writeConcernErrors", BSON_ARRAY(BSON("code" << 64 << "errmsg" << "timed out"))).obj();
    EXPECT_THROW(applyImportBatchReply(MongoNamespace("db", "items"), 1, 10,
                                      response, false, writeError(), result), std::runtime_error);
    EXPECT_EQ(0, result.documents);
}

TEST(ImportBatchWriter, TransportFailureDoesNotCountUncertainWrites)
{
    DataTransferResult result;
    bson_error_t error{};
    bson_set_error(&error, MONGOC_ERROR_STREAM, MONGOC_ERROR_STREAM_SOCKET, "connection lost");
    EXPECT_THROW(applyImportBatchReply(MongoNamespace("db", "items"), 1, 10,
                                      reply(10, 0, 0), false, error, result), std::runtime_error);
    EXPECT_EQ(0, result.documents);
}

TEST(ImportBatchWriter, CommandFailureDoesNotCountUncertainWrites)
{
    DataTransferResult result;
    bson_error_t error{};
    bson_set_error(&error, MONGOC_ERROR_SERVER, 13, "not authorized");
    EXPECT_THROW(applyImportBatchReply(MongoNamespace("db", "items"), 1, 10,
                                      reply(4, 0, 0), false, error, result), std::runtime_error);
    EXPECT_EQ(0, result.documents);
}

TEST(ImportBatchWriter, MissingOrOversizedCountsAreRejectedWithoutChangingTotals)
{
    DataTransferResult result;
    result.documents = 12;
    EXPECT_THROW(applyImportBatchReply(MongoNamespace("db", "items"), 1, 10,
                                      BSON("nInserted" << 10), true, {}, result), std::runtime_error);
    EXPECT_THROW(applyImportBatchReply(MongoNamespace("db", "items"), 1, 10,
                                      reply(5, 5, 1), true, {}, result), std::runtime_error);
    EXPECT_EQ(12, result.documents);
}

TEST(ImportBatchWriter, InvalidOrderedErrorPositionDoesNotCountUncertainWrites)
{
    DataTransferResult result;
    EXPECT_THROW(applyImportBatchReply(MongoNamespace("db", "items"), 1, 10,
        duplicateAt(reply(2, 0, 0), 4), false, writeError(), result), std::runtime_error);
    EXPECT_EQ(0, result.documents);
}
