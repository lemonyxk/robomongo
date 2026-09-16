#pragma once


#include <QWidget>
#include <QColor>
#include <QQueue>
#include "robomongo/core/utils/LogSeverity.h"
QT_BEGIN_NAMESPACE
class QPlainTextEdit;
class QAction;
class QTimer;
QT_END_NAMESPACE

namespace Robomongo
{
    class LogWidget : public QWidget
    {
        Q_OBJECT

    public:
        typedef QWidget BaseClass;
        LogWidget(QWidget* parent = 0);        
        void applyFontSettings();

    public Q_SLOTS:
        void addMessage(const QString &message, mongo::logger::LogSeverity level);

    private Q_SLOTS:
        void showContextMenu(const QPoint &pt);
        void flushMessages();
        void clearMessages();

    private:
        struct PendingMessage
        {
            QString timestamp;
            QString text;
            QColor color;
        };

        QPlainTextEdit *const _logTextEdit;
        QTimer *const _flushTimer;
        QQueue<PendingMessage> _pendingMessages;
        QAction *_clear;
    };

}
