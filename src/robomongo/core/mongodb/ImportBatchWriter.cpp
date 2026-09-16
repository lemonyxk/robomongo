#include "robomongo/core/mongodb/ImportBatchWriter.h"

#include "robomongo/core/domain/MongoNamespace.h"
#include "robomongo/core/mongodb/DataTransfer.h"
#include "robomongo/core/mongodb/MongoConnection.h"

#include <memory>
#include <stdexcept>

namespace Robomongo
{
namespace detail
{
namespace
{
    void fail(const QString &message)
    {
        throw std::runtime_error(message.toUtf8().constData());
    }

    bool integer(const mongo::BSONElement &value)
    {
        return value.type() == mongo::NumberInt || value.type() == mongo::NumberLong;
    }

    bool hasErrors(const mongo::BSONElement &value)
    {
        return !value.eoo() && (value.type() != mongo::Array || !value.Obj().isEmpty());
    }
}

    mongo::BSONObj importReplacementSelector(const mongo::BSONObj &document, bool upsert)
    {
        if (upsert) {
            const mongo::BSONElement id = document.getField("_id");
            if (!id.eoo())
                return mongo::BSONObjBuilder().append(id).obj();
        }
        return mongo::BSONObj();
    }

    void applyImportBatchReply(const MongoNamespace &ns, qint64 firstDocument,
                              qint64 documentCount, const mongo::BSONObj &reply,
                              bool succeeded, const bson_error_t &error,
                              DataTransferResult &result)
    {
        const QString collection = QString::fromStdString(ns.collectionName());
        const QString context = QStringLiteral("%1, documents %2-%3")
            .arg(collection).arg(firstDocument).arg(firstDocument + documentCount - 1);
        const QString driverError = error.message[0]
            ? QString::fromUtf8(error.message) : QStringLiteral("Bulk write failed");

        // A bulk reply can aggregate several wire commands. If any required
        // acknowledgement failed, its counters cannot identify which writes
        // satisfied that concern. Do not count uncertain writes as imported.
        if (error.domain == MONGOC_ERROR_WRITE_CONCERN
            || hasErrors(reply.getField("writeConcernErrors"))
            || !reply.getField("writeConcernError").eoo())
            fail(QStringLiteral("%1: write concern failed: %2. This batch may have been applied, "
                                "but was not acknowledged at the required write concern.")
                 .arg(context, driverError));

        const mongo::BSONElement errors = reply.getField("writeErrors");
        if ((!errors.eoo() && errors.type() != mongo::Array)
            || hasErrors(reply.getField("errorReplies")))
            fail(QStringLiteral("%1: %2. This batch's write outcome may be unknown.")
                 .arg(context, driverError));

        const bool writeErrors = !errors.eoo() && !errors.Obj().isEmpty();
        // The application's client uses error API v2. A structured server write
        // error gives an acknowledged ordered prefix; transport/command errors
        // do not. Never retry the application batch after either kind of error.
        if (!succeeded && (!writeErrors || error.domain != MONGOC_ERROR_SERVER))
            fail(QStringLiteral("%1: %2. This batch's write outcome may be unknown.")
                 .arg(context, driverError));

        qint64 confirmed = 0;
        for (const char *field : {"nInserted", "nMatched", "nUpserted"}) {
            const mongo::BSONElement count = reply.getField(field);
            if (!integer(count) || count.numberLong() < 0
                || count.numberLong() > documentCount - confirmed)
                fail(QStringLiteral("%1: the server did not confirm how many documents were written. "
                                    "This batch's write outcome is unknown.").arg(context));
            confirmed += count.numberLong();
        }

        if (writeErrors) {
            const std::vector<mongo::BSONElement> details = errors.Array();
            if (details.size() != 1 || details.front().type() != mongo::Object)
                fail(QStringLiteral("%1: invalid ordered write error response.").arg(context));
            const mongo::BSONObj detail = details.front().Obj();
            const mongo::BSONElement index = detail.getField("index");
            if (!integer(index) || index.numberLong() < 0 || index.numberLong() >= documentCount
                || index.numberLong() != confirmed)
                fail(QStringLiteral("%1: invalid ordered write error position; "
                                    "this batch's write outcome is unknown.").arg(context));
            result.documents += confirmed;
            const QString message = QString::fromUtf8(detail.getStringField("errmsg"));
            fail(QStringLiteral("%1, document %2: %3")
                 .arg(collection).arg(firstDocument + index.numberLong())
                 .arg(message.isEmpty() ? driverError : message));
        }

        // Count matched replacements even when nModified is zero (same data).
        result.documents += confirmed;
        if (confirmed != documentCount)
            fail(QStringLiteral("%1: only %2 of %3 documents were confirmed written.")
                 .arg(context).arg(confirmed).arg(documentCount));
    }

