#pragma once

// Application-owned BSON value types. Storage and validation are provided by
// the current, public libbson API; no MongoDB server headers are required.
#include <bson/bson.h>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>

namespace mongo {
enum BSONType { MinKey=-1, EOO=0, NumberDouble=1, String=2, Object=3, Array=4,
    BinData=5, Undefined=6, jstOID=7, Bool=8, Date=9, jstNULL=10, RegEx=11,
    DBRef=12, Code=13, Symbol=14, CodeWScope=15, NumberInt=16, bsonTimestamp=17,
    NumberLong=18, NumberDecimal=19, MaxKey=127 };
enum BinDataType { BinDataGeneral=0, Function=1, ByteArrayDeprecated=2,
    bdtUUID=3, newUUID=4, MD5Type=5, Encrypt=6, Column=7, Sensitive=8, Vector=9,
    bdtCustom=128 };
enum JsonStringFormat { Strict, TenGen, JS };

class StringData {
public:
    StringData() = default;
    StringData(const char *s) : _value(s ? s : "") {}
    StringData(const std::string &s) : _value(s) {}
    StringData(const char *s, std::size_t n) : _value(s, n) {}
    std::string toString() const { return _value; }
    const char *rawData() const { return _value.data(); }
    const char *data() const { return _value.data(); }
    std::size_t size() const { return _value.size(); }
    bool empty() const { return _value.empty(); }
    int compare(StringData other) const { return _value.compare(other._value); }
    operator std::string() const { return _value; }
private:
    std::string _value;
};

class Date_t {
public:
    Date_t() = default;
    explicit Date_t(std::int64_t value) : _millis(value) {}
    static Date_t fromMillisSinceEpoch(std::int64_t value) { return Date_t(value); }
    std::int64_t toMillisSinceEpoch() const { return _millis; }
private:
    std::int64_t _millis = 0;
};

class Timestamp {
public:
    Timestamp(std::uint32_t secs=0, std::uint32_t inc=0) : _secs(secs), _inc(inc) {}
    std::uint32_t getSecs() const { return _secs; }
    std::uint32_t getInc() const { return _inc; }
private:
    std::uint32_t _secs, _inc;
};

class OID {
public:
    OID();
    explicit OID(const std::string &hex);
    explicit OID(const bson_oid_t &value) : _value(value) {}
    static OID gen();
    std::string toString() const;
    const bson_oid_t *raw() const { return &_value; }
private:
    bson_oid_t _value{};
};
std::ostream &operator<<(std::ostream &, const OID &);

class Decimal128 {
public:
    Decimal128();
    explicit Decimal128(const std::string &value);
    explicit Decimal128(bson_decimal128_t value) : _value(value) {}
    std::string toString() const;
    const bson_decimal128_t *raw() const { return &_value; }
private:
    bson_decimal128_t _value{};
};
struct BSONCode { explicit BSONCode(std::string value) : code(std::move(value)) {} std::string code; };

class BSONElement;
class BSONObjIterator;
class BSONObj {
public:
    BSONObj();
    explicit BSONObj(const bson_t *value, bool array=false);
    explicit BSONObj(const char *rawBytes);
    static BSONObj fromJson(const std::string &json);
    static BSONObj fromBytes(const void *bytes, std::size_t size, bool array=false);
    BSONObj getOwned() const { return *this; }
    bool isOwned() const { return true; }
    const bson_t *raw() const { return _data.get(); }
    const char *objdata() const { return reinterpret_cast<const char *>(bson_get_data(raw())); }
    int objsize() const { return static_cast<int>(raw()->len); }
    bool isEmpty() const { return bson_empty(raw()); }
    bool isValid() const { return bson_validate(raw(), BSON_VALIDATE_NONE, nullptr); }
    bool isArray() const { return _array; }
    void markAsArray(bool value=true) { _array = value; }
    int nFields() const { return static_cast<int>(bson_count_keys(raw())); }
    BSONElement getField(const std::string &name) const;
    BSONElement operator[](const std::string &name) const;
    BSONElement firstElement() const;
    bool hasField(const std::string &name) const;
    bool hasElement(const std::string &name) const { return hasField(name); }
    const char *getStringField(const std::string &name) const;
    int getIntField(const std::string &name) const;
    bool getBoolField(const std::string &name) const;
    BSONObj getObjectField(const std::string &name) const;
    BSONObj removeField(const std::string &name) const;
    BSONObj addField(const BSONElement &element) const;
    std::string toExtendedJson(bool canonical=true) const;
    std::string jsonString(JsonStringFormat format=Strict, int pretty=0, bool array=false) const;
    std::string toString() const { return toExtendedJson(); }
    int woCompare(const BSONObj &other) const { return bson_compare(raw(), other.raw()); }
    std::vector<BSONElement> elements() const;
    // Range iteration keeps a shared owner in each iterator and element.
    BSONObjIterator begin() const;
    BSONObjIterator end() const;
private:
    std::shared_ptr<bson_t> _data;
    bool _array = false;
    friend class BSONElement;
    friend class BSONObjIterator;
};
using BSONArray = BSONObj;

class BSONElement {
public:
    BSONElement() = default;
    BSONType type() const;
    bool eoo() const { return !_valid; }
    explicit operator bool() const { return _valid; }
    bool isNull() const { return !_valid || type() == jstNULL; }
    bool isABSONObj() const { return type() == Object || type() == mongo::Array; }
    bool isNumber() const { return type()==NumberInt || type()==NumberLong || type()==NumberDouble || type()==NumberDecimal; }
    const char *fieldName() const { return _valid ? bson_iter_key(&_iter) : ""; }
    StringData fieldNameStringData() const { return fieldName(); }
    std::string String() const;
    std::string str() const { return String(); }
    const char *valuestr() const;
    int valuestrsize() const;
    bool Bool() const;
    bool boolean() const { return Bool(); }
    bool trueValue() const;
    int Int() const { return static_cast<int>(safeNumberLong()); }
    int numberInt() const { return Int(); }
    int _numberInt() const { return Int(); }
    long long Long() const { return safeNumberLong(); }
    long long numberLong() const { return safeNumberLong(); }
    long long _numberLong() const { return safeNumberLong(); }
    long long safeNumberLong() const;
    double Double() const;
    double numberDouble() const { return Double(); }
    double number() const { return Double(); }
    Decimal128 numberDecimal() const;
    Decimal128 _numberDecimal() const { return numberDecimal(); }
    mongo::OID OID() const;
    mongo::OID __oid() const { return OID(); }
    Date_t date() const;
    Date_t Date() const { return date(); }
    Timestamp timestamp() const;
    unsigned timestampInc() const { return timestamp().getInc(); }
    unsigned timestampTime() const { return timestamp().getSecs(); }
    BSONObj Obj() const;
    BSONObj embeddedObject() const { return Obj(); }
    int embeddedFieldCount() const;
    std::vector<BSONElement> Array() const { return Obj().elements(); }
    BinDataType binDataType() const;
    const char *binData(int &length) const;
    const char *regex() const;
    const char *regexFlags() const;
    std::string _asCode() const;
    BSONObj codeWScopeObject() const;
    BSONObj wrap() const;
    std::string toString(bool includeFieldName=true) const;
    const bson_value_t *rawValue() const;
private:
    BSONElement(std::shared_ptr<bson_t> owner, bson_iter_t iter)
        : _owner(std::move(owner)), _iter(iter), _valid(true) {}
    std::shared_ptr<bson_t> _owner;
    mutable bson_iter_t _iter{};
    bool _valid = false;
    friend class BSONObj;
    friend class BSONObjIterator;
};

class BSONObjIterator {
public:
    BSONObjIterator() = default;
    explicit BSONObjIterator(const BSONObj &obj);
    bool more() const { return _valid; }
    BSONElement next();
    BSONElement operator*() const { return _valid ? BSONElement(_owner,_iter) : BSONElement(); }
    BSONObjIterator &operator++();
    bool operator!=(const BSONObjIterator &other) const { return _valid != other._valid || (_valid && (_owner != other._owner || _iter.off != other._iter.off)); }
private:
    std::shared_ptr<bson_t> _owner;
    bson_iter_t _iter{};
    bool _valid = false;
};

class BSONObjBuilder {
public:
    BSONObjBuilder();
    explicit BSONObjBuilder(const BSONObj &initial);
    BSONObj obj() const { return BSONObj(_data.get()); }
    BSONObj done() const { return obj(); }
    BSONObjBuilder &append(const std::string &key, const std::string &value);
    BSONObjBuilder &append(const std::string &key, const char *value) { return append(key,std::string(value ? value : "")); }
    BSONObjBuilder &append(const std::string &key, StringData value) { return append(key,value.toString()); }
    BSONObjBuilder &append(const std::string &key, bool value);
    BSONObjBuilder &append(const std::string &key, int value);
    BSONObjBuilder &append(const std::string &key, unsigned value) { return append(key,static_cast<long long>(value)); }
    BSONObjBuilder &append(const std::string &key, long value) { return append(key,static_cast<long long>(value)); }
    BSONObjBuilder &append(const std::string &key, unsigned long value) { return append(key,static_cast<long long>(value)); }
    BSONObjBuilder &append(const std::string &key, long long value);
    BSONObjBuilder &append(const std::string &key, unsigned long long value) { return append(key,static_cast<long long>(value)); }
    BSONObjBuilder &append(const std::string &key, double value);
    BSONObjBuilder &append(const std::string &key, const BSONObj &value);
    BSONObjBuilder &append(const std::string &key, const BSONElement &value);
    BSONObjBuilder &append(const std::string &key, const OID &value);
    BSONObjBuilder &append(const std::string &key, const Decimal128 &value);
    BSONObjBuilder &append(const std::string &key, Date_t value);
    BSONObjBuilder &append(const std::string &key, Timestamp value);
    BSONObjBuilder &append(const std::string &key, const BSONCode &value);
    BSONObjBuilder &append(const BSONElement &value) { return append(value.fieldName(), value); }
    BSONObjBuilder &appendAs(const BSONElement &value, const std::string &key) { return append(key,value); }
    BSONObjBuilder &appendElements(const BSONObj &value);
    BSONObjBuilder &appendArray(const std::string &key, const BSONObj &value);
    BSONObjBuilder &appendNull(const std::string &key);
    BSONObjBuilder &appendUndefined(const std::string &key);
    BSONObjBuilder &appendMinKey(const std::string &key);
    BSONObjBuilder &appendMaxKey(const std::string &key);
    BSONObjBuilder &appendBinData(const std::string &key,int length,BinDataType type,const void *data);
    BSONObjBuilder &appendRegex(const std::string &key,const std::string &pattern,const std::string &flags="");
    BSONObjBuilder &appendCodeWScope(const std::string &key,const std::string &code,const BSONObj &scope);
    BSONObjBuilder &appendBool(const std::string &key,bool value) { return append(key,value); }
    BSONObjBuilder &appendDate(const std::string &key,Date_t value) { return append(key,value); }
    BSONObjBuilder &appendDate(const std::string &key,long long value) { return append(key,Date_t(value)); }
    template<class T> BSONObjBuilder &appendNumber(const std::string &key,const T &value) { return append(key,value); }
    BSONObjBuilder &operator<<(const char *value) { return streamString(value); }
    BSONObjBuilder &operator<<(const std::string &value) { return streamString(value); }
    BSONObjBuilder &operator<<(StringData value) { return streamString(value.toString()); }
    template<class T> BSONObjBuilder &operator<<(const T &value) {
        if (!_hasKey) throw std::runtime_error("BSON builder requires a field name");
        append(_key,value); _hasKey=false; return *this;
    }
private:
    bson_t *writable() const { return reinterpret_cast<bson_t *>(_data.get()); }
    BSONObjBuilder &streamString(const std::string &value) {
        if (!_hasKey) { _key=value; _hasKey=true; }
        else { append(_key,value); _hasKey=false; } return *this;
    }
    std::shared_ptr<bson_t> _data;
    std::string _key;
    bool _hasKey=false;
};

class BSONArrayBuilder {
public:
    template<class T> BSONArrayBuilder &append(const T &value) { _builder.append(std::to_string(_size++),value); return *this; }
    template<class T> BSONArrayBuilder &operator<<(const T &value) { return append(value); }
    BSONObj arr() const { auto result=_builder.obj(); result.markAsArray(); return result; }
    BSONObj obj() const { return arr(); }
    BSONObj done() const { return arr(); }
private:
    BSONObjBuilder _builder;
    std::size_t _size=0;
};

BSONObj fromjson(const std::string &text);
BSONObj fromjson(const char *text);
std::string tojson(const BSONObj &obj, JsonStringFormat format=Strict, bool pretty=false);
std::string quoteJson(const std::string &text);

namespace Robomongo {
class ParseMsgAssertionException : public std::runtime_error {
public:
    ParseMsgAssertionException(int,const std::string &message,int offset,const std::string &reason)
        : std::runtime_error(message.empty()?reason:message), _reason(reason),_offset(offset) {}
    std::string reason() const { return _reason; }
    int offset() const { return _offset; }
private:
    std::string _reason; int _offset;
};
BSONObj fromjson(const std::string &text);
BSONObj fromjson(const char *text,int *length=nullptr);
// Keep existing Int64 fields when their editor representation is a bare integer.
// Explicit type constructors and Extended JSON always take precedence.
BSONObj fromjson(const char *text,int *length,const BSONObj &original);
bool isArray(StringData text);
std::string tojson(const BSONObj &obj,JsonStringFormat format=Strict,bool pretty=false);
}
}

#ifndef BSON
#define BSON(...) ((::mongo::BSONObjBuilder() << __VA_ARGS__).obj())
#endif
#ifndef BSON_ARRAY
#define BSON_ARRAY(...) ((::mongo::BSONArrayBuilder() << __VA_ARGS__).arr())
#endif
