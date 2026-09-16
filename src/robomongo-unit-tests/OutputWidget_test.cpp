#include <gtest/gtest.h>
#include <climits>
#include <utility>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QEventLoop>
#include <QKeyEvent>
#include <QPointer>
#include <QSettings>
#include <QTabBar>
#include <QTimer>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/EventBus.h"
#include "robomongo/core/domain/MongoServer.h"
#include "robomongo/core/domain/MongoShell.h"
#include "robomongo/core/domain/Notifier.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/gui/dialogs/DocumentTextEditor.h"
#include "robomongo/gui/editors/FindFrame.h"
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"
#include "robomongo/gui/widgets/workarea/BsonTableView.h"
#include "robomongo/gui/widgets/workarea/BsonTableModel.h"
#include "robomongo/gui/widgets/workarea/BsonTreeItem.h"
#include "robomongo/gui/widgets/workarea/BsonTreeModel.h"
#include "robomongo/gui/widgets/workarea/BsonTreeView.h"
#include "robomongo/gui/widgets/workarea/JsonPrepareThread.h"
#include "robomongo/gui/widgets/workarea/OutputItemContentWidget.h"
#include "robomongo/gui/widgets/workarea/OutputWidget.h"
#include "robomongo/gui/widgets/workarea/QueryWidget.h"
#include "robomongo/gui/widgets/workarea/ScriptWidget.h"
#include "robomongo/gui/widgets/workarea/IndicatorLabel.h"
#include "robomongo/gui/widgets/workarea/WorkAreaTabBar.h"

using namespace Robomongo;

namespace {
MongoQueryInfo queryInfo(const std::string &cursor, int skip = 0)
{
    MongoQueryInfo info(CollectionInfo("localhost:27017", "test", "documents"),
                        mongo::BSONObj(), mongo::BSONObj(), 0, skip, 20, 0, false);
    info.runtimeCursorId = cursor;
    return info;
}

std::vector<MongoDocumentPtr> documents(const std::string &value)
{
    return {MongoDocument::fromBsonObj(BSON("_id" << 1 << "value" << value))};
}

MongoShellResult result(const MongoQueryInfo &info, const std::vector<MongoDocumentPtr> &docs)
{
    return MongoShellResult("", "", docs, info, "db.documents.find()", 1);
}

class OutputWidgetTest : public testing::Test {
protected:
    MongoServer server{1, new ConnectionSettings(false), ConnectionSecondary};
    MongoShell shell{&server, ScriptInfo(QString())};
    std::unique_ptr<QueryWidget> query;
    OutputWidget *output = nullptr;

    void SetUp() override
    {
        AppRegistry::instance().settingsManager()->setViewMode(Tree);
        // Tests must not depend on preferences left in the persistent test profile.
        AppRegistry::instance().settingsManager()->setAutoExpand(false);
        query = std::make_unique<QueryWidget>(&shell);
        output = query->findChild<OutputWidget*>();
        ASSERT_NE(output, nullptr);
    }

