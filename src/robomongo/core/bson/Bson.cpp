#include "robomongo/core/bson/Bson.h"
#include "robomongo/core/HexUtils.h"
#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <cctype>
#include <iomanip>
#include <climits>

namespace mongo {
namespace {
std::shared_ptr<bson_t> owned(bson_t *value) {
    if (!value) throw std::runtime_error("Cannot allocate BSON document");
    return {value, bson_destroy};
}
void check(bool result) {
    if (!result) throw std::runtime_error("Cannot append BSON value (invalid field or document too large)");
}
}

OID::OID() = default;
OID::OID(const std::string &hex) {
    if (!bson_oid_is_valid(hex.data(),hex.size())) throw std::runtime_error("ObjectId requires 24 hexadecimal characters");
    bson_oid_init_from_string(&_value,hex.c_str());
}
OID OID::gen() { OID result; bson_oid_init(&result._value,nullptr); return result; }
std::string OID::toString() const { char out[25]; bson_oid_to_string(&_value,out); return out; }
std::ostream &operator<<(std::ostream &out,const OID &value) { return out << value.toString(); }
Decimal128::Decimal128() { bson_decimal128_from_string("0",&_value); }
Decimal128::Decimal128(const std::string &text) {
    if (!bson_decimal128_from_string(text.c_str(),&_value)) throw std::runtime_error("Invalid Decimal128: "+text);
}
std::string Decimal128::toString() const { char out[BSON_DECIMAL128_STRING]; bson_decimal128_to_string(&_value,out); return out; }

BSONObj::BSONObj() : _data(owned(bson_new())) {}
BSONObj::BSONObj(const bson_t *value,bool array) : _data(owned(value?bson_copy(value):bson_new())),_array(array) {}
BSONObj::BSONObj(const char *bytes) {
    if (!bytes) { _data=owned(bson_new()); return; }
    std::uint32_t size; std::memcpy(&size,bytes,sizeof(size)); size=BSON_UINT32_FROM_LE(size);
    if (size<5) throw std::runtime_error("Invalid BSON document length");
    _data=owned(bson_new_from_data(reinterpret_cast<const std::uint8_t *>(bytes),size));
}
BSONObj BSONObj::fromBytes(const void *bytes,std::size_t size,bool array) {
    BSONObj result;
    result._data=owned(bson_new_from_data(static_cast<const std::uint8_t *>(bytes),size));
    result._array=array; return result;
}
BSONObj BSONObj::fromJson(const std::string &json) {
    bson_error_t error{};
    bson_t *value=bson_new_from_json(reinterpret_cast<const std::uint8_t *>(json.data()),json.size(),&error);
    if (!value) throw std::runtime_error(error.message);
    BSONObj result; result._data=owned(value);
    auto start=json.find_first_not_of(" \t\r\n");
    result._array=start!=std::string::npos && json[start]=='[';
    return result;
}
BSONElement BSONObj::getField(const std::string &name) const {
    bson_iter_t it;
    return bson_iter_init_find(&it,raw(),name.c_str()) ? BSONElement(_data,it) : BSONElement();
}
BSONElement BSONObj::operator[](const std::string &name) const { return getField(name); }
BSONElement BSONObj::firstElement() const { return *begin(); }
bool BSONObj::hasField(const std::string &name) const { return !getField(name).eoo(); }
const char *BSONObj::getStringField(const std::string &name) const { return getField(name).valuestr(); }
int BSONObj::getIntField(const std::string &name) const { return getField(name).numberInt(); }
bool BSONObj::getBoolField(const std::string &name) const { return getField(name).trueValue(); }
BSONObj BSONObj::getObjectField(const std::string &name) const { return getField(name).Obj(); }
BSONObj BSONObj::removeField(const std::string &name) const { BSONObjBuilder out; for(auto e:*this) if(e.fieldName()!=name) out.append(e); return out.obj(); }
BSONObj BSONObj::addField(const BSONElement &element) const { BSONObjBuilder out(*this); out.append(element); return out.obj(); }
std::string BSONObj::toExtendedJson(bool canonical) const {
    std::size_t length=0;
    char *json=canonical?bson_as_canonical_extended_json(raw(),&length):bson_as_relaxed_extended_json(raw(),&length);
    if (!json) throw std::runtime_error("Cannot render BSON document");
    std::string out(json,length); bson_free(json); return out;
}
std::string BSONObj::jsonString(JsonStringFormat,int,bool array) const {
    if (!array && !_array) return toExtendedJson();
    std::string out="["; bool first=true;
    for(auto e:*this) { if(!first)out+=","; first=false; out+=e.toString(false); }
    return out+"]";
}
std::vector<BSONElement> BSONObj::elements() const { std::vector<BSONElement> out; for(auto e:*this)out.push_back(e); return out; }
BSONObjIterator BSONObj::begin() const { return BSONObjIterator(*this); }
BSONObjIterator BSONObj::end() const { return BSONObjIterator(); }
BSONObjIterator::BSONObjIterator(const BSONObj &obj) : _owner(obj._data) { _valid=bson_iter_init(&_iter,obj.raw()) && bson_iter_next(&_iter); }
BSONElement BSONObjIterator::next() { auto out=operator*(); operator++(); return out; }
BSONObjIterator &BSONObjIterator::operator++() { if(_valid)_valid=bson_iter_next(&_iter); return *this; }

BSONType BSONElement::type() const { if(!_valid)return EOO; auto t=bson_iter_type(&_iter); return t==BSON_TYPE_MINKEY?MinKey:static_cast<BSONType>(t); }
const char *BSONElement::valuestr() const {
    if(!_valid)return "";
    if(type()==mongo::String)return bson_iter_utf8(&_iter,nullptr);
    if(type()==Symbol)return bson_iter_symbol(&_iter,nullptr);
    if(type()==Code)return bson_iter_code(&_iter,nullptr);
    return "";
}
int BSONElement::valuestrsize() const { std::uint32_t n=0; if(type()==mongo::String)bson_iter_utf8(&_iter,&n); else n=std::strlen(valuestr()); return static_cast<int>(n)+1; }
std::string BSONElement::String() const { return std::string(valuestr(),valuestrsize()-1); }
bool BSONElement::Bool() const { return type()==mongo::Bool && bson_iter_bool(&_iter); }
bool BSONElement::trueValue() const { return type()==mongo::Bool?Bool():isNumber()?Double()!=0:!isNull(); }
long long BSONElement::safeNumberLong() const {
    if(type()==NumberLong)return bson_iter_int64(&_iter);
    if(type()==NumberInt)return bson_iter_int32(&_iter);
    if(type()==NumberDouble) { auto n=bson_iter_double(&_iter); if(std::isnan(n))return 0; if(n>=static_cast<double>(LLONG_MAX))return LLONG_MAX; if(n<=static_cast<double>(LLONG_MIN))return LLONG_MIN; return static_cast<long long>(n); }
    if(type()==NumberDecimal) { try { return std::stoll(numberDecimal().toString()); } catch(...) { return 0; } }
    return 0;
}
double BSONElement::Double() const { if(type()==NumberDouble)return bson_iter_double(&_iter); if(type()==NumberDecimal)return std::stod(numberDecimal().toString()); return static_cast<double>(safeNumberLong()); }
Decimal128 BSONElement::numberDecimal() const { bson_decimal128_t d{}; if(type()!=NumberDecimal||!bson_iter_decimal128(&_iter,&d))return Decimal128(); return Decimal128(d); }
mongo::OID BSONElement::OID() const { return type()==jstOID?mongo::OID(*bson_iter_oid(&_iter)):mongo::OID(); }
Date_t BSONElement::date() const { return Date_t(type()==mongo::Date?bson_iter_date_time(&_iter):0); }
Timestamp BSONElement::timestamp() const { std::uint32_t t=0,i=0; if(type()==bsonTimestamp)bson_iter_timestamp(&_iter,&t,&i); return Timestamp(t,i); }
BSONObj BSONElement::Obj() const {
    if(!isABSONObj())return BSONObj();
    std::uint32_t size=0; const std::uint8_t *bytes=nullptr;
    if(type()==mongo::Array)bson_iter_array(&_iter,&size,&bytes); else bson_iter_document(&_iter,&size,&bytes);
    return BSONObj::fromBytes(bytes,size,type()==mongo::Array);
}
int BSONElement::embeddedFieldCount() const {
    bson_iter_t child;
    if(!isABSONObj() || !bson_iter_recurse(&_iter,&child))return 0;
    int count=0;
    while(bson_iter_next(&child))++count;
    return count;
}
const char *BSONElement::binData(int &length) const { std::uint32_t n=0; const std::uint8_t *data=nullptr; bson_subtype_t t; if(type()==BinData)bson_iter_binary(&_iter,&t,&n,&data); length=static_cast<int>(n); return reinterpret_cast<const char *>(data); }
BinDataType BSONElement::binDataType() const { std::uint32_t n; const std::uint8_t *data; bson_subtype_t t=BSON_SUBTYPE_BINARY; if(type()==BinData)bson_iter_binary(&_iter,&t,&n,&data); return static_cast<BinDataType>(t); }
const char *BSONElement::regex() const { return type()==RegEx?bson_iter_regex(&_iter,nullptr):""; }
const char *BSONElement::regexFlags() const { const char *flags=""; if(type()==RegEx)bson_iter_regex(&_iter,&flags); return flags; }
std::string BSONElement::_asCode() const { if(type()==CodeWScope){std::uint32_t n,l;const std::uint8_t *scope;return bson_iter_codewscope(&_iter,&n,&l,&scope);} return String(); }
BSONObj BSONElement::codeWScopeObject() const { if(type()!=CodeWScope)return BSONObj(); std::uint32_t n,l;const std::uint8_t *scope; bson_iter_codewscope(&_iter,&n,&l,&scope); return BSONObj::fromBytes(scope,l); }
const bson_value_t *BSONElement::rawValue() const { return _valid?bson_iter_value(&_iter):nullptr; }
BSONObj BSONElement::wrap() const { BSONObjBuilder b; if(_valid)b.append(*this); return b.obj(); }
std::string BSONElement::toString(bool includeFieldName) const {
    if(!_valid)return "null";
    auto json=wrap().toExtendedJson();
    if(includeFieldName)return json;
    // Locate the end of the JSON-escaped field name, not a colon inside it.
    auto p=json.find('"'); ++p; bool escape=false;
    for(;p<json.size();++p) { if(!escape&&json[p]=='"'){++p;break;} if(!escape&&json[p]=='\\')escape=true;else escape=false; }
    p=json.find(':',p)+1;
    auto end=json.find_last_of('}');
    while(p<end&&std::isspace(static_cast<unsigned char>(json[p])))++p;
    while(end>p&&std::isspace(static_cast<unsigned char>(json[end-1])))--end;
    return json.substr(p,end-p);
}

BSONObjBuilder::BSONObjBuilder() : _data(owned(bson_new())) {}
BSONObjBuilder::BSONObjBuilder(const BSONObj &initial) : _data(owned(bson_copy(initial.raw()))) {}
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,const std::string &v) { check(bson_append_utf8(writable(),k.c_str(),k.size(),v.data(),v.size())); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,bool v) { check(bson_append_bool(writable(),k.c_str(),k.size(),v)); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,int v) { check(bson_append_int32(writable(),k.c_str(),k.size(),v)); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,long long v) { check(bson_append_int64(writable(),k.c_str(),k.size(),v)); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,double v) { check(bson_append_double(writable(),k.c_str(),k.size(),v)); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,const BSONObj &v) { if(v.isArray())return appendArray(k,v); check(bson_append_document(writable(),k.c_str(),k.size(),v.raw())); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,const BSONElement &v) { if(!v.eoo())check(bson_append_value(writable(),k.c_str(),k.size(),v.rawValue())); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,const OID &v) { check(bson_append_oid(writable(),k.c_str(),k.size(),v.raw())); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,const Decimal128 &v) { check(bson_append_decimal128(writable(),k.c_str(),k.size(),v.raw())); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,Date_t v) { check(bson_append_date_time(writable(),k.c_str(),k.size(),v.toMillisSinceEpoch())); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,Timestamp v) { check(bson_append_timestamp(writable(),k.c_str(),k.size(),v.getSecs(),v.getInc())); return *this; }
BSONObjBuilder &BSONObjBuilder::append(const std::string &k,const BSONCode &v) { check(bson_append_code(writable(),k.c_str(),k.size(),v.code.c_str())); return *this; }
BSONObjBuilder &BSONObjBuilder::appendElements(const BSONObj &v) { for(auto e:v)append(e); return *this; }
BSONObjBuilder &BSONObjBuilder::appendArray(const std::string &k,const BSONObj &v) { check(bson_append_array(writable(),k.c_str(),k.size(),v.raw())); return *this; }
BSONObjBuilder &BSONObjBuilder::appendNull(const std::string &k) { check(bson_append_null(writable(),k.c_str(),k.size())); return *this; }
BSONObjBuilder &BSONObjBuilder::appendUndefined(const std::string &k) { check(bson_append_undefined(writable(),k.c_str(),k.size())); return *this; }
BSONObjBuilder &BSONObjBuilder::appendMinKey(const std::string &k) { check(bson_append_minkey(writable(),k.c_str(),k.size())); return *this; }
BSONObjBuilder &BSONObjBuilder::appendMaxKey(const std::string &k) { check(bson_append_maxkey(writable(),k.c_str(),k.size())); return *this; }
BSONObjBuilder &BSONObjBuilder::appendBinData(const std::string &k,int n,BinDataType t,const void *v) { if(n<0)throw std::runtime_error("Negative binary length"); check(bson_append_binary(writable(),k.c_str(),k.size(),static_cast<bson_subtype_t>(t),static_cast<const std::uint8_t *>(v),n)); return *this; }
BSONObjBuilder &BSONObjBuilder::appendRegex(const std::string &k,const std::string &v,const std::string &flags) { check(bson_append_regex(writable(),k.c_str(),k.size(),v.c_str(),flags.c_str())); return *this; }
BSONObjBuilder &BSONObjBuilder::appendCodeWScope(const std::string &k,const std::string &code,const BSONObj &scope) { check(bson_append_code_with_scope(writable(),k.c_str(),k.size(),code.c_str(),scope.raw())); return *this; }

