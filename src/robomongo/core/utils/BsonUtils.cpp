#include "robomongo/core/utils/BsonUtils.h"
#include "robomongo/core/HexUtils.h"
#include <QDateTime>
#include <QTimeZone>
#include <QString>
#include <QByteArray>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
using namespace mongo;
namespace Robomongo { namespace BsonUtils {
        namespace detail
        {
            template<>
            mongo::BSONObj getField<mongo::BSONObj>(const mongo::BSONElement &elem) 
            {
                return elem.embeddedObject();
            }

            template<>
            bool getField<bool>(const mongo::BSONElement &elem)
            {
                return elem.Bool();
            }

            template<>
            std::string getField<std::string>(const mongo::BSONElement &elem)
            {
                return elem.String();
            }

            template<>
            std::vector<BSONElement> getField<std::vector<BSONElement> >(const mongo::BSONElement &elem)
            {
                return elem.Array();
            }

            template<>
            int getField<int>(const mongo::BSONElement &elem)
            {
                return elem.Int();
            }

            template<>
            double getField<double>(const mongo::BSONElement &elem)
            {
                return elem.numberDouble();
            }

            template<>
            long long getField<long long>(const mongo::BSONElement &elem)
            {
                return elem.safeNumberLong();
            }
        }


namespace {
std::string doubleText(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";
    std::ostringstream out; out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    auto text=out.str(); if(text.find_first_of(".eE")==std::string::npos)text+=".0";
    return text;
}
std::string dateText(long long millis, SupportedTimes timeFormat) {
    // Qt represents a wider date range than ISODate's four-digit year syntax.
    auto date=QDateTime::fromMSecsSinceEpoch(millis,QTimeZone::utc());
    if(!date.isValid() || date.date().year()<1 || date.date().year()>9999)
        return "Date("+std::to_string(millis)+")";
    if(timeFormat==LocalTime)date=date.toLocalTime();
    return "ISODate("+mongo::quoteJson(date.toString(Qt::ISODateWithMs).toStdString())+")";
}
}
std::string jsonString(const BSONObj &obj,JsonStringFormat format,int pretty,
                      UUIDEncoding uuid,SupportedTimes timezone,bool array,bool plainIntegers) {
    array=array||obj.isArray();
    if(format==Strict) return obj.jsonString(Strict,pretty,array);
    std::string out=array?"[":"{"; bool first=true;
    for(auto element:obj) {
        if(!first)out+=","; first=false;
        if(pretty)out+="\n"+std::string(pretty*4,' '); else out+=" ";
        out+=jsonString(element,format,!array,pretty?pretty+1:0,uuid,timezone,array,plainIntegers);
    }
    if(!first && pretty)out+="\n"+std::string((pretty-1)*4,' ');
    else if(!first)out+=" ";
    return out+(array?"]":"}");
}
std::string jsonString(const BSONElement &e,JsonStringFormat format,bool includeFieldNames,
                      int pretty,UUIDEncoding uuid,SupportedTimes timezone,bool array,bool plainIntegers) {
    const auto prefix=includeFieldNames&&!array?mongo::quoteJson(e.fieldName())+" : ":"";
    if(format==Strict)return prefix+e.toString(false);
    std::string value;
    switch(e.type()) {
    case mongo::String: value=mongo::quoteJson(e.String());break;
    case NumberInt: value=plainIntegers?std::to_string(e.Int()):"NumberInt("+std::to_string(e.Int())+")";break;
    case NumberLong: value=plainIntegers?std::to_string(e.Long()):"NumberLong("+mongo::quoteJson(std::to_string(e.Long()))+")";break;
    case NumberDouble: value=doubleText(e.Double());break;
    case NumberDecimal: value="NumberDecimal("+mongo::quoteJson(e.numberDecimal().toString())+")";break;
    case mongo::Bool: value=e.Bool()?"true":"false";break;
    case jstNULL: value="null";break;
    case Undefined: value="undefined";break;
    case Object: case mongo::Array: value=jsonString(e.Obj(),format,pretty,uuid,timezone,e.type()==mongo::Array,plainIntegers);break;
    case jstOID: value="ObjectId("+mongo::quoteJson(e.OID().toString())+")";break;
    case mongo::Date: value=dateText(e.date().toMillisSinceEpoch(),timezone);break;
    case bsonTimestamp: value="Timestamp("+std::to_string(e.timestamp().getSecs())+", "+std::to_string(e.timestampInc())+")";break;
    case BinData: {
        int n=0;auto bytes=e.binData(n);
        if((e.binDataType()==newUUID||e.binDataType()==bdtUUID)&&n==16)value=HexUtils::formatUuid(e,uuid);
        else value="BinData("+std::to_string(static_cast<int>(e.binDataType()))+", "+mongo::quoteJson(QByteArray(bytes,n).toBase64().toStdString())+")";
        break;
    }
    case RegEx: value="RegExp("+mongo::quoteJson(e.regex())+", "+mongo::quoteJson(e.regexFlags())+")";break;
    case Code: value="Code("+mongo::quoteJson(e._asCode())+")";break;
    case CodeWScope: value="Code("+mongo::quoteJson(e._asCode())+", "+jsonString(e.codeWScopeObject(),format,pretty,uuid,timezone,false,plainIntegers)+")";break;
    case MinKey: value="MinKey()";break;
    case MaxKey: value="MaxKey()";break;
    default: value=e.toString(false);break;
    }
    return prefix+value;
}
        bool isArray(const mongo::BSONElement &elem)
        {
            return isArray(elem.type());
        }

