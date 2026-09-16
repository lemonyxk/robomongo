#include "robomongo/gui/widgets/workarea/JsonPrepareThread.h"

#include <QElapsedTimer>

#include "robomongo/core/domain/MongoDocument.h"
#include "robomongo/core/utils/BsonUtils.h"
#include "robomongo/core/utils/QtUtils.h"

namespace Robomongo
{
    JsonPrepareThread::JsonPrepareThread(const std::vector<MongoDocumentPtr> &bsonObjects, UUIDEncoding uuidEncoding, SupportedTimes timeZone)
        :_bsonObjects(bsonObjects),
        _uuidEncoding(uuidEncoding),
        _timeZone(timeZone),
        _stop(false)
    {
    }

    void JsonPrepareThread::stop()
    {
        _stop = true;
    }

    void JsonPrepareThread::run()
    {
        constexpr std::size_t chunkBytes = 64 * 1024;
        std::string chunk;
        chunk.reserve(chunkBytes);
        QElapsedTimer lastDelivery;
        lastDelivery.start();
        int position = 1; // 1-based numbering to match tree & table views
        for (std::vector<MongoDocumentPtr>::const_iterator it = _bsonObjects.begin(); it != _bsonObjects.end(); ++it)
        {
            if (_stop)
                break;
            const MongoDocumentPtr &doc = *it;
            if (position == 1)
                chunk.append("/* 1 */\n");
            else
                chunk.append("\n\n/* " + std::to_string(position) + " */\n");

            chunk.append(BsonUtils::jsonString(doc->bsonObj(), mongo::TenGen, 1,
                                              _uuidEncoding, _timeZone, false, true));

            if (_stop)
                break;

            // Show the first document immediately, then amortize queued UI
            // signals, UTF-8 conversions, editor appends and syntax updates.
            if (position == 1 || chunk.size() >= chunkBytes || lastDelivery.elapsed() >= 32) {
                emit partReady(QtUtils::toQString(chunk));
                chunk.clear();
                lastDelivery.restart();
            }

            position++;
        }

        if (!_stop && !chunk.empty())
            emit partReady(QtUtils::toQString(chunk));

        emit done();
    }
}
