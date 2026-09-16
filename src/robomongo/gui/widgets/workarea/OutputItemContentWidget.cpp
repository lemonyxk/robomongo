#include "robomongo/gui/widgets/workarea/OutputItemContentWidget.h"

#include <QVBoxLayout>
#include <QTimer>
#include <Qsci/qscilexerjavascript.h>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/core/domain/MongoShell.h"
#include "robomongo/core/domain/MongoAggregateInfo.h"

#include "robomongo/gui/widgets/workarea/OutputWidget.h"
#include "robomongo/gui/widgets/workarea/OutputItemHeaderWidget.h"
#include "robomongo/gui/widgets/workarea/JsonPrepareThread.h"
#include "robomongo/gui/widgets/workarea/BsonTreeView.h"
#include "robomongo/gui/widgets/workarea/BsonTreeModel.h"
#include "robomongo/gui/widgets/workarea/BsonTableView.h"
#include "robomongo/gui/widgets/workarea/BsonTableModel.h"
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"
#include "robomongo/gui/widgets/workarea/CollectionStatsTreeWidget.h"
#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/gui/editors/JSLexer.h"
#include "robomongo/gui/editors/FindFrame.h"

namespace Robomongo
{
    OutputItemContentWidget::OutputItemContentWidget(ViewMode viewMode, MongoShell *shell, 
                                                     const QString &text, double secs, bool multipleResults, 
                                                     bool tabbedResults, bool firstItem, bool lastItem, 
                                                     AggrInfo aggrInfo, QWidget *parent) :
        BaseClass(parent),
        _textView(NULL),
        _bsonTreeview(NULL),
        _thread(NULL),
        _bsonTable(NULL),
        _collectionStats(nullptr),
        _isTextModeSupported(true),
        _isTreeModeSupported(false),
        _isTableModeSupported(false),
        _isCustomModeSupported(false),
        _isTextModeInitialized(false),
        _isTreeModeInitialized(false),
        _isCustomModeInitialized(false),
        _isTableModeInitialized(false),
        _isFirstPartRendered(false),
        _text(text),
        _shell(shell),
        _outputWidget(dynamic_cast<OutputWidget*>(parentWidget())),
        _initialSkip(0),
        _initialLimit(0),
        _mod(NULL),
        _viewMode(viewMode),
        _aggrInfo(aggrInfo)
    {
        setup(secs, multipleResults, tabbedResults, firstItem, lastItem);
    }

    OutputItemContentWidget::OutputItemContentWidget(ViewMode viewMode, MongoShell *shell, 
                                                     const QString &type, 
                                                     const std::vector<MongoDocumentPtr> &documents, 
                                                     const MongoQueryInfo &queryInfo, double secs, 
                                                     bool multipleResults, bool tabbedResults,
                                                     bool firstItem, bool lastItem, AggrInfo aggrInfo,
                                                     QWidget *parent) :
        BaseClass(parent),
        _textView(NULL),
        _bsonTreeview(NULL),
        _thread(NULL),
        _bsonTable(NULL),
        _collectionStats(nullptr),
        _isTextModeSupported(true),
        _isTreeModeSupported(true),
        _isTableModeSupported(true),
        _isCustomModeSupported(!type.isEmpty()),
        _isTextModeInitialized(false),
        _isTreeModeInitialized(false),
        _isCustomModeInitialized(false),
        _isTableModeInitialized(false),
        _isFirstPartRendered(false),
        _documents(documents),
        _queryInfo(queryInfo),
        _type(type),
        _shell(shell),
        _initialSkip(queryInfo._skip),
        _initialLimit(queryInfo._limit),
        _outputWidget(dynamic_cast<OutputWidget*>(parentWidget())),
        _mod(NULL),
        _viewMode(viewMode),
        _aggrInfo(aggrInfo)
    {
        setup(secs, multipleResults, tabbedResults, firstItem, lastItem);
    }

