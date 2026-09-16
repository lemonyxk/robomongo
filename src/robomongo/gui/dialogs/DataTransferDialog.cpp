#include "robomongo/gui/dialogs/DataTransferDialog.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <exception>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/domain/MongoDatabase.h"
#include "robomongo/core/domain/MongoServer.h"
#include "robomongo/core/mongodb/MongoConnection.h"
#include "robomongo/core/settings/ConnectionSettings.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/gui/GuiRegistry.h"

namespace Robomongo
{
    DataTransferThread::DataTransferThread(std::unique_ptr<ConnectionSettings> connection,
                                         const DataTransferOptions &options, double timeout, QObject *parent)
        : QThread(parent), _connection(std::move(connection)), _options(options),
          _timeout(timeout > 0 ? qBound(1.0, timeout, 60.0) : 60.0)
    {
    }

    DataTransferThread::~DataTransferThread()
    {
        cancel();
        wait();
    }

    void DataTransferThread::cancel()
    {
        _cancelled.store(true);
    }

    void DataTransferThread::run()
    {
        try {
            if (_cancelled.load()) {
                _result.cancelled = true;
                return;
            }
            // The active server record already contains its SSH forwarding endpoint.
            mongo::DBClientBase client(makeConnectionUri(*_connection, _timeout), makeTlsOptions(*_connection));
            _result = runDataTransfer(client, _options, _cancelled,
                [this](const DataTransferProgress &update) {
                    emit progress(update.collection, update.documents, update.collectionsDone,
                                  update.collectionsTotal, update.message);
                });
        } catch (const std::exception &error) {
            _result.error = QString::fromUtf8(error.what());
            _result.cancelled = _cancelled.load();
        } catch (...) {
            _result.error = tr("An unexpected error stopped the transfer.");
            _result.cancelled = _cancelled.load();
        }
    }

    DataTransferDialog::DataTransferDialog(MongoDatabase *database, const QString &collection,
                                           DataTransferDirection direction, QWidget *parent)
        : QDialog(parent), _database(database), _server(database->server())
    {
        _options.direction = direction;
        _options.database = QString::fromStdString(database->name());
        _options.collection = collection;
        const bool importing = direction == DataTransferDirection::Import;
        const bool databaseScope = collection.isEmpty();
        setWindowTitle(importing ? (databaseScope ? tr("Import Database") : tr("Import Collection"))
                                 : (databaseScope ? tr("Export Database") : tr("Export Collection")));
        setWindowIcon(GuiRegistry::instance().mainWindowIcon());
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
        auto *layout = new QVBoxLayout(this);
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *contents = new QWidget(scroll);
        auto *contentLayout = new QVBoxLayout(contents);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
        scroll->setWidget(contents);
        layout->addWidget(scroll, 1);

        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto prepareWrappedLabel = [](QLabel *text) {
            text->setTextFormat(Qt::PlainText);
            text->setWordWrap(true);
            text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            policy.setHeightForWidth(true);
            text->setSizePolicy(policy);
        };
        auto addValue = [form, prepareWrappedLabel](const QString &label, const QString &value) {
            auto *text = new QLabel(value);
            prepareWrappedLabel(text);
            text->setToolTip(value);
            text->setTextInteractionFlags(Qt::TextSelectableByMouse);
            form->addRow(label, text);
        };
        addValue(tr("Connection:"), QString::fromStdString(_server->connectionRecord()->connectionName()));
        addValue(tr("Database:"), _options.database);
        addValue(tr("Collection:"), databaseScope ? tr("All non-system collections (excluding views)") : collection);
        addValue(tr("Format:"), tr("MongoDB Extended JSON (document data only)"));

        _path = new QLineEdit;
        _path->setObjectName("transferPath");
        _path->setClearButtonEnabled(true);
        _path->setMinimumWidth(0);
        _path->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        _browse = new QPushButton(tr("Browse..."));
        _browse->setAutoDefault(false);
        _browse->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        auto *pathLayout = new QHBoxLayout;
        pathLayout->addWidget(_path, 1);
        pathLayout->addWidget(_browse);
        form->addRow(databaseScope ? tr("Folder:") : tr("File:"), pathLayout);
        contentLayout->addLayout(form);

        QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (documents.isEmpty())
            documents = QDir::homePath();
        if (!importing) {
            const QString name = databaseScope
                ? "database-export-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss")
                : QString::fromLatin1(QUrl::toPercentEncoding(collection).left(100)) + ".jsonl";
            _path->setText(QDir(documents).filePath(name));
        } else {
            _path->setPlaceholderText(databaseScope ? tr("Select a folder containing collection JSON files")
                                                   : tr("Select a JSON or JSONL file"));
        }

