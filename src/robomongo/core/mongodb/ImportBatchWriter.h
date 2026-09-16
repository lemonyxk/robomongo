#pragma once

#include "robomongo/core/bson/Bson.h"
#include <QtGlobal>
#include <QString>
#include <vector>

namespace mongo { class DBClientBase; }

namespace Robomongo
{
    class MongoNamespace;
    struct DataTransferResult;

namespace detail
{
    // An empty selector means insert. A present _id, including null, means
    // replace that document when upsert is enabled.
    mongo::BSONObj importReplacementSelector(const mongo::BSONObj &document, bool upsert);

    // Kept separate from I/O so acknowledgement and partial failure handling can
    // be checked without a database. Throws after recording confirmed writes.
    void applyImportBatchReply(const MongoNamespace &ns, qint64 firstDocument,
                              qint64 documentCount, const mongo::BSONObj &reply,
                              bool succeeded, const bson_error_t &error,
                              DataTransferResult &result);

    // Sends one ordered bulk operation, inheriting the connection's write
    // concern. The driver splits wire messages to the server's size limits.
    void writeImportBatch(mongo::DBClientBase &connection, const MongoNamespace &ns,
                          const std::vector<mongo::BSONObj> &documents, bool upsert,
                          qint64 firstDocument, DataTransferResult &result);
}
}
