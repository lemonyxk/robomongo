#include "robomongo/gui/widgets/workarea/WorkAreaTabBar.h"

#include <QMouseEvent>
#include <QAbstractButton>
#include <QPainter>
#include <QRegion>
#include <QTabWidget>
#include <QScrollArea>

namespace Robomongo
{
    namespace
    {
        class TabCloseButton final : public QAbstractButton
        {
        public:
            explicit TabCloseButton(QWidget *parent) : QAbstractButton(parent)
            {
                setFixedSize(18, 18);
                setFocusPolicy(Qt::NoFocus);
                setCursor(Qt::ArrowCursor);
                setToolTip(tr("Close tab"));
                setAccessibleName(tr("Close tab"));
            }

        protected:
            bool event(QEvent *event) override
            {
                const bool handled = QAbstractButton::event(event);
                if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
                    update();
                return handled;
            }

            void paintEvent(QPaintEvent *) override
            {
                QPainter painter(this);
                painter.setRenderHint(QPainter::Antialiasing);
                if (underMouse() || isDown()) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(isDown() ? QColor("#dedede") : QColor("#eeeeee"));
                    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 2, 2);
                }

                const QColor color = underMouse() ? QColor("#444444") : QColor("#888888");
                painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(QPointF(6, 6), QPointF(12, 12));
                painter.drawLine(QPointF(12, 6), QPointF(6, 12));
            }
        };
    }

    /**
     * @brief Creates WorkAreaTabBar, without parent widget. We are
     * assuming, that tab bar will be installed to (and owned by)
     * WorkAreaTabWidget, using QTabWidget::setTabBar().
     */
    WorkAreaTabBar::WorkAreaTabBar(QWidget *parent) 
        : QTabBar(parent)
    {
        setDrawBase(false);
        setExpanding(false);
        setUsesScrollButtons(true);
        setElideMode(Qt::ElideRight);
        setIconSize(QSize(16, 16));
        setStyleSheet(buildStyleSheet());

        _menu = new QMenu(this);
        _newShellAction = new QAction("&New Shell", _menu);
        _newShellAction->setShortcut(QKeySequence(QKeySequence::AddTab));
        _reloadShellAction = new QAction("&Re-execute Query", _menu);
        _reloadShellAction->setShortcut(Qt::CTRL | Qt::Key_R);
        _duplicateShellAction = new QAction("&Duplicate Query In New Tab", _menu);
        _duplicateShellAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_T);
        _pinShellAction = new QAction("&Pin Shell", _menu);
        _closeShellAction = new QAction("&Close Shell", _menu);
        _closeShellAction->setShortcut(Qt::CTRL | Qt::Key_W);
        _closeOtherShellsAction = new QAction("Close &Other Shells", _menu);
        _closeShellsToTheRightAction = new QAction("Close Shells to the R&ight", _menu);

        _menu->addAction(_newShellAction);
        _menu->addSeparator();
        _menu->addAction(_reloadShellAction);
        _menu->addAction(_duplicateShellAction);
        _menu->addSeparator();
        _menu->addAction(_closeShellAction);
        _menu->addAction(_closeOtherShellsAction);
        _menu->addAction(_closeShellsToTheRightAction);
    }

    void WorkAreaTabBar::paintEvent(QPaintEvent *event)
    {
        QTabBar::paintEvent(event);

        // Some native styles paint the unused strip independently of ::tab.
        // Explicitly fill that area so a macOS document background cannot leak through.
        QRegion unusedArea(rect());
        for (int i = 0; i < count(); ++i)
            unusedArea -= tabRect(i);

        QPainter painter(this);
        painter.setClipRegion(unusedArea);
        painter.fillRect(rect(), QColor("#ededed"));
        painter.setPen(QColor("#d6d6d6"));
        painter.drawLine(0, height() - 1, width(), height() - 1);
    }

    void WorkAreaTabBar::tabInserted(int index)
    {
        QTabBar::tabInserted(index);
        if (!tabsClosable())
            return;

        // Native styles choose different sides and supply legacy close icons.
        // Own this small control to keep its size and appearance consistent.
        for (ButtonPosition side : {LeftSide, RightSide}) {
            QWidget *oldButton = tabButton(index, side);
            setTabButton(index, side, nullptr);
            if (oldButton)
                oldButton->deleteLater();
        }

        auto closeButton = new TabCloseButton(this);
        setTabButton(index, RightSide, closeButton);
        connect(closeButton, &QAbstractButton::clicked, this, [this, closeButton]() {
            // Resolve at click time: tab indices change when the user reorders them.
            for (int i = 0; i < count(); ++i) {
                if (tabButton(i, RightSide) == closeButton) {
                    emit tabCloseRequested(i);
                    return;
                }
            }
        });
    }

    QSize WorkAreaTabBar::tabSizeHint(int index) const
    {
        const QSize preferred = QTabBar::tabSizeHint(index);
        return QSize(qBound(132, preferred.width(), 236), qMax(28, fontMetrics().height() + 10));
    }

    QSize WorkAreaTabBar::minimumTabSizeHint(int) const
    {
        return QSize(112, qMax(28, fontMetrics().height() + 10));
    }

    /**
     * @brief Overrides QTabBar::mouseReleaseEvent() in order to support
     * middle-mouse tab close and to implement tab context menu.
     */
    void WorkAreaTabBar::mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::MiddleButton)
            middleMouseReleaseEvent(event);
        else if (event->button() == Qt::RightButton)
            rightMouseReleaseEvent(event);

        // always calling base event handler, even if we
        // were interested by this event
        QTabBar::mouseReleaseEvent(event);
    }

    void WorkAreaTabBar::mouseDoubleClickEvent(QMouseEvent *event)
    {
        int tabIndex = tabAt(event->pos());

        // if tab was double-clicked, ignore this action
        if (tabIndex >= 0)
            return;

        int currentTab = currentIndex();
        if (currentTab < 0)
            return;

        emit newTabRequested(currentTab);
        QTabBar::mouseDoubleClickEvent(event);
    }

    /**
     * @brief Handles middle-mouse release event in order to close tab.
     */
    void WorkAreaTabBar::middleMouseReleaseEvent(QMouseEvent *event)
    {
        int tabIndex = tabAt(event->pos());
        if (tabIndex < 0)
            return;

        emit tabCloseRequested(tabIndex);
    }

    /**
     * @brief Handles right-mouse release event to show tab context menu.
     */
    void WorkAreaTabBar::rightMouseReleaseEvent(QMouseEvent *event)
    {
        int tabIndex = tabAt(event->pos());
        if (tabIndex < 0)
            return;

        // If this is a Welcome tab, do not show right click menu. 
        // Note: Scroll area represents a WelcomeTab.
        auto tabWidget = qobject_cast<QTabWidget*>(parentWidget());
        if (!tabWidget || qobject_cast<QScrollArea*>(tabWidget->widget(tabIndex)))
            return;

        QAction *selected = _menu->exec(QCursor::pos());
        if (!selected)
            return;

        emitSignalForContextMenuAction(tabIndex, selected);
    }

    /**
     * @brief Emits signal, based on specified action. Only actions
     * specified in this class are supported. If we don't know specified
     * action - no signal will be emited.
     * @param tabIndex: index of tab, for which signal will be emited.
     * @param action: context menu action.
     */
    void WorkAreaTabBar::emitSignalForContextMenuAction(int tabIndex, QAction *action)
    {
        if (action == _newShellAction)
            emit newTabRequested(tabIndex);
        else if (action == _reloadShellAction)
            emit reloadTabRequested(tabIndex);
        else if (action == _duplicateShellAction)
            emit duplicateTabRequested(tabIndex);
        else if (action == _pinShellAction)
            emit pinTabRequested(tabIndex);
        else if (action == _closeShellAction)
            emit tabCloseRequested(tabIndex);
        else if (action == _closeOtherShellsAction)
            emit closeOtherTabsRequested(tabIndex);
        else if (action == _closeShellsToTheRightAction)
            emit closeTabsToTheRightRequested(tabIndex);
    }

    /**
     * @brief Builds stylesheet for this WorkAreaTabBar widget.
     */
    QString WorkAreaTabBar::buildStyleSheet()
    {
        return QStringLiteral(
            "QTabBar { background: #ededed; }"
            "QTabBar::tab {"
                "color: #666666; background: #ededed;"
                "border: none; border-right: 1px solid #d6d6d6;"
                "border-bottom: 1px solid #d6d6d6;"
                "padding: 4px 16px; margin: 0; min-height: 34px;"
            "}"
            "QTabBar::tab:hover { color: #444444; background: #eeeeee; }"
            "QTabBar::tab:selected {"
                "color: #444444; background: #ffffff;"
                "border-bottom-color: #ffffff;"
            "}"
            "QTabBar QToolButton {"
                "color: #666666; background: #ededed;"
                "border: none; border-left: 1px solid #d6d6d6;"
                "border-radius: 0; padding: 0;"
            "}"
            "QTabBar QToolButton:hover { background: #eeeeee; }"
            "QTabBar QToolButton:pressed { background: #dedede; }"
        );
    }
}
