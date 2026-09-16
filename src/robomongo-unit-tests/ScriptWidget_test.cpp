#include <gtest/gtest.h>
#include <memory>

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QStringListModel>
#include <QTimer>
#include <QTimerEvent>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/domain/MongoServer.h"
#include "robomongo/core/domain/MongoShell.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/gui/editors/FindFrame.h"
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"
#include "robomongo/gui/widgets/workarea/QueryWidget.h"
#include "robomongo/gui/widgets/workarea/ScriptWidget.h"

using namespace Robomongo;

namespace {

class ScriptWidgetTest : public testing::Test {
protected:
    // Never initialize a worker or connect to MongoDB. Completion responses are
    // supplied below, so these tests exercise the GUI without database timing.
    MongoServer server{1, new ConnectionSettings(false), ConnectionSecondary};
    MongoShell shell{&server, ScriptInfo(QString())};
    std::unique_ptr<QueryWidget> query;
    ScriptWidget *script = nullptr;
    RoboScintilla *editor = nullptr;
    QCompleter *completer = nullptr;
    QTimer *completionTimer = nullptr;
    AutocompletionMode savedMode = AutocompleteAll;

    void SetUp() override
    {
        auto *settings = AppRegistry::instance().settingsManager();
        savedMode = settings->autocompletionMode();
        settings->setAutocompletionMode(AutocompleteAll);
        query = std::make_unique<QueryWidget>(&shell);
        script = query->findChild<ScriptWidget*>();
        ASSERT_NE(script, nullptr);
        editor = script->findChild<RoboScintilla*>("queryEditor");
        completer = script->findChild<QCompleter*>();
        completionTimer = script->findChild<QTimer*>("queryAutocompletionTimer");
        ASSERT_NE(editor, nullptr);
        ASSERT_NE(completer, nullptr);
        ASSERT_NE(completionTimer, nullptr);
        ASSERT_EQ(server.worker(), nullptr);
        query->resize(1000, 700);
        query->show();
        QApplication::setActiveWindow(query.get());
        editor->setFocus();
        QApplication::processEvents();
        ASSERT_TRUE(editor->hasFocus());
    }

    void TearDown() override
    {
        if (script)
            script->hideAutocompletion();
        query.reset();
        AppRegistry::instance().settingsManager()->setAutocompletionMode(savedMode);
    }

    // A single '|' marks the caret. Return exactly the context sent by the
    // editor for ordinary-sized queries, including all preceding lines.
    QString placeCaret(const QString &markedText)
    {
        const int caret = markedText.indexOf('|');
        EXPECT_GE(caret, 0);
        QString text = markedText;
        text.remove(caret, 1);
        const QString prefix = text.left(caret);
        script->setText(text);
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, static_cast<int>(prefix.toUtf8().size()));
        editor->setFocus();
        flushEditorUpdate();
        script->hideAutocompletion();
        return prefix;
    }

    void flushEditorUpdate()
    {
        // SCI_GOTOPOS and keyboard navigation queue SC_UPDATE_SELECTION.
        // A real viewport paint runs Editor::NotifyUpdateUI(), which delivers
        // QScintilla's cursorPositionChanged signal; moving alone is not input.
        editor->viewport()->repaint();
    }

    void pressKey(int key, const QString &text = QString(), bool repaint = true)
    {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
        QApplication::sendEvent(editor, &event);
        if (repaint)
            flushEditorUpdate();
    }

    QString typeLastCharacter(QString markedText)
    {
        const int caret = markedText.indexOf('|');
        EXPECT_GT(caret, 0);
        const QChar typed = markedText.at(caret - 1);
        QString expected = markedText;
        expected.remove(caret, 1);
        markedText.remove(caret - 1, 1);
        // Opening a quote or object now creates its closing delimiter.
        // Leave that work to the actual editor keypress instead of seeding it.
        const QChar closing = typed == '{' ? QChar('}') : typed;
        if ((typed == '"' || typed == '{') &&
                caret < markedText.size() && markedText.at(caret) == closing)
            markedText.remove(caret, 1);
        const QString prefix = placeCaret(markedText) + typed;
        pressKey(typed.toUpper().unicode(), QString(typed));
        EXPECT_EQ(script->text(), expected);
        return prefix;
    }

