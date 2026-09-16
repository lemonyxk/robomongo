#include "robomongo/gui/widgets/workarea/ScriptWidget.h"

#include <QVBoxLayout>
#include <QKeyEvent>
#include <QCompleter>
#include <QStringListModel>
#include <QListView>
#include <QLabel>
#include <QTimer>
#include <QSet>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qsciscintilla.h>

#include "robomongo/core/domain/MongoShell.h"
#include "robomongo/core/domain/MongoServer.h"
#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"
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
    constexpr int MaximumVisibleQueryLines = 100;

    bool isCompletionChar(char32_t ch)
    {
        return QChar::isLetterOrNumber(ch) || ch == '_' || ch == '$' || ch == '.';
    }

    // Only used on the bounded completion window, never in the key event path.
    // The surrounding quote determines how much of a partially edited key to
    // replace: hyphens/spaces are part of the key, not replacement boundaries.
    QChar completionQuote(const QString &code, bool *blocked = nullptr, bool *inObject = nullptr,
                          int *openingIndex = nullptr)
    {
        QChar quote, previous;
        int quoteStart = -1;
        QString delimiters;
        bool lineComment = false, blockComment = false;
        bool regex = false, characterClass = false;
        for (int i = 0; i < code.size(); ++i) {
            const QChar ch = code.at(i);
            const QChar next = i + 1 < code.size() ? code.at(i + 1) : QChar();
            if (lineComment) {
                if (ch == '\n') lineComment = false;
                continue;
            }
            if (blockComment) {
                if (ch == '*' && next == '/') { blockComment = false; ++i; }
                continue;
            }
            if (!quote.isNull()) {
                if (ch == '\\') { ++i; continue; }
                if (ch == quote) { quote = QChar(); quoteStart = -1; previous = ')'; }
                continue;
            }
            if (regex) {
                if (ch == '\\') { ++i; continue; }
                if (ch == '[') characterClass = true;
                if (ch == ']') characterClass = false;
                if (ch == '/' && !characterClass) { regex = false; previous = ')'; }
                continue;
            }
            if (ch == '/' && next == '/') { lineComment = true; ++i; continue; }
            if (ch == '/' && next == '*') { blockComment = true; ++i; continue; }
            if (ch == '/' && (previous.isNull() || QStringLiteral("([{:,=;!?&|^~").contains(previous))) {
                regex = true;
                continue;
            }
            if (ch == '\'' || ch == '"' || ch == '`') { quote = ch; quoteStart = i; continue; }
            if (ch == '{' || ch == '(' || ch == '[')
                delimiters.append(ch);
            else if ((ch == '}' || ch == ')' || ch == ']') && !delimiters.isEmpty())
                delimiters.chop(1);
            if (!ch.isSpace()) previous = ch;
        }
        if (blocked)
            *blocked = lineComment || blockComment || regex || quote == '`';
        if (inObject)
            *inObject = !delimiters.isEmpty() && delimiters.back() == '{';
        if (openingIndex)
            *openingIndex = quote == '\'' || quote == '"' ? quoteStart : -1;
        return quote == '\'' || quote == '"' ? quote : QChar();
    }

    bool automaticCompletionContext(const QString &code, QChar quote, bool blocked, bool inObject)
    {
        if (blocked)
            return false;
        // The runtime distinguishes field/collection quotes from ordinary
        // string values; only the former receive contextual suggestions.
        if (!quote.isNull())
            return true;
        int start = code.size();
        bool memberAccess = false;
        char32_t firstCharacter = 0;
        while (start > 0) {
            const QChar last = code.at(start - 1);
            int width = 1;
            char32_t character = last.unicode();
            if (last.isLowSurrogate() && start > 1 && code.at(start - 2).isHighSurrogate()) {
                width = 2;
                character = QChar::surrogateToUcs4(code.at(start - 2).unicode(), last.unicode());
            }
            if (!isCompletionChar(character))
                break;
            firstCharacter = character;
            memberAccess = memberAccess || character == '.';
            start -= width;
        }
        if (memberAccess)
            return true;
        // Global names such as rs, sh and ObjectId also need completion before
        // a dot is typed. The runtime filters candidates using the full context.
        if (QChar::isLetter(firstCharacter) || firstCharacter == '_' || firstCharacter == '$')
            return true;
        while (start > 0 && code.at(start - 1).isSpace())
            --start;
        if (start == 0)
            return false;
        const QChar boundary = code.at(start - 1);
        return boundary == '.' || (inObject && (boundary == '{' || boundary == ','));
    }
}

