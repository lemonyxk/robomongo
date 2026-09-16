#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QtGlobal>

#include <string>

namespace mongo { class BSONObj; }

namespace Robomongo {

// Reads JSON/Extended JSON object streams and arrays without loading the file.
// Object streams may use whitespace/newlines or directly adjacent objects.
// Errors include the input path, one-based document number, and byte position.
class JsonDocumentReader
{
public:
    explicit JsonDocumentReader(const QString &path);
    bool next(mongo::BSONObj &document);
    qint64 documentNumber() const { return _documentNumber; }

private:
    int peekByte();
    int readByte();
    void skipWhitespace();
    void initialize();
    void finishArray();
    const std::string &readObject();
    [[noreturn]] void fail(const QString &message);

    QFile _file;
    QByteArray _buffer;
    std::string _json;
    int _position = 0;
    qint64 _offset = 0;
    qint64 _documentNumber = 0;
    bool _eof = false;
    bool _initialized = false;
    bool _array = false;
    bool _arrayNeedsSeparator = false;
    bool _finished = false;
    bool _failed = false;
};

}
