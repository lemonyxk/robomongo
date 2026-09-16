#include <gtest/gtest.h>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFontMetrics>
#include <QLayout>
#include <QPixmap>
#include <QSplitter>
#include <QToolBar>
#include <QTreeWidget>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/domain/MongoServer.h"
#include "robomongo/core/domain/MongoShell.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/gui/AppStyle.h"
#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/gui/MainWindow.h"
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"
#include "robomongo/gui/widgets/explorer/ExplorerWidget.h"
#include "robomongo/gui/widgets/workarea/OutputWidget.h"
#include "robomongo/gui/widgets/workarea/QueryWidget.h"
#include "robomongo/gui/widgets/workarea/ScriptWidget.h"
#include "robomongo/gui/widgets/workarea/WorkAreaTabBar.h"
#include "robomongo/gui/widgets/workarea/WorkAreaTabWidget.h"

using namespace Robomongo;

namespace {
// Deliver layout requests only: MainWindow deliberately schedules its connection
// dialog at startup, and this offline fixture must not run that timer.
void settleLayout(QWidget &widget)
{
    for (int pass = 0; pass < 3; ++pass) {
        if (widget.layout())
            widget.layout()->activate();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    }
}

class WorkspaceLayoutTest : public testing::Test {
protected:
    MongoServer server{1, new ConnectionSettings(false), ConnectionSecondary};
    MongoShell shell{&server, ScriptInfo(QStringLiteral(
        "db.getCollection('users').find({\n"
        "    active: true,\n"
        "    age: { $gte: 18 }\n"
        "}).sort({ createdAt: -1 }).limit(50)"))};
    SettingsManager *settings = AppRegistry::instance().settingsManager();
    const bool savedUpdates = settings->checkForUpdates();
    const ViewMode savedViewMode = settings->viewMode();
    const int savedFontSize = settings->textFontPointSize();
    const QString savedSheet = qApp->styleSheet();
    const QPalette savedPalette = qApp->palette();

    void SetUp() override
    {
        settings->setCheckForUpdates(false);
        settings->setViewMode(Table);
        settings->setTextFontPointSize(12);
        AppStyleUtils::applyStyle(settings->currentStyle());
    }