    void expectCaretAfter(const QString &prefix)
    {
        EXPECT_EQ(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS),
                  static_cast<long>(prefix.toUtf8().size()));
    }

    void expectStructuredNewline(const QString &before, QString after,
                                 int key = Qt::Key_Return)
    {
        placeCaret(before);
        const QString original = script->text();
        const int caret = after.indexOf('|');
        ASSERT_GE(caret, 0);
        after.remove(caret, 1);
        int inputs = 0;
        const auto connection = QObject::connect(editor, &RoboScintilla::textInsertedByUser,
                                                 [&inputs] { ++inputs; });
        pressKey(key, "\r");
        QObject::disconnect(connection);
        EXPECT_EQ(script->text(), after);
        expectCaretAfter(after.left(caret));
        EXPECT_EQ(inputs, 0);
        EXPECT_FALSE(completionTimer->isActive());
        EXPECT_FALSE(completer->popup()->isVisible());

        // Delimiter splitting and its indentation form one reversible edit.
        editor->undo();
        flushEditorUpdate();
        EXPECT_EQ(script->text(), original);
        EXPECT_FALSE(completionTimer->isActive());
    }

    QString request(const QString &markedText)
    {
        const QString prefix = placeCaret(markedText);
        script->showAutocompletion();
        return prefix;
    }

    void fireCompletionTimer()
    {
        ASSERT_TRUE(completionTimer->isActive());
        // Exercise the scheduled timer's real timeout signal deterministically,
        // without sleeping or invoking ScriptWidget's completion slot directly.
        QTimerEvent timeout(completionTimer->timerId());
        QApplication::sendEvent(completionTimer, &timeout);
        EXPECT_FALSE(completionTimer->isActive());
    }

    void reply(const QString &prefix, const QStringList &tokens)
    {
        script->showAutocompletion(tokens, prefix);
    }

    QStringList suggestions() const
    {
        return static_cast<QStringListModel*>(completer->model())->stringList();
    }

    void accept(const QString &suggestion)
    {
        ASSERT_TRUE(completer->popup()->isVisible());
        ASSERT_TRUE(suggestions().contains(suggestion));
        ASSERT_TRUE(QMetaObject::invokeMethod(script, "onCompletionActivated",
                                             Qt::DirectConnection,
                                             Q_ARG(QString, suggestion)));
    }

    void acceptWithKeyAndTypeAgain(int key)
    {
        const QString prefix = request("db.users.find({na|})");
        reply(prefix, {"name", "nationality"});
        ASSERT_TRUE(completer->popup()->isVisible());
        ASSERT_EQ(completer->popup()->currentIndex().data().toString(), "name");
        const QString keyText = key == Qt::Key_Tab ? "\t" : "\r";
        QKeyEvent acceptKey(QEvent::KeyPress, key, Qt::NoModifier, keyText);
        QApplication::sendEvent(completer->popup(), &acceptKey);
        ASSERT_EQ(script->text(), "db.users.find({name})");
        ASSERT_FALSE(completer->popup()->isVisible());

        // Completion must consume its own key without leaving the editor's
        // ignore flag set: the very next ordinary keypress still edits text.
        editor->setFocus();
        const QString completed = script->text();
        const int lines = editor->lines();
        QKeyEvent ordinaryKey(QEvent::KeyPress, key, Qt::NoModifier, keyText);
        QApplication::sendEvent(editor, &ordinaryKey);
        EXPECT_NE(script->text(), completed);
        EXPECT_EQ(editor->lines(), lines + (key == Qt::Key_Tab ? 0 : 1));
    }
};

TEST_F(ScriptWidgetTest, TypingDotSchedulesDebouncedAutomaticCompletion)
{
    placeCaret("db|");
    pressKey(Qt::Key_Period, ".");
    ASSERT_EQ(script->text(), "db.");
    ASSERT_TRUE(completionTimer->isActive());
    EXPECT_TRUE(completionTimer->isSingleShot());
    EXPECT_EQ(completionTimer->interval(), 120);
    reply("db.", {"db.users"});
    EXPECT_FALSE(completer->popup()->isVisible());
    fireCompletionTimer();
    reply("db.", {"db.users"});
    EXPECT_TRUE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, AutomaticCompletionContinuesAfterDotAndPartialMethod)
{
    const QStringList contexts{"db.|", "db.users.fi|", "db.getCollection('users').|"};
    const QStringList candidates{"db.users", "db.users.find", ".find"};
    for (int i = 0; i < contexts.size(); ++i) {
        SCOPED_TRACE(contexts.at(i).toStdString());
        const QString prefix = typeLastCharacter(contexts.at(i));
        fireCompletionTimer();
        reply(prefix, {candidates.at(i)});
        EXPECT_TRUE(completer->popup()->isVisible());
    }
}

TEST_F(ScriptWidgetTest, MovingCaretIntoExistingBracesOrQuotesNeverTriggersCompletion)
{
    const QStringList contexts{
        "db.users.find({}|)", "db.users.find({''|: 1})", "db.users.find({\"\"|: 1})"};
    for (const QString &context : contexts) {
        SCOPED_TRACE(context.toStdString());
        const QString prefix = placeCaret(context);
        const QString original = script->text();
        const long originalPosition = editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS);
        int textChanges = 0, cursorChanges = 0;
        const auto textConnection = QObject::connect(editor, &RoboScintilla::textChanged,
                                                     [&textChanges] { ++textChanges; });
        const auto cursorConnection = QObject::connect(editor, &RoboScintilla::cursorPositionChanged,
                                                       [&cursorChanges](int, int) { ++cursorChanges; });
        pressKey(Qt::Key_Left);
        QObject::disconnect(textConnection);
        QObject::disconnect(cursorConnection);
        EXPECT_EQ(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS), originalPosition - 1);
        EXPECT_GT(cursorChanges, 0);
        EXPECT_EQ(textChanges, 0);
        EXPECT_EQ(script->text(), original);
        EXPECT_FALSE(completionTimer->isActive());
        reply(prefix.left(prefix.size() - 1), {"name", "age"});
        EXPECT_FALSE(completer->popup()->isVisible());
    }

    // Navigation cancels an edit's pending request even before QScintilla has
    // painted the edit-associated cursor update.
    const QString prefix = placeCaret("db.users.find({n|})");
    pressKey(Qt::Key_A, "a", false);
    ASSERT_TRUE(completionTimer->isActive());
    pressKey(Qt::Key_Left, QString(), false);
    EXPECT_FALSE(completionTimer->isActive());
    flushEditorUpdate();
    EXPECT_FALSE(completionTimer->isActive());
    reply(prefix, {"name", "nationality"});
    EXPECT_FALSE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, AutomaticCompletionInsideEitherQuoteStyle)
{
    const QStringList contexts{
        "db.users.find({\"|\": 1})", "db.users.find({'|': 1})",
        "db.getCollection('|')", "db.getCollection(\"|\")"};
    for (const QString &context : contexts) {
        SCOPED_TRACE(context.toStdString());
        const QString prefix = typeLastCharacter(context);
        fireCompletionTimer();
        reply(prefix, {"name"});
        EXPECT_TRUE(completer->popup()->isVisible());
    }
}

