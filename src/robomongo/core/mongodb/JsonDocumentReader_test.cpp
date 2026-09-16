#include "robomongo/core/mongodb/JsonDocumentReader.h"
#include "robomongo/core/bson/Bson.h"

#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <climits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class JsonInput
{
public:
    explicit JsonInput(const QByteArray &contents)
    {
        if (!_directory.isValid())
            throw std::runtime_error("Cannot create test input directory");
        _path = _directory.filePath(QStringLiteral("input.json"));
        QFile file(_path);
        if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()
                || !file.flush())
            throw std::runtime_error("Cannot create test JSON input");
    }

    QString path() const { return _path; }

private:
    QTemporaryDir _directory;
    QString _path;
};

void readAllDocuments(Robomongo::JsonDocumentReader &reader)
{
    mongo::BSONObj document;
    while (reader.next(document)) {}
}

}

TEST(JsonDocumentReader, EmptyFilesWhitespaceBomAndEmptyArraysAreEmptyCollections)
{
    const std::vector<QByteArray> inputs = {
        QByteArray(), QByteArray(" \r\n\t"), QByteArray::fromHex("efbbbf"),
        QByteArray::fromHex("efbbbf") + " \n[]\t", QByteArray("[]"),
        QByteArray("[ \r\n\t ]")
    };
    for (const auto &input : inputs) {
        JsonInput file(input);
        Robomongo::JsonDocumentReader reader(file.path());
        mongo::BSONObj document = BSON("unchanged" << 7);
        EXPECT_FALSE(reader.next(document));
        EXPECT_FALSE(reader.next(document));
        EXPECT_EQ(0, reader.documentNumber());
        EXPECT_EQ(7, document["unchanged"].Int());
    }
}

TEST(JsonDocumentReader, ReadsMultilineAndWhitespaceSeparatedObjects)
{
    JsonInput file("\n{\n \"n\": 1,\n \"nested\": [{\"n\":2}, []]\n}\r\n"
                   "{\"n\": 3}\t{\"n\": 4}  ");
    Robomongo::JsonDocumentReader reader(file.path());
    mongo::BSONObj document;
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(1, document["n"].Int());
    EXPECT_EQ(mongo::Array, document["nested"].type());
    EXPECT_EQ(1, reader.documentNumber());
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(3, document["n"].Int());
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(4, document["n"].Int());
    EXPECT_FALSE(reader.next(document));
    EXPECT_EQ(3, reader.documentNumber());
}

TEST(JsonDocumentReader, ReadsMultilineArrayAndKeepsEscapedStringDelimiters)
{
    JsonInput file(R"json([
      {"text": "quoted \"{[ ]}\" and \\ slash", "items": [{"a": []}]},
      {
        "n": 2
      }
    ])json");
    Robomongo::JsonDocumentReader reader(file.path());
    mongo::BSONObj document;
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ("quoted \"{[ ]}\" and \\ slash", document["text"].String());
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(2, document["n"].Int());
    EXPECT_FALSE(reader.next(document));
    EXPECT_EQ(2, reader.documentNumber());
}

TEST(JsonDocumentReader, AcceptsUtf8BomAndDirectlyAdjacentObjects)
{
    JsonInput file(QByteArray::fromHex("efbbbf") + "{\"n\":1}{\"n\":2}");
    Robomongo::JsonDocumentReader reader(file.path());
    mongo::BSONObj document;
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(1, document["n"].Int());
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(2, document["n"].Int());
    EXPECT_FALSE(reader.next(document));
    EXPECT_EQ(2, reader.documentNumber());
}

