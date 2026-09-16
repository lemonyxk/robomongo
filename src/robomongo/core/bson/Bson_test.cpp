#include "robomongo/core/bson/Bson.h"
#include "robomongo/core/utils/BsonUtils.h"
#include <gtest/gtest.h>
#include <climits>
#include <cmath>

namespace {
mongo::BSONObj roundTrip(const mongo::BSONObj &value,
                        Robomongo::UUIDEncoding encoding=Robomongo::DefaultEncoding) {
    return mongo::Robomongo::fromjson(Robomongo::BsonUtils::jsonString(
        value,mongo::TenGen,1,encoding,Robomongo::Utc));
}
}

TEST(BsonValues, Int64AndDecimalRemainExactThroughEditorText) {
    auto value=BSON("max"<<LLONG_MAX<<"min"<<LLONG_MIN
        <<"decimal"<<mongo::Decimal128("1234567890123456789012345678901234"));
    auto copy=roundTrip(value);
    EXPECT_EQ(LLONG_MAX,copy["max"].Long());
    EXPECT_EQ(LLONG_MIN,copy["min"].Long());
    EXPECT_EQ(mongo::NumberLong,copy["max"].type());
    EXPECT_EQ("1234567890123456789012345678901234",copy["decimal"].numberDecimal().toString());
}

TEST(BsonValues, NumericTypesAndFloatingPointBitsSurviveEditing) {
    auto value=BSON("integer"<<1<<"long"<<1LL<<"double"<<1.0
        <<"precision"<<1.2345678901234567<<"negativeZero"<<-0.0);
    auto copy=roundTrip(value);
    EXPECT_EQ(mongo::NumberInt,copy["integer"].type());
    EXPECT_EQ(mongo::NumberLong,copy["long"].type());
    EXPECT_EQ(mongo::NumberDouble,copy["double"].type());
    EXPECT_EQ(value["precision"].Double(),copy["precision"].Double());
    EXPECT_TRUE(std::signbit(copy["negativeZero"].Double()));
}

TEST(BsonValues, DateTimestampObjectIdAndNestedArrayKeepTypes) {
    auto value=BSON("date"<<mongo::Date_t(-1234567890123LL)
        <<"stamp"<<mongo::Timestamp(1234567890,456)
        <<"oid"<<mongo::OID("00112233445566778899aabb")
        <<"nested"<<BSON_ARRAY(1<<"two"<<BSON("inside"<<3LL)));
    auto copy=roundTrip(value);
    EXPECT_EQ(0,value.woCompare(copy));
    EXPECT_EQ(mongo::Array,copy["nested"].type());
    EXPECT_TRUE(copy["nested"].Obj().isArray());
}