TEST_F(ScriptWidgetTest, AutomaticFieldCompletionContinuesAfterBraceCommaAndWhitespace)
{
    const QStringList contexts{
        "db.users.find({|})", "db.users.find({na|})", "db.users.find({\n    na|\n})",
        "db.users.find({active: true,|})", "db.users.find({active: true, na|})",
        "db.users.find({active: true,   |})"};
    for (const QString &context : contexts) {
        SCOPED_TRACE(context.toStdString());
        const QString prefix = typeLastCharacter(context);
        fireCompletionTimer();
        reply(prefix, {"name", "nationality"});
        EXPECT_TRUE(completer->popup()->isVisible());
    }
}

TEST_F(ScriptWidgetTest, AutomaticCompletionOffersGlobalObjectsAndFunctions)
{
    const QStringList contexts{
        "r|", "s|", "Obj|", "pri|", "const value = Obj|",
        "db.users.find({_id: Obj|})", "db.users.find({count: NumberL|})",
        "rs.status();\nObj|", "print(Obj|)", "customHe|"};
    const QStringList candidates{
        "rs", "sh", "ObjectId()", "print()", "ObjectId()",
        "ObjectId()", "NumberLong()", "ObjectId()", "ObjectId()", "customHelper()"};
    for (int i = 0; i < contexts.size(); ++i) {
        SCOPED_TRACE(contexts.at(i).toStdString());
        const QString prefix = typeLastCharacter(contexts.at(i));
        fireCompletionTimer();
        reply(prefix, {candidates.at(i)});
        EXPECT_TRUE(completer->popup()->isVisible());
    }
}

TEST_F(ScriptWidgetTest, AutomaticCompletionOffersGlobalObjectMethods)
{
    const QStringList contexts{"rs.|", "rs.st|", "sh.en|", "JSON.str|", "Math.ma|"};
    const QStringList candidates{
        "rs.status()", "rs.status()", "sh.enableSharding()", "JSON.stringify()", "Math.max()"};
    for (int i = 0; i < contexts.size(); ++i) {
        SCOPED_TRACE(contexts.at(i).toStdString());
        const QString prefix = typeLastCharacter(contexts.at(i));
        fireCompletionTimer();
        reply(prefix, {candidates.at(i)});
        EXPECT_TRUE(completer->popup()->isVisible());
    }
}

TEST_F(ScriptWidgetTest, AutomaticGlobalCompletionSkipsCommentsRegexTemplatesAndNumbers)
{
    const QStringList contexts{
        "// rs|", "/* Obj|", "const pattern = /rs|",
        "const pattern = true ? /Obj|", "const pattern = value && /Obj|",
        "const text = `Obj|", "123|"};
    for (const QString &context : contexts) {
        SCOPED_TRACE(context.toStdString());
        const QString prefix = typeLastCharacter(context);
        fireCompletionTimer();
        reply(prefix, {"rs", "ObjectId()"});
        EXPECT_FALSE(completer->popup()->isVisible());
    }
}

TEST_F(ScriptWidgetTest, AutomaticGlobalCompletionWithoutMatchingNamesStaysClosed)
{
    const QString prefix = typeLastCharacter("db.users.find({active: tr|})");
    fireCompletionTimer();
    reply(prefix, {});
    EXPECT_FALSE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, AutomaticGlobalMethodAcceptancePreservesArgumentsAndCaret)
{
    const QString prefix = typeLastCharacter("rs.st|atus({initialSync: 1})");
    fireCompletionTimer();
    reply(prefix, {"rs.status()"});
    accept("rs.status()");
    EXPECT_EQ(script->text(), "rs.status({initialSync: 1})");
    expectCaretAfter("rs.status(");
}

TEST_F(ScriptWidgetTest, ClosedQuotesAndBracesDoNotAutomaticallyOfferGlobals)
{
    const QStringList contexts{
        "db.users.find({\"name\"|: 1})", "db.users.find({'name'|: 1})",
        "db.users.find({}|)", "db.users.find({})|"};
    for (const QString &context : contexts) {
        SCOPED_TRACE(context.toStdString());
        const QString prefix = typeLastCharacter(context);
        fireCompletionTimer();
        reply(prefix, {"ObjectId", "db"});
        EXPECT_FALSE(completer->popup()->isVisible());
    }
}

TEST_F(ScriptWidgetTest, ManualCompletionBypassesAutomaticContextGate)
{
    const QString prefix = request("const value = |");
    reply(prefix, {"ObjectId"});
    EXPECT_TRUE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, EscapeStopsScheduledAutomaticCompletion)
{
    const QString prefix = typeLastCharacter("db.users.find({|})");
    ASSERT_TRUE(completionTimer->isActive());
    pressKey(Qt::Key_Escape);
    EXPECT_FALSE(completionTimer->isActive());
    reply(prefix, {"name", "age"});
    EXPECT_FALSE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, ReturnAcceptsPopupWithoutNewlineAndNextReturnStillWorks)
{
    acceptWithKeyAndTypeAgain(Qt::Key_Return);
}

