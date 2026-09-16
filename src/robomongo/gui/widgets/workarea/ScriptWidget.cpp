#include "robomongo/gui/widgets/workarea/ScriptWidget.h"

#include <QVBoxLayout>
#include <QKeyEvent>
#include <QCompleter>
#include <QStringListModel>
#include <QListView>
#include <QLabel>
#include <QTimer>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qsciscintilla.h>

#include "robomongo/core/domain/MongoShell.h"
#include "robomongo/core/domain/MongoServer.h"
#include "robomongo/core/settings/ConnectionSettings.h"
#include "robomongo/core/utils/QtUtils.h"

#include "robomongo/gui/widgets/workarea/IndicatorLabel.h"
#include "robomongo/gui/widgets/workarea/QueryWidget.h"
#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/gui/editors/JSLexer.h"
#include "robomongo/gui/editors/FindFrame.h"
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"

namespace
{
    bool isStopChar(const QChar &ch, bool direction)
    {
        if (ch == '='  ||  ch == ';'  ||
            ch == '('  ||  ch == ')'  ||
            ch == '{'  ||  ch == '}'  ||
            ch == '-'  ||  ch == '/'  ||
            ch == '+'  ||  ch == '*'  ||
            ch == '\r' ||  ch == '\n' ||
            ch == ' ' ) {
                return true;
        }

        if (direction) { // right direction
            if (ch == '.')
                return true;
        }

        return false;
    }

    bool isForbiddenChar(const QChar &ch)
    {
        return ch == '\"' ||  ch == '\'';
    }
}

namespace Robomongo
{
    ScriptWidget::ScriptWidget(MongoShell *shell, QueryWidget *parent) :
        _shell(shell),
        _parent(parent),
        _textChanged(false),
        _disableTextAndCursorNotifications(false)
    {
        setObjectName("scriptWidget");
        setStyleSheet("QFrame#scriptWidget {background-color: #ffffff; border: 0;}");

        _queryText = new FindFrame(this);
        _topStatusBar = new TopStatusBar(_shell->server()->connectionRecord()->connectionName(), 
                                         _shell->server()->connectionRecord()->getFullAddress(), "loading...");

        QVBoxLayout *layout = new QVBoxLayout;
        layout->setSpacing(0);
        layout->setContentsMargins(12, 0, 12, 10);
        layout->addWidget(_topStatusBar, 0, Qt::AlignTop);
        layout->addWidget(_queryText);
        setLayout(layout);

        _completer = new QCompleter(this);
        _completer->setWidget(_queryText->sciScintilla());
        _completer->setCompletionMode(QCompleter::PopupCompletion);
        _completer->setCaseSensitivity(Qt::CaseInsensitive);
        _completer->setMaxVisibleItems(20);
        _completer->setWrapAround(false);
        _completer->popup()->setFont(GuiRegistry::instance().font());
        if (QListView *popup = qobject_cast<QListView *>(_completer->popup()))
            popup->setUniformItemSizes(true);
        VERIFY(connect(_completer, SIGNAL(activated(const QString &)), this, SLOT(onCompletionActivated(const QString&))));

        _autocompletionTimer = new QTimer(this);
        _autocompletionTimer->setSingleShot(true);
        _autocompletionTimer->setInterval(120);
        VERIFY(connect(_autocompletionTimer, SIGNAL(timeout()), this, SLOT(onAutocompletionTimeout())));

        QStringListModel *model = new QStringListModel(_completer);
        _completer->setModel(model);

        // Completion state must exist before connecting editor notifications.
        configureQueryText();
        _queryText->sciScintilla()->setFocus();
        _queryText->sciScintilla()->installEventFilter(this);
        _completer->popup()->installEventFilter(this);

        setText(QtUtils::toQString(shell->query()));
        setTextCursor(shell->cursor());
    }

