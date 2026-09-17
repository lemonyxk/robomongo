#include "robomongo/gui/dialogs/DocumentTextEditor.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QScreen>
#include <QSettings>
#include <Qsci/qscilexerjavascript.h>

#include "robomongo/core/mongodb/MongoConnection.h"

#include "robomongo/gui/editors/JSLexer.h"
#include "robomongo/gui/editors/FindFrame.h"
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"
#include "robomongo/gui/widgets/workarea/IndicatorLabel.h"
#include "robomongo/gui/GuiRegistry.h"

#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/core/bson/Bson.h"
#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/utils/BsonUtils.h"


namespace Robomongo
{
    const QSize DocumentTextEditor::minimumSize = QSize(800, 400);

    DocumentTextEditor::DocumentTextEditor(const CollectionInfo &info, const mongo::BSONObj &document, bool readonly, QWidget *parent) :
        DocumentTextEditor(info, QtUtils::toQString(BsonUtils::jsonString(document, mongo::TenGen, 1,
            AppRegistry::instance().settingsManager()->uuidEncoding(),
            AppRegistry::instance().settingsManager()->timeZone(), false, true)), readonly, parent)
    {
        _originalDocument = document;
        _hasOriginalDocument = true;
    }

    DocumentTextEditor::DocumentTextEditor(const CollectionInfo &info, const QString &json, bool readonly /* = false */, QWidget *parent) :
        QDialog(parent),
        _info(info),
        _readonly(readonly)
    {
        setObjectName("documentTextEditor");
        setStyleSheet(QStringLiteral(R"qss(
QDialog#documentTextEditor { background: #f2f2f0; color: #262626; }
QDialog#documentTextEditor QLabel, QDialog#documentTextEditor QCheckBox { color: #4d4d4d; }
QDialog#documentTextEditor QLineEdit {
    background: #fcfcfa; color: #262626; border-color: #d0d0cd;
    selection-background-color: #dcdcd7; selection-color: #202020;
}
QDialog#documentTextEditor QLineEdit:focus { border-color: #999994; }
QDialog#documentTextEditor QPushButton { background: #f7f7f5; color: #262626; border-color: #d0d0cd; }
QDialog#documentTextEditor QPushButton:hover { background: #e7e7e3; border-color: #999999; }
QDialog#documentTextEditor QPushButton:pressed { background: #dcdcd7; }
QDialog#documentTextEditor QPushButton:default, QDialog#documentTextEditor QPushButton:focus { border-color: #999994; }
QDialog#documentTextEditor QPushButton:disabled { background: #ededea; color: #777777; border-color: #dcdcd7; }
)qss"));
        QRect screenGeometry = screen()->availableGeometry();
        int horizontalMargin = (int)(screenGeometry.width() * 0.35);
        int verticalMargin = (int)(screenGeometry.height() * 0.20);
        QSize size(screenGeometry.width() - horizontalMargin,
                   screenGeometry.height() - verticalMargin);

        QSettings settings("3T", "Robomongo");
        if (settings.contains("DocumentTextEditor/size"))
        {
            restoreWindowSettings();
        }
        else
        {
            resize(size);
        }

        setWindowFlags(Qt::Window | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);

        Indicator *collectionIndicator = new Indicator(GuiRegistry::instance().collectionIcon(), QtUtils::toQString(_info._ns.collectionName()));
        Indicator *databaseIndicator = new Indicator(GuiRegistry::instance().databaseIcon(), QtUtils::toQString(_info._ns.databaseName()));
        Indicator *serverIndicator = new Indicator(GuiRegistry::instance().serverIcon(), QtUtils::toQString(detail::prepareServerAddress(_info._serverAddress)));
        collectionIndicator->setTextColor(QColor("#4d4d4d"));
        databaseIndicator->setTextColor(QColor("#4d4d4d"));
        serverIndicator->setTextColor(QColor("#4d4d4d"));

        QPushButton *validate = new QPushButton("Validate");
        validate->setIcon(qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation));
        VERIFY(connect(validate, SIGNAL(clicked()), this, SLOT(onValidateButtonClicked())));

        _queryText = new FindFrame(this);
        _configureQueryText();
        _queryText->sciScintilla()->setText(json);
        // clear modification state after setting the content
        _queryText->sciScintilla()->setModified(false);

        VERIFY(connect(_queryText->sciScintilla(), SIGNAL(textChanged()), this, SLOT(onQueryTextChanged())));