    void OutputItemContentWidget::setup(double secs, bool multipleResults, bool tabbedResults,
                                        bool firstItem, bool lastItem)
    {      
        setContentsMargins(0, 0, 0, 0);
        _header = new OutputItemHeaderWidget(this, multipleResults, tabbedResults, firstItem, lastItem);

        if (_queryInfo._info.isValid()) {
            _header->setCollection(QtUtils::toQString(_queryInfo._info._ns.collectionName()));
            _header->paging()->setBatchSize(_queryInfo._batchSize);
            _header->paging()->setSkip(_queryInfo._skip);
        }
        else if (_aggrInfo.isValid) {
            _initialLimit = 0;
            _initialSkip = 0;
            _header->setCollection(QtUtils::toQString(_aggrInfo.collectionName));
            _header->paging()->setBatchSize(_aggrInfo.batchSize);
            _header->paging()->setSkip(_aggrInfo.skip);
        }

        _header->setTime(QString("%1 sec.").arg(secs, 0, 'g', 3));

        QVBoxLayout *layout = new QVBoxLayout();
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(_header);
        _stack = new QStackedWidget;
        layout->addWidget(_stack);
        setLayout(layout);
        VERIFY(connect(_header->paging(), SIGNAL(refreshed(int, int)), this, SLOT(refresh(int, int))));
        VERIFY(connect(_header->paging(), SIGNAL(leftClicked(int, int)), this, SLOT(paging_leftClicked(int, int))));
        VERIFY(connect(_header->paging(), SIGNAL(rightClicked(int, int)), this, SLOT(paging_rightClicked(int, int))));
        VERIFY(connect(_header, SIGNAL(maximizedPart()), this, SIGNAL(maximizedPart())));
        VERIFY(connect(_header, SIGNAL(restoredSize()), this, SIGNAL(restoredSize())));

        refreshOutputItem();
    }

    void OutputItemContentWidget::paging_leftClicked(int skip, int limit)
    {
        int s = skip - limit;

        if (s < 0)
            s = 0;

        refresh(s, limit);
    }

    void OutputItemContentWidget::refreshOutputItem()
    {
        switch(_viewMode) {
            case Text: showText(); break;
            case Tree: showTree(); break;
            case Table: showTable(); break;
            case Custom: showCustom(); break;
            default: showTree();
        }
    }

    void OutputItemContentWidget::paging_rightClicked(int skip, int limit)
    {
        skip += limit;
        refresh(skip, limit);
    }

    void OutputItemContentWidget::refresh(int skip, int batchSize)
    {
        // Cannot set skip lower than in the text query
        if (skip <  _initialSkip) {
            _header->paging()->setSkip(_initialSkip);
            skip = _initialSkip;
        }

        int skipDelta = skip - _initialSkip;
        int limit = batchSize;

        // If limit is set to 0 it means UNLIMITED number of documents (limited only by batch size)
        // This is according to MongoDB documentation.
        if (_initialLimit != 0) {
            limit = _initialLimit - skipDelta;
            if (limit <= 0)
                limit = -1; // It means that we do not need to load documents

            if (limit > batchSize)
                limit = batchSize;
        }

        MongoQueryInfo info(_queryInfo);
        info._limit = limit;
        info._skip = skip;
        info._batchSize = batchSize;
        _outputWidget->showProgress();
                
        _shell->setScriptExecutable(true);
        if (_aggrInfo.isValid && info.runtimeCursorId.empty()) {
            // Build original pipeline object, and append extra skip and limit for paging
            std::string pipelineModified = "[";
            for (int i = 0; ; i++) {
                auto const obj = mongo::tojson(_aggrInfo.pipeline.getObjectField(std::to_string(i)));
                if (obj.empty() || "{}" == obj)
                    break;

                pipelineModified.append("EJSON.parse(" + mongo::quoteJson(obj) + "),");
            }
            pipelineModified.append("{$skip:" + std::to_string(skip) + "}, " +
                                    "{$limit:" + std::to_string(batchSize) + "}" + 
                                    "]");

            std::string const query = "db.getCollection(" + mongo::quoteJson(_aggrInfo.collectionName) + ").aggregate(" +
                                      pipelineModified + ", EJSON.parse(" +
                                      mongo::quoteJson(mongo::tojson(_aggrInfo.options)) + "))";
            
            // Create aggr. info with new skip and batchsize
            AggrInfo const aggrInfo { _aggrInfo.collectionName, skip, batchSize, _aggrInfo.pipeline, 
                                      _aggrInfo.options, _outputWidget->resultIndex(this) };
            _shell->setAggrInfo(aggrInfo);
            _shell->execute(query);
        }
        else
            _shell->query(_outputWidget->resultIndex(this), info);
    }

    void OutputItemContentWidget::updateWithInfo(const MongoQueryInfo &inf, 
                                                 const std::vector<MongoDocumentPtr> &documents)
    {
        _queryInfo = inf;
        update(documents, inf._skip, inf._batchSize);
    }

