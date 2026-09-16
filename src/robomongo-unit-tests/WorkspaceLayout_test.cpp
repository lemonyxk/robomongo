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

TEST_F(WorkspaceLayoutTest, EditorHeightTracksInsertedAndDeletedLines)
{
    const QString initialQuery = QStringLiteral("db.users.find({})");
    shell.setScript(initialQuery);
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

    const int oneRowPanelHeight = splitter->sizes()[0];
    editor->setCursorPosition(0, initialQuery.size());
    editor->insert(QStringLiteral("\n// second\n// third\n// fourth"));
    script->hideAutocompletion();
    settleLayout(query);
    ASSERT_EQ(editor->lines(), 4);
    const int fourRowHeight = editor->textHeight(0) * 4;
    EXPECT_GE(editor->viewport()->height(), fourRowHeight);
    EXPECT_LE(editor->viewport()->height(), fourRowHeight + 12);
    const int multilinePanelHeight = splitter->sizes()[0];
    EXPECT_GT(multilinePanelHeight, oneRowPanelHeight);

    editor->setSelection(0, initialQuery.size(), 3, editor->text(3).size());
    editor->removeSelectedText();
    settleLayout(query);
    ASSERT_EQ(editor->text(), initialQuery);
    ASSERT_EQ(editor->lines(), 1);
    EXPECT_LT(splitter->sizes()[0], multilinePanelHeight);
    EXPECT_GE(editor->viewport()->height(), editor->textHeight(0));
    EXPECT_LE(editor->viewport()->height(), editor->textHeight(0) + 12);

    // A line-count change resumes content sizing after a manual resize.
    splitter->setSizes(QList<int>() << 330 << 425);
    settleLayout(query);
    editor->setCursorPosition(0, initialQuery.size());
    editor->insert(QStringLiteral("\n"));
    settleLayout(query);
    ASSERT_EQ(editor->lines(), 2);
    EXPECT_GE(editor->viewport()->height(), editor->textHeight(0) * 2);
    EXPECT_LE(editor->viewport()->height(), editor->textHeight(0) * 2 + 12);

    editor->clear();
    settleLayout(query);
    EXPECT_GE(editor->viewport()->height(), editor->textHeight(0));
    EXPECT_LE(editor->viewport()->height(), editor->textHeight(0) + 12);
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

TEST_F(WorkspaceLayoutTest, EditorShowsAtMostOneHundredRowsAndScrollsToAdditionalLines)
{
    shell.setScript(QStringLiteral("db.users.find({})"));
    QueryWidget query(&shell);
    auto *editor = query.findChild<RoboScintilla *>("queryEditor");
    auto *splitter = query.findChild<QSplitter *>("queryResultSplitter");
    ASSERT_NE(editor, nullptr);
    ASSERT_NE(splitter, nullptr);
    const int hundredRowHeight = editor->textHeight(0) * 100;
    // Make room for all 100 rows without depending on the desktop's dimensions.
    query.setAttribute(Qt::WA_DontShowOnScreen);
    query.resize(1000, hundredRowHeight + 1000);
    query.show();
    editor->setText(QString(99, '\n'));
    settleLayout(query);
    ASSERT_EQ(editor->lines(), 100);
    EXPECT_GE(editor->viewport()->height(), hundredRowHeight);
    EXPECT_LE(editor->viewport()->height(), hundredRowHeight + 12);
    const int cappedPanelHeight = splitter->sizes()[0];

    editor->append(QString(50, '\n'));
    settleLayout(query);
    ASSERT_EQ(editor->lines(), 150);
    EXPECT_EQ(splitter->sizes()[0], cappedPanelHeight);
    EXPECT_GE(editor->viewport()->height(), hundredRowHeight);
    EXPECT_LE(editor->viewport()->height(), hundredRowHeight + 12);
    editor->SendScintilla(QsciScintilla::SCI_LINESCROLL, 0, 149);
    const int firstVisibleLine = editor->SendScintilla(QsciScintilla::SCI_GETFIRSTVISIBLELINE);
    const int visibleLines = editor->SendScintilla(QsciScintilla::SCI_LINESONSCREEN);
    EXPECT_GT(firstVisibleLine, 0);
    EXPECT_GE(firstVisibleLine + visibleLines, editor->lines());

    editor->setText(QString(2, '\n'));
    settleLayout(query);
    ASSERT_EQ(editor->lines(), 3);
    EXPECT_GE(editor->viewport()->height(), editor->textHeight(0) * 3);
    EXPECT_LE(editor->viewport()->height(), editor->textHeight(0) * 3 + 12);
}

TEST_F(WorkspaceLayoutTest, EditorRecoversContentHeightWhenTheWindowGetsMoreSpace)
{
    shell.setScript(QString(49, '\n'));
    QueryWidget query(&shell);
    query.setAttribute(Qt::WA_DontShowOnScreen);
    query.resize(1000, 360);
    query.show();
    settleLayout(query);
    auto *editor = query.findChild<RoboScintilla *>("queryEditor");
    auto *splitter = query.findChild<QSplitter *>("queryResultSplitter");
    ASSERT_NE(editor, nullptr);
    ASSERT_NE(splitter, nullptr);
    ASSERT_EQ(editor->lines(), 50);
    const int contentHeight = editor->textHeight(0) * 50;
    EXPECT_LT(editor->viewport()->height(), contentHeight);
    EXPECT_GT(splitter->sizes()[1], 0);

    query.resize(1000, contentHeight + 600);
    settleLayout(query);
    EXPECT_GE(editor->viewport()->height(), contentHeight);
    EXPECT_LE(editor->viewport()->height(), contentHeight + 12);
    EXPECT_GT(splitter->sizes()[1], 0);

    query.resize(1000, 360);
    settleLayout(query);
    EXPECT_LT(editor->viewport()->height(), contentHeight);
    EXPECT_GT(splitter->sizes()[1], 0);
}

TEST_F(WorkspaceLayoutTest, QueryHeightFollowsFontSizeChanges)
{
    QueryWidget query(&shell);
    query.resize(1000, 760);
    query.show();
    settleLayout(query);
    auto *editor = query.findChild<RoboScintilla *>("queryEditor");
    ASSERT_NE(editor, nullptr);
    ASSERT_EQ(editor->lines(), 4);
    const int originalHeight = editor->viewport()->height();
    const QString originalText = editor->text();

    settings->setTextFontPointSize(20);
    editor->applyFontSettings();
    settleLayout(query);
    const int contentHeight = editor->textHeight(0) * editor->lines();
    EXPECT_GT(editor->viewport()->height(), originalHeight);
    EXPECT_GE(editor->viewport()->height(), contentHeight);
    EXPECT_LE(editor->viewport()->height(), contentHeight + 12);
    EXPECT_EQ(editor->text(), originalText);

    // Shrinking the font must also discard the cached larger single-row height.
    settings->setTextFontPointSize(12);
    editor->applyFontSettings();
    settleLayout(query);
    EXPECT_EQ(editor->viewport()->height(), originalHeight);
    EXPECT_GE(editor->viewport()->height(), editor->textHeight(0) * editor->lines());
    EXPECT_EQ(editor->text(), originalText);
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

TEST_F(WorkspaceLayoutTest, RedockingAfterAnEditUsesTheNewContentHeight)
{
    QueryWidget query(&shell);
    query.resize(1000, 760);
    query.show();
    settleLayout(query);
    auto *dock = query.findChild<QueryWidget::CustomDockWidget *>();
    auto *editor = query.findChild<RoboScintilla *>("queryEditor");
    ASSERT_NE(dock, nullptr);
    ASSERT_NE(editor, nullptr);
    dock->setFloating(true);
    settleLayout(query);

    editor->setText(QString(8, '\n'));
    settleLayout(query);
    dock->close();
    settleLayout(query);
    EXPECT_TRUE(query.outputWindowDocked());
    ASSERT_EQ(editor->lines(), 9);
    const int contentHeight = editor->textHeight(0) * editor->lines();
    EXPECT_GE(editor->viewport()->height(), contentHeight);
    EXPECT_LE(editor->viewport()->height(), contentHeight + 12);
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
