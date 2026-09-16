#include "robomongo/gui/editors/PlainJavaScriptEditor.h"

#include <QPainter>
#include <QByteArray>
#include <QApplication>
#include <QKeyEvent>
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
    const QColor RoboScintilla::marginsBackgroundColor = QColor("#f4f6fa");
    const QColor RoboScintilla::caretForegroundColor = QColor("#243247");
    const QColor RoboScintilla::matchedBraceForegroundColor = QColor("#247c68");

    RoboScintilla::RoboScintilla(QWidget *parent) : QsciScintilla(parent),
        _ignoreEnterKey(false),
        _ignoreTabKey(false),
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
        setMatchedBraceBackgroundColor(QColor("#dcefe8"));
        setUnmatchedBraceForegroundColor(QColor("#bd4052"));
        setUnmatchedBraceBackgroundColor(QColor("#fdecef"));
        setPaper(QColor("#ffffff"));
        setColor(caretForegroundColor);
        setSelectionBackgroundColor(QColor("#dcefe8"));
        setSelectionForegroundColor(caretForegroundColor);
        setCaretLineBackgroundColor(QColor("#f4f8f7"));
        setCaretLineVisible(true);
        setIndentationGuidesForegroundColor(QColor("#dce3ec"));
        setContentsMargins(0, 0, 0, 0);
        setViewportMargins(3, 3, 3, 3);
        QFont ourFont = GuiRegistry::instance().font();
        setMarginsFont(ourFont);
        setMarginLineNumbers(0, true);
        setMarginsBackgroundColor(marginsBackgroundColor);
        setMarginsForegroundColor(QColor("#8794a7"));

        setFont(ourFont);
        // Retain layout for visible lines without caching the entire document.
        SendScintilla(SCI_SETLAYOUTCACHE, SC_CACHE_PAGE);
        SendScintilla(SCI_SETPOSITIONCACHE, 2048);
        SendScintilla(QsciScintilla::SCI_SETHSCROLLBAR, 0);

        setWrapMode((QsciScintilla::WrapMode)QsciScintilla::SC_WRAP_NONE);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); 

        // Cache width of one digit
#ifdef Q_OS_WIN
        _lineNumberDigitWidth = rowNumberWidth;
#else
        _lineNumberDigitWidth = textWidth(STYLE_LINENUMBER, "0");
#endif
        updateLineNumbersMarginWidth();

        setLineNumbers(AppRegistry::instance().settingsManager()->lineNumbers());
        VERIFY(connect(this, SIGNAL(linesChanged()), this, SLOT(updateLineNumbersMarginWidth())));
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
            BaseClass::keyPressEvent(keyEvent);
        }
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
