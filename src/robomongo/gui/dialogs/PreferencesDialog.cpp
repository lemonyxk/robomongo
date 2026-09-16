#include "robomongo/gui/dialogs/PreferencesDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QFontComboBox>
#include <QFontInfo>
#include <QFontMetrics>
#include <QSpinBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QScrollArea>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>

#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/gui/AppStyle.h"
#include "robomongo/gui/utils/ComboBoxUtils.h"
#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"

namespace Robomongo
{
    PreferencesDialog::PreferencesDialog(QWidget *parent)
        : BaseClass(parent)
    {
        setWindowIcon(GuiRegistry::instance().mainWindowIcon());
        setWindowTitle(tr("Preferences - ") + PROJECT_NAME_TITLE);
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
        setMinimumSize(560, 480);
        resize(680, 640);

        auto *layout = new QVBoxLayout(this);
        auto *tabs = new QTabWidget(this);
        layout->addWidget(tabs, 1);

        // Scrollable pages keep all controls reachable with large interface fonts.
        auto addPage = [tabs](QWidget *page, const QString &title) {
            auto *scroll = new QScrollArea(tabs);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(page);
            tabs->addTab(scroll, title);
        };

        auto *generalPage = new QWidget;
        auto *generalLayout = new QFormLayout(generalPage);
        generalLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        _defDisplayModeComboBox = new QComboBox;
        for (int i = Text; i <= Custom; ++i)
            _defDisplayModeComboBox->addItem(convertViewModeToString(static_cast<ViewMode>(i)));
        generalLayout->addRow(tr("Default display mode:"), _defDisplayModeComboBox);

        _timeZoneComboBox = new QComboBox;
        for (int i = Utc; i <= LocalTime; ++i)
            _timeZoneComboBox->addItem(convertTimesToString(static_cast<SupportedTimes>(i)));
        generalLayout->addRow(tr("Display dates in:"), _timeZoneComboBox);

        _uuidEncodingComboBox = new QComboBox;
        for (int i = DefaultEncoding; i <= PythonLegacy; ++i)
            _uuidEncodingComboBox->addItem(convertUUIDEncodingToString(static_cast<UUIDEncoding>(i)));
        generalLayout->addRow(tr("Legacy UUID encoding:"), _uuidEncodingComboBox);

        _loadMongoRcJsCheckBox = new QCheckBox(tr("Load .mongorc.js"));
        generalLayout->addRow(_loadMongoRcJsCheckBox);
        _disabelConnectionShortcutsCheckBox = new QCheckBox(tr("Disable connection shortcuts"));
        generalLayout->addRow(_disabelConnectionShortcutsCheckBox);

        _stylesComboBox = new QComboBox;
        _stylesComboBox->addItems(AppStyleUtils::getSupportedStyles());
        generalLayout->addRow(tr("Style:"), _stylesComboBox);
        addPage(generalPage, tr("General"));

        auto *appearancePage = new QWidget;
        auto *appearanceLayout = new QVBoxLayout(appearancePage);
        auto *appearanceForm = new QFormLayout;
        appearanceForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        appearanceLayout->addLayout(appearanceForm);

        _useSystemFontCheckBox = new QCheckBox(tr("Use system interface font"));
        appearanceForm->addRow(_useSystemFontCheckBox);
        _uiFontComboBox = new QFontComboBox;
        _uiFontComboBox->setObjectName("interfaceFont");
        appearanceForm->addRow(tr("Interface font:"), _uiFontComboBox);
        _uiFontSizeSpinBox = new QSpinBox;
        _uiFontSizeSpinBox->setRange(6, 48);
        _uiFontSizeSpinBox->setSuffix(tr(" pt"));
        appearanceForm->addRow(tr("Interface font size:"), _uiFontSizeSpinBox);

        _useDefaultEditorFontCheckBox = new QCheckBox(tr("Use default editor font"));
        appearanceForm->addRow(_useDefaultEditorFontCheckBox);
        _textFontComboBox = new QFontComboBox;
        _textFontComboBox->setObjectName("editorFont");
        appearanceForm->addRow(tr("Editor font:"), _textFontComboBox);
        _textFontSizeSpinBox = new QSpinBox;
        _textFontSizeSpinBox->setRange(6, 48);
        _textFontSizeSpinBox->setSuffix(tr(" pt"));
        appearanceForm->addRow(tr("Editor font size:"), _textFontSizeSpinBox);

        _autoTableRowHeightCheckBox = new QCheckBox(tr("Automatic table row height"));
        appearanceForm->addRow(_autoTableRowHeightCheckBox);
        _tableRowHeightSpinBox = new QSpinBox;
        _tableRowHeightSpinBox->setObjectName("tableRowHeight");
        _tableRowHeightSpinBox->setRange(18, 100);
        _tableRowHeightSpinBox->setSuffix(tr(" px"));
        appearanceForm->addRow(tr("Table row height:"), _tableRowHeightSpinBox);

        auto *hint = new QLabel(tr("Row height grows when needed to fit the font. "
                                  "Changes take effect after saving."));
        hint->setWordWrap(true);
        appearanceLayout->addWidget(hint);
        appearanceLayout->addWidget(new QLabel(tr("Table preview:")));
        _fontPreview = new QTableWidget(2, 2);
        _fontPreview->setHorizontalHeaderLabels(QStringList() << tr("Field") << tr("Value"));
        _fontPreview->setItem(0, 0, new QTableWidgetItem("name"));
        _fontPreview->setItem(0, 1, new QTableWidgetItem("Robomongo"));
        _fontPreview->setItem(1, 0, new QTableWidgetItem("count"));
        _fontPreview->setItem(1, 1, new QTableWidgetItem("1234567890"));
        _fontPreview->setEditTriggers(QAbstractItemView::NoEditTriggers);
        _fontPreview->setFocusPolicy(Qt::NoFocus);
        _fontPreview->setAlternatingRowColors(true);
        _fontPreview->setWordWrap(false);
        _fontPreview->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        _fontPreview->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        _fontPreview->setMinimumHeight(130);
        appearanceLayout->addWidget(_fontPreview);

        _editorPreview = new QLabel("db.collection.find({ active: true })");
        _editorPreview->setWordWrap(true);
        _editorPreview->setTextFormat(Qt::PlainText);
        _editorPreview->setFrameShape(QFrame::StyledPanel);
        _editorPreview->setMargin(8);
        appearanceLayout->addWidget(new QLabel(tr("Editor preview:")));
        appearanceLayout->addWidget(_editorPreview);
        appearanceLayout->addStretch();
        addPage(appearancePage, tr("Appearance"));
        tabs->setCurrentIndex(1);

        auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this);
        connect(buttonBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &PreferencesDialog::reject);
        layout->addWidget(buttonBox);