    bool ScriptWidget::eventFilter(QObject *obj, QEvent *event)
    {
        // QCompleter forwards popup keys directly to QWidget::event(), which
        // bypasses the editor's event filters. Cancel pending responses here
        // so an Escape-dismissed popup cannot reopen from a late reply.
        if (obj == _completer->popup()) {
            if (event->type() == QEvent::KeyPress &&
                    static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                hideAutocompletion();
                return true;
            }
            return false;
        }
        if (obj == _queryText->sciScintilla()) {
            if (event->type() == QEvent::KeyPress) {
                QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

                if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter
                        || keyEvent->key() == Qt::Key_Tab) {
                    // A visible popup owns acceptance keys. Keep its context
                    // alive until QCompleter emits activated().
                    if (!_completer->popup()->isVisible())
                        hideAutocompletion();
                    return false;
                }
                if (keyEvent->key() == Qt::Key_Escape)
                    hideAutocompletion();
            }
        }
        return QFrame::eventFilter(obj, event);
    }

    void ScriptWidget::setup(const MongoShellExecResult &execResult)
    {
        setCurrentDatabase(execResult.currentDatabase(), execResult.isCurrentDatabaseValid());
        setCurrentServer(execResult.currentServer(), execResult.isCurrentServerValid());
    }

    void ScriptWidget::setText(const QString &text)
    {
        const bool notificationsDisabled = _disableTextAndCursorNotifications;
        setDisableTextAndCursorNotifications(true);
        _queryText->sciScintilla()->setText(text);
        setDisableTextAndCursorNotifications(notificationsDisabled);
    }

    void ScriptWidget::setTextCursor(const CursorPosition &cursor)
    {
        if (cursor.isNull()) {
            _queryText->sciScintilla()->setCursorPosition(15, 1000);
            return;
        }

        int column = cursor.column();
        if (column < 0) {
            column = _queryText->sciScintilla()->text(cursor.line()).length() + column;
        }

        _queryText->sciScintilla()->setCursorPosition(cursor.line(), column);
    }

    QString ScriptWidget::text() const
    {
        return _queryText->sciScintilla()->text();
    }

    QString ScriptWidget::selectedText() const
    {
        return _queryText->sciScintilla()->selectedText();
    }

    void ScriptWidget::selectAll()
    {
        _queryText->sciScintilla()->selectAll();
    }

    void ScriptWidget::setScriptFocus()
    {
        _queryText->sciScintilla()->setFocus();
    }

    void ScriptWidget::setCurrentDatabase(const std::string &database, bool isValid)
    {
        _topStatusBar->setCurrentDatabase(database, isValid);
    }

    void ScriptWidget::setCurrentServer(const std::string &address, bool isValid)
    {
        _topStatusBar->setCurrentServer(address, isValid);
    }

    void ScriptWidget::showAutocompletion(const QStringList &list, const QString &prefix)
    {
        RoboScintilla *scin = _queryText->sciScintilla();
        if (!scin->hasFocus() || _currentAutoCompletionInfo.isEmpty() ||
                prefix != _currentAutoCompletionInfo.text())
            return;

        // The shell answers asynchronously. Never apply a result to edited text
        // or to a different cursor position.
        const AutoCompletionInfo current = sanitizeForAutocompletion();
        if (current.text() != prefix || current.line() != _currentAutoCompletionInfo.line() ||
                current.lineIndexLeft() != _currentAutoCompletionInfo.lineIndexLeft() ||
                current.lineIndexRight() != _currentAutoCompletionInfo.lineIndexRight())
            return;

        if (list.isEmpty()) {
            hideAutocompletion();
            return;
        }

        // do not show single autocompletion which is identical to existing prefix
        // or if it identical to prefix + '('.
        if (list.count() == 1) {
            if (list.at(0) == prefix ||
                list.at(0) == (prefix + "(")) {
                hideAutocompletion();
                return;
            }
        }

        // update list of completions
        QStringListModel * model = static_cast<QStringListModel *>(_completer->model());
        if (model->stringList() != list)
            model->setStringList(list);

        const int position = scin->positionFromLineIndex(current.line(), current.lineIndexLeft());
        const int x = scin->SendScintilla(QsciScintilla::SCI_POINTXFROMPOSITION, 0, position);
        const int y = scin->SendScintilla(QsciScintilla::SCI_POINTYFROMPOSITION, 0, position);
        const QPoint anchor = scin->viewport()->pos() + QPoint(x, y);
        const QRect rect(anchor, QSize(qMin(550, qMax(260, scin->width())), lineHeight()));

        _completer->complete(rect);
        _completer->popup()->setCurrentIndex(_completer->completionModel()->index(0, 0));
        scin->setIgnoreEnterKey(true);
        scin->setIgnoreTabKey(true);
    }

    void ScriptWidget::showAutocompletion()
    {
        _autocompletionTimer->stop();
        _currentAutoCompletionInfo = sanitizeForAutocompletion();

        if (_currentAutoCompletionInfo.isEmpty()) {
            hideAutocompletion();
            return;
        }

        _shell->autocomplete(QtUtils::toStdString(_currentAutoCompletionInfo.text()));
    }

    void ScriptWidget::hideAutocompletion()
    {
        _autocompletionTimer->stop();
        _currentAutoCompletionInfo = AutoCompletionInfo();
        _completer->popup()->hide();
        RoboScintilla *scin = static_cast<RoboScintilla*>(_queryText->sciScintilla());
        scin->setIgnoreEnterKey(false);
        scin->setIgnoreTabKey(false);
    }

    void ScriptWidget::setDisableTextAndCursorNotifications(bool value)
    {
        _disableTextAndCursorNotifications = value;
        if (value) {
            _textChanged = false;
            hideAutocompletion();
        }
    }

    void ScriptWidget::onAutocompletionTimeout()
    {
        if (_disableTextAndCursorNotifications || !_queryText->sciScintilla()->hasFocus())
            return;

        int row = 0;
        int column = 0;
        _queryText->sciScintilla()->getCursorPosition(&row, &column);
        // Avoid copying minified documents on every keystroke. Explicit
        // completion remains available for unusually long lines.
        if (_queryText->sciScintilla()->SendScintilla(QsciScintilla::SCI_LINELENGTH, row) <= 65536)
            showAutocompletion();
    }

    void ScriptWidget::disableFixedHeight() const
    {
        _queryText->setMinimumSize(0, 0);
        _queryText->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        _queryText->sciScintilla()->setMinimumSize(0, 0);
        _queryText->sciScintilla()->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        _queryText->sciScintilla()->setFocus();
    }

    void ScriptWidget::ui_queryLinesCountChanged()
    {
        // Set fixed size only if output widget is docked
        if (_parent->outputWindowDocked())
        {
            const int lines = qBound(1, _queryText->sciScintilla()->lines(), 18);
            const int editorTotalHeight = editorHeight(lines);
            if (_queryText->sciScintilla()->minimumHeight() == editorTotalHeight &&
                    _queryText->sciScintilla()->maximumHeight() == editorTotalHeight &&
                    _queryText->minimumHeight() == editorTotalHeight &&
                    _queryText->maximumHeight() == editorTotalHeight + FindFrame::HeightFindPanel)
                return;

            _queryText->sciScintilla()->setFixedHeight(editorTotalHeight);
            _queryText->setMinimumHeight(editorTotalHeight);
            _queryText->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
            _queryText->setMaximumHeight(editorTotalHeight + FindFrame::HeightFindPanel);
        }
    }

    void ScriptWidget::onTextChanged()
    {
        emit textChanged();
        if (!_disableTextAndCursorNotifications) {
            hideAutocompletion();
            _textChanged = true;
        }
    }

    void ScriptWidget::onCursorPositionChanged(int, int)
    {
        if (_disableTextAndCursorNotifications)
            return;

        if (_textChanged) {
            _autocompletionTimer->start();
            _textChanged = false;
        } else {
            hideAutocompletion();
        }
    }

    void ScriptWidget::onCompletionActivated(const QString &text)
    {
        if (_currentAutoCompletionInfo.isEmpty())
            return;

        int row = _currentAutoCompletionInfo.line();
        int colLeft = _currentAutoCompletionInfo.lineIndexLeft();
        int colRight = _currentAutoCompletionInfo.lineIndexRight();
        QString line = _queryText->sciScintilla()->text(row);

        int selectionIndexRight = colRight + 1;

        // overwrite open parenthesis, if it already exists in text
        if (text.endsWith('(')) {
            if (line.length() > colRight + 1) {
                if (line.at(colRight + 1) == '(') {
                    ++selectionIndexRight;
                }
            }
        }

        const bool notificationsDisabled = _disableTextAndCursorNotifications;
        setDisableTextAndCursorNotifications(true);

        _queryText->sciScintilla()->beginUndoAction();
        _queryText->sciScintilla()->setSelection(row, colLeft, row, selectionIndexRight);
        _queryText->sciScintilla()->replaceSelectedText(text);
        _queryText->sciScintilla()->endUndoAction();

        setDisableTextAndCursorNotifications(notificationsDisabled);
    }

    /*
    ** Configure QsciScintilla query widget
    */
    void ScriptWidget::configureQueryText()
    {
        QsciLexerJavaScript *javaScriptLexer = new JSLexer(this);
        javaScriptLexer->setFont(GuiRegistry::instance().font());
        int height = editorHeight(1);
        _queryText->sciScintilla()->setMinimumHeight(height);
        _queryText->sciScintilla()->setFixedHeight(height);
        _queryText->sciScintilla()->setAppropriateBraceMatching();
        _queryText->sciScintilla()->setFont(GuiRegistry::instance().font());
        _queryText->sciScintilla()->setLexer(javaScriptLexer);

        _queryText->sciScintilla()->setObjectName("queryEditor");
        _queryText->sciScintilla()->setStyleSheet(
            "QFrame#queryEditor {background-color: #ffffff; border: 1px solid #dce3ec; border-radius: 5px;}"
            "QFrame#queryEditor:focus {border-color: #247c68;}");
        VERIFY(connect(_queryText->sciScintilla(), SIGNAL(linesChanged()), SLOT(ui_queryLinesCountChanged())));
        VERIFY(connect(_queryText->sciScintilla(), SIGNAL(textChanged()), SLOT(onTextChanged())));
        VERIFY(connect(_queryText->sciScintilla(), SIGNAL(cursorPositionChanged(int, int)), SLOT(onCursorPositionChanged(int, int))));
    }

    /**
     * @brief Calculates line height of text editor
     */
    int ScriptWidget::lineHeight() const
    {  
        return _queryText->sciScintilla()->textHeight(-1);
    }

    /**
     * @brief Calculates preferable editor height for specified number of lines
     */
    int ScriptWidget::editorHeight(int lines) const
    {
        return lines * lineHeight() + 8;
    }

    AutoCompletionInfo ScriptWidget::sanitizeForAutocompletion()
    {
        int row = 0;
        int col = 0;
        _queryText->sciScintilla()->getCursorPosition(&row, &col);
        QString line = _queryText->sciScintilla()->text(row);
        col = qBound(0, col, static_cast<int>(line.length()));

        int leftStop = -1;
        for (int i = col - 1; i >= 0; --i) {
            const QChar ch = line.at(i);

            if (isForbiddenChar(ch))
                return AutoCompletionInfo();

            if (isStopChar(ch, false)) {
                leftStop = i;
                break;
            }
        }

        int rightStop = line.length();
        for (int i = col; i < line.length(); ++i) {
            const QChar ch = line.at(i);

            if (isForbiddenChar(ch))
                return AutoCompletionInfo();

            if (isStopChar(ch, true)) {
                rightStop = i;
                break;
            }
        }

        leftStop = leftStop + 1;
        rightStop = rightStop - 1;
        //int len = ondemand ? col - leftStop : rightStop - leftStop + 1;
        int len = col - leftStop;

        QString final = line.mid(leftStop, len);
        return AutoCompletionInfo(final, row, leftStop, rightStop);
    }

    TopStatusBar::TopStatusBar(const std::string &connectionName, const std::string &serverName, const std::string &dbName)
    {
        setObjectName("queryContextBar");
        setContentsMargins(0, 0, 0, 0);
        _textColor = QColor("#748398");

        // Indicators paint plain text. Keep presentation state separate from
        // connection data instead of passing QLabel's old HTML markup to them.
        _currentConnectionLabel = new Indicator(QIcon(), QString::fromStdString(connectionName));
        _currentConnectionLabel->setObjectName("connectionContext");
        _currentConnectionLabel->setMaximumWidth(200);
        _currentConnectionLabel->setTextColor(QColor("#405269"));
        QFont connectionFont = font();
        connectionFont.setWeight(QFont::Medium);
        _currentConnectionLabel->setFont(connectionFont);

        _currentServerLabel = new Indicator(QIcon());
        _currentServerLabel->setObjectName("serverContext");
        _currentServerLabel->setMaximumWidth(480);
        setCurrentServer(serverName);

        _currentDatabaseLabel = new Indicator(QIcon());
        _currentDatabaseLabel->setObjectName("databaseContext");
        _currentDatabaseLabel->setMaximumWidth(240);
        _currentDatabaseLabel->setFont(connectionFont);
        setCurrentDatabase(dbName);

        auto *topLayout = new QHBoxLayout(this);
        topLayout->setSpacing(10);
        topLayout->setContentsMargins(2, 6, 2, 6);
        auto addSeparator = [this, topLayout]() {
            auto *separator = new QLabel(QStringLiteral("/"), this);
            separator->setStyleSheet("color: #b1bbc9; background: transparent;");
            topLayout->addWidget(separator);
        };
        topLayout->addWidget(_currentConnectionLabel);
        addSeparator();
        topLayout->addWidget(_currentServerLabel);
        addSeparator();
        topLayout->addWidget(_currentDatabaseLabel);
        topLayout->addStretch(1);
    }

    void TopStatusBar::setCurrentDatabase(const std::string &database, bool isValid)
    {
        _currentDatabaseLabel->setTextColor(QColor(isValid ? "#247c68" : "#bd4052"));
        _currentDatabaseLabel->setText(QString::fromStdString(database));
    }

    void TopStatusBar::setCurrentServer(const std::string &address, bool isValid)
    {
        _currentServerLabel->setTextColor(isValid ? _textColor : QColor("#bd4052"));
        _currentServerLabel->setText(QString::fromStdString(detail::prepareServerAddress(address)));
    }
}
