#include "robomongo/core/mongodb/DataTransfer.h"

#include "robomongo/core/mongodb/JsonDocumentReader.h"
#include "robomongo/core/mongodb/ImportBatchWriter.h"
#include "robomongo/core/mongodb/MongoConnection.h"
#include "robomongo/core/domain/MongoNamespace.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace Robomongo
{
namespace
{
    const QString ManifestName = QStringLiteral("robomongo-manifest.json");
    const QString ManifestFormat = QStringLiteral("robomongo-jsonl");
    constexpr qint64 MaxManifestBytes = 16 * 1024 * 1024;
    constexpr std::size_t MaxImportBatchDocuments = 2000;
    constexpr qint64 MaxImportBatchBytes = 8 * 1024 * 1024;

    struct Cancelled {};

    void fail(const QString &message)
    {
        throw std::runtime_error(message.toUtf8().constData());
    }

    void checkCancelled(const std::atomic_bool &cancelled)
    {
        if (cancelled.load(std::memory_order_relaxed))
            throw Cancelled();
    }

    void validateCollectionName(const QString &name)
    {
        if (name.isEmpty() || name.contains(QChar(0)) || name.contains(QLatin1Char('$'))
            || name.startsWith(QStringLiteral("system.")))
            fail(QStringLiteral("Unsupported collection name: %1").arg(name));
    }

    void validateFile(const QString &path)
    {
        const QFileInfo info(path);
        if (info.isSymLink() || !info.isFile() || !info.isReadable())
            fail(QStringLiteral("Input must be a readable regular file, not a symbolic link: %1").arg(path));
    }

    struct Entry
    {
        QString name;
        QString path;
        qint64 validatedSize = -1;
        QDateTime validatedModified;
    };

    class Reporter
    {
    public:
        explicit Reporter(const std::function<void(const DataTransferProgress &)> &callback)
            : _callback(callback) { _timer.start(); }

        bool due() const { return _callback && _timer.elapsed() >= 100; }

        void send(const Entry &entry, qint64 documents, int done, int total,
                  const QString &message, bool force = false)
        {
            if (_callback && (force || _timer.elapsed() >= 100)) {
                _callback({entry.name, documents, done, total, message});
                _timer.restart();
            }
        }

    private:
        const std::function<void(const DataTransferProgress &)> &_callback;
        QElapsedTimer _timer;
    };

    void writeBytes(QSaveFile &file, const char *data, qint64 size)
    {
        if (file.write(data, size) != size)
            fail(QStringLiteral("Could not write %1: %2").arg(file.fileName(), file.errorString()));
    }

    void commitFile(QSaveFile &file)
    {
        if (!file.commit())
            fail(QStringLiteral("Could not save %1: %2").arg(file.fileName(), file.errorString()));
    }

    std::vector<Entry> importEntries(const DataTransferOptions &options)
    {
        if (!options.collection.isEmpty()) {
            validateCollectionName(options.collection);
            validateFile(options.path);
            return {{options.collection, options.path}};
        }

        const QFileInfo directoryInfo(options.path);
        if (directoryInfo.isSymLink() || !directoryInfo.isDir() || !directoryInfo.isReadable())
            fail(QStringLiteral("Select a readable database directory, not a symbolic link."));
        const QDir directory(options.path);
        const QString manifestPath = directory.filePath(ManifestName);
        std::vector<Entry> entries;
        if (QFileInfo::exists(manifestPath)) {
            validateFile(manifestPath);
            QFile manifest(manifestPath);
            if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > MaxManifestBytes)
                fail(QStringLiteral("Could not read the database manifest, or it is too large."));
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                fail(QStringLiteral("Invalid database manifest: %1").arg(parseError.errorString()));
            const QJsonObject object = document.object();
            if (object.value(QStringLiteral("format")).toString() != ManifestFormat
                || object.value(QStringLiteral("version")).toDouble() != 1
                || !object.value(QStringLiteral("collections")).isArray())
                fail(QStringLiteral("Unsupported database manifest format or version."));
            for (const QJsonValue &value : object.value(QStringLiteral("collections")).toArray()) {
                if (!value.isObject())
                    fail(QStringLiteral("Invalid collection entry in database manifest."));
                const QJsonObject item = value.toObject();
                const QString name = item.value(QStringLiteral("name")).toString();
                const QString file = item.value(QStringLiteral("file")).toString();
                if (file.isEmpty() || file == QStringLiteral(".") || file == QStringLiteral("..")
                    || file == ManifestName || file.contains(QLatin1Char('/'))
                    || file.contains(QLatin1Char('\\')) || file.contains(QLatin1Char(':'))
                    || file.contains(QChar(0)) || QDir::isAbsolutePath(file))
                    fail(QStringLiteral("Invalid data filename in database manifest."));
                entries.push_back({name, directory.filePath(file)});
            }
        } else {
            const QFileInfoList files = directory.entryInfoList(
                QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &file : files) {
                const QString suffix = file.suffix().toLower();
                if (suffix == QStringLiteral("json") || suffix == QStringLiteral("jsonl")
                    || suffix == QStringLiteral("ndjson"))
                    entries.push_back({file.completeBaseName(), file.absoluteFilePath()});
            }
            if (entries.empty())
                fail(QStringLiteral("The directory contains no .json, .jsonl or .ndjson collection files."));
        }

        QSet<QString> names;
        QSet<QString> paths;
        for (const Entry &entry : entries) {
            validateCollectionName(entry.name);
            validateFile(entry.path);
            const QString canonicalPath = QFileInfo(entry.path).canonicalFilePath();
            if (names.contains(entry.name) || paths.contains(canonicalPath))
                fail(QStringLiteral("Duplicate collection name or file in the database import: %1").arg(entry.name));
            names.insert(entry.name);
            paths.insert(canonicalPath);
        }
        return entries;
    }

    void validateImport(std::vector<Entry> &entries, const std::atomic_bool &cancelled,
                        Reporter &reporter)
    {
        for (Entry &entry : entries) {
            checkCancelled(cancelled);
            validateFile(entry.path);
            const QFileInfo before(entry.path);
            const qint64 beforeSize = before.size();
            const QDateTime beforeModified = before.lastModified();
            JsonDocumentReader reader(entry.path);
            reporter.send(entry, 0, 0, static_cast<int>(entries.size()),
                          QStringLiteral("Validating input"), true);
            mongo::BSONObj document;
            while (reader.next(document)) {
                checkCancelled(cancelled);
                if (reporter.due())
                    reporter.send(entry, 0, 0, static_cast<int>(entries.size()),
                                  QStringLiteral("Validating input: document %1").arg(reader.documentNumber()));
            }
            const QFileInfo after(entry.path);
            if (beforeSize != after.size() || beforeModified != after.lastModified())
                fail(QStringLiteral("Input changed while it was being validated: %1").arg(entry.path));
            entry.validatedSize = after.size();
            entry.validatedModified = after.lastModified();
        }
    }

    void importData(mongo::DBClientBase &connection, const DataTransferOptions &options,
                    const std::atomic_bool &cancelled, Reporter &reporter, DataTransferResult &result)
    {
        std::vector<Entry> entries = importEntries(options);
        if (options.validateBeforeImport) {
            validateImport(entries, cancelled, reporter);
        } else {
            // Snapshot metadata without decoding the entire input twice.
            // Each document is still fully validated before being queued.
            for (Entry &entry : entries) {
                checkCancelled(cancelled);
                const QFileInfo info(entry.path);
                entry.validatedSize = info.size();
                entry.validatedModified = info.lastModified();
            }
        }
        checkCancelled(cancelled);

        const std::string database = options.database.toStdString();
        QSet<QString> existing;
        QSet<QString> views;
        for (const mongo::BSONObj &info : connection.getCollectionInfos(database)) {
            const QString name = QString::fromUtf8(info.getStringField("name"));
            existing.insert(name);
            if (QString::fromUtf8(info.getStringField("type")) == QStringLiteral("view"))
                views.insert(name);
        }
        // Reject a read-only destination before importing any other collection.
        for (const Entry &entry : entries) {
            if (views.contains(entry.name))
                fail(QStringLiteral("Cannot import documents into the view %1.").arg(entry.name));
        }

        for (const Entry &entry : entries) {
            checkCancelled(cancelled);
            validateFile(entry.path);
            const QFileInfo current(entry.path);
            if (current.size() != entry.validatedSize || current.lastModified() != entry.validatedModified)
                fail(QStringLiteral("Input changed after import preparation: %1").arg(entry.path));
            JsonDocumentReader reader(entry.path);
            const MongoNamespace ns(database, entry.name.toStdString());
            if (!existing.contains(entry.name)) {
                mongo::BSONObj reply;
                if (!connection.createCollection(ns.toString(), 0, false, 0, &reply))
                    fail(QStringLiteral("Could not create %1: %2").arg(entry.name,
                         QString::fromUtf8(reply.getStringField("errmsg"))));
                existing.insert(entry.name);
            }
            reporter.send(entry, result.documents, result.collections, static_cast<int>(entries.size()),
                          QStringLiteral("Importing documents"), true);
            std::vector<mongo::BSONObj> batch;
            batch.reserve(MaxImportBatchDocuments);
            qint64 batchBytes = 0;
            qint64 firstBatchDocument = 0;
            const auto flushBatch = [&]() {
                if (batch.empty())
                    return;
                checkCancelled(cancelled);
                detail::writeImportBatch(connection, ns, batch, options.upsert, firstBatchDocument, result);
                batch.clear();
                batchBytes = 0;
                reporter.send(entry, result.documents, result.collections, static_cast<int>(entries.size()),
                              QStringLiteral("Importing documents"));
            };
            mongo::BSONObj document;
            while (reader.next(document)) {
                checkCancelled(cancelled);
                // The driver splits commands to fit server limits. Bound our
                // queue as well; an oversized valid document travels alone.
                const qint64 bytes = static_cast<qint64>(document.objsize()) + 64;
                if (batch.size() >= MaxImportBatchDocuments
                    || (!batch.empty() && batchBytes + bytes > MaxImportBatchBytes))
                    flushBatch();
                if (batch.empty())
                    firstBatchDocument = reader.documentNumber();
                batch.push_back(document);
                batchBytes += bytes;
            }
            flushBatch();
            ++result.collections;
            reporter.send(entry, result.documents, result.collections, static_cast<int>(entries.size()),
                          QStringLiteral("Collection imported"), true);
        }
    }

    qint64 exportCollection(mongo::DBClientBase &connection, const QString &database, const Entry &entry,
                            const std::atomic_bool &cancelled, Reporter &reporter,
                            const DataTransferResult &result, int total)
    {
        checkCancelled(cancelled);
        if (QFileInfo(entry.path).isSymLink())
            fail(QStringLiteral("Export destination cannot be a symbolic link: %1").arg(entry.path));
        QSaveFile file(entry.path);
        // Keep QSaveFile's default atomic-only behavior; never fall back to
        // directly truncating an existing destination on open/commit failure.
        if (!file.open(QIODevice::WriteOnly))
            fail(QStringLiteral("Could not open %1: %2").arg(entry.path, file.errorString()));
        const auto cursor = connection.find(
            mongo::NamespaceString(database.toStdString(), entry.name.toStdString()),
            mongo::BSONObj(), BSON("batchSize" << 500));
        qint64 count = 0;
        reporter.send(entry, result.documents, result.collections, total,
                      QStringLiteral("Exporting documents"), true);
        while (true) {
            checkCancelled(cancelled);
            if (!cursor->more())
                break;
            const std::string json = cursor->next().toExtendedJson(true);
            writeBytes(file, json.data(), static_cast<qint64>(json.size()));
            writeBytes(file, "\n", 1);
            ++count;
            reporter.send(entry, result.documents + count, result.collections, total,
                          QStringLiteral("Exporting documents"));
        }
        checkCancelled(cancelled);
        commitFile(file);
        return count;
    }

    void exportData(mongo::DBClientBase &connection, const DataTransferOptions &options,
                    const std::atomic_bool &cancelled, Reporter &reporter, DataTransferResult &result)
    {
        if (!options.collection.isEmpty()) {
            validateCollectionName(options.collection);
            if (!connection.exists(MongoNamespace(options.database.toStdString(),
                                                  options.collection.toStdString()).toString()))
                fail(QStringLiteral("The source collection no longer exists."));
            const Entry entry{options.collection, options.path};
            result.documents = exportCollection(connection, options.database, entry,
                                                cancelled, reporter, result, 1);
            result.collections = 1;
            return;
        }

        std::vector<Entry> entries;
        for (const mongo::BSONObj &info : connection.getCollectionInfos(options.database.toStdString())) {
            const QString name = QString::fromUtf8(info.getStringField("name"));
            if (name.startsWith(QStringLiteral("system."))
                || QString::fromUtf8(info.getStringField("type")) == QStringLiteral("view"))
                continue;
            validateCollectionName(name);
            entries.push_back({name, {}});
        }
        std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
            return left.name < right.name;
        });

        const QFileInfo destination(options.path);
        if (destination.isSymLink() || (destination.exists() && !destination.isDir()))
            fail(QStringLiteral("Database export destination must be a directory, not a symbolic link."));
        const bool createdDirectory = !destination.exists();
        QDir directory(options.path);
        if (createdDirectory && !QDir().mkdir(options.path))
            fail(QStringLiteral("Could not create the export directory. Its parent must already exist."));
        if (!directory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty())
            fail(QStringLiteral("Database export requires an empty directory."));

        QStringList createdFiles;
        try {
            QJsonArray manifestEntries;
            for (Entry &entry : entries) {
                checkCancelled(cancelled);
                const QString filename = QStringLiteral("collection-%1.jsonl")
                    .arg(result.collections + 1, 6, 10, QLatin1Char('0'));
                entry.path = directory.filePath(filename);
                if (QFileInfo::exists(entry.path))
                    fail(QStringLiteral("The export directory changed during export."));
                const qint64 count = exportCollection(connection, options.database, entry,
                                                      cancelled, reporter, result,
                                                      static_cast<int>(entries.size()));
                createdFiles.append(entry.path);
                result.documents += count;
                ++result.collections;
                manifestEntries.append(QJsonObject{{QStringLiteral("name"), entry.name},
                                                    {QStringLiteral("file"), filename}});
                reporter.send(entry, result.documents, result.collections, static_cast<int>(entries.size()),
                              QStringLiteral("Collection exported"), true);
            }
            checkCancelled(cancelled);
            const QJsonObject manifest{{QStringLiteral("format"), ManifestFormat},
                                       {QStringLiteral("version"), 1},
                                       {QStringLiteral("database"), options.database},
                                       {QStringLiteral("collections"), manifestEntries}};
            const QByteArray bytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
            if (bytes.size() > MaxManifestBytes)
                fail(QStringLiteral("The database manifest exceeds the supported 16 MiB limit."));
            QSaveFile file(directory.filePath(ManifestName));
            if (QFileInfo::exists(file.fileName()) || !file.open(QIODevice::WriteOnly))
                fail(QStringLiteral("Could not create the database manifest: %1").arg(file.errorString()));
            writeBytes(file, bytes.constData(), bytes.size());
            checkCancelled(cancelled);
            commitFile(file);
        } catch (...) {
            // Only files committed by this operation are ours to remove.
            for (const QString &file : createdFiles)
                QFile::remove(file);
            if (createdDirectory)
                QDir().rmdir(options.path);
            result.documents = 0;
            result.collections = 0;
            throw;
        }
    }
}

DataTransferResult runDataTransfer(
    mongo::DBClientBase &connection, const DataTransferOptions &options,
    const std::atomic_bool &cancelled,
    const std::function<void(const DataTransferProgress &)> &progress)
{
    DataTransferResult result;
    try {
        checkCancelled(cancelled);
        if (options.database.isEmpty() || options.database.contains(QChar(0)) || options.path.isEmpty())
            fail(QStringLiteral("A database and a file or directory path are required."));
        Reporter reporter(progress);
        if (options.direction == DataTransferDirection::Import)
            importData(connection, options, cancelled, reporter, result);
        else
            exportData(connection, options, cancelled, reporter, result);
        result.success = true;
    } catch (const Cancelled &) {
        result.cancelled = true;
    } catch (const std::exception &error) {
        result.cancelled = cancelled.load(std::memory_order_relaxed);
        result.error = QString::fromUtf8(error.what());
    } catch (...) {
        result.cancelled = cancelled.load(std::memory_order_relaxed);
        result.error = QStringLiteral("Unexpected error during data transfer.");
    }
    return result;
}
}