TEST_F(ScriptWidgetTest, KeypadEnterAcceptsPopupWithoutNewlineAndNextEnterStillWorks)
{
    acceptWithKeyAndTypeAgain(Qt::Key_Enter);
}

TEST_F(ScriptWidgetTest, TabAcceptsPopupWithoutWhitespaceAndNextTabStillWorks)
{
    acceptWithKeyAndTypeAgain(Qt::Key_Tab);
}

TEST_F(ScriptWidgetTest, MultilineFieldCompletionPreservesQueryAndShowsOnlyTheField)
{
    const QString prefix = request("db.users.find({\n    na|\n}).limit(20)");
    reply(prefix, {"name", "nationality"});
    EXPECT_EQ(suggestions(), (QStringList{"name", "nationality"}));
    accept("name");
    EXPECT_EQ(script->text(), "db.users.find({\n    name\n}).limit(20)");
}

TEST_F(ScriptWidgetTest, OperatorCompletionReplacesOnlyTheOperator)
{
    const QString prefix = request("db.users.find({\n    age: { $g|: 21 }\n})");
    reply(prefix, {"$gt", "$gte"});
    EXPECT_EQ(suggestions(), (QStringList{"$gt", "$gte"}));
    accept("$gte");
    EXPECT_EQ(script->text(), "db.users.find({\n    age: { $gte: 21 }\n})");
}

TEST_F(ScriptWidgetTest, EmptyTokenAtObjectStartAcceptsAField)
{
    const QString prefix = request("db.users.find({|})");
    reply(prefix, {"name", "age"});
    accept("name");
    EXPECT_EQ(script->text(), "db.users.find({name})");
}

TEST_F(ScriptWidgetTest, MiddleOfTokenReplacementPreservesFollowingPunctuation)
{
    const QString prefix = request("db.users.find({ na|tionality: 'US', active: true })");
    reply(prefix, {"name"});
    accept("name");
    EXPECT_EQ(script->text(), "db.users.find({ name: 'US', active: true })");
    editor->undo();
    EXPECT_EQ(script->text(), "db.users.find({ nationality: 'US', active: true })");
}

TEST_F(ScriptWidgetTest, QuotedNestedFieldKeepsExactlyOneClosingQuote)
{
    const QString prefix = request("db.users.find({ \"prof|ile.old\": 1 }).limit(5)");
    reply(prefix, {"profile.name\""});
    accept("profile.name\"");
    EXPECT_EQ(script->text(), "db.users.find({ \"profile.name\": 1 }).limit(5)");
}

TEST_F(ScriptWidgetTest, CollectionCompletionKeepsExactlyOneSingleQuote)
{
    const QString prefix = request("db.getCollection('us|').find({})");
    reply(prefix, {"users'"});
    accept("users'");
    EXPECT_EQ(script->text(), "db.getCollection('users').find({})");
}

TEST_F(ScriptWidgetTest, QuotedCollectionCompletionReplacesHyphenAndSpaceSuffix)
{
    const QString prefix = request("db.getCollection('ord|ers-archive old').find({active: true})");
    reply(prefix, {"orders"});
    accept("orders");
    EXPECT_EQ(script->text(), "db.getCollection('orders').find({active: true})");
}

TEST_F(ScriptWidgetTest, QuotedFieldCompletionScansPastEscapedQuoteAndPreservesQuerySuffix)
{
    const QString prefix = request("db.users.find({ \"user-na|me\\\"old-extra\": 1, active: true })");
    // The already typed 'user-' is before the replaceable trailing token.
    reply(prefix, {"name"});
    accept("name");
    EXPECT_EQ(script->text(), "db.users.find({ \"user-name\": 1, active: true })");
}

TEST_F(ScriptWidgetTest, FullJsonFieldCandidateReplacesUnquotedTokenAndUndoesOnce)
{
    const QString original = "db.users.find({ nationality: 'US', active: true })";
    const QString prefix = request("db.users.find({ na|tionality: 'US', active: true })");
    reply(prefix, {"\"name\"", "\"nationality\""});
    EXPECT_EQ(suggestions(), (QStringList{"\"name\"", "\"nationality\""}));
    accept("\"name\"");
    EXPECT_EQ(script->text(), "db.users.find({ \"name\": 'US', active: true })");
    editor->undo();
    EXPECT_EQ(script->text(), original);
}

TEST_F(ScriptWidgetTest, FullJsonOperatorCandidateKeepsSurroundingFilter)
{
    const QString prefix = request("db.users.find({ \"age\": { $g|: 21 } })");
    reply(prefix, {"\"$gt\"", "\"$gte\""});
    accept("\"$gte\"");
    EXPECT_EQ(script->text(), "db.users.find({ \"age\": { \"$gte\": 21 } })");
}

TEST_F(ScriptWidgetTest, FullJsonCandidateReplacesEitherQuoteStyleAndWholeKey)
{
    for (const QChar quote : {QChar('\''), QChar('"')}) {
        const QString marked = QString::fromUtf8("db.users.find({ icon: '😀', ") +
            quote + "user-na|me old" + quote + ": 1 }).limit(5)";
        const QString prefix = request(marked);
        reply(prefix, {"\"user-name\""});
        accept("\"user-name\"");
        EXPECT_EQ(script->text(), QString::fromUtf8(
            "db.users.find({ icon: '😀', \"user-name\": 1 }).limit(5)"));
        QString original = marked;
        original.remove('|');
        editor->undo();
        EXPECT_EQ(script->text(), original);
    }
}

