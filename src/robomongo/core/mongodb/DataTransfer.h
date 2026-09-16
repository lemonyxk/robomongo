#pragma once

#include <QString>
#include <QtGlobal>
#include <atomic>
#include <functional>

namespace mongo { class DBClientBase; }

namespace Robomongo
{
    enum class DataTransferDirection { Import, Export };

    struct DataTransferOptions
    {
        DataTransferDirection direction = DataTransferDirection::Export;
        QString database;
        // Empty means all ordinary collections in a database directory.
        QString collection;
        QString path;
        bool upsert = false;
        // Stream by default; an optional full preflight parses every file before
        // any write, at the cost of reading and decoding the input twice.
        bool validateBeforeImport = false;
    };

    struct DataTransferProgress
    {
        QString collection;
        qint64 documents = 0;
        int collectionsDone = 0;
        int collectionsTotal = 0;
        QString message;
    };

    struct DataTransferResult
    {
        bool success = false;
        bool cancelled = false;
        qint64 documents = 0;
        int collections = 0;
        QString error;
    };

    // Runs synchronously on the caller's worker thread; never shares a driver
    // connection with another thread. Import does not drop data or alter indexes.
    DataTransferResult runDataTransfer(
        mongo::DBClientBase &connection, const DataTransferOptions &options,
        const std::atomic_bool &cancelled,
        const std::function<void(const DataTransferProgress &)> &progress);
}