namespace Robomongo
{
    ScriptWidget::ScriptWidget(MongoShell *shell, QueryWidget *parent) :
        _shell(shell),
        _parent(parent),
        _completionTextChanged(false),
        _disableTextAndCursorNotifications(false)
    {
        setObjectName("scriptWidget");
        setStyleSheet("QFrame#scriptWidget {background-color: #ffffff; border: 0;}");

        _queryText = new FindFrame(this);
        _topStatusBar = new TopStatusBar(_shell->server()->connectionRecord()->connectionName(), 
                                         _shell->server()->connectionRecord()->getFullAddress(), "loading...");

        QVBoxLayout *layout = new QVBoxLayout;
        layout->setSpacing(0);
        layout->setContentsMargins(8, 0, 8, 6);
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
        _autocompletionTimer->setObjectName("queryAutocompletionTimer");
        _autocompletionTimer->setSingleShot(true);
        _autocompletionTimer->setInterval(120);
        VERIFY(connect(_autocompletionTimer, SIGNAL(timeout()), this, SLOT(onAutocompletionTimeout())));

        QStringListModel *model = new QStringListModel(_completer);
        _completer->setModel(model);

        // Completion state must exist before connecting editor notifications.
        configureQueryText();
        _queryText->sciScintilla()->setFocus();
        _queryText->sciScintilla()->installEventFilter(this);
        _queryText->sciScintilla()->viewport()->installEventFilter(this);
        _completer->popup()->installEventFilter(this);

        setText(QtUtils::toQString(shell->query()));
        ui_queryLinesCountChanged();
        setTextCursor(shell->cursor());
    }

    bool ScriptWidget::eventFilter(QObject *obj, QEvent *event)
    {
        if ((obj == _queryText->sciScintilla() || obj == _queryText->sciScintilla()->viewport()) &&
                event->type() == QEvent::MouseButtonPress)
            hideAutocompletion();
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

                // Cancel on explicit navigation before Scintilla's deferred
                // cursor notification can be mistaken for the preceding edit.
                switch (keyEvent->key()) {
                case Qt::Key_Up:
                case Qt::Key_Down:
                case Qt::Key_PageUp:
                case Qt::Key_PageDown:
                    if (!_completer->popup()->isVisible())
                        hideAutocompletion();
                    break;
                case Qt::Key_Left:
                case Qt::Key_Right:
                case Qt::Key_Home:
                case Qt::Key_End:
                    hideAutocompletion();
                    break;
                default:
                    break;
                }

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
        if (AppRegistry::instance().settingsManager()->autocompletionMode() == AutocompleteNone) {
            hideAutocompletion();
            return;
        }
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

        // The runtime receives bounded context and returns only insertion
        // tokens, avoiding a repeated 32KB query prefix for every candidate.
        int row = 0, column = 0;
        scin->getCursorPosition(&row, &column);
        const int tokenLength = column - current.lineIndexLeft();
        const auto lineCharacters = scin->text(row).toUcs4();
        const QString token = QString::fromUcs4(lineCharacters.constData() + current.lineIndexLeft(), tokenLength);
        QStringList suggestions;
        QSet<QString> seen;
        for (const QString &suggestion : list) {
            if (suggestion.isEmpty() || suggestion.size() > 512 ||
                    suggestion.contains('\n') || seen.contains(suggestion))
                continue;
            suggestions.append(suggestion);
            seen.insert(suggestion);
            if (suggestions.size() >= 200)
                break;
        }
        if (suggestions.isEmpty()) {
            hideAutocompletion();
            return;
        }

        // A lone candidate identical to the existing token adds no information.
        if (suggestions.count() == 1) {
            if (suggestions.at(0) == token) {
                hideAutocompletion();
                return;
            }
        }

        // update list of completions
        QStringListModel * model = static_cast<QStringListModel *>(_completer->model());
        if (model->stringList() != suggestions)
            model->setStringList(suggestions);

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
        requestAutocompletion(false);
    }