TEST_F(ScriptWidgetTest, FullJsonCandidatePreservesEscapedQuotesAndBackslashes)
{
    const QString prefix = request(R"JS(db.users.find({ "a|bc\"old": 1 }))JS");
    const QString candidate = R"JS("a\"b\\c")JS";
    reply(prefix, {candidate});
    accept(candidate);
    EXPECT_EQ(script->text(), R"JS(db.users.find({ "a\"b\\c": 1 }))JS");
}

TEST_F(ScriptWidgetTest, FullJsonCollectionCandidateNormalizesQuotesWithoutDuplicating)
{
    const QString prefix = request("db.getCollection('orders-ar|chive old').find({})");
    reply(prefix, {"\"orders-archive\""});
    accept("\"orders-archive\"");
    EXPECT_EQ(script->text(), "db.getCollection(\"orders-archive\").find({})");
}

TEST_F(ScriptWidgetTest, FullJsonCandidateSuppliesMissingClosingQuote)
{
    const QString prefix = request("db.getCollection('us|");
    reply(prefix, {"\"users\""});
    accept("\"users\"");
    EXPECT_EQ(script->text(), "db.getCollection(\"users\"");
}

TEST_F(ScriptWidgetTest, MethodCompletionKeepsExistingOpeningParenthesis)
{
    const QString prefix = request("db.users.fi|nd({active: true}).limit(5)");
    reply(prefix, {"db.users.find("});
    accept("db.users.find(");
    EXPECT_EQ(script->text(), "db.users.find({active: true}).limit(5)");
}

TEST_F(ScriptWidgetTest, UnicodeBeforeCaretUsesCharacterColumnsAndPreservesText)
{
    const QString prefix = request(QString::fromUtf8(
        "// 查询用户\ndb.users.find({ 城市: '台北', na|: 1 })"));
    reply(prefix, {"name"});
    accept("name");
    EXPECT_EQ(script->text(), QString::fromUtf8(
        "// 查询用户\ndb.users.find({ 城市: '台北', name: 1 })"));
}

TEST_F(ScriptWidgetTest, AstralCharacterBeforeCaretDoesNotShiftTokenReplacement)
{
    QString text = QString::fromUtf8("db.users.find({ icon: '😀', na|tionality: 1 })");
    const int caret = text.indexOf('|');
    text.remove(caret, 1);
    const QString prefix = text.left(caret);
    script->setText(text);
    // Scintilla's native positions are UTF-8 bytes, while its public line
    // indexes count code points rather than QString's UTF-16 code units.
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, static_cast<int>(prefix.toUtf8().size()));
    script->showAutocompletion();
    reply(prefix, {"name"});
    accept("name");
    EXPECT_EQ(script->text(), QString::fromUtf8(
        "db.users.find({ icon: '😀', name: 1 })"));
}

TEST_F(ScriptWidgetTest, EditingTextDiscardsLateReplyAndPreventsStaleAcceptance)
{
    const QString prefix = request("db.users.find({na|})");
    editor->insert("m");
    const QString edited = script->text();
    reply(prefix, {"name"});
    EXPECT_FALSE(completer->popup()->isVisible());
    ASSERT_TRUE(QMetaObject::invokeMethod(script, "onCompletionActivated",
                                         Qt::DirectConnection,
                                         Q_ARG(QString, QString("name"))));
    EXPECT_EQ(script->text(), edited);
}

TEST_F(ScriptWidgetTest, MovingCaretDiscardsLateReply)
{
    const QString prefix = request("db.users.find({na|})");
    editor->setCursorPosition(0, 0);
    reply(prefix, {"name"});
    EXPECT_FALSE(completer->popup()->isVisible());
    ASSERT_TRUE(QMetaObject::invokeMethod(script, "onCompletionActivated",
                                         Qt::DirectConnection,
                                         Q_ARG(QString, QString("name"))));
    EXPECT_EQ(script->text(), "db.users.find({na})");
}

TEST_F(ScriptWidgetTest, EscapeBeforeReplyCancelsThePendingPopup)
{
    const QString prefix = request("db.users.find({na|})");
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(editor, &escape);
    reply(prefix, {"name"});
    EXPECT_FALSE(completer->popup()->isVisible());
    EXPECT_EQ(script->text(), "db.users.find({na})");
}

TEST_F(ScriptWidgetTest, EscapeOnPopupPreventsLateReplyFromReopeningIt)
{
    const QString prefix = request("db.users.find({na|})");
    reply(prefix, {"name"});
    ASSERT_TRUE(completer->popup()->isVisible());
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(completer->popup(), &escape);
    ASSERT_FALSE(completer->popup()->isVisible());
    editor->setFocus();
    reply(prefix, {"name"});
    EXPECT_FALSE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, DisabledPreferenceSuppressesCompletion)
{
    AppRegistry::instance().settingsManager()->setAutocompletionMode(AutocompleteNone);
    const QString prefix = request("db.users.find({na|})");
    reply(prefix, {"name"});
    EXPECT_FALSE(completer->popup()->isVisible());
    EXPECT_TRUE(suggestions().isEmpty());
}

TEST_F(ScriptWidgetTest, DisablingPreferenceWhileRequestIsPendingSuppressesReply)
{
    const QString prefix = request("db.users.find({na|})");
    AppRegistry::instance().settingsManager()->setAutocompletionMode(AutocompleteNone);
    reply(prefix, {"name"});
    EXPECT_FALSE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, PopupDeduplicatesAndBoundsAnOversizedResponse)
{
    const QString prefix = request("db.users.find({f|})");
    QStringList candidates{"", "f\ninvalid", QString(600, 'x')};
    for (int i = 0; i < 1000; ++i) {
        const QString candidate = "f" + QString::number(i);
        candidates << candidate << candidate;
    }
    reply(prefix, candidates);
    ASSERT_TRUE(completer->popup()->isVisible());
    EXPECT_LE(suggestions().size(), 200);
    EXPECT_EQ(suggestions().count("f0"), 1);
    EXPECT_EQ(suggestions().first(), "f0");
}

