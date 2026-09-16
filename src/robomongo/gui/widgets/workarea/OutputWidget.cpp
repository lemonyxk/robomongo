#include "robomongo/gui/widgets/workarea/OutputWidget.h"

#include <QHBoxLayout>
#include <QSplitter>
#include <QWidget>
#include <QMouseEvent>
#include <algorithm>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/domain/MongoShell.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/utils/QtUtils.h"

#include "robomongo/gui/widgets/workarea/OutputItemContentWidget.h"
#include "robomongo/gui/widgets/workarea/ProgressBarPopup.h"
#include "robomongo/gui/widgets/workarea/WorkAreaTabBar.h"

namespace Robomongo
{
    OutputWidget::OutputWidget(QWidget *parent) :
        QTabWidget(parent), _splitter(new QSplitter), _tabbedResults(false), _prevResultsCount(0)
    {
        _splitter->setOrientation(Qt::Vertical);
        _splitter->setHandleWidth(4);
        _splitter->setOpaqueResize(false);
        _splitter->setContentsMargins(0, 0, 0, 0);

        QVBoxLayout *layout = new QVBoxLayout();
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(_splitter);
        setLayout(layout);

        setObjectName("resultTabs");
        tabBar()->setObjectName("resultTabBar");
        tabBar()->setExpanding(false);
        tabBar()->setUsesScrollButtons(true);
        setTabsClosable(true);
        setElideMode(Qt::ElideRight);
        setMovable(true);
#ifdef __APPLE__      
        setDocumentMode(false);
#else        
        setDocumentMode(true);
#endif        
        setStyleSheet(buildStyleSheet());
        VERIFY(connect(this, SIGNAL(tabCloseRequested(int)), SLOT(tabCloseRequested(int))));
        
        _progressBarPopup = new ProgressBarPopup(this);
    }

    void OutputWidget::present(MongoShell *shell, const std::vector<MongoShellResult> &results)
    {
        const bool updatesWereEnabled = updatesEnabled();
        setUpdatesEnabled(false);
        if (_prevResultsCount > 0)
            clearAllParts();
        
        int const RESULTS_SIZE = _prevResultsCount = results.size();
        bool const multipleResults = (RESULTS_SIZE > 1);
        _tabbedResults = (RESULTS_SIZE > 2);
        _splitter->setHidden(_tabbedResults ? true : false);
        _outputItemContentWidgets.clear();
        _outputItemContentWidgets.reserve(results.size());

        while (count() > 0)
            removeTab(count()-1);

        for (int i = 0; i < RESULTS_SIZE; ++i) {
            const MongoShellResult &shellResult = results[i];
            double secs = shellResult.elapsedMs() / 1000.f;
            ViewMode viewMode = AppRegistry::instance().settingsManager()->viewMode();
            if (_prevViewModes.size()) {
                viewMode = _prevViewModes.back();
                _prevViewModes.pop_back();
            }

            bool const firstItem = (0 == i);
            bool const lastItem = (RESULTS_SIZE-1 == i);

            OutputItemContentWidget* item = nullptr;
            if (!shellResult.documents().empty() || shellResult.queryInfo()._info.isValid() ||
                !shellResult.queryInfo().runtimeCursorId.empty()) {
                item = new OutputItemContentWidget(viewMode, shell, QtUtils::toQString(shellResult.type()),
                                                   shellResult.documents(), shellResult.queryInfo(), secs, 
                                                   multipleResults, _tabbedResults, firstItem, lastItem,
                                                   shellResult.aggrInfo(), this);
            } else {
                item = new OutputItemContentWidget(viewMode, shell, QtUtils::toQString(shellResult.response()), 
                                                   secs, multipleResults, _tabbedResults, firstItem, lastItem,
                                                   shellResult.aggrInfo(), this);
            }
            VERIFY(connect(item, SIGNAL(maximizedPart()), this, SLOT(maximizePart())));
            VERIFY(connect(item, SIGNAL(restoredSize()), this, SLOT(restoreSize())));

            if (_tabbedResults) {
                addTab(item, QString::fromStdString(shellResult.statementShort()));
                setTabToolTip(i, QString::fromStdString(shellResult.statement()));
            }
            else
                _splitter->addWidget(item);
             
            _outputItemContentWidgets.push_back(item);
        }
        
        tryToMakeAllPartsEqualInSize();
        setUpdatesEnabled(updatesWereEnabled);
    }

    void OutputWidget::updatePart(int partIndex, const MongoQueryInfo &queryInfo, 
                                  const std::vector<MongoDocumentPtr> &documents)
    {
        if (partIndex < 0 || partIndex >= static_cast<int>(_outputItemContentWidgets.size()))
            return;
        auto *outputItemContentWidget = _outputItemContentWidgets[partIndex];
        if (!outputItemContentWidget)
            return;

        outputItemContentWidget->updateWithInfo(queryInfo, documents);
        outputItemContentWidget->refreshOutputItem();
    }

    void OutputWidget::updatePart(int partIndex, const AggrInfo &agrrInfo, 
                                  const std::vector<MongoDocumentPtr> &documents)
    {
        if (partIndex < 0 || partIndex >= static_cast<int>(_outputItemContentWidgets.size()))
            return;
        auto *outputItemContentWidget = _outputItemContentWidgets[partIndex];
        if (!outputItemContentWidget)
            return;

        outputItemContentWidget->updateWithInfo(agrrInfo, documents);
        outputItemContentWidget->refreshOutputItem();
    }

