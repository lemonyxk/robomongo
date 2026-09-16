#include "robomongo/gui/AppStyle.h"

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"

namespace Robomongo
{
    const QString AppStyle::StyleName = "Native";

    namespace AppStyleUtils
    {
        namespace
        {
            QPalette workspacePalette()
            {
                QPalette palette = QApplication::style()->standardPalette();
                palette.setColor(QPalette::Window, QColor("#f4f6fa"));
                palette.setColor(QPalette::WindowText, QColor("#243247"));
                palette.setColor(QPalette::Base, Qt::white);
                palette.setColor(QPalette::AlternateBase, QColor("#f7f9fc"));
                palette.setColor(QPalette::Text, QColor("#243247"));
                palette.setColor(QPalette::Button, Qt::white);
                palette.setColor(QPalette::ButtonText, QColor("#243247"));
                palette.setColor(QPalette::Highlight, QColor("#247c68"));
                palette.setColor(QPalette::HighlightedText, Qt::white);
                palette.setColor(QPalette::Link, QColor("#247c68"));
                palette.setColor(QPalette::LinkVisited, QColor("#586da5"));
                palette.setColor(QPalette::ToolTipBase, QColor("#243247"));
                palette.setColor(QPalette::ToolTipText, Qt::white);
                palette.setColor(QPalette::PlaceholderText, QColor("#7c899c"));
                palette.setColor(QPalette::Mid, QColor("#dce3ec"));
                palette.setColor(QPalette::Light, Qt::white);
                palette.setColor(QPalette::Dark, QColor("#a5b2c3"));
                palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#8b97a8"));
                palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#8b97a8"));
                palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#8b97a8"));
                palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#e5eaf1"));
                palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#68788e"));
                return palette;
            }

