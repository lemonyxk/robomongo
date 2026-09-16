#include "robomongo/gui/widgets/workarea/QueryWidget.h"

#include <QRegularExpression>
#include <QObject>
#include <QPushButton>
#include <QApplication>
#include <QLabel>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QMainWindow>
#include <QDockWidget>
#include <QSplitter>
#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexerjavascript.h>
#include "robomongo/core/mongodb/MongoConnection.h"

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/EventBus.h"
#include "robomongo/core/domain/App.h"
#include "robomongo/core/domain/MongoCollection.h"
#include "robomongo/core/domain/MongoDatabase.h"
#include "robomongo/core/domain/MongoServer.h"
#include "robomongo/core/domain/MongoShell.h"
#include "robomongo/core/domain/MongoAggregateInfo.h"
#include "robomongo/core/events/MongoEvents.h"
#include "robomongo/core/settings/ConnectionSettings.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/core/utils/Logger.h"

#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/gui/widgets/workarea/OutputWidget.h"
#include "robomongo/gui/widgets/workarea/ScriptWidget.h"
#include "robomongo/gui/widgets/workarea/OutputItemContentWidget.h"
#include "robomongo/gui/widgets/workarea/OutputItemHeaderWidget.h"
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"
#include "robomongo/gui/editors/JSLexer.h"
#include "robomongo/gui/dialogs/ChangeShellTimeoutDialog.h"

using namespace mongo;

