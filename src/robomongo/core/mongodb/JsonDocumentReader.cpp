#include "robomongo/core/mongodb/JsonDocumentReader.h"
#include "robomongo/core/bson/Bson.h"

#include <QDir>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Robomongo {
namespace {
const int ReadChunkSize = 64 * 1024;
const std::size_t MaxJsonDocumentBytes = 128 * 1024 * 1024;
const int MaxBsonDocumentBytes = 16 * 1024 * 1024;
const std::size_t MaxNestingDepth = 200;

QString diagnosticText(QString text)
{
    // Keep paths and operating-system errors on one readable diagnostic line.
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i).isLowSurrogate() || text.at(i).isHighSurrogate())
            continue;
        if (!text.at(i).isPrint())
            text[i] = QLatin1Char('?');
    }
    return text;
}
}

JsonDocumentReader::JsonDocumentReader(const QString &path)
    : _file(path)
{
    if (!_file.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Cannot open input: %1").arg(diagnosticText(_file.errorString())));
    _json.reserve(4096);
}

[[noreturn]] void JsonDocumentReader::fail(const QString &message)
{
    _failed = true;
    const QString path = diagnosticText(QDir::toNativeSeparators(_file.fileName()));
    throw std::runtime_error(
        QStringLiteral("%1 (file \"%2\", document %3, byte %4)")
            .arg(message)
            .arg(path)
            .arg(_documentNumber + 1)
            .arg(_offset + 1)
            .toUtf8().toStdString());
}

int JsonDocumentReader::peekByte()
{
    if (_position == _buffer.size()) {
        if (_eof)
            return -1;
        _buffer = _file.read(ReadChunkSize);
        _position = 0;
        if (_file.error() != QFileDevice::NoError)
            fail(QStringLiteral("Cannot read input: %1").arg(diagnosticText(_file.errorString())));
        if (_buffer.isEmpty()) {
            _eof = true;
            return -1;
        }
    }
    return static_cast<unsigned char>(_buffer.at(_position));
}

int JsonDocumentReader::readByte()
{
    const int value = peekByte();
    if (value != -1) {
        ++_position;
        ++_offset;
    }
    return value;
}

void JsonDocumentReader::skipWhitespace()
{
    while (peekByte() != -1) {
        const char *begin = _buffer.constData() + _position;
        const char *cursor = begin;
        const char *end = _buffer.constData() + _buffer.size();
        while (cursor != end && (*cursor == ' ' || *cursor == '\t'
                || *cursor == '\r' || *cursor == '\n'))
            ++cursor;
        const int consumed = static_cast<int>(cursor - begin);
        _position += consumed;
        _offset += consumed;
        if (cursor != end)
            return;
    }
}

void JsonDocumentReader::initialize()
{
    // A UTF-8 BOM is permitted only at the very beginning of the file.
    if (peekByte() == 0xef) {
        readByte();
        if (readByte() != 0xbb || readByte() != 0xbf)
            fail(QStringLiteral("Invalid UTF-8 byte order mark"));
    }
    skipWhitespace();
    if (peekByte() == '[') {
        readByte();
        _array = true;
    }
    _initialized = true;
}

void JsonDocumentReader::finishArray()
{
    readByte(); // Closing ']'.
    skipWhitespace();
    if (peekByte() != -1)
        fail(QStringLiteral("Unexpected data after the JSON array"));
    _finished = true;
}

const std::string &JsonDocumentReader::readObject()
{
    // Keep capacity across documents, and copy input once per buffer span.
    _json.clear();
    char nesting[MaxNestingDepth];
    std::size_t depth = 0;
    bool inString = false;
    bool escaped = false;

    for (;;) {
        if (peekByte() == -1)
            fail(QStringLiteral("Incomplete JSON object"));
        if (_json.size() == MaxJsonDocumentBytes) {
            // Match readByte() diagnostics: the offending byte is consumed.
            readByte();
            fail(QStringLiteral("JSON document exceeds the 128 MiB input limit"));
        }

        const char *begin = _buffer.constData() + _position;
        const char *cursor = begin;
        const std::size_t available = std::min(
            static_cast<std::size_t>(_buffer.size() - _position),
            MaxJsonDocumentBytes - _json.size());
        const char *end = begin + available;
        const auto advance = [&]() {
            const int consumed = static_cast<int>(cursor - begin);
            _position += consumed;
            _offset += consumed;
        };

        while (cursor != end) {
            const char character = *cursor++;
            if (inString) {
                if (static_cast<unsigned char>(character) < 0x20) {
                    advance();
                    fail(QStringLiteral("Unescaped control character in a JSON string"));
                }
                if (escaped)
                    escaped = false;
                else if (character == '\\')
                    escaped = true;
                else if (character == '"')
                    inString = false;
                continue;
            }
            if (character == '"') {
                inString = true;
            } else if (character == '{' || character == '[') {
                if (depth == MaxNestingDepth) {
                    advance();
                    fail(QStringLiteral("JSON document exceeds the nesting limit of 200"));
                }
                nesting[depth++] = character;
            } else if (character == '}' || character == ']') {
                if (depth == 0
                        || (character == '}' && nesting[depth - 1] != '{')
                        || (character == ']' && nesting[depth - 1] != '[')) {
                    advance();
                    fail(QStringLiteral("Mismatched JSON object or array delimiters"));
                }
                if (--depth == 0)
                    break;
            }
        }

        advance();
        _json.append(begin, static_cast<std::size_t>(cursor - begin));
        if (depth == 0)
            return _json;
    }
}

bool JsonDocumentReader::next(mongo::BSONObj &document)
{
    if (_failed)
        fail(QStringLiteral("Cannot continue reading after an input error"));
    if (_finished)
        return false;
    if (!_initialized)
        initialize();
    skipWhitespace();

    if (_array) {
        if (_arrayNeedsSeparator) {
            if (peekByte() == ']') {
                finishArray();
                return false;
            }
            if (readByte() != ',')
                fail(QStringLiteral("Expected ',' or ']' after a JSON array document"));
            skipWhitespace();
            if (peekByte() == ']')
                fail(QStringLiteral("Trailing commas are not allowed in the JSON array"));
        } else if (peekByte() == ']') {
            finishArray();
            return false;
        }
        if (peekByte() == -1)
            fail(QStringLiteral("Incomplete JSON array"));
    } else if (peekByte() == -1) {
        _finished = true;
        return false;
    }

    if (peekByte() != '{')
        fail(QStringLiteral("Expected a JSON object"));
    const std::string &json = readObject();
    mongo::BSONObj parsed;
    try {
        parsed = mongo::BSONObj::fromJson(json);
    } catch (const std::exception &) {
        // libbson diagnostics may quote document contents; keep them private.
        fail(QStringLiteral("Invalid JSON or Extended JSON document"));
    }
    if (parsed.objsize() > MaxBsonDocumentBytes)
        fail(QStringLiteral("BSON document exceeds MongoDB's 16 MiB document limit"));

    document = std::move(parsed);
    ++_documentNumber;
    _arrayNeedsSeparator = _array;
    return true;
}

}
