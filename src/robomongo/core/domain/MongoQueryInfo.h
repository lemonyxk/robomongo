#pragma once

#include "robomongo/core/bson/Bson.h"
#include "robomongo/core/domain/MongoNamespace.h"

namespace Robomongo
{
    namespace detail
    {
        std::string prepareServerAddress(const std::string &address);
    }

    struct CollectionInfo
    {
        CollectionInfo();
        CollectionInfo(const std::string &server, const std::string &database, const std::string &collection);
        bool isValid() const;

        std::string _serverAddress;
        MongoNamespace _ns;
    };

    struct MongoQueryInfo
    {
        MongoQueryInfo();

        MongoQueryInfo(const CollectionInfo &info,
                  mongo::BSONObj query, mongo::BSONObj fields, int limit, int skip, int batchSize,
                  int options, bool special);

        CollectionInfo _info;
        mongo::BSONObj _query;
        mongo::BSONObj _fields;
        int _limit = 0;
        int _skip = 0;
        int _batchSize = 50;
        int _options = 0;
        std::string runtimeCursorId;
        bool readOnly = false;
        bool _special = false; // flag, indicating that `query` contains special fields on
                      // first level, and query data in `query` field.
        
    };
}
