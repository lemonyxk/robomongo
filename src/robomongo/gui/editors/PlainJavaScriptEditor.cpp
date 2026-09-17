#include "robomongo/gui/editors/PlainJavaScriptEditor.h"

#include <QPainter>
#include <QByteArray>
#include <QApplication>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QFontMetrics>
#include <Qsci/qscilexer.h>
#include <Qsci/qscilexercpp.h>
#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/core/utils/QtUtils.h"

namespace
{
    /**
    * @brief Returns the number of digits in an 32-bit integer
    * http://stackoverflow.com/questions/1489830/efficient-way-to-determine-number-of-digits-in-an-integer
    */
    int getNumberOfDigits(int x)
    {
        if (x < 0) return getNumberOfDigits(-x) + 1;

        if (x >= 10000) {
            if (x >= 10000000) {
                if (x >= 100000000) {
                    if (x >= 1000000000)
                        return 10;
                    return 9;
                }
                return 8;
            }
            if (x >= 100000) {
                if (x >= 1000000)
                    return 7;
                return 6;
            }
            return 5;
        }
        if (x >= 100) {
            if (x >= 1000)
                return 4;
            return 3;
        }
        if (x >= 10)
            return 2;
        return 1;
    }
}

namespace Robomongo
{
    const QColor RoboScintilla::marginsBackgroundColor = QColor("#f5f5f5");
    const QColor RoboScintilla::caretForegroundColor = QColor("#444444");
    const QColor RoboScintilla::matchedBraceForegroundColor = QColor("#666666");

    RoboScintilla::RoboScintilla(QWidget *parent) : QsciScintilla(parent),
        _ignoreEnterKey(false),
        _ignoreTabKey(false),
        _autoPairingEnabled(false),
        _handlingUserInput(false),
        _userCharacterAdded(false),
        _lineNumberMarginWidth(0),
        _lineNumberDigitWidth(0)
    {
        setAutoIndent(true);
        setIndentationsUseTabs(false);
        setIndentationWidth(indentationWidth);
        setUtf8(true);
        setMarginWidth(1, 0);
        setCaretForegroundColor(caretForegroundColor);
        setMatchedBraceForegroundColor(matchedBraceForegroundColor);
        setMatchedBraceBackgroundColor(QColor("#dedede"));
        setUnmatchedBraceForegroundColor(QColor("#a6535c"));
        setUnmatchedBraceBackgroundColor(QColor("#f8eeee"));
        setPaper(QColor("#ffffff"));
        setColor(caretForegroundColor);
        setSelectionBackgroundColor(QColor("#dedede"));
        setSelectionForegroundColor(QColor("#333333"));
        setCaretLineBackgroundColor(QColor("#f8f8f8"));
        setCaretLineVisible(true);
        setIndentationGuidesForegroundColor(QColor("#d6d6d6"));
        setContentsMargins(0, 0, 0, 0);
        setViewportMargins(3, 3, 3, 3);
        setMarginLineNumbers(0, true);
        setMarginsBackgroundColor(marginsBackgroundColor);
        setMarginsForegroundColor(QColor("#707070"));

        // Retain layout for visible lines without caching the entire document.
        SendScintilla(SCI_SETLAYOUTCACHE, SC_CACHE_PAGE);
        SendScintilla(SCI_SETPOSITIONCACHE, 2048);
        SendScintilla(QsciScintilla::SCI_SETHSCROLLBAR, 0);

        setWrapMode((QsciScintilla::WrapMode)QsciScintilla::SC_WRAP_NONE);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); 

        applyFontSettings();