    void ScriptWidget::requestAutocompletion(bool automatic)
    {
        _completionTextChanged = false;
        _autocompletionTimer->stop();
        if (AppRegistry::instance().settingsManager()->autocompletionMode() == AutocompleteNone) {
            hideAutocompletion();
            return;
        }
        _currentAutoCompletionInfo = sanitizeForAutocompletion();

        if (_currentAutoCompletionInfo.isEmpty() ||
                (automatic && !_currentAutoCompletionInfo.supportsAutomaticCompletion())) {
            hideAutocompletion();
            return;
        }

        _shell->autocomplete(QtUtils::toStdString(_currentAutoCompletionInfo.text()));
    }

    void ScriptWidget::hideAutocompletion()
    {
        _completionTextChanged = false;
        _autocompletionTimer->stop();
        _currentAutoCompletionInfo = AutoCompletionInfo();
        _shell->cancelAutocomplete();
        _completer->popup()->hide();
        RoboScintilla *scin = static_cast<RoboScintilla*>(_queryText->sciScintilla());
        scin->setIgnoreEnterKey(false);
        scin->setIgnoreTabKey(false);
    }

    void ScriptWidget::setDisableTextAndCursorNotifications(bool value)
    {
        _disableTextAndCursorNotifications = value;
        if (value) {
            hideAutocompletion();
        }
    }

