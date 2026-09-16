#pragma once

#include <QDialog>
QT_BEGIN_NAMESPACE
class QComboBox;
class QCheckBox;
class QFontComboBox;
class QSpinBox;
class QTableWidget;
class QLabel;
QT_END_NAMESPACE

namespace Robomongo
{
    class PreferencesDialog : public QDialog
    {
        Q_OBJECT

    public:
        typedef QDialog BaseClass;
        explicit PreferencesDialog(QWidget *parent);
    public Q_SLOTS:
        virtual void accept();
    private:
        void syncWithSettings();
        void updateAppearancePreview();

        QComboBox *_defDisplayModeComboBox;
        QComboBox *_timeZoneComboBox;
        QComboBox *_uuidEncodingComboBox;
        QCheckBox *_loadMongoRcJsCheckBox;
        QCheckBox *_disabelConnectionShortcutsCheckBox;
        QComboBox *_stylesComboBox;
        QCheckBox *_useSystemFontCheckBox;
        QFontComboBox *_uiFontComboBox;
        QSpinBox *_uiFontSizeSpinBox;
        QCheckBox *_useDefaultEditorFontCheckBox;
        QFontComboBox *_textFontComboBox;
        QSpinBox *_textFontSizeSpinBox;
        QCheckBox *_autoTableRowHeightCheckBox;
        QSpinBox *_tableRowHeightSpinBox;
        QTableWidget *_fontPreview;
        QLabel *_editorPreview;
    };
}