    OutputItemContentWidget *item(int resultIndex)
    {
        for (auto *item : output->findChildren<OutputItemContentWidget*>())
            if (output->resultIndex(item) == resultIndex)
                return item;
        return nullptr;
    }
};

TEST_F(OutputWidgetTest, PagingResponseReplacesCursorAndAllQueryMetadata)
{
    output->present(&shell, {result(queryInfo("original"), documents("before"))});
    auto changed = queryInfo("replacement", 40);
    changed._fields = BSON("value" << 1);
    changed.readOnly = true;
    changed._limit = 7;
    changed._batchSize = 7;
    output->updatePart(0, changed, documents("after"));
    ASSERT_NE(item(0), nullptr);
    EXPECT_EQ(item(0)->queryInfo().runtimeCursorId, "replacement");
    EXPECT_EQ(item(0)->queryInfo()._skip, 40);
    EXPECT_EQ(item(0)->queryInfo()._limit, 7);
    EXPECT_EQ(item(0)->queryInfo()._batchSize, 7);
    EXPECT_TRUE(item(0)->queryInfo().readOnly);
    EXPECT_FALSE(item(0)->queryInfo()._fields.isEmpty());
    changed.runtimeCursorId.clear();
    output->updatePart(0, changed, documents("fresh database result"));
    EXPECT_TRUE(item(0)->queryInfo().runtimeCursorId.empty());
}

TEST_F(OutputWidgetTest, TabReorderingAndSelectionDoNotRedirectPagingResponse)
{
    output->present(&shell, {result(queryInfo("zero"), documents("0")),
                            result(queryInfo("one"), documents("1")),
                            result(queryInfo("two"), documents("2"))});
    auto *target = item(1);
    ASSERT_NE(target, nullptr);
    output->tabBar()->moveTab(1, 0);
    output->setCurrentWidget(item(2));
    output->updatePart(1, queryInfo("one-page-two", 20), documents("changed"));
    EXPECT_EQ(output->resultIndex(target), 1);
    EXPECT_EQ(target->queryInfo().runtimeCursorId, "one-page-two");
    EXPECT_EQ(item(2)->queryInfo().runtimeCursorId, "two");
    QPointer<OutputItemContentWidget> removed = target;
    ASSERT_TRUE(QMetaObject::invokeMethod(output, "tabCloseRequested", Qt::DirectConnection,
                                         Q_ARG(int, output->indexOf(target))));
    EXPECT_TRUE(removed.isNull());
    output->updatePart(1, queryInfo("late response"), documents("ignored"));
    EXPECT_EQ(item(2)->queryInfo().runtimeCursorId, "two");
}

TEST_F(OutputWidgetTest, EmptyCursorRetainsNamespaceAndPagingState)
{
    output->present(&shell, {result(queryInfo("empty", 60), {})});
    ASSERT_NE(item(0), nullptr);
    EXPECT_EQ(item(0)->queryInfo().runtimeCursorId, "empty");
    EXPECT_EQ(item(0)->queryInfo()._skip, 60);
    EXPECT_TRUE(item(0)->isTreeModeSupported());
}

TEST_F(OutputWidgetTest, WriteInvalidatesCorrectResultsWithoutChangingReadOnlyResults)
{
    auto aggregate = queryInfo("aggregate");
    aggregate.readOnly = true;
    output->present(&shell, {result(queryInfo("find-zero"), documents("0")),
                            result(queryInfo("find-one", 20), documents("1")),
                            result(aggregate, documents("aggregation"))});
    item(1)->showTable(); // Both the tree and table notifier receive this event.
    AppRegistry::instance().bus()->publish(new InsertDocumentResponse(&server));
    EXPECT_TRUE(item(0)->queryInfo().runtimeCursorId.empty());
    EXPECT_TRUE(item(1)->queryInfo().runtimeCursorId.empty());
    EXPECT_EQ(item(1)->queryInfo()._skip, 20);
    EXPECT_EQ(item(2)->queryInfo().runtimeCursorId, "aggregate");
    QApplication::processEvents();
}

TEST_F(OutputWidgetTest, ReadOnlyAndProjectedViewsRejectAllWriteEntrypoints)
{
    int openedDialogs = 0;
    QTimer dismissDialogs;
    QObject::connect(&dismissDialogs, &QTimer::timeout, [&] {
        if (auto *dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            ++openedDialogs;
            dialog->reject();
        }
    });
    dismissDialogs.start(1);
    for (bool projection : {false, true}) {
        auto info = queryInfo("protected");
        info.readOnly = !projection;
        if (projection)
            info._fields = BSON("value" << 1);
        output->present(&shell, {result(info, documents("protected"))});
        auto *content = item(0);
        content->showTree();
        auto *tree = content->findChild<BsonTreeView*>();
        ASSERT_NE(tree, nullptr);
        tree->setCurrentIndex(tree->model()->index(0, 0));
        QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(tree, &deleteKey);
        QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::ControlModifier);
        QApplication::sendEvent(tree, &backspace);
        for (auto *action : tree->findChildren<QAction*>()) {
            if (action->text().startsWith("Edit Document") ||
                action->text().startsWith("Insert Document") ||
                action->text().startsWith("Delete Document"))
                action->trigger();
        }
        content->showTable();
        auto *table = content->findChild<BsonTableView*>();
        ASSERT_NE(table, nullptr);
        table->selectAll();
        QApplication::sendEvent(table, &deleteKey);
        // Direct slots also protect future double-click/shortcut bindings.
        Notifier notifier(tree, &shell, info);
        notifier.onEditDocument();
        notifier.onInsertDocument();
        notifier.onDeleteDocument();
        notifier.onDeleteDocuments();
        notifier.handleDeleteCommand();
        notifier.deleteDocuments({}, true);
    }
    EXPECT_EQ(openedDialogs, 0);
}