        syncWithSettings();
        for (QCheckBox *checkBox : {_useSystemFontCheckBox, _useDefaultEditorFontCheckBox,
                                    _autoTableRowHeightCheckBox})
            connect(checkBox, &QCheckBox::toggled, this, &PreferencesDialog::updateAppearancePreview);
        for (QFontComboBox *comboBox : {_uiFontComboBox, _textFontComboBox})
            connect(comboBox, &QFontComboBox::currentFontChanged, this, &PreferencesDialog::updateAppearancePreview);
        for (QSpinBox *spinBox : {_uiFontSizeSpinBox, _textFontSizeSpinBox, _tableRowHeightSpinBox})
            connect(spinBox, qOverload<int>(&QSpinBox::valueChanged), this, &PreferencesDialog::updateAppearancePreview);
        updateAppearancePreview();
    }

    void PreferencesDialog::syncWithSettings()
    {
        const SettingsManager *settings = AppRegistry::instance().settingsManager();
        utils::setCurrentText(_defDisplayModeComboBox, convertViewModeToString(settings->viewMode()));
        utils::setCurrentText(_timeZoneComboBox, convertTimesToString(settings->timeZone()));
        utils::setCurrentText(_uuidEncodingComboBox, convertUUIDEncodingToString(settings->uuidEncoding()));
        _loadMongoRcJsCheckBox->setChecked(settings->loadMongoRcJs());
        _disabelConnectionShortcutsCheckBox->setChecked(settings->disableConnectionShortcuts());
        utils::setCurrentText(_stylesComboBox, settings->currentStyle());

        _useSystemFontCheckBox->setChecked(settings->uiFontFamily().isEmpty() && settings->uiFontPointSize() <= 0);
        const QFont uiFont = QApplication::font();
        _uiFontComboBox->setCurrentFont(uiFont);
        _uiFontSizeSpinBox->setValue(QFontInfo(uiFont).pointSize());

        _useDefaultEditorFontCheckBox->setChecked(settings->textFontFamily().isEmpty() && settings->textFontPointSize() <= 0);
        const QFont textFont = GuiRegistry::instance().font();
        _textFontComboBox->setCurrentFont(textFont);
        _textFontSizeSpinBox->setValue(QFontInfo(textFont).pointSize());
        _autoTableRowHeightCheckBox->setChecked(settings->tableRowHeight() == 0);
        _tableRowHeightSpinBox->setValue(settings->tableRowHeight() > 0 ? settings->tableRowHeight()
            : qMax(28, QFontMetrics(uiFont).height() + 10));
    }

    void PreferencesDialog::updateAppearancePreview()
    {
        const bool systemFont = _useSystemFontCheckBox->isChecked();
        _uiFontComboBox->setEnabled(!systemFont);
        _uiFontSizeSpinBox->setEnabled(!systemFont);
        QFont uiFont = AppStyleUtils::defaultInterfaceFont();
        if (!systemFont) {
            uiFont.setFamily(_uiFontComboBox->currentFont().family());
            uiFont.setPointSize(_uiFontSizeSpinBox->value());
        }
        _fontPreview->setFont(uiFont);
        QHeaderView *header = _fontPreview->verticalHeader();
        header->setMinimumSectionSize(-1);
        const int minimumHeight = qMax(qMax(18, header->minimumSectionSize()), QFontMetrics(uiFont).height() + 4);
        const bool automaticHeight = _autoTableRowHeightCheckBox->isChecked();
        _tableRowHeightSpinBox->setEnabled(!automaticHeight);
        const int rowHeight = qMax(minimumHeight, automaticHeight ? qMax(28, QFontMetrics(uiFont).height() + 10)
                                                                 : _tableRowHeightSpinBox->value());
        header->setMinimumSectionSize(minimumHeight);
        header->setDefaultSectionSize(rowHeight);
        _fontPreview->setMinimumHeight(qMin(300, rowHeight * 2 + _fontPreview->horizontalHeader()->sizeHint().height() + 8));

        const bool defaultEditorFont = _useDefaultEditorFontCheckBox->isChecked();
        _textFontComboBox->setEnabled(!defaultEditorFont);
        _textFontSizeSpinBox->setEnabled(!defaultEditorFont);
        QFont textFont = GuiRegistry::instance().defaultFont();
        if (!defaultEditorFont) {
            textFont.setFamily(_textFontComboBox->currentFont().family());
            textFont.setPointSize(_textFontSizeSpinBox->value());
        }
        _editorPreview->setFont(textFont);
    }

    void PreferencesDialog::accept()
    {
        SettingsManager *settings = AppRegistry::instance().settingsManager();
        // Keep Cancel meaningful even if writing the configuration fails.
        const ViewMode oldMode = settings->viewMode();
        const SupportedTimes oldTime = settings->timeZone();
        const UUIDEncoding oldEncoding = settings->uuidEncoding();
        const bool oldLoadJs = settings->loadMongoRcJs();
        const bool oldDisableShortcuts = settings->disableConnectionShortcuts();
        const QString oldStyle = settings->currentStyle();
        const QString oldUiFamily = settings->uiFontFamily();
        const int oldUiSize = settings->uiFontPointSize();
        const QString oldTextFamily = settings->textFontFamily();
        const int oldTextSize = settings->textFontPointSize();
        const int oldRowHeight = settings->tableRowHeight();

        settings->setViewMode(convertStringToViewMode(QtUtils::toStdString(_defDisplayModeComboBox->currentText()).c_str()));
        settings->setTimeZone(convertStringToTimes(QtUtils::toStdString(_timeZoneComboBox->currentText()).c_str()));
        settings->setUuidEncoding(convertStringToUUIDEncoding(QtUtils::toStdString(_uuidEncodingComboBox->currentText()).c_str()));
        settings->setLoadMongoRcJs(_loadMongoRcJsCheckBox->isChecked());
        settings->setDisableConnectionShortcuts(_disabelConnectionShortcutsCheckBox->isChecked());
        settings->setCurrentStyle(_stylesComboBox->currentText());
        settings->setUiFontFamily(_useSystemFontCheckBox->isChecked() ? QString() : _uiFontComboBox->currentFont().family());
        settings->setUiFontPointSize(_useSystemFontCheckBox->isChecked() ? -1 : _uiFontSizeSpinBox->value());
        settings->setTextFontFamily(_useDefaultEditorFontCheckBox->isChecked() ? QString() : _textFontComboBox->currentFont().family());
        settings->setTextFontPointSize(_useDefaultEditorFontCheckBox->isChecked() ? -1 : _textFontSizeSpinBox->value());
        settings->setTableRowHeight(_autoTableRowHeightCheckBox->isChecked() ? 0 : _tableRowHeightSpinBox->value());

        if (!settings->save()) {
            settings->setViewMode(oldMode);
            settings->setTimeZone(oldTime);
            settings->setUuidEncoding(oldEncoding);
            settings->setLoadMongoRcJs(oldLoadJs);
            settings->setDisableConnectionShortcuts(oldDisableShortcuts);
            settings->setCurrentStyle(oldStyle);
            settings->setUiFontFamily(oldUiFamily);
            settings->setUiFontPointSize(oldUiSize);
            settings->setTextFontFamily(oldTextFamily);
            settings->setTextFontPointSize(oldTextSize);
            settings->setTableRowHeight(oldRowHeight);
            QMessageBox::warning(this, tr("Unable to save preferences"),
                                 tr("The configuration file could not be written. Check its permissions and try again."));
            return;
        }

        if (oldStyle != settings->currentStyle())
            AppStyleUtils::applyStyle(settings->currentStyle());
        AppStyleUtils::applyAppearanceSettings();
        BaseClass::accept();
    }
}