std::string quoteJson(const std::string &text) {
    QJsonArray a; a.append(QString::fromUtf8(text.data(),static_cast<int>(text.size())));
    auto json=QJsonDocument(a).toJson(QJsonDocument::Compact);
    return std::string(json.constData()+1,static_cast<std::size_t>(json.size()-2));
}

namespace {
// Deliberately parse data expressions, never execute JavaScript from an editor.
// Integers and Decimal128 are converted from their original decimal strings.
class DataParser {
public:
    explicit DataParser(const std::string &text,const BSONObj *original=nullptr)
        :_text(text),_original(original){}
    BSONObj parse(bool whole=true) {
        space(); if(_pos==_text.size())return BSONObj();
        if(peek()!='{'&&peek()!='[')fail("Expected an object or array");
        const bool array=peek()=='[';
        auto out=document(array,_original && _original->isArray()==array ? _original : nullptr);
        space(); if(whole && _pos!=_text.size())fail("Unexpected trailing input"); return out;
    }
    std::size_t consumed() const { return _pos; }
private:
    const std::string &_text; const BSONObj *_original;
    std::size_t _pos=0; unsigned _depth=0;
    [[noreturn]] void fail(const std::string &message) const { throw Robomongo::ParseMsgAssertionException(0,message,static_cast<int>(_pos),message); }
    void space() {
        for(;;) {
            while(_pos<_text.size()&&std::isspace(static_cast<unsigned char>(_text[_pos])))++_pos;
            if(_text.compare(_pos,2,"//")==0) {
                auto end=_text.find('\n',_pos+2);_pos=end==std::string::npos?_text.size():end+1;
            } else if(_text.compare(_pos,2,"/*")==0) {
                auto end=_text.find("*/",_pos+2);if(end==std::string::npos)fail("Unterminated comment");_pos=end+2;
            } else return;
        }
    }
    char peek() { space(); return _pos<_text.size()?_text[_pos]:'\0'; }
    bool take(char c) { if(peek()!=c)return false; ++_pos; return true; }
    void expect(char c) { if(!take(c))fail(std::string("Expected '")+c+"'"); }
    std::string word() {
        space(); auto start=_pos;
        while(_pos<_text.size()&&(std::isalnum(static_cast<unsigned char>(_text[_pos]))||_text[_pos]=='_'||_text[_pos]=='$'))++_pos;
        if(start==_pos)fail("Expected a name"); return _text.substr(start,_pos-start);
    }
    std::string string() {
        char q=peek(); if(q!='\''&&q!='"')fail("Expected a string"); ++_pos;
        std::string encoded="\"";
        while(_pos<_text.size()) {
            char c=_text[_pos++];
            if(c==q) { encoded+='"'; auto a=QJsonDocument::fromJson(QByteArray::fromStdString("["+encoded+"]")); if(!a.isArray())fail("Invalid string escape"); return a.array().at(0).toString().toUtf8().toStdString(); }
            if(c=='\\') { if(_pos==_text.size())fail("Unterminated string escape"); char n=_text[_pos++]; if(n=='\''&&q=='\'')encoded+='\''; else {encoded+='\\';encoded+=n;} }
            else if(c=='"')encoded+="\\\"";
            else encoded+=c;
        }
        fail("Unterminated string");
    }
    std::string numericToken() {
        space(); auto start=_pos;
        while(_pos<_text.size()&&(std::isdigit(static_cast<unsigned char>(_text[_pos]))||_text[_pos]=='-'||_text[_pos]=='+'||_text[_pos]=='.'||_text[_pos]=='e'||_text[_pos]=='E'))++_pos;
        if(start==_pos)fail("Expected a number");return _text.substr(start,_pos-start);
    }
    long long integer(const std::string &token) { try {std::size_t n; auto v=std::stoll(token,&n); if(n!=token.size())fail("Invalid integer"); return v;}catch(const std::exception&){fail("Integer is outside the Int64 range");} }
    std::string scalarArgument() { char c=peek(); return c=='\''||c=='"'?string():numericToken(); }
    BSONObj document(bool array,const BSONObj *original=nullptr) {
        if(++_depth>200)fail("Document nesting exceeds 200 levels");
        expect(array?'[':'{'); BSONObjBuilder out; std::size_t index=0;
        if(!take(array?']':'}'))for(;;) {
            std::string key=array?std::to_string(index++):(peek()=='\''||peek()=='"'?string():word());
            if(!array)expect(':'); value(out,key,original ? original->getField(key) : BSONElement());
            if(take(array?']':'}'))break;
            expect(','); if(take(array?']':'}'))break;
        }
        auto result=out.obj(); result.markAsArray(array); --_depth; return result;
    }
    void value(BSONObjBuilder &out,const std::string &key,const BSONElement &original) {
        char c=peek();
        if(c=='{'||c=='[') {
            BSONObj obj;
            if((c=='{' && original.type()==Object) || (c=='[' && original.type()==mongo::Array)) {
                const auto fields=original.Obj();
                obj=document(c=='[',&fields);
            } else if(c=='{' && original.type()==CodeWScope) {
                const auto fields=BSON("$scope"<<original.codeWScopeObject());
                obj=document(false,&fields);
            } else obj=document(c=='[');
            if(c=='{' && obj.firstElement().fieldName()[0]=='$') {
                const std::string marker=obj.firstElement().fieldName();
                if(marker=="$code") {
                    if(obj["$code"].type()!=mongo::String)fail("$code must be a string");
                    if(obj.hasField("$scope")) {
                        if(obj["$scope"].type()!=Object)fail("$scope must be an object");
                        out.appendCodeWScope(key,obj["$code"].String(),obj["$scope"].Obj());
                    } else out.append(key,BSONCode(obj["$code"].String()));
                    return;
                }
                const std::vector<std::string> markers={"$oid","$numberInt","$numberLong","$numberDouble","$numberDecimal","$date","$binary","$regularExpression","$regex","$timestamp","$minKey","$maxKey","$undefined","$symbol","$dbPointer"};
                if(std::find(markers.begin(),markers.end(),marker)!=markers.end()) {
                    try {auto decoded=BSONObj::fromJson(BSON("v"<<obj).toExtendedJson(false));out.append(key,decoded["v"]);return;}
                    catch(const std::exception &error){fail(error.what());}
                }
            }
            out.append(key,obj);return;
        }
        if(c=='\''||c=='"') {out.append(key,string());return;}
        if(c=='/') {
            ++_pos; std::string pattern; bool closed=false;
            while(_pos<_text.size()) {char x=_text[_pos++];if(x=='/'){closed=true;break;}if(x=='\\'&&_pos<_text.size()){char n=_text[_pos++];if(n!='/')pattern+='\\';pattern+=n;}else pattern+=x;}
            if(!closed)fail("Unterminated regular expression");std::string flags;while(_pos<_text.size()&&std::isalpha(static_cast<unsigned char>(_text[_pos])))flags+=_text[_pos++];out.appendRegex(key,pattern,flags);return;
        }
        if(_text.compare(_pos,9,"-Infinity")==0) {_pos+=9;out.append(key,-std::numeric_limits<double>::infinity());return;}
        if(std::isdigit(static_cast<unsigned char>(c))||c=='-'||c=='+') {
            auto token=numericToken(); if(token.find_first_of(".eE")!=std::string::npos){try{std::size_t n;auto d=std::stod(token,&n);if(n!=token.size())fail("Invalid double");out.append(key,d);}catch(const std::exception&){fail("Invalid double");}}
            else {
                const auto n=integer(token);
                if(original.type()!=NumberLong && n>=INT_MIN && n<=INT_MAX)
                    out.append(key,static_cast<int>(n));
                else out.append(key,n);
            }
            return;
        }
        auto name=word(); if(name=="new")name=word();
        if(name=="true"||name=="false"){out.append(key,name=="true");return;}
        if(name=="null"){out.appendNull(key);return;}
        if(name=="undefined"){out.appendUndefined(key);return;}
        if(name=="NaN"){out.append(key,std::numeric_limits<double>::quiet_NaN());return;}
        if(name=="Infinity"){out.append(key,std::numeric_limits<double>::infinity());return;}
        expect('(');
        if(name=="MinKey"||name=="MaxKey"){expect(')');if(name=="MinKey")out.appendMinKey(key);else out.appendMaxKey(key);return;}
        if(name=="NumberLong"||name=="Long"||name=="NumberInt"||name=="Int32") {
            auto n=integer(scalarArgument()); expect(')');if(name=="NumberInt"||name=="Int32"){if(n<INT_MIN||n>INT_MAX)fail("NumberInt is outside the Int32 range");out.append(key,static_cast<int>(n));}else out.append(key,n);return;
        }
        if(name=="NumberDecimal"||name=="Decimal128"){auto text=scalarArgument();expect(')');out.append(key,Decimal128(text));return;}
        if(name=="NumberDouble"||name=="Double"){auto text=scalarArgument();expect(')');out.append(key,std::stod(text));return;}
        if(name=="ObjectId"){auto text=string();expect(')');out.append(key,OID(text));return;}
        if(name=="ISODate"||name=="Date") {
            long long millis;
            if(peek()=='\''||peek()=='"'){auto text=QString::fromStdString(string());auto date=QDateTime::fromString(text,Qt::ISODateWithMs);if(!date.isValid())date=QDateTime::fromString(text,Qt::ISODate);if(!date.isValid())fail("Invalid ISO date");millis=date.toMSecsSinceEpoch();}
            else millis=integer(scalarArgument());expect(')');out.appendDate(key,millis);return;
        }
        if(name=="Timestamp"){auto secs=integer(scalarArgument());expect(',');auto inc=integer(scalarArgument());expect(')');if(secs<0||secs>UINT_MAX||inc<0||inc>UINT_MAX)fail("Timestamp component outside uint32 range");out.append(key,Timestamp(secs,inc));return;}
        if(name=="BinData"){
            auto type=integer(scalarArgument());expect(',');auto text=string();expect(')');
            auto data=QByteArray::fromBase64Encoding(QByteArray::fromStdString(text),QByteArray::AbortOnBase64DecodingErrors);
            if(!data)fail("Invalid base64 binary data");
            if(type<0||type>255)fail("Invalid binary subtype");
            out.appendBinData(key,data.decoded.size(),static_cast<BinDataType>(type),data.decoded.constData());return;
        }
        if(name=="UUID"||name=="LUUID"||name=="JUUID"||name=="NUUID"||name=="PYUUID") {
            auto text=string();expect(')');auto encoding=name=="JUUID" ? ::Robomongo::JavaLegacy : name=="NUUID" ? ::Robomongo::CSharpLegacy : name=="PYUUID" ? ::Robomongo::PythonLegacy : ::Robomongo::DefaultEncoding;
            auto hex=::Robomongo::HexUtils::uuidToHex(text,encoding);if(hex.size()!=32||!::Robomongo::HexUtils::isHexString(hex))fail("Invalid UUID");auto data=QByteArray::fromHex(QByteArray::fromStdString(hex));out.appendBinData(key,16,name=="UUID"?newUUID:bdtUUID,data.constData());return;
        }
        if(name=="RegExp"){auto pattern=string();std::string flags;if(take(','))flags=string();expect(')');out.appendRegex(key,pattern,flags);return;}
        if(name=="Code") {
            auto code=string();
            if(take(',')) {
                BSONObj scope;
                if(original.type()==CodeWScope) {
                    const auto fields=original.codeWScopeObject();
                    scope=document(false,&fields);
                } else scope=document(false);
                expect(')'); out.appendCodeWScope(key,code,scope);
            } else {expect(')');out.append(key,BSONCode(code));}
            return;
        }
        fail("Unsupported data constructor: "+name);
    }
};
}

