#include "robomongo/core/mongodb/DataTransfer.h"
#include "robomongo/core/mongodb/MongoConnection.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

namespace
{
    class NoWriteConnection : public mongo::DBClientBase
    {
    public:
        int writes = 0;
        bool runCommand(const std::string &, const mongo::BSONObj &, mongo::BSONObj &, int = 0) override
        {
            ++writes;
            throw std::runtime_error("Import validation must finish before database access");
        }
    };

    class DataTransferValidation : public testing::Test
    {
    protected:
        QTemporaryDir directory;
        NoWriteConnection connection;
        std::atomic_bool cancelled{false};

        void SetUp() override { ASSERT_TRUE(directory.isValid()); }

        QString write(const QString &name, const QByteArray &contents)
        {
            const QString path = directory.filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()
                || !file.flush())
                throw std::runtime_error("Cannot create data-transfer test input");
            return path;
        }

        void manifest(const QJsonArray &collections, int version = 1)
        {
            const QJsonObject object{{QStringLiteral("format"), QStringLiteral("robomongo-jsonl")},
                                     {QStringLiteral("version"), version},
                                     {QStringLiteral("collections"), collections}};
            write(QStringLiteral("robomongo-manifest.json"), QJsonDocument(object).toJson());
        }

        Robomongo::DataTransferResult run(const QString &collection = {}, const QString &path = {})
        {
            Robomongo::DataTransferOptions options;
            options.direction = Robomongo::DataTransferDirection::Import;
            options.validateBeforeImport = true;
            options.database = QStringLiteral("target");
            options.collection = collection;
            options.path = path.isEmpty() ? directory.path() : path;
            return Robomongo::runDataTransfer(connection, options, cancelled, {});
        }

        void expectRejected(const Robomongo::DataTransferResult &result, const QString &message)
        {
            EXPECT_FALSE(result.success);
            EXPECT_FALSE(result.cancelled);
            EXPECT_TRUE(result.error.contains(message)) << result.error.toStdString();
            EXPECT_EQ(0, result.documents);
            EXPECT_EQ(0, result.collections);
            EXPECT_EQ(0, connection.writes);
        }
    };

    QJsonObject entry(const QString &name, const QString &file)
    {
        return {{QStringLiteral("name"), name}, {QStringLiteral("file"), file}};
    }
}

TEST_F(DataTransferValidation, MalformedLaterDocumentRejectsWholeCollectionBeforeWriting)
{
    const QString path = write(QStringLiteral("documents.jsonl"), "{\"_id\":1}\n{\"_id\":2");
    expectRejected(run(QStringLiteral("documents"), path), QStringLiteral("document 2"));
}

TEST_F(DataTransferValidation, TrailingArrayDataRejectsWholeCollectionBeforeWriting)
{
    const QString path = write(QStringLiteral("documents.json"), "[{\"_id\":1}] trailing");
    expectRejected(run(QStringLiteral("documents"), path), QStringLiteral("after the JSON array"));
}

TEST_F(DataTransferValidation, MalformedLaterCollectionPreventsWritesToEarlierValidCollection)
{
    write(QStringLiteral("a.jsonl"), "{\"_id\":1}\n");
    write(QStringLiteral("z.jsonl"), "{\"_id\":");
    expectRejected(run(), QStringLiteral("z.jsonl"));
}

TEST_F(DataTransferValidation, NdjsonFilesAreValidatedInDatabaseDirectories)
{
    write(QStringLiteral("orders.history.ndjson"), "{\"_id\":1}\n{\"_id\":");
    expectRejected(run(), QStringLiteral("orders.history.ndjson"));
}

TEST_F(DataTransferValidation, ManifestPathsCannotEscapeOrReferenceTheManifest)
{
    const std::vector<QString> files = {
        QStringLiteral("../outside.json"), QStringLiteral("nested/data.json"),
        QStringLiteral("nested\\data.json"), QStringLiteral("/tmp/data.json"),
        QStringLiteral("C:data.json"), QStringLiteral("."), QStringLiteral(".."),
        QStringLiteral("robomongo-manifest.json")
    };
    for (const QString &file : files) {
        SCOPED_TRACE(file.toStdString());
        manifest(QJsonArray{entry(QStringLiteral("documents"), file)});
        expectRejected(run(), QStringLiteral("Invalid data filename"));
    }
}

TEST_F(DataTransferValidation, DuplicateCollectionNamesAreRejectedBeforeReadingDocuments)
{
    write(QStringLiteral("one.jsonl"), "{}\n");
    write(QStringLiteral("two.jsonl"), "{}\n");
    manifest(QJsonArray{entry(QStringLiteral("documents"), QStringLiteral("one.jsonl")),
                        entry(QStringLiteral("documents"), QStringLiteral("two.jsonl"))});
    expectRejected(run(), QStringLiteral("Duplicate collection"));
}

TEST_F(DataTransferValidation, OneInputFileCannotBeAssignedToTwoCollections)
{
    write(QStringLiteral("one.jsonl"), "{}\n");
    manifest(QJsonArray{entry(QStringLiteral("one"), QStringLiteral("one.jsonl")),
                        entry(QStringLiteral("two"), QStringLiteral("one.jsonl"))});
    expectRejected(run(), QStringLiteral("Duplicate collection name or file"));
}

TEST_F(DataTransferValidation, MissingManifestDataFilesAreRejectedBeforeWriting)
{
    manifest(QJsonArray{entry(QStringLiteral("documents"), QStringLiteral("missing.jsonl"))});
    expectRejected(run(), QStringLiteral("readable regular file"));
}

TEST_F(DataTransferValidation, UnsupportedManifestVersionsAreRejected)
{
    manifest(QJsonArray{}, 2);
    expectRejected(run(), QStringLiteral("Unsupported database manifest"));
}

TEST_F(DataTransferValidation, OversizedManifestIsRejectedBeforeParsingOrDatabaseAccess)
{
    write(QStringLiteral("robomongo-manifest.json"), QByteArray(16 * 1024 * 1024 + 1, ' '));
    expectRejected(run(), QStringLiteral("too large"));
}

#ifdef Q_OS_UNIX
TEST_F(DataTransferValidation, SymbolicLinkInputFilesAreRejectedBeforeWriting)
{
    const QString target = write(QStringLiteral("source.jsonl"), "{}\n");
    const QString link = directory.filePath(QStringLiteral("link.jsonl"));
    ASSERT_TRUE(QFile::link(target, link));
    expectRejected(run(QStringLiteral("documents"), link), QStringLiteral("symbolic link"));
}
#endif

TEST_F(DataTransferValidation, InitialCancellationStopsBeforeFilesystemOrDatabaseAccess)
{
    cancelled.store(true);
    const auto result = run(QStringLiteral("documents"), directory.filePath(QStringLiteral("missing.jsonl")));
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
    EXPECT_TRUE(result.error.isEmpty());
    EXPECT_EQ(0, result.documents);
    EXPECT_EQ(0, connection.writes);
}