        QHBoxLayout *hlayout = new QHBoxLayout();
        hlayout->setContentsMargins(2, 0, 5, 1);
        hlayout->setSpacing(0);
        hlayout->addWidget(serverIndicator, 0, Qt::AlignLeft);
        hlayout->addWidget(databaseIndicator, 0, Qt::AlignLeft);
        hlayout->addWidget(collectionIndicator, 0, Qt::AlignLeft);
        hlayout->addStretch(1);

        QDialogButtonBox *buttonBox = new QDialogButtonBox (this);
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Save);
        VERIFY(connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept())));
        VERIFY(connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject())));

        QHBoxLayout *bottomlayout = new QHBoxLayout();
        bottomlayout->addWidget(validate);
        bottomlayout->addStretch(1);
        bottomlayout->addWidget(buttonBox);

        QVBoxLayout *layout = new QVBoxLayout();

        // show top bar only if we have info for it
        if (_info.isValid())
            layout->addLayout(hlayout);

        layout->addWidget(_queryText);
        layout->addLayout(bottomlayout);
        setLayout(layout);

        if (_readonly) {
            validate->hide();
            buttonBox->button(QDialogButtonBox::Save)->hide();
            _queryText->sciScintilla()->setReadOnly(true);
        }
    }

    QString DocumentTextEditor::jsonText() const
    {
        return _queryText->sciScintilla()->text().trimmed();
    }

    void DocumentTextEditor::setCursorPosition(int line, int column)
    {
        _queryText->sciScintilla()->setCursorPosition(line, column);
    }

    void DocumentTextEditor::accept()
    {
        if (!validate())
            return;

        saveWindowSettings();

        QDialog::accept();
    }

    void DocumentTextEditor::reject()
    {
        if (_queryText->sciScintilla()->isModified()) {
            int ret = QMessageBox::warning(this, tr("Robo 3T"),
                               tr("The document has been modified.\n"
                                  "Do you want to save your changes?"),
                               QMessageBox::Save | QMessageBox::Discard
                               | QMessageBox::Cancel,
                               QMessageBox::Save);

            if (ret == QMessageBox::Save) {
                this->accept();
            } else if (ret == QMessageBox::Discard) {
                QDialog::reject();
            }

            return;
        }

        saveWindowSettings();

        QDialog::reject();
    }

    bool DocumentTextEditor::validate(bool silentOnSuccess /* = true */)
    {
        QString text = jsonText();
        int len = 0;
        try {
            std::string textString = QtUtils::toStdString(text);
            const char *json = textString.c_str();
            int jsonLen = textString.length();
            int offset = 0;
            _obj.clear();
            while (offset != jsonLen)
            {
                // Bare small integers retain their original Int64 type in Edit Document.
                mongo::BSONObj doc = _hasOriginalDocument && _obj.empty()
                    ? mongo::Robomongo::fromjson(json+offset, &len, _originalDocument)
                    : mongo::Robomongo::fromjson(json+offset, &len);
                _obj.push_back(doc);
                offset += len;
            }
        } catch (const mongo::Robomongo::ParseMsgAssertionException &ex) {
//            v0.9
            QString message = QtUtils::toQString(ex.reason());
            int offset = ex.offset();

            int line = 0, pos = 0;
            _queryText->sciScintilla()->lineIndexFromPosition(offset, &line, &pos);
            _queryText->sciScintilla()->setCursorPosition(line, pos);

            int lineHeight = _queryText->sciScintilla()->lineLength(line);
            _queryText->sciScintilla()->fillIndicatorRange(line, pos, line, lineHeight, 0);

            message = QString("Unable to parse JSON:<br /> <b>%1</b>, at (%2, %3).")
                .arg(message).arg(line + 1).arg(pos + 1);

            QMessageBox::critical(NULL, "Parsing error", message);
            _queryText->setFocus();
            activateWindow();
            return false;
        }

        if (!silentOnSuccess) {
            QMessageBox::information(NULL, "Validation", "JSON is valid!");
            _queryText->setFocus();
            activateWindow();
        }

        return true;
    }

    void DocumentTextEditor::onQueryTextChanged()
    {
        _queryText->sciScintilla()->clearIndicatorRange(0, 0, _queryText->sciScintilla()->lines(), 40, 0);
    }

    void DocumentTextEditor::onValidateButtonClicked()
    {
        validate(false);
    }

    void DocumentTextEditor::closeEvent(QCloseEvent *event)
    {
        saveWindowSettings();
        QWidget::closeEvent(event);
    }

    /*
    ** Configure QsciScintilla query widget
    */
    void DocumentTextEditor::_configureQueryText()
    {
        QsciLexerJavaScript *javaScriptLexer = new JSLexer(this);
        // Scintilla paints token backgrounds itself, independently of the frame
        // stylesheet. Keep these overrides local to Edit/View Document.
        const QColor paper("#fcfcfa");
        const QColor text("#262626");
        javaScriptLexer->setDefaultPaper(paper);
        javaScriptLexer->setPaper(paper);
        javaScriptLexer->setDefaultColor(text);
        javaScriptLexer->setColor(text);
        javaScriptLexer->setColor(text, QsciScintillaBase::STYLE_DEFAULT);
        for (int style : {QsciLexerJavaScript::Comment, QsciLexerJavaScript::CommentLine,
                          QsciLexerJavaScript::CommentDoc, QsciLexerJavaScript::CommentLineDoc})
            javaScriptLexer->setColor(QColor("#555555"), style);
        for (int style : {QsciLexerJavaScript::DoubleQuotedString, QsciLexerJavaScript::SingleQuotedString,
                          QsciLexerJavaScript::RawString})
            javaScriptLexer->setColor(QColor("#365442"), style);
        javaScriptLexer->setColor(QColor("#665034"), QsciLexerJavaScript::Number);
        javaScriptLexer->setColor(QColor("#52435f"), QsciLexerJavaScript::Keyword);
        javaScriptLexer->setColor(QColor("#614451"), QsciLexerJavaScript::Regex);
        javaScriptLexer->setColor(QColor("#8c3f47"), QsciLexerJavaScript::UnclosedString);
        javaScriptLexer->setColor(QColor("#8c3f47"), QsciLexerJavaScript::CommentDocKeywordError);
        QFont font = GuiRegistry::instance().font();
        javaScriptLexer->setFont(font);
        _queryText->sciScintilla()->setAppropriateBraceMatching();
        _queryText->sciScintilla()->setFont(font);
        _queryText->sciScintilla()->setPaper(paper);
        _queryText->sciScintilla()->setLexer(javaScriptLexer);
        _queryText->sciScintilla()->setCaretForegroundColor(text);
        _queryText->sciScintilla()->setMarginsBackgroundColor(QColor("#f2f2f0"));
        _queryText->sciScintilla()->setMarginsForegroundColor(QColor("#4d4d4d"));
        _queryText->sciScintilla()->setCaretLineBackgroundColor(QColor("#fafaf8"));
        _queryText->sciScintilla()->setSelectionBackgroundColor(QColor("#dcdcd7"));
        _queryText->sciScintilla()->setSelectionForegroundColor(QColor("#202020"));
        _queryText->sciScintilla()->setMatchedBraceForegroundColor(QColor("#262626"));
        _queryText->sciScintilla()->setMatchedBraceBackgroundColor(QColor("#e5e5e0"));
        _queryText->sciScintilla()->setUnmatchedBraceForegroundColor(QColor("#8c3f47"));
        _queryText->sciScintilla()->setUnmatchedBraceBackgroundColor(QColor("#f4eaea"));
        _queryText->sciScintilla()->setIndentationGuidesForegroundColor(QColor("#d0d0cd"));
        // JSON editing experience: keep the JavaScript lexer (JSON is a subset of JS)
        // but enable editor behaviours similar to vue-json-pretty: folding,
        // indentation, brace matching and clear structure navigation.
        _queryText->sciScintilla()->setAutoIndent(true);
        _queryText->sciScintilla()->setIndentationsUseTabs(false);
        _queryText->sciScintilla()->setIndentationWidth(4);
        _queryText->sciScintilla()->setIndentationGuides(true);
        _queryText->sciScintilla()->setFolding(QsciScintilla::BoxedTreeFoldStyle);
        _queryText->sciScintilla()->setWrapMode((QsciScintilla::WrapMode)QsciScintilla::SC_WRAP_WORD);
        _queryText->sciScintilla()->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        _queryText->sciScintilla()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        _queryText->sciScintilla()->setObjectName("documentEditor");
        _queryText->sciScintilla()->setStyleSheet(
            "QFrame#documentEditor { background-color: #fcfcfa; border: 1px solid #d0d0cd; border-radius: 4px; margin: 0; padding: 0; }"
            "QFrame#documentEditor:focus { border-color: #999994; }");
    }

    void DocumentTextEditor::saveWindowSettings() const
    {
        QSettings settings("3T", "Robomongo");
        settings.setValue("DocumentTextEditor/size", size());
    }

    void DocumentTextEditor::restoreWindowSettings()
    {
        QSettings settings("3T", "Robomongo");
        resize(settings.value("DocumentTextEditor/size").toSize());
    }

}