TEST(JsonDocumentReader, CanonicalExtendedJsonPreservesMongoTypesAndIntegerPrecision)
{
    JsonInput file(R"json({
      "maximum": {"$numberLong": "9223372036854775807"},
      "minimum": {"$numberLong": "-9223372036854775808"},
      "date": {"$date": {"$numberLong": "-1234567890123"}},
      "_id": {"$oid": "00112233445566778899aabb"},
      "decimal": {"$numberDecimal": "1234567890123456789012345678901234"}
    })json");
    Robomongo::JsonDocumentReader reader(file.path());
    mongo::BSONObj document;
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(mongo::NumberLong, document["maximum"].type());
    EXPECT_EQ(LLONG_MAX, document["maximum"].Long());
    EXPECT_EQ(LLONG_MIN, document["minimum"].Long());
    EXPECT_EQ(mongo::Date, document["date"].type());
    EXPECT_EQ(-1234567890123LL, document["date"].Date().toMillisSinceEpoch());
    EXPECT_EQ(mongo::jstOID, document["_id"].type());
    EXPECT_EQ("00112233445566778899aabb", document["_id"].OID().toString());
    EXPECT_EQ("1234567890123456789012345678901234",
              document["decimal"].numberDecimal().toString());
    EXPECT_FALSE(reader.next(document));
}

TEST(JsonDocumentReader, EscapeSequencesAndObjectsCanCrossReadChunkBoundaries)
{
    QByteArray json("{\"value\":\"");
    const int padding = 64 * 1024 - json.size() - 1;
    json.append(QByteArray(padding, 'x'));
    json.append("\\\"{}[]\\\\end\"}\n{\"n\":2}");
    JsonInput file(json);
    Robomongo::JsonDocumentReader reader(file.path());
    mongo::BSONObj document;
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(std::string(padding, 'x') + "\"{}[]\\end", document["value"].String());
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(2, document["n"].Int());
    EXPECT_FALSE(reader.next(document));
}

TEST(JsonDocumentReader, ArraySeparatorsAndNestedDelimitersCrossReadChunkBoundaries)
{
    for (int boundaryOffset = -2; boundaryOffset <= 2; ++boundaryOffset) {
        SCOPED_TRACE(boundaryOffset);
        QByteArray json("[{\"value\":\"");
        const int padding = 64 * 1024 - json.size() - 2 + boundaryOffset;
        json.append(QByteArray(padding, 'x'));
        json.append("\"},{\"n\":2,\"nested\":[{\"values\":[1,2]},[]]}]");
        JsonInput file(json);
        Robomongo::JsonDocumentReader reader(file.path());
        mongo::BSONObj document;
        ASSERT_TRUE(reader.next(document));
        const mongo::BSONObj first = document;
        ASSERT_TRUE(reader.next(document));
        EXPECT_EQ(2, document["n"].Int());
        EXPECT_EQ(mongo::Array, document["nested"].type());
        EXPECT_EQ(std::string(padding, 'x'), first["value"].String());
        EXPECT_FALSE(reader.next(document));
        EXPECT_EQ(2, reader.documentNumber());
    }
}

TEST(JsonDocumentReader, EscapedBackslashAcrossReadBoundaryAllowsClosingStringAndNestedObject)
{
    QByteArray json("{\"value\":\"");
    const int padding = 2 * 64 * 1024 - json.size() - 1;
    json.append(QByteArray(padding, 'x'));
    json.append(R"json(\\","nested":[{"n":3}],"n":1}{"n":2})json");
    JsonInput file(json);
    Robomongo::JsonDocumentReader reader(file.path());
    mongo::BSONObj document;
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(std::string(padding, 'x') + "\\", document["value"].String());
    EXPECT_EQ(mongo::Array, document["nested"].type());
    EXPECT_EQ(1, document["n"].Int());
    ASSERT_TRUE(reader.next(document));
    EXPECT_EQ(2, document["n"].Int());
    EXPECT_FALSE(reader.next(document));
}

TEST(JsonDocumentReader, ErrorsAfterMultipleReadSpansKeepExactBytePosition)
{
    for (bool controlCharacter : {false, true}) {
        SCOPED_TRACE(controlCharacter);
        QByteArray json(64 * 1024 + 7, ' ');
        json.append("{\"value\":\"");
        json.append(QByteArray(2 * 64 * 1024 + 3, 'x'));
        if (!controlCharacter)
            json.append("\",\"nested\":[");
        const int badByte = json.size();
        json.append(controlCharacter ? '\n' : '}');
        JsonInput file(json);
        Robomongo::JsonDocumentReader reader(file.path());
        mongo::BSONObj document = BSON("unchanged" << 7);
        try {
            reader.next(document);
            FAIL() << "Expected an input framing error";
        } catch (const std::runtime_error &error) {
            const std::string message(error.what());
            // Diagnostics report the next one-based position after consumption.
            EXPECT_NE(std::string::npos,
                      message.find("byte " + std::to_string(badByte + 2) + ")"));
            EXPECT_NE(std::string::npos, message.find(controlCharacter
                ? "Unescaped control character" : "Mismatched JSON object"));
        }
        EXPECT_EQ(0, reader.documentNumber());
        EXPECT_EQ(7, document["unchanged"].Int());
    }
}