TEST_F(OutputWidgetTest, ReplacingTextResultDropsQueuedOldJson)
{
    AppRegistry::instance().settingsManager()->setViewMode(Text);
    for (bool deleteFinishedWorker : {false, true}) {
        SCOPED_TRACE(deleteFinishedWorker);
        output->present(&shell, {result(queryInfo("old"), documents("OLD_TEXT"))});
        auto *content = item(0);
        ASSERT_NE(content, nullptr);
        QPointer<JsonPrepareThread> oldWorker = content->findChild<JsonPrepareThread*>();
        ASSERT_NE(oldWorker, nullptr);
        // Complete the old job without pumping the UI queue, so its text is
        // guaranteed to be pending when the result is replaced.
        ASSERT_TRUE(oldWorker->wait(5000));
        if (deleteFinishedWorker) {
            oldWorker->deleteLater();
            QCoreApplication::sendPostedEvents(oldWorker.data(), QEvent::DeferredDelete);
            ASSERT_TRUE(oldWorker.isNull());
        }
        output->updatePart(0, queryInfo("new"), documents("NEW_TEXT"));
        ASSERT_TRUE(oldWorker.isNull());
        auto *newWorker = content->findChild<JsonPrepareThread*>();
        ASSERT_NE(newWorker, nullptr);
        ASSERT_TRUE(newWorker->wait(5000));
        QCoreApplication::sendPostedEvents(content, QEvent::MetaCall);
        auto *text = content->findChild<FindFrame*>();
        ASSERT_NE(text, nullptr);
        EXPECT_TRUE(text->sciScintilla()->text().contains("NEW_TEXT"));
        EXPECT_FALSE(text->sciScintilla()->text().contains("OLD_TEXT"));
    }
}

TEST_F(OutputWidgetTest, QueuedCurrentJsonSurvivesFinishedWorkerDeletion)
{
    AppRegistry::instance().settingsManager()->setViewMode(Text);
    const std::vector<MongoDocumentPtr> docs = {
        MongoDocument::fromBsonObj(BSON("value" << "FIRST_CHUNK")),
        MongoDocument::fromBsonObj(BSON("value" << "LAST_CHUNK"))};
    output->present(&shell, {result(queryInfo("current"), docs)});
    auto *content = item(0);
    ASSERT_NE(content, nullptr);
    QPointer<JsonPrepareThread> worker = content->findChild<JsonPrepareThread*>();
    ASSERT_NE(worker, nullptr);
    ASSERT_TRUE(worker->wait(5000));
    worker->deleteLater();
    QCoreApplication::sendPostedEvents(worker.data(), QEvent::DeferredDelete);
    ASSERT_TRUE(worker.isNull());
    QCoreApplication::sendPostedEvents(content, QEvent::MetaCall);
    auto *text = content->findChild<FindFrame*>();
    ASSERT_NE(text, nullptr);
    const auto json = text->sciScintilla()->text();
    EXPECT_EQ(json.count("FIRST_CHUNK"), 1);
    EXPECT_EQ(json.count("LAST_CHUNK"), 1);
    EXPECT_LT(json.indexOf("FIRST_CHUNK"), json.indexOf("LAST_CHUNK"));
}

TEST_F(OutputWidgetTest, IntegerCellsAndTextDisplayDigitsWithoutConstructors)
{
    auto doc = BSON("_id" << 1 << "count" << 1 << "timestamp" << 1715960456LL
                    << "exact" << LLONG_MAX);
    output->present(&shell, {result(queryInfo("integers"), {MongoDocument::fromBsonObj(doc)})});
    auto *content = item(0);
    ASSERT_NE(content, nullptr);
    auto *tree = content->findChild<BsonTreeView*>();
    ASSERT_NE(tree, nullptr);
    const auto root = tree->model()->index(0, 0);
    ASSERT_TRUE(root.isValid());
    // Collapsed documents load their fields on demand. Exercise the integer
    // presentation after loading instead of relying on the auto-expand setting.
    ASSERT_TRUE(tree->model()->canFetchMore(root));
    tree->model()->fetchMore(root);
    ASSERT_EQ(tree->model()->rowCount(root), 4);
    EXPECT_EQ(tree->model()->index(1, 1, root).data().toString(), "1");
    EXPECT_EQ(tree->model()->index(2, 1, root).data().toString(), "1715960456");
    EXPECT_EQ(tree->model()->index(3, 1, root).data().toString(), "9223372036854775807");

    content->showTable();
    auto *table = content->findChild<BsonTableView*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->model()->index(0, 1).data().toString(), "1");
    EXPECT_EQ(table->model()->index(0, 2).data().toString(), "1715960456");
    EXPECT_EQ(table->model()->index(0, 3).data().toString(), "9223372036854775807");

    content->showText();
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
    auto *text = content->findChild<FindFrame*>();
    ASSERT_NE(text, nullptr);
    const auto json = text->sciScintilla()->text();
    EXPECT_TRUE(json.contains("9223372036854775807"));
    EXPECT_FALSE(json.contains("NumberInt("));
    EXPECT_FALSE(json.contains("NumberLong("));
}