    void ScriptWidget::onAutocompletionTimeout()
    {
        if (_disableTextAndCursorNotifications || !_queryText->sciScintilla()->hasFocus())
            return;

        RoboScintilla *scin = _queryText->sciScintilla();
        const int position = scin->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS);
        const int row = scin->SendScintilla(QsciScintilla::SCI_LINEFROMPOSITION, position);
        // Do not copy unusually large/minified lines on the UI thread.
        if (scin->SendScintilla(QsciScintilla::SCI_LINELENGTH, row) <= 65536)
            requestAutocompletion(true);
    }

    void ScriptWidget::disableFixedHeight() const
    {
        _queryText->setMinimumSize(0, 0);
        _queryText->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        _queryText->sciScintilla()->setMinimumSize(0, editorHeight(1));
        _queryText->sciScintilla()->setMaximumSize(QWIDGETSIZE_MAX, editorHeight(MaximumVisibleQueryLines));
        _queryText->sciScintilla()->setFocus();
    }

    int ScriptWidget::preferredHeight() const
    {
        const int lines = qBound(1, _queryText->sciScintilla()->lines(), MaximumVisibleQueryLines);
        return minimumSizeHint().height() + (lines - 1) * lineHeight();
    }

    QSize ScriptWidget::sizeHint() const
    {
        return QSize(QWIDGETSIZE_MAX, preferredHeight());
    }

    void ScriptWidget::ui_queryLinesCountChanged()
    {
        // Keep a one-row minimum so a small window can scroll the document.
        // QSplitter retains its sizes after updateGeometry(), so explicitly
        // notify the query pane to apply the new content height as well.
        _queryText->sciScintilla()->setMinimumHeight(editorHeight(1));
        _queryText->sciScintilla()->setMaximumHeight(editorHeight(MaximumVisibleQueryLines));
        _queryText->setMinimumSize(0, 0);
        _queryText->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        _queryText->layout()->invalidate();
        // The outer layout also caches FindFrame's minimum size. Refresh that
        // widget item before preferredHeight() reads the new single-row height.
        _queryText->updateGeometry();
        layout()->invalidate();
        setMaximumHeight(minimumSizeHint().height() + (MaximumVisibleQueryLines - 1) * lineHeight());
        updateGeometry();
        emit preferredHeightChanged();
    }

    void ScriptWidget::onFontSettingsChanged()
    {
        hideAutocompletion();
        _completer->popup()->setFont(GuiRegistry::instance().font());
        ui_queryLinesCountChanged();
    }

    void ScriptWidget::onTextChanged()
    {
        emit textChanged();
        if (!_disableTextAndCursorNotifications)
            hideAutocompletion();
    }

    void ScriptWidget::onTextInsertedByUser()
    {
        if (_disableTextAndCursorNotifications)
            return;
        _completionTextChanged = true;
        _autocompletionTimer->start();
    }

    void ScriptWidget::onCursorPositionChanged(int, int)
    {
        if (_disableTextAndCursorNotifications)
            return;

        // Scintilla reports the caret advance caused by an edit during a later
        // UI update. Keep that edit's timer; navigation alone must not arm one.
        if (_completionTextChanged) {
            _completionTextChanged = false;
            return;
        }
        hideAutocompletion();
    }

    void ScriptWidget::onCompletionActivated(const QString &text)
    {
        if (_currentAutoCompletionInfo.isEmpty())
            return;

        const AutoCompletionInfo current = sanitizeForAutocompletion();
        if (current.text() != _currentAutoCompletionInfo.text() ||
                current.line() != _currentAutoCompletionInfo.line() ||
                current.lineIndexLeft() != _currentAutoCompletionInfo.lineIndexLeft() ||
                current.lineIndexRight() != _currentAutoCompletionInfo.lineIndexRight()) {
            hideAutocompletion();
            return;
        }

        int row = _currentAutoCompletionInfo.line();
        int colLeft = _currentAutoCompletionInfo.lineIndexLeft();
        int colRight = _currentAutoCompletionInfo.lineIndexRight();
        const auto line = _queryText->sciScintilla()->text(row).toUcs4();

        int selectionIndexRight = colRight + 1;
        QString insertion = text;
        int caretColumn = -1;
        int quoteStart = -1;
        const QChar quote = completionQuote(current.text(), nullptr, nullptr, &quoteStart);
        const bool quotedLiteral = insertion.size() >= 2 &&
            insertion.front() == '"' && insertion.back() == '"';
        const bool function = quote.isNull() &&
            (insertion.endsWith('(') || insertion.endsWith(QStringLiteral("()")));

        if (quotedLiteral && !quote.isNull() && quoteStart >= 0) {
            // The runtime sends a complete JSON string for field/collection
            // candidates. Replace the whole existing literal, including any
            // already typed spaces or escapes and either style of quote.
            int cursorRow = 0, cursorColumn = 0;
            _queryText->sciScintilla()->getCursorPosition(&cursorRow, &cursorColumn);
            const int openingColumn = cursorColumn -
                static_cast<int>(current.text().mid(quoteStart).toUcs4().size());
            if (openingColumn < 0 || cursorRow != row ||
                    line.at(openingColumn) != quote.unicode()) {
                hideAutocompletion();
                return;
            }
            colLeft = openingColumn;
            if (selectionIndexRight < line.size() &&
                    line.at(selectionIndexRight) == quote.unicode())
                ++selectionIndexRight;
        } else if (function) {
            insertion.chop(insertion.endsWith(QStringLiteral("()")) ? 2 : 1);
            caretColumn = colLeft + static_cast<int>(insertion.toUcs4().size()) + 1;
            const bool existingOpening = selectionIndexRight < line.size() &&
                line.at(selectionIndexRight) == '(';
            const bool emptyOpening = existingOpening &&
                (selectionIndexRight + 1 == line.size() ||
                 line.at(selectionIndexRight + 1) == '\n' ||
                 line.at(selectionIndexRight + 1) == '\r');
            // Reuse a call already present after the token. Its arguments and
            // closing parenthesis remain untouched; the caret enters the call.
            if (!existingOpening || emptyOpening) {
                insertion += QStringLiteral("()");
                if (emptyOpening)
                    ++selectionIndexRight;
            }
        } else if ((text.endsWith('\'') || text.endsWith('"')) &&
                   selectionIndexRight < line.size() &&
                   line.at(selectionIndexRight) == text.back().unicode()) {
            ++selectionIndexRight;
        }

        const bool notificationsDisabled = _disableTextAndCursorNotifications;
        setDisableTextAndCursorNotifications(true);

        _queryText->sciScintilla()->beginUndoAction();
        _queryText->sciScintilla()->setSelection(row, colLeft, row, selectionIndexRight);
        _queryText->sciScintilla()->replaceSelectedText(insertion);
        if (caretColumn >= 0)
            _queryText->sciScintilla()->setCursorPosition(row, caretColumn);
        _queryText->sciScintilla()->endUndoAction();

        setDisableTextAndCursorNotifications(notificationsDisabled);
    }

    /*
    ** Configure QsciScintilla query widget
    */
    void ScriptWidget::configureQueryText()
    {
        QsciLexerJavaScript *javaScriptLexer = new JSLexer(this);
        _queryText->sciScintilla()->setLexer(javaScriptLexer);
        _queryText->sciScintilla()->applyFontSettings();
        int height = editorHeight(1);
        _queryText->sciScintilla()->setMinimumHeight(height);
        _queryText->sciScintilla()->setMaximumHeight(QWIDGETSIZE_MAX);
        _queryText->sciScintilla()->setAppropriateBraceMatching();
        _queryText->sciScintilla()->setAutoPairingEnabled(true);

        _queryText->sciScintilla()->setObjectName("queryEditor");
        _queryText->sciScintilla()->setStyleSheet(
            "QFrame#queryEditor {background-color: #ffffff; border: 1px solid #dce3ec; border-radius: 5px;}"
            "QFrame#queryEditor:focus {border-color: #247c68;}");
        VERIFY(connect(_queryText->sciScintilla(), SIGNAL(linesChanged()), SLOT(ui_queryLinesCountChanged())));
        VERIFY(connect(_queryText->sciScintilla(), SIGNAL(fontSettingsChanged()), SLOT(onFontSettingsChanged())));
        VERIFY(connect(_queryText->sciScintilla(), SIGNAL(textChanged()), SLOT(onTextChanged())));
        VERIFY(connect(_queryText->sciScintilla(), SIGNAL(textInsertedByUser()), SLOT(onTextInsertedByUser())));
        VERIFY(connect(_queryText->sciScintilla(), &RoboScintilla::completionCancelled,
                       this, &ScriptWidget::hideAutocompletion));
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
        RoboScintilla *scin = _queryText->sciScintilla();
        if (scin->hasSelectedText())
            return AutoCompletionInfo();
        int row = 0;
        int col = 0;
        const int position = scin->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS);
        row = scin->SendScintilla(QsciScintilla::SCI_LINEFROMPOSITION, position);
        if (scin->SendScintilla(QsciScintilla::SCI_LINELENGTH, row) > 65536)
            return AutoCompletionInfo();
        scin->getCursorPosition(&row, &col);
        // QScintilla's line indexes count Unicode code points, while QString
        // indexes UTF-16 units. Keep replacements correct after emoji as well.
        const auto line = scin->text(row).toUcs4();
        col = qBound(0, col, static_cast<int>(line.length()));
        int left = col;
        while (left > 0 && isCompletionChar(line.at(left - 1)))
            --left;
        int right = col;
        while (right < line.size() && isCompletionChar(line.at(right)))
            ++right;

        // QScintilla positions are UTF-8 byte offsets. Read only a bounded
        // context window; never materialize the whole editor for completion.
        const int windowStart = qMax(0, position - 32768);
        QByteArray bytes = scin->bytes(windowStart, position);
        if (bytes.endsWith('\0'))
            bytes.chop(1);
        if (windowStart > 0) {
            const int newline = bytes.indexOf('\n');
            if (newline < 0)
                return AutoCompletionInfo();
            bytes.remove(0, newline + 1);
        }
        const QString context = QString::fromUtf8(bytes);
        if (context.size() < col - left)
            return AutoCompletionInfo();
        bool blocked = false, inObject = false;
        const QChar quote = completionQuote(context, &blocked, &inObject);
        if (!quote.isNull()) {
            right = col;
            while (right < line.size() && line.at(right) != quote.unicode() &&
                    line.at(right) != '\n' && line.at(right) != '\r') {
                if (line.at(right) == '\\' && right + 1 < line.size())
                    ++right;
                ++right;
            }
        }
        return AutoCompletionInfo(context, row, left, right - 1,
            automaticCompletionContext(context, quote, blocked, inObject));
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