    void writeImportBatch(mongo::DBClientBase &connection, const MongoNamespace &ns,
                          const std::vector<mongo::BSONObj> &documents, bool upsert,
                          qint64 firstDocument, DataTransferResult &result)
    {
        if (documents.empty())
            return;
        if (!connection.raw())
            fail(QStringLiteral("Cannot import without a database connection."));

        using Collection = std::unique_ptr<mongoc_collection_t, decltype(&mongoc_collection_destroy)>;
        const Collection collection(mongoc_client_get_collection(
            connection.raw(), ns.databaseName().c_str(), ns.collectionName().c_str()),
            mongoc_collection_destroy);
        if (!collection)
            fail(QStringLiteral("Could not prepare the import collection."));
        if (!mongoc_write_concern_is_acknowledged(mongoc_collection_get_write_concern(collection.get())))
            fail(QStringLiteral("Import requires acknowledged writes; the connection's write concern is unacknowledged."));

        const mongo::BSONObj options = BSON("ordered" << true);
        using Bulk = std::unique_ptr<mongoc_bulk_operation_t, decltype(&mongoc_bulk_operation_destroy)>;
        const Bulk bulk(mongoc_collection_create_bulk_operation_with_opts(collection.get(), options.raw()),
                        mongoc_bulk_operation_destroy);
        if (!bulk)
            fail(QStringLiteral("Could not prepare the import batch."));

        const mongo::BSONObj replaceOptions = BSON("upsert" << true);
        for (std::size_t index = 0; index < documents.size(); ++index) {
            const mongo::BSONObj &document = documents[index];
            bson_error_t error{};
            // Keep adjacent inserts/replacements in their original order. The
            // driver groups compatible operations without reordering the batch,
            // so repeated _id values retain last-document-wins behavior.
            bool queued;
            if (upsert && document.hasField("_id")) {
                const mongo::BSONObj selector = importReplacementSelector(document, true);
                queued = mongoc_bulk_operation_replace_one_with_opts(
                    bulk.get(), selector.raw(), document.raw(), replaceOptions.raw(), &error);
            } else {
                queued = mongoc_bulk_operation_insert_with_opts(bulk.get(), document.raw(), nullptr, &error);
            }
            if (!queued)
                fail(QStringLiteral("%1, document %2: %3. No documents in this batch were sent.")
                     .arg(QString::fromStdString(ns.collectionName()))
                     .arg(firstDocument + static_cast<qint64>(index)).arg(QString::fromUtf8(error.message)));
        }

        bson_t rawReply;
        bson_error_t error{};
        const bool succeeded = mongoc_bulk_operation_execute(bulk.get(), &rawReply, &error) != 0;
        const std::unique_ptr<bson_t, decltype(&bson_destroy)> replyGuard(&rawReply, bson_destroy);
        applyImportBatchReply(ns, firstDocument, static_cast<qint64>(documents.size()),
                              mongo::BSONObj(&rawReply), succeeded, error, result);
    }
}
}