BSONObj fromjson(const std::string &text) {
    try { return BSONObj::fromJson(text); } catch(const std::exception &) {}
    try {return DataParser(text).parse();}catch(const Robomongo::ParseMsgAssertionException&){throw;}catch(const std::exception &e){throw Robomongo::ParseMsgAssertionException(0,e.what(),0,e.what());}
}
BSONObj fromjson(const char *text) { return fromjson(std::string(text?text:"")); }
std::string tojson(const BSONObj &obj,JsonStringFormat format,bool pretty) { return obj.jsonString(format,pretty?1:0); }
namespace Robomongo {
BSONObj fromjson(const std::string &text) { return mongo::fromjson(text); }
BSONObj fromjson(const char *text,int *length) {
    if(!length)return mongo::fromjson(text);
    std::string input=text?text:"";
    DataParser parser(input);
    try {auto out=parser.parse(false);*length=static_cast<int>(parser.consumed());return out;}
    catch(const ParseMsgAssertionException&){throw;}
    catch(const std::exception &e){throw ParseMsgAssertionException(0,e.what(),static_cast<int>(parser.consumed()),e.what());}
}
BSONObj fromjson(const char *text,int *length,const BSONObj &original) {
    // Use source tokens even for strict JSON: a decoder's inferred Int32 type
    // would discard the original type of small Int64 values shown as digits.
    std::string input=text?text:"";
    DataParser parser(input,&original);
    try {
        auto out=parser.parse(length==nullptr);
        if(length)*length=static_cast<int>(parser.consumed());
        return out;
    }
    catch(const ParseMsgAssertionException&){throw;}
    catch(const std::exception &e){throw ParseMsgAssertionException(0,e.what(),static_cast<int>(parser.consumed()),e.what());}
}
bool isArray(StringData text) { auto value=text.toString();auto pos=value.find_first_not_of(" \t\r\n");return pos!=std::string::npos&&value[pos]=='['; }
std::string tojson(const BSONObj &obj,JsonStringFormat format,bool pretty) {return mongo::tojson(obj,format,pretty);}
}
}
