#include "robomongo/gui/AppStyle.h"

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"
#include "robomongo/gui/widgets/LogWidget.h"
#include "robomongo/gui/widgets/workarea/BsonTableView.h"

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
                palette.setColor(QPalette::Window, QColor("#efefef"));
                palette.setColor(QPalette::WindowText, QColor("#444444"));
                palette.setColor(QPalette::Base, QColor("#ffffff"));
                palette.setColor(QPalette::AlternateBase, QColor("#f7f7f7"));
                palette.setColor(QPalette::Text, QColor("#444444"));
                palette.setColor(QPalette::Button, QColor("#ffffff"));
                palette.setColor(QPalette::ButtonText, QColor("#444444"));
                palette.setColor(QPalette::Highlight, QColor("#dedede"));
                palette.setColor(QPalette::HighlightedText, QColor("#333333"));
                palette.setColor(QPalette::Inactive, QPalette::Highlight, QColor("#e3e3e3"));
                palette.setColor(QPalette::Accent, QColor("#666666"));
                palette.setColor(QPalette::Disabled, QPalette::Accent, QColor("#aaaaaa"));
                palette.setColor(QPalette::Link, QColor("#666666"));
                palette.setColor(QPalette::LinkVisited, QColor("#737373"));
                palette.setColor(QPalette::ToolTipBase, QColor("#f5f5f5"));
                palette.setColor(QPalette::ToolTipText, QColor("#444444"));
                palette.setColor(QPalette::PlaceholderText, QColor("#7c7c7c"));
                palette.setColor(QPalette::Mid, QColor("#d6d6d6"));
                palette.setColor(QPalette::Midlight, QColor("#e5e5e5"));
                palette.setColor(QPalette::Light, Qt::white);
                palette.setColor(QPalette::Dark, QColor("#aaaaaa"));
                palette.setColor(QPalette::Shadow, QColor("#707070"));
                palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#999999"));
                palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#999999"));
                palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#999999"));
                palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#e5e5e5"));
                palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#6d6d6d"));
                return palette;
            }

            const QString &workspaceStyleSheet()
            {
                // One shared stylesheet avoids repolishing the entire application
                // whenever a dialog or query view is opened.
                static const QString sheet = QStringLiteral(R"qss(
QMainWindow, QDialog { background: #efefef; }
QWidget#queryWidget { background: #ffffff; }
QMainWindow::separator { background: #d6d6d6; width: 4px; height: 4px; }
QMainWindow::separator:hover, QSplitter::handle:hover { background: #bfbfbf; }
QSplitter::handle { background: #e5e5e5; }
QDockWidget { titlebar-close-icon: url(:/robomongo/icons/close_2_16x16.png); }
QDockWidget::title { background: #ededed; color: #6d6d6d; padding: 8px 10px; }
QToolBar { background: #efefef; border: none; spacing: 4px; padding: 5px 4px; }
QWidget#workspaceTools { background: #efefef; border-bottom: 1px solid #d6d6d6; }
QToolBar#workspaceConnectionsToolbar, QToolBar#workspaceFilesToolbar, QToolBar#workspaceExecutionToolbar {
    background: transparent; spacing: 2px; padding: 0 3px; border: none;
}
QToolBar#workspaceConnectionsToolbar QToolButton, QToolBar#workspaceFilesToolbar QToolButton,
QToolBar#workspaceExecutionToolbar QToolButton { padding: 3px; }
QToolBar::separator { background: #d6d6d6; width: 1px; margin: 5px; }
QToolButton { color: #444444; border: 1px solid transparent; border-radius: 2px; padding: 4px; }
QToolButton:hover { background: #e6e6e6; }
QToolButton:pressed, QToolButton:checked { background: #dedede; border-color: #c6c6c6; }
QToolButton:focus { border-color: #666666; }
QToolButton:disabled { color: #999999; }
QToolButton#logsToggle { padding: 3px 12px; }
QPushButton { background: #ffffff; color: #444444; border: 1px solid #d3d3d3; border-radius: 3px; padding: 4px 10px; }
QPushButton:hover { background: #f7f7f7; border-color: #b9b9b9; }
QPushButton:checked { background: #dedede; color: #333333; border-color: #c6c6c6; }
QPushButton:pressed { background: #dedede; border-color: #666666; }
QPushButton:focus { border-color: #666666; }
QPushButton:default { background: #ededed; color: #333333; border-color: #aaaaaa; }
QPushButton:default:hover { background: #e3e3e3; }
QPushButton:default:pressed { background: #d9d9d9; }
QPushButton:disabled { background: #ededed; color: #999999; border-color: #e5e5e5; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: #ffffff; color: #444444; border: 1px solid #d3d3d3; border-radius: 4px;
    padding: 4px 7px; selection-background-color: #dedede; selection-color: #333333;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #666666; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { background: #ededed; color: #999999; }
QComboBox { padding-right: 22px; }
QComboBox::drop-down { border: none; width: 20px; }
QTextEdit, QPlainTextEdit {
    background: #ffffff; color: #444444; border: 1px solid #d6d6d6;
    selection-background-color: #dedede; selection-color: #333333;
}
QTextEdit:focus, QPlainTextEdit:focus { border-color: #666666; }
QTextEdit:disabled, QPlainTextEdit:disabled { background: #ededed; color: #999999; }
QCheckBox, QRadioButton { color: #444444; }
QCheckBox:disabled, QRadioButton:disabled { color: #999999; }
QCheckBox::indicator, QGroupBox::indicator, QRadioButton::indicator {
    width: 14px; height: 14px; background: #ffffff; border: 1px solid #b9b9b9;
}
QCheckBox::indicator, QGroupBox::indicator { border-radius: 3px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked, QGroupBox::indicator:checked {
    background: #dedede; border-color: #a8a8a8; image: url(:/robomongo/icons/theme_check.svg);
}
QCheckBox::indicator:indeterminate {
    background: #dedede; border-color: #a8a8a8; image: url(:/robomongo/icons/theme_mixed.svg);
}
QRadioButton::indicator:checked {
    background: #dedede; border-color: #a8a8a8; image: url(:/robomongo/icons/theme_radio.svg);
}
QCheckBox::indicator:hover, QGroupBox::indicator:hover, QRadioButton::indicator:hover,
QCheckBox::indicator:focus, QGroupBox::indicator:focus, QRadioButton::indicator:focus { border-color: #666666; }
QCheckBox::indicator:disabled, QGroupBox::indicator:disabled, QRadioButton::indicator:disabled {
    background: #ededed; border-color: #d3d3d3;
}
QCheckBox::indicator:checked:disabled, QGroupBox::indicator:checked:disabled { image: url(:/robomongo/icons/theme_check_disabled.svg); }
QCheckBox::indicator:indeterminate:disabled { image: url(:/robomongo/icons/theme_mixed_disabled.svg); }
QRadioButton::indicator:checked:disabled { image: url(:/robomongo/icons/theme_radio_disabled.svg); }
QProgressBar { background: #ededed; color: #333333; border: 1px solid #d6d6d6; border-radius: 3px; text-align: center; }
QProgressBar::chunk { background: #bdbdbd; border-radius: 2px; }
QProgressBar:disabled { color: #999999; }
QProgressBar::chunk:disabled { background: #d3d3d3; }
QAbstractItemView { selection-background-color: #dedede; selection-color: #333333; outline: 0; }
QTreeView, QTableView, QListView { background: #ffffff; alternate-background-color: #f7f7f7; border: 1px solid #d6d6d6; }
QTreeView::item { padding: 3px 4px; }
QTreeView::item:hover, QListView::item:hover { background: #eeeeee; }
QTreeView::item:selected, QListView::item:selected { background: #dedede; color: #333333; }
QTreeView::item:selected:!active, QTableView::item:selected:!active, QListView::item:selected:!active { background: #e3e3e3; color: #333333; }
QTreeView#explorerTree { background: #e3e5e7; border: none; }
QTreeView#explorerTree::item { min-height: 18px; padding: 1px 4px; }
QTreeView#explorerTree::item:hover:!selected { background: #dcdcdc; }
QTreeView#explorerTree::item:selected { background: #d0d0d0; color: #333333; }
QTreeView#explorerTree::item:selected:active { background: #d0d0d0; }
QHeaderView::section { background: #fafafa; color: #666666; border: none; border-right: 1px solid #eeeeee; border-bottom: 1px solid #d6d6d6; padding: 3px 6px; }
QTableView { gridline-color: #eeeeee; }
QTableView#resultTable { background: #ffffff; alternate-background-color: #f8f8f8; border: none; }
QTableView#resultTable::item:selected { background: #dedede; color: #333333; }
QTableView#resultTable::item:selected:!active { background: #e5e5e5; color: #444444; }
/* In-cell editing should be slightly lighter than the gray table background while keeping text contrast. */
QTableView#resultTable QLineEdit, QTableView#resultTable QTextEdit, QTableView#resultTable QPlainTextEdit {
    background: #fcfcfa; color: #333333; border: 1px solid #cfcfcb;
    selection-background-color: #dcdcd7; selection-color: #222222;
}
QTableView#resultTable QLineEdit:focus, QTableView#resultTable QTextEdit:focus, QTableView#resultTable QPlainTextEdit:focus {
    background: #ffffff; border-color: #999994;
}
QTableView#resultTable QLineEdit:disabled, QTableView#resultTable QTextEdit:disabled, QTableView#resultTable QPlainTextEdit:disabled {
    background: #ededed; color: #999999;
}
QTableView#resultTable QTableCornerButton::section { background: #fafafa; border: none; border-right: 1px solid #eeeeee; border-bottom: 1px solid #d6d6d6; }
QTreeView#resultTree { background: #ffffff; alternate-background-color: #f8f8f8; border: none; }
QTreeView#resultTree::item { padding: 1px 4px; }
QTabWidget::pane { background: #ffffff; border: 1px solid #d6d6d6; }
QTabBar::tab { background: #ededed; color: #6d6d6d; border: 1px solid transparent; padding: 4px 10px; }
QTabBar::tab:hover { background: #e8e8e8; color: #444444; }
QTabBar::tab:selected { background: #ffffff; color: #333333; }
QGroupBox { border: 1px solid #d6d6d6; border-radius: 6px; margin-top: 12px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0px 4px; color: #6d6d6d; }
QStatusBar { background: #ededed; color: #6d6d6d; border-top: 1px solid #d6d6d6; }
QStatusBar::item { border: none; }
QToolBar#updateBar { background: #dedede; border-bottom: 1px solid #c6c6c6; }
QMenuBar { background: #efefef; color: #444444; }
QMenuBar::item { background: transparent; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: #dedede; color: #333333; }
QMenuBar::item:disabled { color: #999999; }
QToolTip { color: #444444; background: #f5f5f5; border: 1px solid #d6d6d6; padding: 6px 9px; }
QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle { background: #c1c1c1; border: 2px solid transparent; border-radius: 5px; }
QScrollBar::handle:vertical { min-height: 28px; }
QScrollBar::handle:horizontal { min-width: 28px; }
QScrollBar::handle:hover { background: #a3a3a3; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QMessageBox { messagebox-text-interaction-flags: 5; }
)qss")
#ifndef Q_OS_MAC
                    + QStringLiteral(R"qss(
QMenu { background: #f8f8f8; color: #444444; border: 1px solid #d6d6d6; padding: 4px 0; }
QMenu::item { padding: 3px 22px; margin: 0; }
QMenu::item:selected { background: #dedede; color: #333333; }
QMenu::item:disabled { color: #999999; }
QMenu::separator { height: 1px; background: #e5e5e5; margin: 4px 8px; }
)qss")
#endif
                    ;
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
            // Let the macOS style draw its rounded menu panel and native item
            // spacing. A class palette changes colors without replacing it in QSS.
            QPalette menuPalette = QApplication::palette();
            menuPalette.setColor(QPalette::Window, QColor("#f8f8f8"));
            menuPalette.setColor(QPalette::Base, QColor("#f8f8f8"));
            menuPalette.setColor(QPalette::ButtonText, QColor("#444444"));
            menuPalette.setColor(QPalette::Highlight, QColor("#dedede"));
            menuPalette.setColor(QPalette::HighlightedText, QColor("#333333"));
            menuPalette.setColor(QPalette::Disabled, QPalette::Text, QColor("#999999"));
            menuPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#999999"));
            QApplication::setPalette(menuPalette, "QMenu");
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

        const QFont &defaultInterfaceFont()
        {
            // Capture the system font before applying a saved override.
            static const QFont font = QApplication::font();
            return font;
        }

        void applyAppearanceSettings()
        {
            const SettingsManager *settings = AppRegistry::instance().settingsManager();
            QFont font = defaultInterfaceFont();
            if (!settings->uiFontFamily().isEmpty())
                font.setFamily(settings->uiFontFamily());
            if (settings->uiFontPointSize() > 0)
                font.setPointSize(settings->uiFontPointSize());
            QApplication::setFont(font);

            // Existing query tabs keep their documents and selections intact.
            const auto widgets = QApplication::allWidgets();
            for (QWidget *widget : widgets) {
                if (auto *table = qobject_cast<BsonTableView *>(widget))
                    table->applyAppearanceSettings();
                else if (auto *editor = qobject_cast<RoboScintilla *>(widget))
                    editor->applyFontSettings();
                else if (auto *log = qobject_cast<LogWidget *>(widget))
                    log->applyFontSettings();
            }
        }

        void initStyle()
        {
            QString style = AppRegistry::instance().settingsManager()->currentStyle();
            applyStyle(style);
            applyAppearanceSettings();
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
