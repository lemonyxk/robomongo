// End-to-end regression against the isolated local test replica set only.
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include "robomongo/core/engine/ScriptEngine.h"
#include "robomongo/core/settings/ConnectionSettings.h"

using namespace Robomongo;
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
MongoShellExecResult execute(ScriptEngine& engine, const std::string& script) {
    auto result = engine.exec(script);
    require(!result.error(), result.errorMessage());
    return result;
}
}
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir profile;
    qputenv("ROBOMONGO_PROFILE_DIR", profile.path().toUtf8());
    ConnectionSettings settings(true);
    settings.setServerHost("127.0.0.1");
    settings.setServerPort(37027);
    settings.setReplicaSet(false);
    std::string const db = "robomongo_shell_test_" + std::to_string(QCoreApplication::applicationPid());
    settings.setDefaultDatabase(db);
    ScriptEngine engine(&settings, 10);
    try {
        engine.init(false);
        engine.setBatchSize(2);
        execute(engine, "db.items.insertMany(Array.from({length:8}, (_,i)=>({_id:i,n:i,keep:true})))");
        auto multi = execute(engine, "var remembered=40; remembered+2; print('shell output'); ({oid:ObjectId('0123456789abcdef01234567'),long:NumberLong('9223372036854775807')})");
        require(multi.results().size() == 3, "Expected separate scalar, print and document results");
        require(multi.results()[0].response() == "42", "Incorrect scalar result");
        require(multi.results()[1].response() == "shell output", "Printed output was lost");
        auto bson = multi.results()[2].documents().at(0)->bsonObj();
        require(bson["oid"].type() == mongo::jstOID, "ObjectId type was lost");
        require(bson["long"].numberLong() == 9223372036854775807LL, "64-bit BSON value was truncated");
        require(execute(engine, "remembered").results()[0].response() == "40", "Variables did not persist");
        auto mixed = execute(engine, "use " + db + "\nprint(db.getName());\n1 + 1");
        require(mixed.results().back().response() == "2", "Mixed shell commands and JavaScript failed");
        auto first = execute(engine, "db.items.find({}).sort({_id:1}).skip(1).limit(5)");
        require(first.results().size() == 1, "Expected one cursor result");
        require(first.results()[0].documents().size() == 2, "Initial page size incorrect");
        require(first.results()[0].documents()[0]->bsonObj().getIntField("_id") == 1, "Initial skip incorrect");
        auto info = first.results()[0].queryInfo();
        require(!info.runtimeCursorId.empty(), "Cursor token missing");
        require(!info.readOnly, "Unprojected find should be editable");
        info._skip = 3;
        auto second = engine.queryPage(info);
        require(second.size() == 2 && second[0]->bsonObj().getIntField("_id") == 3, "Second page incorrect");
        info._skip = 5;
        auto last = engine.queryPage(info);
        require(last.size() == 1 && last[0]->bsonObj().getIntField("_id") == 5, "Query limit not preserved");
        info._skip = 7;
        require(engine.queryPage(info).empty(), "Exhausted cursor returned documents");
        info._skip = 1;
        require(engine.queryPage(info)[0]->bsonObj().getIntField("_id") == 1, "Previous page cache failed");
        auto projected = execute(engine, "db.items.find({}, {_id:1}).hint('_id_').sort({_id:-1})");
        require(projected.results()[0].queryInfo().readOnly, "Projected documents must be read-only");
        require(!projected.results()[0].queryInfo()._fields.isEmpty(), "Projection metadata missing");
        auto aggregated = execute(engine, "db.items.aggregate([{$match:{n:{$gte:2}}},{$sort:{n:-1}},{$project:{_id:1,n:1}}])");
        require(aggregated.results()[0].queryInfo().readOnly, "Aggregation results must be read-only");
        auto aggregateInfo = aggregated.results()[0].queryInfo();
        aggregateInfo._skip = 2;
        require(engine.queryPage(aggregateInfo)[0]->bsonObj().getIntField("n") == 5, "Aggregation paging incorrect");
        execute(engine, "db.items.updateOne({_id:2},{$set:{n:42}})");
        auto updated = execute(engine, "db.items.findOne({_id:2})");
        require(updated.results()[0].documents()[0]->bsonObj().getIntField("n") == 42, "Acknowledged write failed");
        auto duplicate = engine.exec("db.items.insertOne({_id:2})");
        require(duplicate.error(), "Duplicate key error was not propagated");
        auto completions = engine.complete("ObjectI", AutocompleteAll);
        require(completions.contains("ObjectId()"), "BSON helper autocomplete failed");
        std::thread interrupter([&engine] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            engine.interrupt();
        });
        auto interrupted = engine.exec("while(true) {}");
        interrupter.join();
        require(interrupted.error(), "Infinite script was not interrupted");
        require(interrupted.errorMessage().find("reset") != std::string::npos, "Interrupted state reset was not disclosed");
        auto recovered = execute(engine, "typeof remembered; db.items.countDocuments({})");
        require(recovered.results()[0].response() == "undefined", "Interrupted variables were unexpectedly reused");
        require(recovered.results()[1].response() == "8", "Connection was not restored after interrupt");
        engine.changeTimeout(1);
        auto timedOut = engine.exec("while(true) {}");
        require(timedOut.error() && timedOut.timeoutReached(), "Execution timeout was not reported");
        engine.changeTimeout(10);
        require(execute(engine, "db.items.countDocuments({})").results()[0].response() == "8", "Timeout recovery failed");
        execute(engine, "db.dropDatabase()");
        std::cout << "Native ScriptEngine integration passed: BSON, state, output, paging, writes, autocomplete, interruption and recovery\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try { engine.exec("db.dropDatabase()"); } catch (...) {}
        return 1;
    }
}