        setLineNumbers(AppRegistry::instance().settingsManager()->lineNumbers());
        VERIFY(connect(this, SIGNAL(linesChanged()), this, SLOT(updateLineNumbersMarginWidth())));
        VERIFY(connect(this, SIGNAL(SCN_CHARADDED(int)), this, SLOT(onCharacterAdded(int))));
        VERIFY(connect(this, SIGNAL(SCN_MODIFIED(int,int,const char*,int,int,int,int,int,int,int)),
                       this, SLOT(updateAutoPairs(int,int,const char*,int))));
    }

    void RoboScintilla::applyFontSettings()
    {
        const QFont editorFont = GuiRegistry::instance().font();
        setFont(editorFont);
        if (QsciLexer *currentLexer = lexer()) {
            currentLexer->setDefaultFont(editorFont);
            currentLexer->setFont(editorFont, -1);
        }
        setMarginsFont(editorFont);

        // Space the actual text rows, not just the surrounding editor frame.
        // Recompute only when the user changes fonts; typing does no layout work.
        const int linePadding = qMax(2, QFontMetrics(editorFont).height() / 7);
        SendScintilla(SCI_SETEXTRAASCENT, linePadding);
        SendScintilla(SCI_SETEXTRADESCENT, linePadding);

        // Fonts selected by the user need not have equally wide digits.
        _lineNumberDigitWidth = 1;
        for (int digit = 0; digit < 10; ++digit)
            _lineNumberDigitWidth = qMax(_lineNumberDigitWidth,
                textWidth(STYLE_LINENUMBER, QString::number(digit)));
        updateLineNumbersMarginWidth();
        updateGeometry();
        viewport()->update();
        emit fontSettingsChanged();
    }

    int RoboScintilla::lineNumberMarginWidth() const
    {
        return marginWidth(0);
    }

    int RoboScintilla::textWidth(int style, const QString &text)
    {
        const QByteArray bytes = text.toUtf8();
        return SendScintilla(SCI_TEXTWIDTH, style, bytes.constData());
    }

    void RoboScintilla::wheelEvent(QWheelEvent *e)
    {
        if (this->isActiveWindow()) {
            QsciScintilla::wheelEvent(e);
        }
        else {
            qApp->sendEvent(parentWidget(), e);
            e->accept();
        }
    }

    void RoboScintilla::setLineNumbers(bool displayNumbers)
    {
        if (displayNumbers) {
            setMarginWidth(0, _lineNumberMarginWidth);
        }
        else {
            setMarginWidth(0, 0);
        }
    }

    void RoboScintilla::toggleLineNumbers()
    {
        setLineNumbers(!lineNumberMarginWidth());
    }

    void RoboScintilla::keyPressEvent(QKeyEvent *keyEvent)
    {
        if (_ignoreEnterKey) {
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                keyEvent->ignore();
                _ignoreEnterKey = false;
                return;
            }
        }

        if (_ignoreTabKey) {
            if (keyEvent->key() == Qt::Key_Tab) {
                keyEvent->ignore();
                _ignoreTabKey = false;
                return;
            }
        }

        if (keyEvent->key() == Qt::Key_F11) {
            keyEvent->ignore();
            toggleLineNumbers();
            return;
        }

        if (((keyEvent->modifiers() & Qt::ControlModifier) &&
            (keyEvent->key() == Qt::Key_F4 || keyEvent->key() == Qt::Key_W ||
             keyEvent->key() == Qt::Key_T || keyEvent->key() == Qt::Key_Space ||
             keyEvent->key() == Qt::Key_F || keyEvent->key() == Qt::Key_Slash))
            || keyEvent->key() == Qt::Key_Escape /*|| keyEvent->key() == Qt::Key_Return*/
            || ((keyEvent->modifiers() & Qt::ControlModifier) && (keyEvent->modifiers() & Qt::AltModifier) && keyEvent->key() == Qt::Key_Left)
            || ((keyEvent->modifiers() & Qt::ControlModifier) && (keyEvent->modifiers() & Qt::AltModifier) && keyEvent->key() == Qt::Key_Right)
            || ((keyEvent->modifiers() & Qt::ControlModifier) && (keyEvent->modifiers() & Qt::ShiftModifier) && keyEvent->key() == Qt::Key_C)
           )
        {
            keyEvent->ignore();
        }
        else
        {
            const bool newlineKey = keyEvent->key() == Qt::Key_Return ||
                keyEvent->key() == Qt::Key_Enter;
            const bool lineBreak = newlineKey || keyEvent->text().contains('\n') ||
                keyEvent->text().contains('\r');
            // QCompleter can forward keys directly to the editor. Cancel here
            // as well as in the parent filter, before auto-indent inserts text.
            if (lineBreak)
                emit completionCancelled();
            const bool textInput = !lineBreak && !keyEvent->text().isEmpty() &&
                keyEvent->text().at(0).isPrint();
            const bool commandModifier = (keyEvent->modifiers() & Qt::MetaModifier) ||
                ((keyEvent->modifiers() & Qt::ControlModifier) &&
                 !(keyEvent->modifiers() & Qt::AltModifier));
            if (newlineKey && !commandModifier && handleStructuredNewline()) {
                keyEvent->accept();
                return;
            }
            if (textInput && !commandModifier && handlePairedInput(keyEvent->text())) {
                keyEvent->accept();
                return;
            }

            // Native character notifications distinguish actual typing from
            // delete, undo, paste commands and programmatic replacements.
            _userCharacterAdded = false;
            _handlingUserInput = textInput && !commandModifier;
            BaseClass::keyPressEvent(keyEvent);
            _handlingUserInput = false;
            if (_userCharacterAdded)
                emit textInsertedByUser();
        }
    }

    void RoboScintilla::inputMethodEvent(QInputMethodEvent *event)
    {
        // Preedit uses the same native insertion path, but is not committed
        // input and must not request suggestions on every composition update.
        const bool lineBreak = event->commitString().contains('\n') ||
            event->commitString().contains('\r');
        if (lineBreak)
            emit completionCancelled();
        _userCharacterAdded = false;
        _handlingUserInput = !lineBreak && !event->commitString().isEmpty();
        BaseClass::inputMethodEvent(event);
        _handlingUserInput = false;
        if (_userCharacterAdded)
            emit textInsertedByUser();
    }

    void RoboScintilla::onCharacterAdded(int character)
    {
        if (_handlingUserInput && character >= 0x20 && character != 0x7f)
            _userCharacterAdded = true;
    }

    void RoboScintilla::setAutoPairingEnabled(bool enabled)
    {
        _autoPairingEnabled = enabled;
        if (!enabled)
            _autoPairs.clear();
    }

    void RoboScintilla::updateAutoPairs(int position, int modificationType,
                                       const char *, int length)
    {
        if (!(modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)))
            return;

        for (int i = _autoPairs.size() - 1; i >= 0; --i) {
            AutoPair &pair = _autoPairs[i];
            if (modificationType & SC_MOD_DELETETEXT) {
                const int end = position + length;
                if ((pair.opening >= position && pair.opening < end) ||
                    (pair.closing >= position && pair.closing < end)) {
                    _autoPairs.removeAt(i);
                    continue;
                }
                if (pair.opening >= end)
                    pair.opening -= length;
                if (pair.closing >= end)
                    pair.closing -= length;
            } else {
                if (pair.opening >= position)
                    pair.opening += length;
                if (pair.closing >= position)
                    pair.closing += length;
            }
        }
    }

    bool RoboScintilla::isEscapedPosition(int position) const
    {
        int backslashes = 0;
        // Limit work even for a pathological line of backslashes. Conservatively
        // avoid pairing when the escape run exceeds this small local window.
        while (position > 0 && SendScintilla(SCI_GETCHARAT, position - 1) == '\\') {
            if (++backslashes == 128)
                return true;
            --position;
        }
        return (backslashes % 2) != 0;
    }

    bool RoboScintilla::canInsertPair(int position) const
    {
        for (const AutoPair &pair : _autoPairs) {
            if (pair.character == '"' && position > pair.opening && position <= pair.closing)
                return false;
        }

        if (position == 0 || !qobject_cast<QsciLexerCPP *>(lexer()))
            return true;

        // Reuse the editor's cached lexer styles; never parse or recolour the
        // document synchronously while handling a key press.
        const int style = SendScintilla(SCI_GETSTYLEAT, position - 1) & 63;
        switch (style) {
        case QsciLexerCPP::DoubleQuotedString:
        case QsciLexerCPP::SingleQuotedString: {
            const char quote = style == QsciLexerCPP::DoubleQuotedString ? '"' : '\'';
            return SendScintilla(SCI_GETCHARAT, position - 1) == quote &&
                !isEscapedPosition(position - 1) &&
                (SendScintilla(SCI_GETSTYLEAT, position) & 63) != style;
        }
        case QsciLexerCPP::Comment:
        case QsciLexerCPP::CommentLine:
        case QsciLexerCPP::CommentDoc:
        case QsciLexerCPP::CommentLineDoc:
        case QsciLexerCPP::CommentDocKeyword:
        case QsciLexerCPP::CommentDocKeywordError:
        case QsciLexerCPP::UnclosedString:
        case QsciLexerCPP::Regex:
        case QsciLexerCPP::RawString:
        case QsciLexerCPP::EscapeSequence:
            return false;
        default:
            return true;
        }
    }

    bool RoboScintilla::handleStructuredNewline()
    {
        if (!_autoPairingEnabled || isReadOnly() || hasSelectedText() ||
            SendScintilla(SCI_GETSELECTIONS) != 1)
            return false;

        // Enter only reads a bounded neighbourhood, never formats/evaluates the
        // entire query or asks Scintilla to match a brace across the document.
        const int limit = 32768;
        const int position = SendScintilla(SCI_GETCURRENTPOS);
        const int row = SendScintilla(SCI_LINEFROMPOSITION, position);
        const int lineStart = SendScintilla(SCI_POSITIONFROMLINE, row);
        const int lineEnd = SendScintilla(SCI_GETLINEENDPOSITION, row);
        if (lineEnd - lineStart > limit)
            return false;
        int start = 0;
        if (position > limit) {
            const int firstRow = SendScintilla(SCI_LINEFROMPOSITION, position - limit) + 1;
            start = SendScintilla(SCI_POSITIONFROMLINE, firstRow);
            if (start >= position || SendScintilla(SCI_GETENDSTYLED) < start)
                return false;
            // A clipped window must begin in ordinary code, not halfway through
            // a multiline literal or comment whose opener is outside the window.
            const int style = SendScintilla(SCI_GETSTYLEAT, start) & 63;
            if (style != QsciLexerCPP::Default && style != QsciLexerCPP::Identifier &&
                style != QsciLexerCPP::Keyword && style != QsciLexerCPP::Number &&
                style != QsciLexerCPP::Operator)
                return false;
        }
        QByteArray code = bytes(start, position);
        if (code.endsWith('\0'))
            code.chop(1);
        const int tabSize = qBound(1, static_cast<int>(SendScintilla(SCI_GETTABWIDTH)), 32);
        int step = SendScintilla(SCI_GETINDENT);
        if (step <= 0)
            step = tabSize;
        if (step > 128)
            return false;

        struct Scope { char opening; int indent; int position; };
        QVector<Scope> scopes;
        const auto matches = [](char opening, char closing) {
            return (opening == '{' && closing == '}') ||
                (opening == '[' && closing == ']') || (opening == '(' && closing == ')');
        };
        const auto identifier = [](unsigned char character) {
            return (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '_' || character == '$' || character >= 0x80;
        };
        char quote = 0, lastToken = 0;
        int lastTokenPosition = -1, currentIndent = 0;
        bool leadingSpace = true, lineComment = false, blockComment = false;
        bool regex = false, characterClass = false, valueExpected = true;
        for (int i = 0; i < code.size(); ++i) {
            const char ch = code.at(i);
            const char next = i + 1 < code.size() ? code.at(i + 1) : 0;
            if (ch == '\n' || ch == '\r') {
                currentIndent = 0;
                leadingSpace = true;
            } else if (leadingSpace && (ch == ' ' || ch == '\t')) {
                currentIndent += ch == '\t' ? tabSize - currentIndent % tabSize : 1;
            } else {
                leadingSpace = false;
            }
            if (lineComment) {
                if (ch == '\n' || ch == '\r')
                    lineComment = false;
                continue;
            }
            if (blockComment) {
                if (ch == '*' && next == '/') { blockComment = false; ++i; }
                continue;
            }
            if (quote) {
                if (ch == '\\') { ++i; continue; }
                if (ch == quote) {
                    quote = 0;
                    lastToken = ch;
                    lastTokenPosition = i;
                    valueExpected = false;
                }
                continue;
            }
            if (regex) {
                if (ch == '\\') { ++i; continue; }
                if (ch == '[') characterClass = true;
                if (ch == ']') characterClass = false;
                if (ch == '/' && !characterClass) {
                    regex = false;
                    lastToken = ch;
                    lastTokenPosition = i;
                    valueExpected = false;
                }
                continue;
            }
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
                continue;
            if (ch == '/' && next == '/') { lineComment = true; ++i; continue; }
            if (ch == '/' && next == '*') { blockComment = true; ++i; continue; }
            if (ch == '"' || ch == '\'' || ch == 0x60) { quote = ch; continue; }
            if (ch == '/' && valueExpected) { regex = true; characterClass = false; continue; }
            lastToken = ch;
            lastTokenPosition = i;
            if (identifier(static_cast<unsigned char>(ch))) {
                const int wordStart = i;
                while (i + 1 < code.size() && identifier(static_cast<unsigned char>(code.at(i + 1))))
                    ++i;
                const QByteArray word = code.mid(wordStart, i - wordStart + 1);
                valueExpected = word == "return" || word == "throw" || word == "case" ||
                    word == "yield" || word == "await" || word == "typeof" ||
                    word == "void" || word == "delete" || word == "instanceof" || word == "in";
                continue;
            }
            if (ch == '{' || ch == '[' || ch == '(') {
                if (scopes.size() >= 128 || currentIndent > 1024)
                    return false;
                scopes.append(Scope{ch, currentIndent, i});
                valueExpected = true;
            } else if (ch == '}' || ch == ']' || ch == ')') {
                if (!scopes.isEmpty()) {
                    if (!matches(scopes.back().opening, ch))
                        return false;
                    scopes.removeLast();
                }
                valueExpected = false;
            } else {
                valueExpected = ch != '.';
            }
        }
        if (quote || regex || lineComment || blockComment || scopes.isEmpty())
            return false;

        const Scope scope = scopes.back();
        QByteArray suffix = bytes(position, lineEnd);
        if (suffix.endsWith('\0'))
            suffix.chop(1);
        int rightSpace = 0;
        while (rightSpace < suffix.size() &&
               (suffix.at(rightSpace) == ' ' || suffix.at(rightSpace) == '\t'))
            ++rightSpace;
        const bool beforeCloser = rightSpace < suffix.size() &&
            matches(scope.opening, suffix.at(rightSpace));
        const bool openingOrComma = lastTokenPosition == scope.position || lastToken == ',';
        const bool splitPair = beforeCloser && openingOrComma;
        const int indent = beforeCloser && !splitPair ? scope.indent :
            qMax(currentIndent, scope.indent + step);
        if (indent > 1024)
            return false;
        const bool useTabs = SendScintilla(SCI_GETUSETABS);
        const auto padding = [useTabs, tabSize](int columns) {
            return useTabs ? QByteArray(columns / tabSize, '\t') + QByteArray(columns % tabSize, ' ') :
                QByteArray(columns, ' ');
        };
        const int eolMode = SendScintilla(SCI_GETEOLMODE);
        const QByteArray eol = eolMode == SC_EOL_CRLF ? "\r\n" :
            (eolMode == SC_EOL_CR ? "\r" : "\n");
        QByteArray insertion = eol + padding(indent);
        const int caretOffset = insertion.size();
        if (splitPair)
            insertion += eol + padding(scope.indent);

        int replaceStart = position;
        while (replaceStart > lineStart && replaceStart > start &&
               (code.at(replaceStart - start - 1) == ' ' ||
                code.at(replaceStart - start - 1) == '\t'))
            --replaceStart;
        beginUndoAction();
        SendScintilla(SCI_SETSEL, replaceStart, position + rightSpace);
        SendScintilla(SCI_REPLACESEL, insertion.constData());
        SendScintilla(SCI_GOTOPOS, replaceStart + caretOffset);
        endUndoAction();
        return true;
    }

    bool RoboScintilla::handlePairedInput(const QString &text)
    {
        if (!_autoPairingEnabled || isReadOnly() || text.size() != 1 ||
            SendScintilla(SCI_GETSELECTIONS) != 1)
            return false;

        const QChar character = text.at(0);
        if (character != '"' && character != '{' && character != '}')
            return false;

        const int start = SendScintilla(SCI_GETSELECTIONSTART);
        const int end = SendScintilla(SCI_GETSELECTIONEND);
        if (character == '"' && isEscapedPosition(start))
            return false;

        if (start == end) {
            for (int i = _autoPairs.size() - 1; i >= 0; --i) {
                const AutoPair &pair = _autoPairs.at(i);
                if (pair.closing == start && pair.character == character.toLatin1() &&
                    SendScintilla(SCI_GETCHARAT, start) == pair.character) {
                    _autoPairs.removeAt(i);
                    SendScintilla(SCI_GOTOPOS, start + 1);
                    emit completionCancelled();
                    return true;
                }
            }
        }

        if (character == '}' || !canInsertPair(start))
            return false;

        const char closing = character == '{' ? '}' : '"';
        const char pairText[] = {character.toLatin1(), closing, '\0'};
        beginUndoAction();
        SendScintilla(SCI_REPLACESEL, pairText);
        SendScintilla(SCI_GOTOPOS, start + 1);
        endUndoAction();

        // Tracking only generated closers prevents skipping an unrelated quote
        // or brace already present in the document. Keep the per-edit work fixed.
        if (_autoPairs.size() == 64)
            _autoPairs.removeFirst();
        _autoPairs.append(AutoPair{start, start + 1, closing});
        emit textInsertedByUser();
        return true;
    }

    void RoboScintilla::updateLineNumbersMarginWidth()
    {
        const int numberOfDigits = qMax(2, getNumberOfDigits(lines()));
        const int width = numberOfDigits * _lineNumberDigitWidth + rowNumberWidth * 2;
        if (_lineNumberMarginWidth == width)
            return;

        _lineNumberMarginWidth = width;

        // If line numbers margin already displayed, update its width
        if (lineNumberMarginWidth()) {
            setMarginWidth(0, _lineNumberMarginWidth);
        }
    }

    void RoboScintilla::setAppropriateBraceMatching() {
#ifdef Q_OS_MAC
        // On Mac OS when brace matching is enabled, text
        // will blink when you move cursor to some brace or
        // when inside braces. This behaviour is not fully fixed
        // in QScintilla 2.9.1 and 2.8.4
        setBraceMatching(QsciScintilla::NoBraceMatch);
#else
        setBraceMatching(QsciScintilla::StrictBraceMatch);
#endif
    }


}