TEST_F(ScriptWidgetTest, VeryLongSingleLineDoesNotStartCompletion)
{
    const QString text = QString(70000, ' ') + "db.users.find({na|})";
    const QString prefix = request(text);
    reply(prefix, {"name"});
    EXPECT_FALSE(completer->popup()->isVisible());
    EXPECT_TRUE(suggestions().isEmpty());
}

TEST_F(ScriptWidgetTest, EditorDefaultsToOneLineAndRemainsResizable)
{
    script->setText("db.users.find({})");
    script->ui_queryLinesCountChanged();
    auto *frame = script->findChild<FindFrame*>();
    ASSERT_NE(frame, nullptr);
    const int oneLineHeight = editor->textHeight(-1) + 8;
    EXPECT_EQ(editor->minimumHeight(), oneLineHeight);
    EXPECT_EQ(frame->minimumSizeHint().height(), oneLineHeight);
    EXPECT_GT(editor->maximumHeight(), editor->minimumHeight());
    EXPECT_GT(frame->maximumHeight(), frame->minimumHeight());
    script->disableFixedHeight();
    EXPECT_EQ(editor->minimumHeight(), oneLineHeight);
    EXPECT_GT(editor->maximumHeight(), editor->minimumHeight());
    EXPECT_GT(frame->maximumHeight(), frame->minimumHeight());
}

TEST_F(ScriptWidgetTest, MultilinePreferredHeightKeepsOneLineMinimumForSplitter)
{
    script->setText("db.users.find({})");
    const int singleLineHeight = script->preferredHeight();
    script->setText("db.users.find({\n  \"name\": \"Ada\"\n})");
    EXPECT_EQ(script->preferredHeight(), singleLineHeight + 2 * editor->textHeight(-1));
    EXPECT_EQ(editor->minimumHeight(), editor->textHeight(-1) + 8);
}