TEST_F(OutputWidgetTest, AutoExpandLoadsOnlyTheFirstDocumentWhenEnabled)
{
    for (const bool autoExpand : {false, true}) {
        SCOPED_TRACE(autoExpand);
        AppRegistry::instance().settingsManager()->setAutoExpand(autoExpand);
        output->present(&shell, {result(queryInfo("expand"), {
            MongoDocument::fromBsonObj(BSON("_id" << 1 << "value" << 7)),
            MongoDocument::fromBsonObj(BSON("_id" << 2 << "value" << 8))})});
        auto *content = item(0);
        ASSERT_NE(content, nullptr);
        auto *tree = content->findChild<BsonTreeView*>();
        ASSERT_NE(tree, nullptr);
        auto *model = tree->model();
        const auto first = model->index(0, 0);
        const auto second = model->index(1, 0);
        ASSERT_TRUE(first.isValid());
        ASSERT_TRUE(second.isValid());
        EXPECT_EQ(model->rowCount(first), autoExpand ? 2 : 0);
        EXPECT_EQ(model->canFetchMore(first), !autoExpand);
        EXPECT_EQ(tree->isExpanded(first), autoExpand);
        EXPECT_EQ(model->rowCount(second), 0);
        EXPECT_TRUE(model->canFetchMore(second));
        EXPECT_FALSE(tree->isExpanded(second));
        if (autoExpand)
            EXPECT_EQ(model->index(1, BsonTreeItem::eValue, first).data().toString(), "7");
    }
}

TEST_F(OutputWidgetTest, TextResultsBuildTreeModelOnlyWhenNeeded)
{
    AppRegistry::instance().settingsManager()->setViewMode(Text);
    output->present(&shell, {result(queryInfo("lazy"), documents("before"))});
    auto *content = item(0);
    ASSERT_NE(content, nullptr);
    EXPECT_EQ(content->findChild<BsonTreeModel*>(), nullptr);
    content->showTable();
    QPointer<BsonTreeModel> model = content->findChild<BsonTreeModel*>();
    ASSERT_NE(model, nullptr);
    content->showTree();
    EXPECT_EQ(content->findChild<BsonTreeModel*>(), model.data());
    content->showText();
    output->updatePart(0, queryInfo("lazy-new"), documents("after"));
    EXPECT_TRUE(model.isNull());
    EXPECT_EQ(content->findChild<BsonTreeModel*>(), nullptr);
    content->showTable();
    auto *table = content->findChild<BsonTableView*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->model()->index(0, 1).data().toString(), "after");
}

TEST_F(OutputWidgetTest, TableCapsLargeCellPreviewsAndRetainsFullValue)
{
    const std::string value = "  " + std::string(10000, 'x');
    output->present(&shell, {result(queryInfo("large-cell"), documents(value))});
    item(0)->showTable();
    auto *table = item(0)->findChild<BsonTableView*>();
    ASSERT_NE(table, nullptr);
    const auto cell = table->model()->index(0, 1);
    EXPECT_EQ(cell.data().toString(), QString::fromStdString(value).left(300));
    EXPECT_EQ(cell.data(Qt::ToolTipRole).toString().size(), 500);
    auto *node = static_cast<BsonTreeItem*>(cell.internalPointer());
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->value().toStdString(), value);
}