        _importMode = new QComboBox(this);
        _importMode->addItem(tr("Insert new documents (stop on duplicate keys)"), false);
        _importMode->addItem(tr("Replace matching documents by _id (upsert)"), true);
        _importMode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        _importMode->setMinimumContentsLength(18);
        _importMode->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        _importMode->setToolTip(_importMode->currentText());
        connect(_importMode, &QComboBox::currentTextChanged, _importMode, &QWidget::setToolTip);
        _importMode->setVisible(importing);
        if (importing)
            form->addRow(tr("Import mode:"), _importMode);

        _validateBeforeImport = new QCheckBox(tr("Full input check"), this);
        _validateBeforeImport->setChecked(false);
        _validateBeforeImport->setVisible(importing);
        _validateBeforeImport->setToolTip(tr("Check all files before writing (slower). "
                                            "Otherwise documents are checked and written in batches."));
        if (importing)
            form->addRow(_validateBeforeImport);

        QString help = databaseScope
            ? (importing ? tr("Select an exported database folder, or a folder with one .json/.jsonl/.ndjson file per collection. "
                              "Without a manifest, file names become collection names.")
                         : tr("Choose a new or empty folder. Each collection is saved as a JSONL file, with a manifest of collection names."))
            : (importing ? tr("Accepts JSON Lines, a JSON array of documents, or one JSON document.")
                         : tr("Exports every document in this collection, one Extended JSON document per line."));
        help += "\n" + tr("Indexes, collection options, views and users are not included.");
        if (importing)
            help += "\n" + tr("By default, documents are checked and imported in batches as the file is read. "
                              "Failed or cancelled imports keep documents already written. "
                              "Upsert replaces the whole document with the same _id.");
        auto *helpLabel = new QLabel(help);
        prepareWrappedLabel(helpLabel);
        contentLayout->addWidget(helpLabel);

        _progress = new QProgressBar;
        _progress->setRange(0, 1);
        _progress->setValue(0);
        _progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        contentLayout->addWidget(_progress);
        _status = new QLabel(tr("Ready"));
        prepareWrappedLabel(_status);
        contentLayout->addWidget(_status);
        _output = new QPlainTextEdit;
        _output->setReadOnly(true);
        _output->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        _output->setMinimumHeight(_output->fontMetrics().lineSpacing() * 5);
        contentLayout->addWidget(_output, 1);