    void TearDown() override
    {
        settings->setCheckForUpdates(savedUpdates);
        settings->setViewMode(savedViewMode);
        settings->setTextFontPointSize(savedFontSize);
        qApp->setStyleSheet(savedSheet);
        qApp->setPalette(savedPalette);
    }
};

TEST_F(WorkspaceLayoutTest, ExplorerAndWorkspaceHaveIndependentHeaders)
{
    MainWindow window;
    auto *tabs = qobject_cast<WorkAreaTabWidget *>(window.centralWidget());
    ASSERT_NE(tabs, nullptr);
    auto *query = new QueryWidget(&shell, tabs);
    tabs->addTab(query, "users · find");
    tabs->setCurrentWidget(query);
    auto *output = query->findChild<OutputWidget *>();
    ASSERT_NE(output, nullptr);
    MongoQueryInfo info(CollectionInfo("localhost:27017", "example", "users"),
                        mongo::BSONObj(), mongo::BSONObj(), 0, 0, 50, 0, false);
    const std::vector<MongoDocumentPtr> docs = {
        MongoDocument::fromBsonObj(BSON("_id" << 1 << "name" << "Alex" << "active" << true << "age" << 28)),
        MongoDocument::fromBsonObj(BSON("_id" << 2 << "name" << "Sam" << "active" << true << "age" << 34))};
    output->present(&shell, {MongoShellResult("", "", docs, info, shell.query(), 4)});
    query->setCurrentDatabase("example");
    if (auto *tree = window.findChild<QTreeWidget *>("explorerTree")) {
        auto *connection = new QTreeWidgetItem(tree, QStringList() << "Local MongoDB (preview)");
        auto *database = new QTreeWidgetItem(connection, QStringList() << "example");
        new QTreeWidgetItem(database, QStringList() << "users");
        connection->setExpanded(true);
        database->setExpanded(true);
    }
    window.resize(1280, 820);
    window.show();
    settleLayout(window);

    auto *bar = tabs->findChild<WorkAreaTabBar *>();
    auto *explorer = window.findChild<ExplorerWidget *>();
    auto *tools = window.findChild<QWidget *>("workspaceTools");
    auto *explorerPanel = window.findChild<QWidget *>("explorerPanel");
    ASSERT_NE(bar, nullptr);
    ASSERT_NE(explorer, nullptr);
    ASSERT_NE(tools, nullptr);
    ASSERT_NE(explorerPanel, nullptr);
    EXPECT_EQ(tabs->cornerWidget(Qt::TopRightCorner), nullptr);
    EXPECT_TRUE(explorerPanel->isAncestorOf(tools));
    EXPECT_TRUE(explorerPanel->isAncestorOf(explorer));
    EXPECT_TRUE(tools->isVisible());
    EXPECT_EQ(tabs->tabPosition(), QTabWidget::North);
    EXPECT_LE(bar->geometry().top(), 1);
    const QRect explorerRect(explorer->mapTo(&window, QPoint()), explorer->size());
    const QRect panelRect(explorerPanel->mapTo(&window, QPoint()), explorerPanel->size());
    const QRect toolsRect(tools->mapTo(&window, QPoint()), tools->size());
    const QRect tabsRect(tabs->mapTo(&window, QPoint()), tabs->size());
    const QRect barRect(bar->mapTo(&window, QPoint()), bar->size());
    EXPECT_GT(tabsRect.left(), panelRect.right());
    EXPECT_LE(qAbs(panelRect.top() - tabsRect.top()), 1);
    EXPECT_LE(qAbs(toolsRect.top() - tabsRect.top()), 1);
    EXPECT_GT(explorerRect.top(), toolsRect.bottom());
    EXPECT_GE(toolsRect.left(), panelRect.left());
    EXPECT_LE(toolsRect.right(), panelRect.right());
    const int expectedActionCounts[] = {1, 2, 3};
    int toolbarIndex = 0;
    int previousRight = toolsRect.left() - 1;
    for (const char *name : {"workspaceConnectionsToolbar", "workspaceFilesToolbar",
                            "workspaceExecutionToolbar"}) {
        auto *toolbar = window.findChild<QToolBar *>(QString::fromLatin1(name));
        ASSERT_NE(toolbar, nullptr);
        EXPECT_TRUE(toolbar->isVisible());
        EXPECT_TRUE(tools->isAncestorOf(toolbar));
        EXPECT_EQ(window.toolBarArea(toolbar), Qt::NoToolBarArea);
        EXPECT_EQ(toolbar->orientation(), Qt::Horizontal);
        EXPECT_FALSE(toolbar->isFloatable());
        const QRect toolbarRect(toolbar->mapTo(&window, QPoint()), toolbar->size());
        EXPECT_GT(toolbarRect.left(), previousRight);
        EXPECT_LE(toolbarRect.right(), toolsRect.right());
        EXPECT_GE(toolbarRect.top(), toolsRect.top());
        // The left tools and right tabs start independently at the content top.
        EXPECT_LE(toolbarRect.top(), barRect.bottom());
        EXPECT_GE(toolbarRect.bottom(), barRect.top());
        previousRight = toolbarRect.right();
        ASSERT_EQ(toolbar->actions().size(), expectedActionCounts[toolbarIndex++]);
        for (QAction *action : toolbar->actions()) {
            auto *button = toolbar->widgetForAction(action);
            ASSERT_NE(button, nullptr);
            EXPECT_TRUE(action->isVisible());
            EXPECT_TRUE(button->isVisible());
            if (toolbar->objectName() == QStringLiteral("workspaceExecutionToolbar"))
                EXPECT_TRUE(window.actions().contains(action));
        }
    }

    // Tools belong to the left pane; hiding it leaves the right workspace visible.
    auto *explorerDock = qobject_cast<QDockWidget *>(explorerPanel->parentWidget());
    ASSERT_NE(explorerDock, nullptr);
    EXPECT_EQ(explorerDock->widget(), explorerPanel);
    EXPECT_EQ(window.dockWidgetArea(explorerDock), Qt::LeftDockWidgetArea);
    explorerDock->hide();
    settleLayout(window);
    for (const char *name : {"workspaceConnectionsToolbar", "workspaceFilesToolbar",
                            "workspaceExecutionToolbar"})
        EXPECT_FALSE(window.findChild<QToolBar *>(QString::fromLatin1(name))->isVisible());
    EXPECT_TRUE(tabs->isVisible());
    explorerDock->show();
    settleLayout(window);

    const QString preview = qEnvironmentVariable("ROBOMONGO_LAYOUT_PREVIEW");
    if (!preview.isEmpty())
        EXPECT_TRUE(window.grab().save(preview));

    // The connection/open controls must remain reachable after closing all tabs.
    tabs->clear();
    settleLayout(window);
    EXPECT_TRUE(tools->isVisible());
    EXPECT_GT(tools->height(), 0);
    for (const char *name : {"workspaceConnectionsToolbar", "workspaceFilesToolbar",
                            "workspaceExecutionToolbar"})
        EXPECT_TRUE(window.findChild<QToolBar *>(QString::fromLatin1(name))->isVisible());
}

TEST_F(WorkspaceLayoutTest, EditorStartsWithOneSpacedRowAndTypingKeepsTheChosenSplit)
{
    shell.setScript(QStringLiteral("db.users.find({})"));
    QueryWidget query(&shell);
    query.resize(1000, 760);
    query.show();
    settleLayout(query);
    auto *splitter = query.findChild<QSplitter *>("queryResultSplitter");
    auto *script = query.findChild<ScriptWidget *>();
    ASSERT_NE(splitter, nullptr);
    ASSERT_NE(script, nullptr);
    auto *editor = script->findChild<RoboScintilla *>();
    ASSERT_NE(editor, nullptr);
    ASSERT_EQ(splitter->count(), 2);
    EXPECT_EQ(splitter->orientation(), Qt::Vertical);
    EXPECT_FALSE(splitter->opaqueResize());
    EXPECT_GE(editor->viewport()->height(), editor->textHeight(0));
    EXPECT_LE(editor->viewport()->height(), editor->textHeight(0) + 12);
    EXPECT_GE(editor->SendScintilla(QsciScintilla::SCI_GETEXTRAASCENT), 2);
    EXPECT_GE(editor->SendScintilla(QsciScintilla::SCI_GETEXTRADESCENT), 2);

    splitter->setSizes(QList<int>() << 330 << 425);
    settleLayout(query);
    const int editorPanelHeight = splitter->sizes()[0];
    editor->append(QString(20000, 'a') + QString(500, '\n'));
    script->hideAutocompletion();
    settleLayout(query);
    EXPECT_EQ(splitter->sizes()[0], editorPanelHeight);
    EXPECT_LE(editor->minimumHeight(), editor->textHeight(0) + 12);
}

TEST_F(WorkspaceLayoutTest, ExistingMultilineQueryGetsItsContentHeightInitially)
{
    QueryWidget query(&shell);
    query.resize(1000, 760);
    query.show();
    settleLayout(query);
    auto *editor = query.findChild<RoboScintilla *>("queryEditor");
    ASSERT_NE(editor, nullptr);
    ASSERT_EQ(editor->lines(), 4);
    const int contentHeight = editor->textHeight(0) * editor->lines();
    EXPECT_GE(editor->viewport()->height(), contentHeight);
    EXPECT_LE(editor->viewport()->height(), contentHeight + 12);
    EXPECT_LE(editor->minimumHeight(), editor->textHeight(0) + 12);
}

TEST_F(WorkspaceLayoutTest, FloatingResultsKeepContentAndRestoreTheChosenSplit)
{
    QueryWidget query(&shell);
    query.resize(1000, 760);
    query.show();
    settleLayout(query);
    auto *splitter = query.findChild<QSplitter *>("queryResultSplitter");
    auto *dock = query.findChild<QueryWidget::CustomDockWidget *>();
    ASSERT_NE(splitter, nullptr);
    ASSERT_NE(dock, nullptr);
    splitter->setSizes(QList<int>() << 320 << 435);
    settleLayout(query);
    const QList<int> before = splitter->sizes();
    QWidget *content = dock->widget();
    dock->setFloating(true);
    settleLayout(query);
    EXPECT_FALSE(query.outputWindowDocked());
    EXPECT_TRUE(dock->isVisible());
    EXPECT_FALSE(splitter->widget(1)->isVisible());
    EXPECT_EQ(splitter->sizes()[0], splitter->height());
    EXPECT_EQ(dock->widget(), content);
    dock->close(); // Floating close must redock rather than discard results.
    settleLayout(query);
    EXPECT_TRUE(query.outputWindowDocked());
    EXPECT_TRUE(splitter->widget(1)->isVisible());
    EXPECT_EQ(splitter->sizes(), before);
    EXPECT_EQ(dock->widget(), content);
}

TEST_F(WorkspaceLayoutTest, FontChangesScaleRowSpacingAndPreserveTheDocument)
{
    RoboScintilla editor;
    editor.setText("db.users.find({active: true})");
    editor.setSelection(0, 3, 0, 8);
    const int smallRow = editor.textHeight(0);
    const int smallPadding = editor.SendScintilla(QsciScintilla::SCI_GETEXTRAASCENT);
    settings->setTextFontPointSize(20);
    editor.applyFontSettings();
    EXPECT_GT(editor.textHeight(0), smallRow);
    EXPECT_GT(editor.SendScintilla(QsciScintilla::SCI_GETEXTRAASCENT), smallPadding);
    EXPECT_EQ(editor.text(), "db.users.find({active: true})");
    EXPECT_EQ(editor.selectedText(), "users");
}
} // namespace