TEST(QueryTabBarTest, CloseButtonFollowsItsTabAfterReordering)
{
    WorkAreaTabBar tabs;
    tabs.setTabsClosable(true);
    tabs.setMovable(true);
    tabs.addTab("Account");
    tabs.addTab("Advertisement");
    tabs.addTab("AdTemplate");
    EXPECT_EQ(tabs.tabButton(0, QTabBar::LeftSide), nullptr);
    auto *close = qobject_cast<QAbstractButton*>(tabs.tabButton(0, QTabBar::RightSide));
    ASSERT_NE(close, nullptr);
    int requestedIndex = -1;
    QObject::connect(&tabs, &QTabBar::tabCloseRequested,
        [&requestedIndex](int index) { requestedIndex = index; });
    tabs.moveTab(0, 2);
    ASSERT_EQ(tabs.tabButton(2, QTabBar::RightSide), close);
    close->click();
    EXPECT_EQ(requestedIndex, 2);
    EXPECT_EQ(tabs.tabText(requestedIndex), "Account");
}

TEST(QueryContextBarTest, DisplaysLiteralNamesAndUpdatesErrorColorsWithoutMarkup)
{
    TopStatusBar bar("XB<&>", "mongo-27:27017", "Center<&>");
    auto *connection = bar.findChild<Indicator*>("connectionContext");
    auto *server = bar.findChild<Indicator*>("serverContext");
    auto *database = bar.findChild<Indicator*>("databaseContext");
    ASSERT_NE(connection, nullptr);
    ASSERT_NE(server, nullptr);
    ASSERT_NE(database, nullptr);
    EXPECT_EQ(connection->text(), "XB<&>");
    EXPECT_EQ(database->text(), "Center<&>");
    EXPECT_EQ(database->accessibleName(), "Center<&>");
    EXPECT_TRUE(database->toolTip().contains("Center&lt;&amp;&gt;"));

    bar.setCurrentServer("unavailable<&>", false);
    bar.setCurrentDatabase("missing<&>", false);
    EXPECT_EQ(server->text(), "unavailable<&>");
    EXPECT_EQ(database->text(), "missing<&>");
    EXPECT_EQ(server->textColor(), QColor("#bd4052"));
    EXPECT_EQ(database->textColor(), QColor("#bd4052"));
    bar.setCurrentServer("mongo-27:27017", true);
    bar.setCurrentDatabase("Center", true);
    EXPECT_NE(server->textColor(), QColor("#bd4052"));
    EXPECT_NE(database->textColor(), QColor("#bd4052"));
    EXPECT_EQ(database->text(), "Center");
}

TEST_F(OutputWidgetTest, CollectionTabTitlesKeepTheCollectionAndOperationVisible)
{
    const std::pair<QString, QString> cases[] = {
        {"db.getCollection('Account').find({})", "Account · find"},
        {"db.getCollection(\"Ad Template\").aggregate([])", "Ad Template · aggregate"},
        {"db.Account.countDocuments({})", "Account · countDocuments"},
        {"print('<literal>')", "print('<literal>')"}
    };
    for (const auto &entry : cases) {
        SCOPED_TRACE(entry.first.toStdString());
        MongoShell titledShell(&server, ScriptInfo(entry.first));
        QueryWidget widget(&titledShell);
        QString title;
        QString tooltip;
        QObject::connect(&widget, &QueryWidget::titleChanged,
            [&title](const QString &value) { title = value; });
        QObject::connect(&widget, &QueryWidget::toolTipChanged,
            [&tooltip](const QString &value) { tooltip = value; });
        widget.textChange();
        EXPECT_EQ(title, "* " + entry.second);
        EXPECT_TRUE(tooltip.contains(entry.first.toHtmlEscaped()));
    }
}

TEST(BsonResultModelTest, LazyFieldsNotifyOnceAndKeepNestedParentsValid)
{
    BsonTreeModel model({MongoDocument::fromBsonObj(
        BSON("_id" << 1 << "nested" << BSON("value" << 7) << "empty" << mongo::BSONObj()))});
    const auto document = model.index(0, 0);
    ASSERT_TRUE(document.isValid());
    EXPECT_EQ(model.rowCount(document), 0);
    EXPECT_TRUE(model.hasChildren(document));
    EXPECT_TRUE(model.canFetchMore(document));

    int insertions = 0;
    QObject::connect(&model, &QAbstractItemModel::rowsInserted,
        [&insertions](const QModelIndex &, int, int) { ++insertions; });
    model.fetchMore(document);
    EXPECT_EQ(insertions, 1);
    EXPECT_EQ(model.rowCount(document), 3);
    model.fetchMore(document);
    EXPECT_EQ(insertions, 1);

    const auto nested = model.index(1, 0, document);
    const auto empty = model.index(2, 0, document);
    EXPECT_EQ(nested.parent(), document);
    EXPECT_FALSE(model.hasChildren(empty));
    EXPECT_FALSE(model.canFetchMore(empty));
    EXPECT_FALSE(model.hasChildren(model.index(1, 1, document)));
    model.fetchMore(nested);
    const auto value = model.index(0, BsonTreeItem::eValue, nested);
    EXPECT_EQ(value.data().toString(), "7");
    EXPECT_EQ(value.parent(), nested);
    EXPECT_EQ(insertions, 2);
}