    void OutputItemContentWidget::updateWithInfo(const AggrInfo &aggrInfo, 
                                                 const std::vector<MongoDocumentPtr> &documents)
    {
        _aggrInfo = aggrInfo;
        update(documents, aggrInfo.skip, aggrInfo.batchSize);
    }

    void OutputItemContentWidget::update(const std::vector<MongoDocumentPtr> &documents, int skip, int batchSize)
    {
        stopJsonPreparation();
        _documents = documents;

        _header->paging()->setSkip(skip);
        _header->paging()->setBatchSize(batchSize);

        _text.clear();
        _isFirstPartRendered = false;
        markUninitialized();

        if (_bsonTable) {
            _stack->removeWidget(_bsonTable);
            delete _bsonTable;
            _bsonTable = NULL;
        }

        if (_bsonTreeview) {
            _stack->removeWidget(_bsonTreeview);
            delete _bsonTreeview;
            _bsonTreeview = NULL;
        }

        if (_textView) {
            _stack->removeWidget(_textView);
            delete _textView;
            _textView = NULL;
        }
        if (_collectionStats) {
            _stack->removeWidget(_collectionStats);
            delete _collectionStats;
            _collectionStats = nullptr;
        }
        delete _mod;
        _mod = nullptr;
    }

    void OutputItemContentWidget::showText()
    {
        _viewMode = Text;
        _header->showText();
        if (!_isTextModeSupported)
            return;

        if (!_isTextModeInitialized)
        {
            _textView = configureLogText();
            if (!_text.isEmpty()) {
                _textView->sciScintilla()->setText(_text);
            }
            else {
                if (_documents.size() > 0) {
                    _textView->sciScintilla()->setText("Loading...");
                    _thread = new JsonPrepareThread(_documents, AppRegistry::instance().settingsManager()->uuidEncoding(), AppRegistry::instance().settingsManager()->timeZone());
                    _thread->setParent(this);
                    const auto generation = _jsonGeneration;
                    VERIFY(connect(_thread, &JsonPrepareThread::partReady, this,
                        [this, generation](const QString &json) { jsonPartReady(json, generation); },
                        Qt::QueuedConnection));
                    VERIFY(connect(_thread, SIGNAL(finished()), _thread, SLOT(deleteLater())));
                    _thread->start();
                }
            }
            _stack->addWidget(_textView);
            _isTextModeInitialized = true;
        }

        _stack->setCurrentWidget(_textView);
    }

    void OutputItemContentWidget::showTree()
    {
        _viewMode = Tree;
        _header->showTree();
        if (!_isTreeModeSupported) {
            // try to downgrade to text mode
            showText();
            _viewMode = Tree;
            return;
        }

        if (!_isTreeModeInitialized) {
            configureModel();
            _bsonTreeview = new BsonTreeView(_shell, _queryInfo, this);
            _bsonTreeview->setModel(_mod);
            _stack->addWidget(_bsonTreeview);

            if (true == AppRegistry::instance().settingsManager()->autoExpand()) {
                // Expanding only one level, because on large
                // documents it can take much time
                const QModelIndex first = _mod->index(0, 0, QModelIndex());
                // QTreeView may defer its first layout and only remember the
                // expansion. Materialize this requested level synchronously.
                if (_mod->canFetchMore(first))
                    _mod->fetchMore(first);
                _bsonTreeview->expand(first);
            }

            _isTreeModeInitialized = true;
        }

        _stack->setCurrentWidget(_bsonTreeview);
    }

    void OutputItemContentWidget::showCustom()
    {
        _viewMode = Custom;
        _header->showCustom();

        if (!_isCustomModeSupported) {
            // try to downgrade to tree mode
            showTree();
            _viewMode = Custom;
            return;
        }

        if (!_isCustomModeInitialized) {

            if (_type == "collectionStats") {
                _collectionStats = new CollectionStatsTreeWidget(_documents, NULL);
                _stack->addWidget(_collectionStats);
            }               
            _isCustomModeInitialized = true;
        }

        if (_collectionStats)
            _stack->setCurrentWidget(_collectionStats);
    }