    void OutputWidget::toggleOrientation()
    {
        bool const horizontal = _splitter->orientation() == Qt::Horizontal;
        _splitter->setOrientation(horizontal ? Qt::Vertical : Qt::Horizontal);
        int const COUNT = _splitter->count();
        if (COUNT > 1) {
            auto const* firstItem = qobject_cast<OutputItemContentWidget*>(_splitter->widget(0));
            auto const* lastItem = qobject_cast<OutputItemContentWidget*>(_splitter->widget(COUNT-1));
            firstItem->toggleOrientation(_splitter->orientation());
            lastItem->toggleOrientation(_splitter->orientation());
        }
    }

    void OutputWidget::switchMode(
        std::function<void(OutputItemContentWidget*)> modeFunc
    )
    {
        if (_tabbedResults) {
            QWidget* currentTab { widget(currentIndex()) };
            if (auto *item = qobject_cast<OutputItemContentWidget*>(currentTab))
                modeFunc(item);
        }
        else {
            for (int i = 0; i < _splitter->count(); i++) {
                QWidget* widget { _splitter->widget(i) };
                modeFunc(qobject_cast<OutputItemContentWidget*>(widget));
            }
        }
    }

    void OutputWidget::enterTreeMode() 
    {
        switchMode(&OutputItemContentWidget::showTree);
    }

    void OutputWidget::enterTextMode() 
    {
        switchMode(&OutputItemContentWidget::showText);
    }

    void OutputWidget::enterTableMode()
    {
        switchMode(&OutputItemContentWidget::showTable);
    }

    void OutputWidget::enterCustomMode()
    {
        switchMode(&OutputItemContentWidget::showCustom);
    }

    void OutputWidget::maximizePart()
    {
        OutputItemContentWidget *result = qobject_cast<OutputItemContentWidget *>(sender());
        int count = _splitter->count();
        for (int i = 0; i < count; i++) {
            OutputItemContentWidget *widget = (OutputItemContentWidget *) _splitter->widget(i);

            if (widget != result)
                widget->hide();
        }
    }

    void OutputWidget::tabCloseRequested(int index)
    {
        auto *item = qobject_cast<OutputItemContentWidget*>(widget(index));
        if (!item)
            return;
        const int result = resultIndex(item);
        removeTab(index);
        if (result >= 0)
            _outputItemContentWidgets[result] = nullptr;
        delete item;
    }

    void OutputWidget::restoreSize()
    {
        int count = _splitter->count();
        for (int i = 0; i < count; i++) {
            OutputItemContentWidget *widget = (OutputItemContentWidget *) _splitter->widget(i);
            widget->show();
        }
    }

    int OutputWidget::resultIndex(OutputItemContentWidget *result)
    {
        const auto found = std::find(_outputItemContentWidgets.begin(), _outputItemContentWidgets.end(), result);
        return found == _outputItemContentWidgets.end() ? -1 :
            static_cast<int>(std::distance(_outputItemContentWidgets.begin(), found));
    }

    void OutputWidget::showProgress()
    {
        QSize siz = size();
        QPoint point(siz.width() / 2 - ProgressBarPopup::width/2, siz.height() / 2 - ProgressBarPopup::height/2);
        _progressBarPopup->move(point);
        _progressBarPopup->show();
    }

    void OutputWidget::hideProgress()
    {
        _progressBarPopup->hide();
    }

    bool OutputWidget::progressBarActive() const 
    {
        return _progressBarPopup->isVisible();
    }

    void OutputWidget::applyDockUndockSettings(bool isDocking) const
    {
        for (auto const& item : _outputItemContentWidgets) {
            if (item)
                item->applyDockUndockSettings(isDocking);
        }
    }

    Qt::Orientation OutputWidget::getOrientation() const
    {
        return _splitter->orientation();
    }

    void OutputWidget::mouseReleaseEvent(QMouseEvent * event)
    {
        if (event->button() != Qt::MiddleButton)
            return;

        int const tabIndex = tabBar()->tabAt(event->pos());
        tabCloseRequested(tabIndex);
        QTabWidget::mouseReleaseEvent(event);
    }

    void OutputWidget::clearAllParts()
    {
        _prevViewModes.clear();
        for (auto it = _outputItemContentWidgets.rbegin(); it != _outputItemContentWidgets.rend(); ++it) {
            auto *item = *it;
            _prevViewModes.push_back(item ? item->viewMode() : AppRegistry::instance().settingsManager()->viewMode());
            delete item;
        }
        _outputItemContentWidgets.clear();
        _prevResultsCount = 0;
    }

    QString OutputWidget::buildStyleSheet()
    {
        return QStringLiteral(R"qss(
QTabWidget#resultTabs::pane { background: white; border: none; }
QTabWidget#resultTabs::tab-bar { alignment: left; }
QTabBar#resultTabBar::tab {
    background: #edf1f6; color: #68788e; padding: 6px 10px;
    border: none; border-top: 2px solid transparent; border-right: 1px solid #dce3ec;
    min-width: 72px; max-width: 240px;
}
QTabBar#resultTabBar::tab:hover { background: #e5ecf3; color: #243247; }
QTabBar#resultTabBar::tab:selected { background: white; color: #195c4d; border-top-color: #247c68; }
QTabBar#resultTabBar::close-button { image: url(:/robomongo/icons/close_2_16x16.png); width: 16px; height: 16px; }
QTabBar#resultTabBar::close-button:hover { image: url(:/robomongo/icons/close_hover_16x16.png); }
)qss");
    }

    void OutputWidget::tryToMakeAllPartsEqualInSize()
    {
        int resultsCount = _splitter->count();

        if (resultsCount <= 1)
            return;

        int dimension = _splitter->orientation() == Qt::Vertical ? _splitter->height() : _splitter->width();
        int step = dimension / resultsCount;

        QList<int> partSizes;
        for (int i = 0; i < resultsCount; ++i) {
            partSizes << step;
        }

        _splitter->setSizes(partSizes);
    }
}