        bool isArray(mongo::BSONType type)
        {
            return type == mongo::Array ; 
        }

        bool isDocument(const mongo::BSONElement &elem)
        {
            return isDocument(elem.type());
        }

        bool isDocument(mongo::BSONType type)
        {
            switch( type ) {
            case mongo::Object:
            case mongo::Array:
                return true;
            default:
                return false;
            }
        }

        bool isSimpleType(const mongo::BSONType type)
        {
            switch( type ) {
            case NumberLong:
            case NumberDouble:
            case NumberDecimal:
            case NumberInt:
            case mongo::String:
            case mongo::Bool:
            case mongo::Date:
            case jstOID:
                return true;
            default:
                return false;
            }
        }

        bool isUuidType(const mongo::BSONType type, mongo::BinDataType binDataType)
        {
            if (type != mongo::BinData)
                return false;

            return (binDataType == mongo::newUUID || binDataType == mongo::bdtUUID);
        }

        bool isSimpleType(const mongo::BSONElement &elem) 
        {
            return isSimpleType(elem.type()); 
        }

        bool isUuidType(const mongo::BSONElement &elem) 
        {
            if (elem.type() != mongo::BinData)
                return false;

            mongo::BinDataType binType = elem.binDataType();
            return (binType == mongo::newUUID || binType == mongo::bdtUUID);
        }

        const char* BSONTypeToString(mongo::BSONType type, mongo::BinDataType binDataType, UUIDEncoding uuidEncoding)
        {
            switch (type)
            {
                /** double precision floating point value */
            case NumberDouble:
                {
                    return "Double";
                }
                /** double precision floating point value */
            case NumberDecimal:
                {
                    return "Decimal128";
                }
                /** character string, stored in utf8 */
            case String:
                {
                    return "String";
                }

                /** an embedded object */
            case Object:
                {
                    return "Object";
                }
                /** an embedded array */
            case Array:
                {
                    return "Array";
                }
            case BinData:
                {
                    if (binDataType == mongo::newUUID) {
                        return "UUID";
                    } else if (binDataType == mongo::bdtUUID) {
                        const char* type;
                        switch(uuidEncoding) {
                        case DefaultEncoding: type = "Legacy UUID"; break;
                        case JavaLegacy:      type = "Java UUID (Legacy)"; break;
                        case CSharpLegacy:    type = ".NET UUID (Legacy)"; break;
                        case PythonLegacy:    type = "Python UUID (Legacy)"; break;
                        default:              type = "Legacy UUID"; break;
                        }

                        return type;
                    } else {
                        return "Binary";
                    }
                }

                /** Undefined type */
            case Undefined:
                {
                    return "Undefined";
                }

                /** ObjectId */
            case jstOID:
                {
                    return "ObjectId";
                }

                /** boolean type */
            case Bool:
                {
                    return "Boolean";
                }

                /** date type */
            case Date:
                {
                    return "Date";
                }

                /** null type */
            case jstNULL:
                {
                    return "Null";
                }
                break;

                /** regular expression, a pattern with options */
            case RegEx:
                {
                    return "Regular Expression";
                }

                /** deprecated / will be redesigned */
            case DBRef:
                {
                    return "DBRef";
                }

                /** deprecated / use CodeWScope */
            case Code:
                {
                    return "Code";
                }
                break;

                /** a programming language (e.g., Python) symbol */
            case Symbol:
                {
                    return "Symbol";
                }

                /** javascript code that can execute on the database server, with SavedContext */
            case CodeWScope:
                {
                    return "CodeWScope";
                }

                /** 32 bit signed integer */
            case NumberInt:
                {
                    return "Int32";
                }

                /** Updated to a Date with value next OpTime on insert */
            case bsonTimestamp:
                {
                    return "Timestamp";
                }

                /** 64 bit integer */
            case NumberLong:
                {
                    return "Int64";
                }
                break;

            default:
                {
                    return "Type is not supported";
                }
            }
        }


void buildJsonString(const BSONObj &obj,std::string &out,UUIDEncoding uuid,SupportedTimes timezone) {
    out+=jsonString(obj,TenGen,1,uuid,timezone,false,true);
}
void buildJsonString(const BSONElement &element,std::string &out,UUIDEncoding uuid,SupportedTimes timezone) {
    if(element.type()==mongo::String || element.type()==Symbol)out+=element.String();
    else out+=jsonString(element,TenGen,false,0,uuid,timezone,false,true);
}
BSONElement indexOf(const BSONObj &obj,int index) {
    for(auto element:obj)if(index--==0)return element;
    return BSONElement();
}
int elementsCount(const BSONObj &obj) { return obj.nFields(); }
std::string reformatDoubleString(QString,double value) { return doubleText(value); }
} }
