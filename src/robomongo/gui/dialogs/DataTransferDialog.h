#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QPointer>
#include <QThread>
#include <atomic>
#include <memory>
#include "robomongo/core/mongodb/DataTransfer.h"

QT_BEGIN_NAMESPACE
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QLabel;
QT_END_NAMESPACE

namespace Robomongo
{
    class ConnectionSettings;
    class MongoDatabase;
    class MongoServer;

    // Owns a dedicated connection: a mongoc client is never shared across threads.
    class DataTransferThread final : public QThread
    {
        Q_OBJECT
    public:
        DataTransferThread(std::unique_ptr<ConnectionSettings> connection,
                           const DataTransferOptions &options, double timeout, QObject *parent);
        ~DataTransferThread() override;
        void cancel();
        const DataTransferResult &result() const { return _result; }
    Q_SIGNALS:
        void progress(const QString &collection, qint64 documents, int completedCollections,
                      int totalCollections, const QString &message);
    protected:
        void run() override;
    private:
        std::unique_ptr<ConnectionSettings> _connection;
        DataTransferOptions _options;
        double _timeout;
        std::atomic_bool _cancelled{false};
        DataTransferResult _result;
    };

    class DataTransferDialog : public QDialog
    {
        Q_OBJECT
    public:
        DataTransferDialog(MongoDatabase *database, const QString &collection,
                           DataTransferDirection direction, QWidget *parent = nullptr);
        ~DataTransferDialog() override;
    public Q_SLOTS:
        void accept() override;
        void reject() override;
    private:
        void browse();
        void setRunning(bool running);
        void transferFinished();
        void showProgress(const QString &collection, qint64 documents,
                          int completedCollections, int totalCollections, const QString &message);

        QPointer<MongoDatabase> _database;
        QPointer<MongoServer> _server;
        DataTransferOptions _options;
        DataTransferThread *_job = nullptr;
        QLineEdit *_path;
        QComboBox *_importMode;
        QCheckBox *_validateBeforeImport;
        QPushButton *_browse;
        QPushButton *_start;
        QPushButton *_close;
        QProgressBar *_progress;
        QLabel *_status;
        QPlainTextEdit *_output;
        bool _refreshNeeded = false;
        QElapsedTimer _elapsed;
    };
}