            const QString &workspaceStyleSheet()
            {
                // One shared stylesheet avoids repolishing the entire application
                // whenever a dialog or query view is opened.
                static const QString sheet = QStringLiteral(R"qss(
QMainWindow, QDialog { background: #f4f6fa; }
QWidget#queryWidget { background: #f4f6fa; }
QMainWindow::separator { background: #dce3ec; width: 4px; height: 4px; }
QMainWindow::separator:hover, QSplitter::handle:hover { background: #a3c9bf; }
QSplitter::handle { background: #e5eaf1; }
QDockWidget { titlebar-close-icon: url(:/robomongo/icons/close_2_16x16.png); }
QDockWidget::title { background: #edf1f6; color: #68788e; padding: 8px 10px; }
QToolBar { background: #f4f6fa; border: none; spacing: 4px; padding: 5px 4px; }
QToolBar::separator { background: #dce3ec; width: 1px; margin: 5px; }
QToolButton { color: #243247; border: 1px solid transparent; border-radius: 5px; padding: 4px; }
QToolButton:hover { background: #e7edf4; border-color: #dce3ec; }
QToolButton:pressed, QToolButton:checked { background: #e1f0ea; border-color: #b8d7cb; }
QToolButton:focus { border-color: #247c68; }
QToolButton:disabled { color: #8b97a8; }
QToolButton#logsToggle { padding: 3px 12px; }
QPushButton { background: white; color: #243247; border: 1px solid #d4dde8; border-radius: 5px; padding: 4px 10px; }
QPushButton:hover { background: #f7f9fc; border-color: #a5b9c9; }
QPushButton:pressed { background: #e1f0ea; border-color: #247c68; }
QPushButton:focus { border-color: #247c68; }
QPushButton:default { background: #247c68; color: white; border-color: #247c68; }
QPushButton:default:hover { background: #1c6a59; }
QPushButton:disabled { background: #edf1f6; color: #8b97a8; border-color: #e0e6ee; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: white; color: #243247; border: 1px solid #d4dde8; border-radius: 4px;
    padding: 4px 7px; selection-background-color: #247c68; selection-color: white;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #247c68; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { background: #edf1f6; color: #8b97a8; }
QComboBox { padding-right: 22px; }
QComboBox::drop-down { border: none; width: 20px; }
QAbstractItemView { selection-background-color: #247c68; selection-color: white; outline: 0; }
QTreeView, QTableView, QListView { background: white; alternate-background-color: #f7f9fc; border: 1px solid #dce3ec; }
QTreeView::item { padding: 3px 4px; }
QTreeView::item:hover, QListView::item:hover { background: #edf5f2; }
QTreeView::item:selected, QListView::item:selected { background: #247c68; color: white; }
QTreeView#explorerTree { background: #f4f6fa; border: none; }
QTreeView#explorerTree::item { min-height: 23px; padding: 3px 6px; }
QTreeView#explorerTree::item:selected { background: #dceee7; color: #195c4d; }
QTreeView#explorerTree::item:selected:active { background: #cfe8de; }
QHeaderView::section { background: #f0f3f8; color: #68788e; border: none; border-right: 1px solid #e0e6ee; border-bottom: 1px solid #dce3ec; padding: 5px 8px; }
QTableView { gridline-color: #edf1f6; }
QTabWidget::pane { background: white; border: 1px solid #dce3ec; }
QTabBar::tab { background: #edf1f6; color: #68788e; border: 1px solid transparent; padding: 7px 12px; }
QTabBar::tab:hover { background: #e5ecf3; color: #243247; }
QTabBar::tab:selected { background: white; color: #195c4d; border-bottom: 2px solid #247c68; }
QGroupBox { border: 1px solid #dce3ec; border-radius: 6px; margin-top: 12px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0px 4px; color: #68788e; }
QStatusBar { background: #edf1f6; color: #68788e; border-top: 1px solid #dce3ec; }
QStatusBar::item { border: none; }
QToolBar#updateBar { background: #e1f0ea; border-bottom: 1px solid #b8d7cb; }
QMenu { background: white; color: #243247; border: 1px solid #dce3ec; padding: 4px; }
QMenu::item { padding: 5px 24px; border-radius: 3px; }
QMenu::item:selected { background: #e1f0ea; color: #195c4d; }
QMenu::item:disabled { color: #8b97a8; }
QMenu::separator { height: 1px; background: #e5eaf1; margin: 4px 8px; }
QToolTip { color: white; background: #243247; border: none; padding: 6px 9px; }
QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle { background: #bdc8d6; border: 2px solid #f4f6fa; border-radius: 5px; }
QScrollBar::handle:vertical { min-height: 28px; }
QScrollBar::handle:horizontal { min-width: 28px; }
QScrollBar::handle:hover { background: #8fa3b9; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QMessageBox { messagebox-text-interaction-flags: 5; }
)qss");
                return sheet;
            }
        }

        void applyStyle(const QString &styleName)
        {
            if (styleName == "Native") {
                QApplication::setStyle(new AppStyle);
            } else if (QStyle *style = QStyleFactory::create(styleName)) {
                QApplication::setStyle(style);
            } else {
                QApplication::setStyle(new AppStyle);
            }
            QApplication::setPalette(workspacePalette());
            if (qApp->styleSheet() != workspaceStyleSheet())
                qApp->setStyleSheet(workspaceStyleSheet());
            QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
            QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
            QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
            QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);
        }

        QStringList getSupportedStyles()
        {
            static QStringList result = QStringList() << AppStyle::StyleName << QStyleFactory::keys();
            return result;
        }

        void initStyle()
        {
            QString style = AppRegistry::instance().settingsManager()->currentStyle();
            applyStyle(style);
        }
    }

    int AppStyle::styleHint(StyleHint hint, const QStyleOption *option,
                           const QWidget *widget, QStyleHintReturn *returnData) const
    {
        if (hint == QStyle::SH_Widget_Animation_Duration)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    void AppStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
    {
        return QProxyStyle::drawControl(element, option, painter, widget);
    }

    void AppStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
    {
#ifdef Q_OS_WIN
        if (element == QStyle::PE_FrameFocusRect)
            return;
#endif

        return QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    QRect AppStyle::subElementRect(SubElement element, const QStyleOption *option, const QWidget *widget /*= 0 */) const
    {
        return QProxyStyle::subElementRect(element, option, widget);
    }
}