TEST_F(ScriptWidgetTest, FindPanelOnlyReservesHeightWhileVisible)
{
    const auto settleLayoutRequests = [] {
        // FindFrame updates first; its ancestors invalidate their cached size
        // hints through posted layout requests. Do not run unrelated timers.
        for (int pass = 0; pass < 3; ++pass)
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    };
    script->setText("db.users.find({})");
    auto *frame = script->findChild<FindFrame*>();
    ASSERT_NE(frame, nullptr);
    auto *findLine = frame->findChild<QLineEdit*>();
    ASSERT_NE(findLine, nullptr);
    ASSERT_FALSE(findLine->isVisible());
    settleLayoutRequests();
    const int hiddenHeight = frame->minimumSizeHint().height();
    const int hiddenPreferredHeight = script->preferredHeight();

    QKeyEvent showFind(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier, "f");
    QApplication::sendEvent(frame, &showFind);
    ASSERT_TRUE(findLine->isVisible());
    settleLayoutRequests();
    EXPECT_GE(frame->minimumSizeHint().height(), hiddenHeight + frame->findPanelHeight());
    EXPECT_GE(script->preferredHeight(), hiddenPreferredHeight + frame->findPanelHeight());

    QKeyEvent hideFind(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(frame, &hideFind);
    EXPECT_FALSE(findLine->isVisible());
    settleLayoutRequests();
    EXPECT_EQ(frame->minimumSizeHint().height(), hiddenHeight);
    EXPECT_EQ(script->preferredHeight(), hiddenPreferredHeight);
}

TEST_F(ScriptWidgetTest, BackspaceAndDeleteDoNotTriggerCompletion)
{
    placeCaret("db.users.fi|n");
    pressKey(Qt::Key_Backspace);
    EXPECT_EQ(script->text(), "db.users.fn");
    EXPECT_FALSE(completionTimer->isActive());
    reply("db.users.f", {"db.users.find("});
    EXPECT_FALSE(completer->popup()->isVisible());

    placeCaret("db.users.fi|n");
    pressKey(Qt::Key_Delete);
    EXPECT_EQ(script->text(), "db.users.fi");
    EXPECT_FALSE(completionTimer->isActive());
    reply("db.users.fi", {"db.users.find("});
    EXPECT_FALSE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, DeletingSelectionDoesNotTriggerCompletion)
{
    for (const int key : {Qt::Key_Backspace, Qt::Key_Delete}) {
        SCOPED_TRACE(key);
        placeCaret("db.users.find|");
        editor->setSelection(0, 9, 0, 13);
        flushEditorUpdate();
        ASSERT_TRUE(editor->hasSelectedText());
        pressKey(key);
        EXPECT_EQ(script->text(), "db.users.");
        EXPECT_FALSE(completionTimer->isActive());
        reply("db.users.", {"db.users.find("});
        EXPECT_FALSE(completer->popup()->isVisible());
    }
}

TEST_F(ScriptWidgetTest, DeletionCancelsPendingTypingBeforeDeferredCursorUpdate)
{
    placeCaret("db.users.f|");
    pressKey(Qt::Key_I, "i", false);
    ASSERT_TRUE(completionTimer->isActive());
    pressKey(Qt::Key_Backspace, QString(), false);
    EXPECT_EQ(script->text(), "db.users.f");
    EXPECT_FALSE(completionTimer->isActive());
    flushEditorUpdate();
    EXPECT_FALSE(completionTimer->isActive());
    reply("db.users.fi", {"db.users.find("});
    EXPECT_FALSE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, NewlineAndAutoIndentCancelPendingCompletion)
{
    for (const int key : {Qt::Key_Return, Qt::Key_Enter}) {
        SCOPED_TRACE(key);
        const QString prefix = placeCaret("db.users.find({\n    na|})") + "m";
        pressKey(Qt::Key_M, "m", false);
        ASSERT_TRUE(completionTimer->isActive());
        const int lines = editor->lines();
        int inputs = 0;
        const auto connection = QObject::connect(editor, &RoboScintilla::textInsertedByUser,
                                                   [&inputs] { ++inputs; });
        pressKey(key, "\r", false);
        QObject::disconnect(connection);
        EXPECT_EQ(editor->lines(), lines + 1);
        EXPECT_EQ(inputs, 0);
        EXPECT_FALSE(completionTimer->isActive());
        flushEditorUpdate();
        EXPECT_FALSE(completionTimer->isActive());
        reply(prefix, {"name", "names"});
        EXPECT_FALSE(completer->popup()->isVisible());
    }
}

TEST_F(ScriptWidgetTest, InputMethodNewlineWithIndentationDoesNotTriggerCompletion)
{
    placeCaret("db.users.find({|})");
    const int lines = editor->lines();
    int inputs = 0;
    const auto connection = QObject::connect(editor, &RoboScintilla::textInsertedByUser,
                                               [&inputs] { ++inputs; });
    QInputMethodEvent newline;
    newline.setCommitString("\n    ");
    QApplication::sendEvent(editor, &newline);
    QObject::disconnect(connection);
    EXPECT_EQ(editor->lines(), lines + 1);
    EXPECT_EQ(inputs, 0);
    EXPECT_FALSE(completionTimer->isActive());
    flushEditorUpdate();
    EXPECT_FALSE(completionTimer->isActive());
    EXPECT_FALSE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, NewlineExpandsEmptyObjectFunctionAndArrayWithAlignedClosers)
{
    editor->setIndentationWidth(4);
    editor->setEolMode(QsciScintilla::EolUnix);
    const QStringList before{
        "db.users.find({|}).limit(5)",
        "db.users.find(|).limit(5)",
        "db.users.aggregate([|])"};
    const QStringList after{
        "db.users.find({\n    |\n}).limit(5)",
        "db.users.find(\n    |\n).limit(5)",
        "db.users.aggregate([\n    |\n])"};
    for (const int key : {Qt::Key_Return, Qt::Key_Enter}) {
        SCOPED_TRACE(key);
        for (int i = 0; i < before.size(); ++i) {
            SCOPED_TRACE(before.at(i).toStdString());
            expectStructuredNewline(before.at(i), after.at(i), key);
        }
    }
}

TEST_F(ScriptWidgetTest, NewlineIndentsNestedObjectFromItsOpeningLine)
{
    editor->setIndentationWidth(4);
    editor->setEolMode(QsciScintilla::EolUnix);
    expectStructuredNewline(
        "db.users.find({\n    profile: {|}\n})",
        "db.users.find({\n    profile: {\n        |\n    }\n})");
}

TEST_F(ScriptWidgetTest, NewlineAfterFieldCommaKeepsNestedContentIndentation)
{
    editor->setIndentationWidth(4);
    editor->setEolMode(QsciScintilla::EolUnix);
    expectStructuredNewline(
        "db.users.find({\n    profile: {\n        age: 21,|\n    }\n})",
        "db.users.find({\n    profile: {\n        age: 21,\n        |\n    }\n})");
}

TEST_F(ScriptWidgetTest, NewlineAlignsNestedFunctionParenthesesAndPreservesArguments)
{
    editor->setIndentationWidth(4);
    editor->setEolMode(QsciScintilla::EolUnix);
    expectStructuredNewline(
        "db.users.find({\n    _id: ObjectId(|)\n}).limit(5)",
        "db.users.find({\n    _id: ObjectId(\n        |\n    )\n}).limit(5)");
    expectStructuredNewline(
        "db.users.find(\n    {active: true}|).limit(5)",
        "db.users.find(\n    {active: true}\n|).limit(5)");
}

TEST_F(ScriptWidgetTest, StructuredNewlineRespectsCrLfAndConfiguredIndentationWidth)
{
    editor->setIndentationWidth(2);
    editor->setEolMode(QsciScintilla::EolWindows);
    expectStructuredNewline(
        "db.users.find({\r\n  profile: {|}\r\n})",
        "db.users.find({\r\n  profile: {\r\n    |\r\n  }\r\n})");
}

TEST_F(ScriptWidgetTest, NewlineInsideQuotedBracesDoesNotCreateAnotherLineForCloser)
{
    editor->setEolMode(QsciScintilla::EolUnix);
    const QStringList contexts{
        "const text = \"{|}\";", "const text = '(|)';",
        "// literal {|}", "/* literal [|] */",
        "db.users.find({name: /[{}|]/})", "db.fn({x: typeof /{|}/})"};
    for (const QString &context : contexts) {
        SCOPED_TRACE(context.toStdString());
        placeCaret(context);
        const int lines = editor->lines();
        pressKey(Qt::Key_Return, "\r");
        EXPECT_EQ(editor->lines(), lines + 1);
        EXPECT_FALSE(completionTimer->isActive());
        EXPECT_FALSE(completer->popup()->isVisible());
    }
}

TEST_F(ScriptWidgetTest, VeryLongLineUsesOrdinaryNewlineWithoutDelimiterSplitting)
{
    editor->setEolMode(QsciScintilla::EolUnix);
    placeCaret(QString(70000, ' ') + "db.users.find({|})");
    const int lines = editor->lines();
    pressKey(Qt::Key_Return, "\r");
    EXPECT_EQ(editor->lines(), lines + 1);
    EXPECT_FALSE(completionTimer->isActive());
    EXPECT_FALSE(completer->popup()->isVisible());
}

TEST_F(ScriptWidgetTest, TypingDoubleQuoteCreatesPairAndOneUndoRestoresText)
{
    placeCaret("const name = |;");
    pressKey(Qt::Key_QuoteDbl, "\"");
    EXPECT_EQ(script->text(), "const name = \"\";");
    expectCaretAfter("const name = \"");
    editor->undo();
    flushEditorUpdate();
    EXPECT_EQ(script->text(), "const name = ;");
    EXPECT_FALSE(completionTimer->isActive());
}

TEST_F(ScriptWidgetTest, TypingOpeningBraceCreatesPairAndOneUndoRestoresText)
{
    placeCaret("db.users.find(|)");
    pressKey(Qt::Key_BraceLeft, "{");
    EXPECT_EQ(script->text(), "db.users.find({})");
    expectCaretAfter("db.users.find({");
    editor->undo();
    flushEditorUpdate();
    EXPECT_EQ(script->text(), "db.users.find()");
    EXPECT_FALSE(completionTimer->isActive());
}

TEST_F(ScriptWidgetTest, TypingPairedClosingQuoteOrBraceAdvancesWithoutDuplication)
{
    placeCaret("|");
    pressKey(Qt::Key_QuoteDbl, "\"", false);
    ASSERT_EQ(script->text(), "\"\"");
    ASSERT_TRUE(completionTimer->isActive());
    pressKey(Qt::Key_QuoteDbl, "\"", false);
    EXPECT_EQ(script->text(), "\"\"");
    expectCaretAfter("\"\"");
    EXPECT_FALSE(completionTimer->isActive());
    flushEditorUpdate();
    EXPECT_FALSE(completionTimer->isActive());

    placeCaret("|");
    pressKey(Qt::Key_BraceLeft, "{", false);
    ASSERT_EQ(script->text(), "{}");
    ASSERT_TRUE(completionTimer->isActive());
    pressKey(Qt::Key_BraceRight, "}", false);
    EXPECT_EQ(script->text(), "{}");
    expectCaretAfter("{}");
    EXPECT_FALSE(completionTimer->isActive());
    flushEditorUpdate();
    EXPECT_FALSE(completionTimer->isActive());
}

TEST_F(ScriptWidgetTest, EscapedDoubleQuoteDoesNotCreateAnotherQuote)
{
    placeCaret("const name = \"a\\|b\";");
    pressKey(Qt::Key_QuoteDbl, "\"");
    EXPECT_EQ(script->text(), "const name = \"a\\\"b\";");
    expectCaretAfter("const name = \"a\\\"");
}

TEST_F(ScriptWidgetTest, FunctionCandidatesInsertParenthesesAndPlaceCaretInside)
{
    const QStringList candidates{"db.users.find(", "db.users.find()"};
    for (const QString &candidate : candidates) {
        SCOPED_TRACE(candidate.toStdString());
        request("db.users.fi|");
        reply("db.users.fi", {candidate});
        accept(candidate);
        EXPECT_EQ(script->text(), "db.users.find()");
        expectCaretAfter("db.users.find(");
        EXPECT_FALSE(completionTimer->isActive());
        editor->undo();
        EXPECT_EQ(script->text(), "db.users.fi");
        EXPECT_FALSE(completionTimer->isActive());
    }
}

TEST_F(ScriptWidgetTest, FunctionCompletionPreservesExistingArgumentsAndClosingParenthesis)
{
    const QStringList candidates{"db.users.find(", "db.users.find()"};
    for (const QString &candidate : candidates) {
        SCOPED_TRACE(candidate.toStdString());
        request("db.users.fi|nd({active: true}).limit(5)");
        reply("db.users.fi", {candidate});
        accept(candidate);
        EXPECT_EQ(script->text(), "db.users.find({active: true}).limit(5)");
        expectCaretAfter("db.users.find(");
    }
}

TEST_F(ScriptWidgetTest, NonFunctionCandidateDoesNotAddParentheses)
{
    request("db.us|");
    reply("db.us", {"db.users"});
    accept("db.users");
    EXPECT_EQ(script->text(), "db.users");
    expectCaretAfter("db.users");

    // Punctuation in a quoted collection name is literal, not a function marker.
    const QStringList names{"orders(", "orders()"};
    for (const QString &name : names) {
        const QString prefix = request("db.getCollection('ord|')");
        reply(prefix, {name});
        accept(name);
        EXPECT_EQ(script->text(), "db.getCollection('" + name + "')");
        expectCaretAfter("db.getCollection('" + name);
    }
}

TEST_F(ScriptWidgetTest, LosingFocusDiscardsLateReply)
{
    request("db.users.fi|");
    editor->clearFocus();
    ASSERT_FALSE(editor->hasFocus());
    reply("db.users.fi", {"db.users.find("});
    EXPECT_FALSE(completer->popup()->isVisible());
}

} // namespace