TEST(BsonValues, AllUuidEncodingsRoundTripTheirOriginalBytes) {
    const unsigned char bytes[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    mongo::BSONObjBuilder builder;
    builder.appendBinData("legacy",16,mongo::bdtUUID,bytes);
    builder.appendBinData("standard",16,mongo::newUUID,bytes);
    auto value=builder.obj();
    for(auto encoding:{Robomongo::DefaultEncoding,Robomongo::JavaLegacy,
                       Robomongo::CSharpLegacy,Robomongo::PythonLegacy})
        EXPECT_EQ(0,value.woCompare(roundTrip(value,encoding)));
}

TEST(BsonValues, RegexCodeScopeAndBinaryKeepTheirData) {
    const char bytes[]={0,1,static_cast<char>(255)};
    mongo::BSONObjBuilder builder;
    builder.appendRegex("pattern","a/\\w+","imsx");
    builder.appendCodeWScope("code","return x;",BSON("x"<<LLONG_MAX));
    builder.appendBinData("binary",3,mongo::BinDataGeneral,bytes);
    auto value=builder.obj();
    EXPECT_EQ(0,value.woCompare(roundTrip(value)));
}

TEST(BsonValues, CanonicalExtendedJsonAndShellSyntaxCanBeMixed) {
    auto value=mongo::fromjson("{plain: NumberInt(1), exact: {$numberLong: '9223372036854775807'}, nested: [NumberDecimal('1.25')]} ");
    EXPECT_EQ(mongo::NumberInt,value["plain"].type());
    EXPECT_EQ(LLONG_MAX,value["exact"].Long());
    EXPECT_EQ(mongo::NumberDecimal,value["nested"].Array().front().type());
}

TEST(BsonValues, EditorParsesSeveralDocumentsAndReportsConsumedBytes) {
    std::string text="/* 1 */ {first: NumberLong('1')}\n /* 2 */ {second: 2} // end\n ";
    int consumed=0;
    auto first=mongo::Robomongo::fromjson(text.c_str(),&consumed);
    ASSERT_GT(consumed,0);
    auto next=consumed;
    auto second=mongo::Robomongo::fromjson(text.c_str()+next,&consumed);
    EXPECT_EQ(1,first["first"].Long());
    EXPECT_EQ(2,second["second"].Int());
    EXPECT_EQ(text.size(),static_cast<std::size_t>(next+consumed));
}

TEST(BsonValues, ElementsRetainStorageAfterTheDocumentGoesOutOfScope) {
    mongo::BSONElement element;
    { auto value=BSON("keep"<<"alive"); element=value["keep"]; }
    EXPECT_EQ("alive",element.String());
}

TEST(BsonValues, StringsPreserveUnicodeEscapesAndEmbeddedNulls) {
    const std::string text="中文 \"quote\" \\ slash\n";
    auto value=BSON("a:b\""<<text<<"nul"<<std::string("a\0b",3));
    auto copy=roundTrip(value);
    EXPECT_EQ(text,copy["a:b\""].String());
    EXPECT_EQ(std::string("a\0b",3),copy["nul"].String());
}

TEST(BsonValues, ParserRejectsOverflowAndDoesNotExecuteCode) {
    EXPECT_THROW(mongo::fromjson("{x:NumberLong('9223372036854775808')}"),std::exception);
    EXPECT_THROW(mongo::fromjson("{x:NumberInt('2147483648')}"),std::exception);
    EXPECT_THROW(mongo::fromjson("{x:(function(){return 1})()}"),std::exception);
    EXPECT_THROW(mongo::fromjson("{x:{$numberLong:'invalid'}}"),std::exception);
    EXPECT_THROW(mongo::fromjson("{x:BinData(0,'%%%')}"),std::exception);
}

TEST(BsonValues, CanonicalCodeScopeRetainsShellInt64Types) {
    auto value=mongo::fromjson("{v:{$code:'return n;', $scope:{n:NumberLong('1')}}}");
    EXPECT_EQ(mongo::CodeWScope,value["v"].type());
    EXPECT_EQ(mongo::NumberLong,value["v"].codeWScopeObject()["n"].type());
}

TEST(BsonValues, TopLevelArraysAndExtremeDatesRoundTrip) {
    auto array=mongo::fromjson("[NumberInt(1), NumberLong('2')]");
    EXPECT_TRUE(array.isArray());
    EXPECT_EQ(2,array.nFields());
    auto value=BSON("low"<<mongo::Date_t(LLONG_MIN)<<"high"<<mongo::Date_t(LLONG_MAX));
    EXPECT_EQ(0,value.woCompare(roundTrip(value)));
}

TEST(BsonValues, PlainIntegerEditorTextKeepsExactValuesAndOriginalTypes) {
    mongo::BSONObjBuilder builder;
    builder.appendElements(BSON("integer"<<1<<"long"<<1LL<<"max"<<LLONG_MAX<<"min"<<LLONG_MIN
        <<"nested"<<BSON("n"<<2LL)
        <<"array"<<BSON_ARRAY(3LL<<4<<BSON("n"<<5LL))));
    builder.appendCodeWScope("code","return n;",BSON("n"<<6LL));
    const auto original=builder.obj();
    const auto text=Robomongo::BsonUtils::jsonString(original,mongo::TenGen,1,
        Robomongo::DefaultEncoding,Robomongo::Utc,false,true);
    EXPECT_EQ(std::string::npos,text.find("NumberInt("));
    EXPECT_EQ(std::string::npos,text.find("NumberLong("));
    EXPECT_NE(std::string::npos,text.find("9223372036854775807"));
    EXPECT_NE(std::string::npos,text.find("-9223372036854775808"));
    const auto copy=mongo::Robomongo::fromjson(text.c_str(),nullptr,original);
    EXPECT_EQ(0,original.woCompare(copy));
}

TEST(BsonValues, EditedBareIntegersRetainInt64AtMatchingFieldsAndArrayPositions) {
    const auto original=BSON("long"<<1LL<<"integer"<<2
        <<"nested"<<BSON("long"<<3LL)
        <<"array"<<BSON_ARRAY(4LL<<5<<BSON("long"<<6LL)));
    const auto edited=mongo::Robomongo::fromjson(
        "{array:[14,15,{long:16},17], nested:{long:13,added:18}, integer:12, long:11, added:19}",
        nullptr,original);
    EXPECT_EQ(mongo::NumberLong,edited["long"].type());
    EXPECT_EQ(11,edited["long"].Long());
    EXPECT_EQ(mongo::NumberInt,edited["integer"].type());
    EXPECT_EQ(mongo::NumberLong,edited["nested"].Obj()["long"].type());
    EXPECT_EQ(13,edited["nested"].Obj()["long"].Long());
    const auto array=edited["array"].Array();
    ASSERT_EQ(4,array.size());
    EXPECT_EQ(mongo::NumberLong,array[0].type());
    EXPECT_EQ(14,array[0].Long());
    EXPECT_EQ(mongo::NumberInt,array[1].type());
    EXPECT_EQ(mongo::NumberLong,array[2].Obj()["long"].type());
    EXPECT_EQ(16,array[2].Obj()["long"].Long());
    EXPECT_EQ(mongo::NumberInt,array[3].type());
    EXPECT_EQ(mongo::NumberInt,edited["nested"].Obj()["added"].type());
    EXPECT_EQ(mongo::NumberInt,edited["added"].type());
}

TEST(BsonValues, ExplicitTypesAndNonIntegerValuesOverrideEditorHints) {
    const auto original=BSON("constructor"<<1LL<<"extended"<<2LL
        <<"double"<<3LL<<"exponent"<<4LL<<"string"<<5LL<<"explicitLong"<<6);
    const auto edited=mongo::Robomongo::fromjson(
        "{constructor:NumberInt(11),extended:{$numberInt:'12'},double:13.5,"
        "exponent:14e0,string:'15',explicitLong:NumberLong('16')}",nullptr,original);
    EXPECT_EQ(mongo::NumberInt,edited["constructor"].type());
    EXPECT_EQ(mongo::NumberInt,edited["extended"].type());
    EXPECT_EQ(mongo::NumberDouble,edited["double"].type());
    EXPECT_EQ(mongo::NumberDouble,edited["exponent"].type());
    EXPECT_EQ(mongo::String,edited["string"].type());
    EXPECT_EQ(mongo::NumberLong,edited["explicitLong"].type());
}

TEST(BsonValues, EditorIntegerHintsPreserveCodeScopesInBothSupportedForms) {
    mongo::BSONObjBuilder builder;
    const auto scope=BSON("n"<<1LL<<"nested"<<BSON_ARRAY(2LL));
    builder.appendCodeWScope("shell","return n;",scope);
    builder.appendCodeWScope("json","return n;",scope);
    const auto edited=mongo::Robomongo::fromjson(
        "{shell:Code('return n;', {n:11,nested:[12]}),"
        "json:{$code:'return n;', $scope:{n:21,nested:[22]}}}",nullptr,builder.obj());
    EXPECT_EQ(mongo::CodeWScope,edited["shell"].type());
    EXPECT_EQ(mongo::CodeWScope,edited["json"].type());
    const auto shellScope=edited["shell"].codeWScopeObject();
    const auto jsonScope=edited["json"].codeWScopeObject();
    EXPECT_EQ(mongo::NumberLong,shellScope["n"].type());
    EXPECT_EQ(11,shellScope["n"].Long());
    EXPECT_EQ(mongo::NumberLong,shellScope["nested"].Array()[0].type());
    EXPECT_EQ(12,shellScope["nested"].Array()[0].Long());
    EXPECT_EQ(mongo::NumberLong,jsonScope["n"].type());
    EXPECT_EQ(21,jsonScope["n"].Long());
    EXPECT_EQ(mongo::NumberLong,jsonScope["nested"].Array()[0].type());
    EXPECT_EQ(22,jsonScope["nested"].Array()[0].Long());
}

TEST(BsonValues, BareEditorIntegersUseExactInt64BoundsAndRejectOverflow) {
    const auto original=BSON("max"<<1LL<<"min"<<2LL);
    const auto edited=mongo::Robomongo::fromjson(
        "{max:9223372036854775807,min:-9223372036854775808}",nullptr,original);
    EXPECT_EQ(LLONG_MAX,edited["max"].Long());
    EXPECT_EQ(LLONG_MIN,edited["min"].Long());
    EXPECT_THROW(mongo::Robomongo::fromjson("{max:9223372036854775808}",nullptr,original),
        mongo::Robomongo::ParseMsgAssertionException);
    EXPECT_THROW(mongo::Robomongo::fromjson("{min:-9223372036854775809}",nullptr,original),
        mongo::Robomongo::ParseMsgAssertionException);
    int consumed=0;
    const auto inferred=mongo::Robomongo::fromjson("{small:1,large:2147483648}",&consumed);
    EXPECT_EQ(mongo::NumberInt,inferred["small"].type());
    EXPECT_EQ(mongo::NumberLong,inferred["large"].type());
}

TEST(BsonValues, EditorHintsSupportTopLevelArraysAndDocumentStreamLengths) {
    const auto array=BSON_ARRAY(1LL<<2);
    const auto edited=mongo::Robomongo::fromjson("[11,12]",nullptr,array);
    EXPECT_TRUE(edited.isArray());
    EXPECT_EQ(mongo::NumberLong,edited["0"].type());
    EXPECT_EQ(mongo::NumberInt,edited["1"].type());
    const std::string text="/* first */ {n:11}\n /* second */ {n:12}";
    int consumed=0;
    const auto first=mongo::Robomongo::fromjson(text.c_str(),&consumed,BSON("n"<<1LL));
    ASSERT_GT(consumed,0);
    ASSERT_LT(static_cast<std::size_t>(consumed),text.size());
    const int next=consumed;
    const auto second=mongo::Robomongo::fromjson(text.c_str()+next,&consumed,BSON("n"<<2));
    EXPECT_EQ(mongo::NumberLong,first["n"].type());
    EXPECT_EQ(mongo::NumberInt,second["n"].type());
    EXPECT_EQ(text.size(),static_cast<std::size_t>(next+consumed));
}