    void OutputItemContentWidget::showTable()
    {
        _viewMode = Table;
        _header->showTable();
        if (!_isTableModeSupported) {
            // try to downgrade to text mode
            showText();
            _viewMode = Table;
            return;
        }

        if (!_isTableModeInitialized) {
            configureModel();
            _bsonTable = new BsonTableView(_shell, _queryInfo);
            BsonTableModelProxy *modp = new BsonTableModelProxy(_bsonTable);
            modp->setSourceModel(_mod);
            _bsonTable->setModel(modp);
            _stack->addWidget(_bsonTable);
            _isTableModeInitialized = true;
        }

        _stack->setCurrentWidget(_bsonTable);
    }

    void OutputItemContentWidget::markUninitialized()
    {
        _isTextModeInitialized = false;
        _isTreeModeInitialized = false;
        _isCustomModeInitialized = false;
        _isTableModeInitialized = false;
    }

    void OutputItemContentWidget::applyDockUndockSettings(bool isDocking) const
    {
        _header->applyDockUndockSettings(isDocking);
    }

    void OutputItemContentWidget::toggleOrientation(Qt::Orientation orientation) const
    {
        _header->toggleOrientation(orientation);
    }

    void OutputItemContentWidget::jsonPartReady(const QString &json, quint64 generation)
    {
        // A deleted worker's address can be reused while its signals are still
        // queued. The captured generation identifies the result independently
        // of worker lifetime, including delivery after finished/deleteLater.
        if (generation != _jsonGeneration || !_textView)
            return;
        if (_isFirstPartRendered)
            _textView->sciScintilla()->append(json);
        else
            _textView->sciScintilla()->setText(json);
        _isFirstPartRendered = true;
    }

    OutputItemContentWidget::~OutputItemContentWidget()
    {
        stopJsonPreparation();
    }

    void OutputItemContentWidget::stopJsonPreparation()
    {
        // Invalidate queued callbacks even if the worker already deleted itself.
        ++_jsonGeneration;
        if (!_thread)
            return;
        JsonPrepareThread *thread = _thread.data();
        _thread = nullptr;
        disconnect(thread, nullptr, this, nullptr);
        thread->stop();
        if (thread->isRunning()) {
            // The worker owns its documents and formatting settings. Let it
            // finish the current document without blocking paging/closing on
            // the GUI thread; finished() already schedules deleteLater().
            thread->setParent(nullptr);
        } else {
            delete thread;
        }
    }

    void OutputItemContentWidget::refreshAfterWrite()
    {
        if (!_queryInfo._info.isValid() || _queryInfo.readOnly || !_queryInfo._fields.isEmpty())
            return;

        // Invalidate the cached cursor immediately. Tree and table observers can both
        // receive the same write event; dispatch one refresh for this result.
        _queryInfo.runtimeCursorId.clear();
        if (_writeRefreshQueued)
            return;
        _writeRefreshQueued = true;
        QTimer::singleShot(0, this, [this] {
            _writeRefreshQueued = false;
            const int index = _outputWidget->resultIndex(this);
            if (index < 0)
                return;
            // Reuse the pager's limit calculation: an original limit of zero is
            // unlimited, but refreshing one visible page must remain bounded.
            refresh(_queryInfo._skip, _queryInfo._batchSize);
        });
    }

    BsonTreeModel *OutputItemContentWidget::configureModel()
    {
        // Text and custom views do not need per-field tree items. Build these
        // only on demand, and share them when switching between tree and table.
        if (!_mod)
            _mod = new BsonTreeModel(_documents, this);
        return _mod;
    }

    FindFrame *Robomongo::OutputItemContentWidget::configureLogText()
    {
        const QFont &textFont = GuiRegistry::instance().font();

        QsciLexerJavaScript *javaScriptLexer = new JSLexer(this);
        javaScriptLexer->setFont(textFont);

        FindFrame *_logText = new FindFrame(this);
        _logText->sciScintilla()->setLexer(javaScriptLexer);
        _logText->sciScintilla()->setTabWidth(4);        
        _logText->sciScintilla()->setAppropriateBraceMatching();
        _logText->sciScintilla()->setFont(textFont);
        _logText->sciScintilla()->setReadOnly(true);
        _logText->sciScintilla()->setWrapMode((QsciScintilla::WrapMode) QsciScintilla::SC_WRAP_NONE);
        _logText->sciScintilla()->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        _logText->sciScintilla()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // Wrap mode turned off because it introduces huge performance problems
        // even for medium size documents.    
        _logText->sciScintilla()->setFrameShape(QFrame::NoFrame);
        return _logText;
    }
}