TEST(JsonDocumentReader, RejectsIncompleteArraysBadSeparatorsAndTrailingData)
{
    const std::vector<QByteArray> inputs = {
        "[", "[{}", "[{},", "[{},]", "[{} {}]", "[{},,{}]",
        "[,{}]", "[null]", "[1]", "[\"string\"]", "[[]]",
        "[] {}", "[{}] true", "[{}]{}", "{},{}", "{} nonsense",
        QByteArray("{}\0", 3),
        "{", "{\"a\":", "{\"a\":[}", "{\"a\":1", "{\"a\":\"unfinished}",
        "{\"a\":\"bad\\q\"}", "{\"a\":tru}", "{\"a\":}"
    };
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        SCOPED_TRACE(index);
        JsonInput file(inputs[index]);
        Robomongo::JsonDocumentReader reader(file.path());
        EXPECT_THROW(readAllDocuments(reader), std::runtime_error);
    }
}

TEST(JsonDocumentReader, RejectsMisplacedOrPartialBomAndNonObjectRoots)
{
    const std::vector<QByteArray> inputs = {
        QByteArray::fromHex("ef"), QByteArray::fromHex("efbb"),
        QByteArray(" ") + QByteArray::fromHex("efbbbf") + "{}",
        QByteArray("{}") + QByteArray::fromHex("efbbbf"),
        QByteArray("true"), QByteArray("null"), QByteArray("123"), QByteArray("\"text\"")
    };
    for (const auto &input : inputs) {
        JsonInput file(input);
        Robomongo::JsonDocumentReader reader(file.path());
        EXPECT_THROW(readAllDocuments(reader), std::runtime_error);
    }
}

TEST(JsonDocumentReader, RejectsExcessiveNestingWithoutLargeAllocations)
{
    QByteArray json;
    for (int index = 0; index < 201; ++index)
        json.append("{\"a\":");
    json.append('0');
    json.append(QByteArray(201, '}'));
    JsonInput file(json);
    Robomongo::JsonDocumentReader reader(file.path());
    mongo::BSONObj document;
    EXPECT_THROW(reader.next(document), std::runtime_error);
    EXPECT_EQ(0, reader.documentNumber());
}

TEST(JsonDocumentReader, ErrorsRetainLastDocumentAndReportPositionWithoutItsContents)
{
    JsonInput file("{\"n\":1}\n{\"secret\":\"private-value\",\"broken\":}");
    Robomongo::JsonDocumentReader reader(file.path());
    mongo::BSONObj document;
    ASSERT_TRUE(reader.next(document));
    try {
        reader.next(document);
        FAIL() << "Expected a JSON parsing error";
    } catch (const std::runtime_error &error) {
        const std::string message(error.what());
        EXPECT_NE(std::string::npos, message.find("input.json"));
        EXPECT_NE(std::string::npos, message.find("document 2"));
        EXPECT_NE(std::string::npos, message.find("byte "));
        EXPECT_EQ(std::string::npos, message.find("private-value"));
    }
    EXPECT_EQ(1, reader.documentNumber());
    EXPECT_EQ(1, document["n"].Int());
    EXPECT_THROW(reader.next(document), std::runtime_error);
}

TEST(JsonDocumentReader, MissingInputFileReportsItsPath)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("missing.json"));
    try {
        Robomongo::JsonDocumentReader reader(path);
        FAIL() << "Expected an input open error";
    } catch (const std::runtime_error &error) {
        EXPECT_NE(std::string::npos, std::string(error.what()).find("missing.json"));
    }
}