namespace
{
    QString queryTabTitle(const QString &query)
    {
        // Keep the collection visible instead of repeating "db.getCollection"
        // across every tab. Unrecognized scripts retain their text preview.
        static const QRegularExpression collectionCall(QStringLiteral(
            R"rx(^\s*db\s*\.\s*getCollection\s*\(\s*(['"])([^'"\\\r\n]+)\1\s*\)\s*\.\s*([A-Za-z_$][A-Za-z0-9_$]*)\s*\()rx"));
        static const QRegularExpression propertyCall(QStringLiteral(
            R"rx(^\s*db\s*\.\s*([A-Za-z_$][A-Za-z0-9_$]*)\s*\.\s*([A-Za-z_$][A-Za-z0-9_$]*)\s*\()rx"));
        const QString prefix = query.left(700);
        const auto collection = collectionCall.match(prefix);
        if (collection.hasMatch())
            return collection.captured(2) + QStringLiteral(" · ") + collection.captured(3);
        const auto property = propertyCall.match(prefix);
        if (property.hasMatch())
            return property.captured(1) + QStringLiteral(" · ") + property.captured(2);
        return query.left(41).simplified();
    }
}

namespace Robomongo
{
    QueryWidget::QueryWidget(MongoShell *shell, QWidget *parent) :
        QWidget(parent),
        _shell(shell),
        _viewer(nullptr),
        _dock(nullptr),
        _isTextChanged(false)
    {
        AppRegistry::instance().bus()->subscribe(this, DocumentListLoadedEvent::Type, shell);
        AppRegistry::instance().bus()->subscribe(this, ScriptExecutedEvent::Type, shell);
        AppRegistry::instance().bus()->subscribe(this, AutocompleteResponse::Type, shell);

        // Make QMessageBox text selectable
        // setStyleSheet("QMessageBox { messagebox-text-interaction-flags: 5; }");

        _scriptWidget = new ScriptWidget(_shell, this);
        VERIFY(connect(_scriptWidget, SIGNAL(textChanged()), this, SLOT(textChange())));

        // Need to use QMainWindow in order to make use of all features of docking.
        // (Note: Qt full support for dock windows implemented only for QMainWindow)
        _viewer = new OutputWidget(this);
        _outputWindow = new QMainWindow;
        _outputWindow->setContentsMargins(0, 0, 0, 0);
        _outputWindow->setDockOptions(_outputWindow->dockOptions() & ~QMainWindow::AnimatedDocks);
        _dock = new CustomDockWidget(this);
        _dock->setAllowedAreas(Qt::NoDockWidgetArea);
        _dock->setFeatures(QDockWidget::DockWidgetFloatable);
        _dock->setWidget(_viewer);
        auto *dockTitle = new QWidget;
        dockTitle->setFixedHeight(0);
        _dock->setTitleBarWidget(dockTitle);
        _outputWindow->addDockWidget(Qt::BottomDockWidgetArea, _dock);

        _outputLabel = new QLabel(this);
        _outputLabel->setContentsMargins(0, 5, 0, 0);
        _outputLabel->setVisible(false);

        _outputPanel = new QWidget(this);
        auto *outputLayout = new QVBoxLayout(_outputPanel);
        outputLayout->setContentsMargins(0, 0, 0, 0);
        outputLayout->setSpacing(0);
        outputLayout->addWidget(_outputLabel, 0, Qt::AlignTop);
        outputLayout->addWidget(_outputWindow, 1);

        _splitter = new QSplitter(Qt::Vertical, this);
        _splitter->setObjectName("queryResultSplitter");
        _splitter->setChildrenCollapsible(false);
        _splitter->setHandleWidth(5);
        // Result views can be large. Commit resize when a drag ends, avoiding
        // repeated table layouts/JSON viewport repaints on every mouse movement.
        _splitter->setOpaqueResize(false);
        _splitter->addWidget(_scriptWidget);
        _splitter->addWidget(_outputPanel);
        _splitter->setStretchFactor(0, 0);
        _splitter->setStretchFactor(1, 1);
        // Empty and single-line queries start compact; an existing multiline
        // query gets enough initial space without locking the draggable split.
        _splitter->setSizes(QList<int>() << _scriptWidget->preferredHeight() << 600);

        auto *mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(0);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->addWidget(_splitter);
        VERIFY(connect(_dock, SIGNAL(topLevelChanged(bool)), this, SLOT(on_dock_undock())));
    }

    void QueryWidget::setScriptFocus()
    {
        _scriptWidget->setScriptFocus();
    }

    void QueryWidget::showAutocompletion()
    {
        _scriptWidget->showAutocompletion();
    }

    void QueryWidget::hideAutocompletion()
    {
        _scriptWidget->hideAutocompletion();
    }

    void QueryWidget::setCurrentDatabase(const std::string & dbname)
    {
        _scriptWidget->setCurrentDatabase(dbname);
    }

    void QueryWidget::bringDockToFront()
    {
        _dock->raise(); // required for MAC only; possible Qt bug
        _dock->activateWindow();
    }

    bool QueryWidget::outputWindowDocked() const
    {
        if (_dock) {
            return !_dock->isFloating();
        }
        else {  // _dock is not initialized yet, but it will be docked when initialized
            return true;
        }
    }

    void QueryWidget::execute()
    {
        QString query = _scriptWidget->selectedText();

        if (query.isEmpty())
            query = _scriptWidget->text();

        showProgress();
        _shell->open(QtUtils::toStdString(query));
    }

    void QueryWidget::stop()
    {
        _shell->stop();
    }

    void QueryWidget::toggleOrientation()
    {
        _viewer->toggleOrientation();
    }

    void QueryWidget::openNewTab()
    {
        if (_shell) {
            MongoServer *server = _shell->server();
            QString query = _scriptWidget->selectedText();
            AppRegistry::instance().app()->openShell(server, query, _currentResult.currentDatabase(), 
                AppRegistry::instance().settingsManager()->autoExec());
        }
    }

    void QueryWidget::saveToFile()
    {
        if (_shell) {
            _shell->setScript(_scriptWidget->text());
            if (_shell->saveToFile()) {
                _isTextChanged = false;
                updateCurrentTab();
            }
        }
    }

    void QueryWidget::savebToFileAs()
    {
        if (_shell) {
            _shell->setScript(_scriptWidget->text());
            if (_shell->saveToFileAs()) {
                _isTextChanged = false;
                updateCurrentTab();
            }
        }        
    }

    void QueryWidget::openFile()
    {
        if (_shell && _shell->loadFromFile()) {
            _scriptWidget->setText(QtUtils::toQString(_shell->query()));
            _isTextChanged = false;
            updateCurrentTab();
        }
    }

    void QueryWidget::textChange()
    {
        if (_isTextChanged)
            return;
        _isTextChanged = true;
        updateCurrentTab();
    }

    QueryWidget::~QueryWidget()
    {
        AppRegistry::instance().app()->closeShell(_shell);
    }

    void QueryWidget::reload()
    {
        execute();
    }

    void QueryWidget::duplicate()
    {
        _scriptWidget->selectAll();
        openNewTab();
    }

    void QueryWidget::enterTreeMode()
    {
        _viewer->enterTreeMode();
    }

    void QueryWidget::enterTextMode()
    {
        _viewer->enterTextMode();
    }

    void QueryWidget::enterTableMode()
    {
        _viewer->enterTableMode();
    }

    void QueryWidget::enterCustomMode()
    {
        _viewer->enterCustomMode();
    }

    void QueryWidget::showProgress()
    {
        _viewer->showProgress();
    }

    void QueryWidget::dockUndock() 
    {
        // Toggle between dock/undock
        _dock->setFloating(!_dock->isFloating());
    };
    
    void QueryWidget::changeShellTimeout() 
    {
        changeShellTimeoutDialog();
    }

    void QueryWidget::hideProgress()
    {
        _viewer->hideProgress();
    }

    void QueryWidget::handle(DocumentListLoadedEvent *event)
    {
        hideProgress();

        if (event->isError()) {
            QString message = QString("Failed to load documents.\n\nError:\n%1")
                .arg(QtUtils::toQString(event->error().errorMessage()));
            QMessageBox::information(this, "Error", message);
            return;
        }

        // this should be in viewer, subscribed to ScriptExecutedEvent
        _viewer->updatePart(event->resultIndex(), event->queryInfo(), event->documents()); 
    }

    void QueryWidget::handle(ScriptExecutedEvent *event)
    {
        hideProgress();        
        _currentResult = event->result();

        if (_currentResult.results().size() == 1) {
            MongoShellResult const& result = _currentResult.results().front();
            AggrInfo const& aggrInfo = result.aggrInfo();
            if (aggrInfo.isValid && aggrInfo.resultIndex > -1) {
                _viewer->updatePart(aggrInfo.resultIndex, aggrInfo, _currentResult.results().front().documents());
                return;
            }
        }

        updateCurrentTab();

        displayData(event->result().results(), event->empty());
        // this should be in ScriptWidget, which is subscribed to ScriptExecutedEvent              
        _scriptWidget->setup(event->result()); 
        activateTabContent();

        if (event->isError()) {
            // For some cases, event error message already contains string "Error:"
            QString const& subStr =
                QString::fromStdString(event->error().errorMessage()).startsWith("Error", Qt::CaseInsensitive) ?
                "" : "Error:\n";

            QString const& message = "Failed to execute script.\n\n" + subStr +
                QString::fromStdString((event->error().errorMessage()));

            QMessageBox::critical(this, "Error", message);
        }

        if (event->timeoutReached()) {
            auto const shellTimeoutSec = AppRegistry::instance().settingsManager()->shellTimeoutSec();
            QString const subStr = _currentResult.results().size() > 1 ?
                "At least one of the scripts has reached shell timeout" :
                "The script has reached shell timeout";
            QString const secondStr = (shellTimeoutSec > 1) ? " seconds)" : " second)";
            QString messageShort = "Failed to execute all of the script. " + subStr + " (" +
                                    QString::number(shellTimeoutSec) + secondStr + " limit. ";
            QString messageLong = messageShort + 
                                  "\n\nPlease increase the value of shell timeout using button below "
                                  "or from the main window menu \"Options->Change Shell Timeout\".";
            LOG_MSG(messageShort, mongo::logger::LogSeverity::Error());

            auto errorDia = new QMessageBox(QMessageBox::Icon::Critical, "Error", messageLong);
            auto but = new QPushButton("Change Shell Timeout");
            VERIFY(connect(but, SIGNAL(clicked()), this, SLOT(changeShellTimeout())));
            errorDia->addButton(but, QMessageBox::NoRole);
            errorDia->exec();
        }
    }

    void QueryWidget::activateTabContent()
    {
        AppRegistry::instance().bus()->publish(new QueryWidgetUpdatedEvent(this, _currentResult.results().size()));
        _scriptWidget->setScriptFocus();
    }

    void QueryWidget::handle(AutocompleteResponse *event)
    {
        if (event->isError()) {
            // Do not show error message (error should be already logged)
            return;
        }

        _scriptWidget->showAutocompletion(event->list, QtUtils::toQString(event->prefix) );
    }

    void QueryWidget::on_dock_undock()
    {
        if (!_dock->isFloating()) {
            _outputPanel->show();
            if (_dockedSizes.size() == 2)
                _splitter->setSizes(_dockedSizes);
            _dock->setFeatures(QDockWidget::DockWidgetFloatable);
            auto *dockTitle = new QWidget;
            dockTitle->setFixedHeight(0);
            _dock->setTitleBarWidget(dockTitle);
            _viewer->applyDockUndockSettings(true);
        }
        else {
            _dockedSizes = _splitter->sizes();
            // Hide the empty host so the query fills the tab. The floating
            // QDockWidget stays visible and retains ownership of its result view.
            _outputPanel->hide();
            _dock->setFeatures(QDockWidget::DockWidgetClosable);
            QWidget *dockTitle = _dock->titleBarWidget();
            _dock->setTitleBarWidget(nullptr);
            if (dockTitle)
                dockTitle->deleteLater();
            _viewer->applyDockUndockSettings(false);
        }
    }

    void QueryWidget::updateCurrentTab()
    {
        const QString &shellQuery = QtUtils::toQString(_shell->query());
        QString toolTipQuery = shellQuery.left(700);

        QString tabTitle, toolTipText;
        if (_shell) {
            QFileInfo fileInfo(_shell->filePath());
            if (fileInfo.isFile()) {
                    tabTitle = fileInfo.fileName();
                    toolTipText = fileInfo.filePath();
            }
        }

        if (tabTitle.isEmpty() && shellQuery.isEmpty()) {
            tabTitle = "New Shell";
        }
        else {

            if (tabTitle.isEmpty()) {
                tabTitle = queryTabTitle(shellQuery);
                toolTipText = QString("<pre>%1</pre>").arg(toolTipQuery.toHtmlEscaped());
            }
            else {
                //tabTitle = QString("%1 %2").arg(tabTitle).arg(shellQuery);
                toolTipText = QString("<b>%1</b><br/><pre>%2</pre>")
                    .arg(toolTipText.toHtmlEscaped(), toolTipQuery.toHtmlEscaped());
            }
        }

        if (_isTextChanged) {
            tabTitle = "* " + tabTitle;
        }

        emit titleChanged(tabTitle);
        emit toolTipChanged(toolTipText);
    }

    void QueryWidget::displayData(const std::vector<MongoShellResult> &results, bool empty)
    {
        if (!empty) {
            bool isOutVisible = results.size() == 0 && !_scriptWidget->text().isEmpty();
            if (isOutVisible) {
                _outputLabel->setText("  Script executed successfully, but there are no results to show.");
            }
            _outputLabel->setVisible(isOutVisible);
        }

        _viewer->present(_shell, results);
    }
}
