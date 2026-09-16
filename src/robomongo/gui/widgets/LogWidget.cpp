#include "robomongo/gui/widgets/LogWidget.h"

#include <QHBoxLayout>
#include <QScrollBar>
#include <QMenu>
#include <QTime>
#include <QAction>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTimer>
#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/core/utils/QtUtils.h"

namespace Robomongo
{
    LogWidget::LogWidget(QWidget* parent) 
        : BaseClass(parent), _logTextEdit(new QPlainTextEdit(this)),
          _flushTimer(new QTimer(this))
    {
        _logTextEdit->setObjectName("logTextEdit");
        _logTextEdit->setReadOnly(true);
        _logTextEdit->setUndoRedoEnabled(false);
        _logTextEdit->setMaximumBlockCount(10000);
        _logTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
        applyFontSettings();
        _logTextEdit->setFrameShape(QFrame::NoFrame);
        _logTextEdit->setToolTip(tr("Recent log output (up to 10,000 lines)"));
        _logTextEdit->setContextMenuPolicy(Qt::CustomContextMenu);
        _flushTimer->setInterval(33);
        VERIFY(connect(_flushTimer, SIGNAL(timeout()), this, SLOT(flushMessages())));
        VERIFY(connect(_logTextEdit, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint &))));
        QHBoxLayout *hlayout = new QHBoxLayout;
        hlayout->setContentsMargins(0, 0, 0, 0);
        hlayout->addWidget(_logTextEdit);
        _clear = new QAction("Clear All", this);
        VERIFY(connect(_clear, SIGNAL(triggered()), this, SLOT(clearMessages())));
        setLayout(hlayout);      
    }

    void LogWidget::applyFontSettings()
    {
        const QFont logFont = GuiRegistry::instance().font();
        _logTextEdit->setFont(logFont);
        _logTextEdit->document()->setDefaultFont(logFont);
        _logTextEdit->viewport()->update();
    }

    void LogWidget::showContextMenu(const QPoint &pt)
    {
        QMenu *menu = _logTextEdit->createStandardContextMenu();
        menu->addAction(_clear);
        _clear->setEnabled(!_logTextEdit->document()->isEmpty() || !_pendingMessages.isEmpty());

        menu->exec(_logTextEdit->mapToGlobal(pt));
        delete menu;
    }

    void LogWidget::addMessage(const QString &message, mongo::logger::LogSeverity level)
    {
        PendingMessage entry;
        entry.timestamp = QTime::currentTime().toString("HH:mm:ss") + "  ";
        entry.color = QColor("#243247");

        if (level == mongo::logger::LogSeverity::Error())
            entry.color = QColor("#bd4052");
        else if (level == mongo::logger::LogSeverity::Log())
            entry.color = QColor("#748398");
        else if (level == mongo::logger::LogSeverity::Warning())
            entry.color = QColor("#996515");

        const int maxLength = 500;
        if (message.length() <= maxLength) {
            entry.text = message.trimmed();
        } else {
            entry.text = QString("(truncated) ") + message.left(maxLength).trimmed() + "...";
        }

        // Keep both pending output and the visible document bounded during bursts.
        if (_pendingMessages.size() == 10000)
            _pendingMessages.dequeue();
        _pendingMessages.enqueue(entry);
        if (!_flushTimer->isActive())
            _flushTimer->start();
    }

    void LogWidget::flushMessages()
    {
        QScrollBar *sb = _logTextEdit->verticalScrollBar();
        const bool followTail = sb->value() >= sb->maximum() - 1;
        const int scrollPosition = sb->value();
        // This cursor tracks its block when maximumBlockCount removes old
        // output, unlike an absolute scrollbar value.
        const QTextCursor visibleAnchor = _logTextEdit->cursorForPosition(QPoint(0, 0));
        const int anchorBlock = visibleAnchor.blockNumber();
        QTextCursor cursor(_logTextEdit->document());
        cursor.movePosition(QTextCursor::End);
        QTextCharFormat timestampFormat;
        timestampFormat.setForeground(QColor("#8794a7"));

        // One document/layout update per batch; yield between large bursts.
        cursor.beginEditBlock();
        const int count = qMin(250, static_cast<int>(_pendingMessages.size()));
        for (int i = 0; i < count; ++i) {
            const PendingMessage entry = _pendingMessages.dequeue();
            cursor.insertText(entry.timestamp, timestampFormat);
            QTextCharFormat messageFormat;
            messageFormat.setForeground(entry.color);
            cursor.insertText(entry.text, messageFormat);
            cursor.insertBlock();
        }
        cursor.endEditBlock();

        // Reading older output must not be interrupted by new messages.
        const int removedBlocks = anchorBlock - visibleAnchor.blockNumber();
        sb->setValue(followTail ? sb->maximum() : qMax(0, scrollPosition - removedBlocks));
        if (_pendingMessages.isEmpty())
            _flushTimer->stop();
    }

    void LogWidget::clearMessages()
    {
        _flushTimer->stop();
        _pendingMessages.clear();
        _logTextEdit->clear();
    }
}