TEST(BsonResultModelTest, SparseTableMapsCellsToTheirActualSourceFields)
{
    BsonTreeModel source({
        MongoDocument::fromBsonObj(BSON("_id" << 1 << "left" << "one")),
        MongoDocument::fromBsonObj(BSON("_id" << 2 << "right" << "two"))});
    BsonTableModelProxy table;
    table.setSourceModel(&source);
    ASSERT_EQ(table.rowCount(), 2);
    ASSERT_EQ(table.columnCount(QModelIndex()), 3);
    EXPECT_EQ(source.rowCount(source.index(1, 0)), 0);

    const auto right = table.index(1, 2, QModelIndex());
    ASSERT_TRUE(right.isValid());
    EXPECT_EQ(right.data().toString(), "two");
    const auto sourceRight = table.mapToSource(right);
    EXPECT_EQ(sourceRight.parent(), source.index(1, 0));
    EXPECT_EQ(sourceRight.row(), 1);
    EXPECT_EQ(sourceRight.column(), BsonTreeItem::eValue);
    EXPECT_EQ(table.mapFromSource(sourceRight), right);
    EXPECT_EQ(right.sibling(0, 1).data().toString(), "one");
    const auto missing = table.index(0, 2, QModelIndex());
    EXPECT_FALSE(missing.data().isValid());
    EXPECT_FALSE(table.mapToSource(missing).isValid());
    EXPECT_FALSE(table.index(-1, 0, QModelIndex()).isValid());
    EXPECT_FALSE(table.index(0, 3, QModelIndex()).isValid());
}

TEST(DocumentTextEditorTest, ViewAndEditUsePlainIntegersAndSaveOriginalTypes)
{
    const auto doc = BSON("_id" << 1LL << "count" << 1 << "timestamp" << 1715960456LL
                          << "exact" << LLONG_MAX << "nested" << BSON_ARRAY(2LL << 3));
    for (bool readonly : {true, false}) {
        DocumentTextEditor editor(CollectionInfo(), doc, readonly);
        EXPECT_FALSE(editor.jsonText().contains("NumberInt("));
        EXPECT_FALSE(editor.jsonText().contains("NumberLong("));
        EXPECT_TRUE(editor.jsonText().contains("9223372036854775807"));
        if (readonly)
            continue;
        ASSERT_TRUE(editor.validate());
        ASSERT_EQ(editor.bsonObj().size(), 1U);
        EXPECT_EQ(doc.woCompare(editor.bsonObj().front()), 0);

        auto *text = editor.findChild<FindFrame*>();
        ASSERT_NE(text, nullptr);
        text->sciScintilla()->setText(editor.jsonText().replace("1715960456", "1715960504"));
        ASSERT_TRUE(editor.validate());
        const auto saved = editor.bsonObj().front();
        EXPECT_EQ(saved["timestamp"].type(), mongo::NumberLong);
        EXPECT_EQ(saved["timestamp"].Long(), 1715960504LL);
        EXPECT_EQ(saved["_id"].type(), mongo::NumberLong);
        EXPECT_EQ(saved["exact"].Long(), LLONG_MAX);
        EXPECT_EQ(saved["count"].type(), mongo::NumberInt);
        EXPECT_EQ(saved["nested"].Array().front().type(), mongo::NumberLong);
    }
}
} // namespace

int main(int argc, char **argv)
{
    if (!qEnvironmentVariableIsSet("ROBOMONGO_PROFILE_DIR"))
        return 2; // Never read or write the user's connection settings from a test.
    QApplication application(argc, argv);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       qEnvironmentVariable("ROBOMONGO_PROFILE_DIR"));
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