        auto *buttons = new QDialogButtonBox;
        _start = buttons->addButton(importing ? tr("Import") : tr("Export"), QDialogButtonBox::AcceptRole);
        _start->setDefault(true);
        _close = buttons->addButton(QDialogButtonBox::Close);
        buttons->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->addWidget(buttons);
        // Follow the configured interface font without letting the initial
        // window exceed its screen. The body scrolls instead of squeezing rows.
        ensurePolished();
        const QFontMetrics metrics(font());
        QSize initialSize(qMax(680, metrics.averageCharWidth() * 76),
                          qMax(520, metrics.lineSpacing() * 32));
        if (QScreen *currentScreen = screen()) {
            const QSize available = currentScreen->availableGeometry().size();
            initialSize = initialSize.boundedTo(available * 0.9);
        }
        const QMargins margins = layout->contentsMargins();
        const int bodyMinimumWidth = contentLayout->minimumSize().width()
            + scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
        setMinimumWidth(qMin(initialSize.width(), qMax(bodyMinimumWidth, buttons->minimumSizeHint().width())
            + margins.left() + margins.right()));
        resize(initialSize);
        connect(buttons, &QDialogButtonBox::accepted, this, &DataTransferDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &DataTransferDialog::reject);
        connect(_browse, &QPushButton::clicked, this, &DataTransferDialog::browse);
        connect(_server.data(), &QObject::destroyed, this, [this] {
            _start->setEnabled(false);
            if (_job) {
                _job->cancel();
                _close->setEnabled(false);
                _status->setText(tr("Connection closed. Stopping transfer..."));
            } else {
                _status->setText(tr("The connection was closed. Open this dialog again after reconnecting."));
            }
        });
    }

    DataTransferDialog::~DataTransferDialog()
    {
        if (_job) {
            _job->cancel();
            _job->wait();
        }
        // Defer until the initiating context-menu slot has returned. Refreshing
        // the collection tree may delete the item that opened this dialog.
        if (_refreshNeeded && _database && _server)
            QTimer::singleShot(0, _database.data(), &MongoDatabase::loadCollections);
    }

    void DataTransferDialog::browse()
    {
        const bool importing = _options.direction == DataTransferDirection::Import;
        QString path;
        if (_options.collection.isEmpty()) {
            const QString directory = QFileDialog::getExistingDirectory(this,
                importing ? tr("Select Database Import Folder") : tr("Select Export Parent Folder"),
                QFileInfo(_path->text()).absolutePath());
            if (!directory.isEmpty())
                path = importing ? directory : QDir(directory).filePath(
                    "database-export-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss"));
        } else if (importing) {
            path = QFileDialog::getOpenFileName(this, tr("Import Collection"), _path->text(),
                tr("JSON documents (*.json *.jsonl *.ndjson);;All files (*)"));
        } else {
            path = QFileDialog::getSaveFileName(this, tr("Export Collection"), _path->text(),
                tr("JSON Lines (*.jsonl);;JSON documents (*.json)"), nullptr, QFileDialog::DontConfirmOverwrite);
        }
        if (!path.isEmpty())
            _path->setText(QDir::toNativeSeparators(path));
    }

    void DataTransferDialog::setRunning(bool running)
    {
        _path->setEnabled(!running);
        _browse->setEnabled(!running);
        _importMode->setEnabled(!running);
        _validateBeforeImport->setEnabled(!running);
        _start->setEnabled(!running && _server && _database);
        _close->setEnabled(true);
        _close->setText(running ? tr("Cancel") : tr("Close"));
    }

    void DataTransferDialog::accept()
    {
        if (_job)
            return;
        if (!_server || !_database) {
            QMessageBox::warning(this, tr("Connection closed"), tr("Reconnect before starting a transfer."));
            return;
        }
        // Do not trim valid leading/trailing spaces from filesystem paths.
        if (_path->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Choose a path"), tr("Select a file or folder first."));
            return;
        }
        _options.path = QFileInfo(_path->text()).absoluteFilePath();
        _options.upsert = _importMode->currentData().toBool();
        _options.validateBeforeImport = _validateBeforeImport->isChecked();
        if (_options.direction == DataTransferDirection::Export && !_options.collection.isEmpty()
                && QFileInfo::exists(_options.path)) {
            if (QMessageBox::question(this, tr("Replace export file?"),
                    tr("Replace the existing file?\n%1").arg(_options.path),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
        }

        _output->clear();
        // A modal file/overwrite prompt can process a disconnect event.
        if (!_server || !_database) {
            QMessageBox::warning(this, tr("Connection closed"), tr("Reconnect before starting a transfer."));
            return;
        }
        _status->setText(tr("Starting..."));
        _progress->setRange(0, 0);
        setRunning(true);
        std::unique_ptr<ConnectionSettings> connection(_server->connectionRecord()->clone());
        _job = new DataTransferThread(std::move(connection), _options,
            AppRegistry::instance().settingsManager()->mongoTimeoutSec(), this);
        connect(_job, &DataTransferThread::progress, this, &DataTransferDialog::showProgress);
        connect(_job, &QThread::finished, this, &DataTransferDialog::transferFinished);
        if (_options.direction == DataTransferDirection::Import)
            _refreshNeeded = true;
        _elapsed.start();
        _job->start();
    }

    void DataTransferDialog::reject()
    {
        if (_job) {
            _job->cancel();
            _close->setEnabled(false);
            _status->setText(tr("Cancelling... Waiting for the current database operation to finish."));
            return;
        }
        QDialog::reject();
    }

    void DataTransferDialog::showProgress(const QString &collection, qint64 documents,
                                         int completedCollections, int totalCollections, const QString &message)
    {
        if (!_close->isEnabled())
            return;
        if (totalCollections > 0) {
            _progress->setRange(0, totalCollections);
            _progress->setValue(completedCollections);
        }
        const double seconds = qMax(0.001, _elapsed.elapsed() / 1000.0);
        _status->setText(tr("%1\nCollection: %2 | Documents: %3 | Collections: %4/%5"
                           "\nElapsed: %6 s | Average: %7 documents/s")
            .arg(message, collection).arg(documents).arg(completedCollections).arg(totalCollections)
            .arg(seconds, 0, 'f', 1).arg(documents / seconds, 0, 'f', 0));
    }

    void DataTransferDialog::transferFinished()
    {
        DataTransferThread *job = _job;
        const DataTransferResult result = job->result();
        _job = nullptr;
        job->deleteLater();
        setRunning(false);
        _progress->setRange(0, 1);
        _progress->setValue(result.success ? 1 : 0);
        const bool importing = _options.direction == DataTransferDirection::Import;
        const double seconds = qMax(0.001, _elapsed.elapsed() / 1000.0);
        const QString timing = tr("Elapsed: %1 s | Average: %2 documents/s")
            .arg(seconds, 0, 'f', 1).arg(result.documents / seconds, 0, 'f', 0);
        if (result.success) {
            _status->setText(importing ? tr("Import completed") : tr("Export completed"));
            _output->setPlainText(tr("%1 documents across %2 collections.\n%3\n%4")
                .arg(result.documents).arg(result.collections).arg(_options.path, timing));
            _start->setEnabled(false);
            _path->setEnabled(false);
            _browse->setEnabled(false);
            _importMode->setEnabled(false);
            _validateBeforeImport->setEnabled(false);
            _close->setFocus();
        } else {
            _status->setText(result.cancelled ? tr("Transfer cancelled") : tr("Transfer failed"));
            QString detail = result.error;
            if (importing)
                detail += "\n\n" + tr("%1 document writes were acknowledged. Documents already written remain in the database.")
                    .arg(result.documents);
            _output->setPlainText(detail + "\n\n" + timing);
        }
    }
}
