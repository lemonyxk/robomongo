#pragma once

#include <Qsci/qsciscintilla.h>
#include <QVector>

namespace Robomongo
{
    class RoboScintilla : public QsciScintilla
    {
        Q_OBJECT
    public:
        typedef QsciScintilla BaseClass;
        enum { rowNumberWidth = 6, indentationWidth = 4 };
        static const QColor marginsBackgroundColor;
        static const QColor caretForegroundColor;
        static const QColor matchedBraceForegroundColor;

        RoboScintilla(QWidget *parent = NULL);
        void setIgnoreEnterKey(bool ignore) { _ignoreEnterKey = ignore; }
        void setIgnoreTabKey(bool ignore) { _ignoreTabKey = ignore; }
        int lineNumberMarginWidth() const;
        int textWidth(int style, const QString &text);
        void setAppropriateBraceMatching();
        void applyFontSettings();
        void setAutoPairingEnabled(bool enabled);

    Q_SIGNALS:
        void fontSettingsChanged();
        void textInsertedByUser();
        void completionCancelled();

    protected:
        void wheelEvent(QWheelEvent *e);
        void keyPressEvent(QKeyEvent *e);
        void inputMethodEvent(QInputMethodEvent *event);

    private Q_SLOTS:
        void updateLineNumbersMarginWidth();
        void onCharacterAdded(int character);
        void updateAutoPairs(int position, int modificationType,
                             const char *text, int length);

    private:
        void setLineNumbers(bool displayNumbers);
        void toggleLineNumbers();
        bool handlePairedInput(const QString &text);
        bool handleStructuredNewline();
        bool canInsertPair(int position) const;
        bool isEscapedPosition(int position) const;
        struct AutoPair {
            int opening;
            int closing;
            char character;
        };
        bool _ignoreEnterKey;
        bool _ignoreTabKey;
        bool _autoPairingEnabled;
        bool _handlingUserInput;
        bool _userCharacterAdded;
        QVector<AutoPair> _autoPairs;
        int _lineNumberMarginWidth;
        int _lineNumberDigitWidth;
    };
}
