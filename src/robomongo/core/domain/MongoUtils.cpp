#include <QCryptographicHash>
#include "robomongo/core/domain/MongoUtils.h"
#include "robomongo/core/bson/Bson.h"
using namespace std;
#include "robomongo/core/bson/Bson.h"

namespace Robomongo
{
    namespace MongoUtils
    {
        QString buildNiceSizeString(double sizeBytes)
        {
            if (sizeBytes < 1024 * 100) {
                double kb = ((double) sizeBytes) / 1024;
                return QString("%1 kb").arg(kb, 2, 'f', 2);
            }

            double mb = ((double) sizeBytes) / 1024 / 1024;
            return QString("%1 mb").arg(mb, 2, 'f', 2);
        }

        std::string buildPasswordHash(const std::string &username, const std::string &password)
        {
            const QByteArray value = QByteArray::fromStdString(username + ":mongo:" + password);
            return QCryptographicHash::hash(value, QCryptographicHash::Md5).toHex().toStdString();
        }
    }
}
