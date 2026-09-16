#include "robomongo/gui/widgets/workarea/BsonTableModel.h"

#include <QIcon>

#include "robomongo/gui/widgets/workarea/BsonTreeItem.h"
#include "robomongo/gui/widgets/workarea/BsonTreeModel.h"
#include "robomongo/core/utils/QtUtils.h"

namespace Robomongo
{
    BsonTableModelProxy::BsonTableModelProxy(QObject *parent) 
        : BaseClass(parent)
    {
       
    }

    int BsonTableModelProxy::rowCount(const QModelIndex &parent) const
    {
        return !parent.isValid() && sourceModel() ? sourceModel()->rowCount() : 0;
    }

    QModelIndex BsonTableModelProxy::parent(const QModelIndex &) const
    {
        return QModelIndex();
    }

    int BsonTableModelProxy::columnCount(const QModelIndex &parent) const
    {
        return parent.isValid() ? 0 : static_cast<int>(_columns.size());
    }

    QModelIndex BsonTableModelProxy::mapFromSource(const QModelIndex &sourceIndex) const
    {
        if (!sourceIndex.isValid() || sourceIndex.model() != sourceModel())
            return QModelIndex();
        const QModelIndex document = sourceIndex.parent();
        if (!document.isValid())
            return index(sourceIndex.row(), 0, QModelIndex());
        // Only the immediate fields of a result document are table columns.
        if (document.parent().isValid())
            return QModelIndex();
        const auto *node = QtUtils::item<BsonTreeItem *>(sourceIndex);
        const auto column = _columnIndexes.constFind(node->key());
        return column == _columnIndexes.constEnd() ? QModelIndex()
            : index(document.row(), column.value(), QModelIndex());
    }

    QModelIndex BsonTableModelProxy::sibling(int row, int column, const QModelIndex &) const
    {
        return index(row, column, QModelIndex());
    }

    QModelIndex BsonTableModelProxy::index(int row, int col, const QModelIndex &parent) const
    {
        if (parent.isValid() || row < 0 || col < 0 || row >= rowCount()
            || col >= static_cast<int>(_columns.size()))
            return QModelIndex();
        const QModelIndex document = sourceModel()->index(row, 0);
        // Fixed-height table rows request only the visible documents. Discovering
        // column names does not allocate fields or serialize their BSON values.
        if (sourceModel()->canFetchMore(document))
            sourceModel()->fetchMore(document);
        BsonTreeItem *node = QtUtils::item<BsonTreeItem *>(document);
        return node ? createIndex(row, col, node->childByKey(_columns[col])) : QModelIndex();
    }

    QModelIndex BsonTableModelProxy::mapToSource(const QModelIndex &proxyIndex) const
    {
        if (!proxyIndex.isValid() || proxyIndex.model() != this || !sourceModel())
            return QModelIndex();
        const auto *child = QtUtils::item<BsonTreeItem *>(proxyIndex);
        if (!child)
            return QModelIndex();
        const QModelIndex document = sourceModel()->index(proxyIndex.row(), 0);
        return sourceModel()->index(child->row(), BsonTreeItem::eValue, document);
    }

    void BsonTableModelProxy::setSourceModel(QAbstractItemModel *model)
    {
        beginResetModel();
        _columns.clear();
        _columnIndexes.clear();
        BaseClass::setSourceModel(model);
        if (model) {
            for (int row = 0; row < model->rowCount(); ++row) {
                const auto *document = QtUtils::item<BsonTreeItem *>(model->index(row, 0));
                if (!document)
                    continue;
                const auto bson = document->root();
                for (const auto element : bson) {
                    const QString name = QString::fromUtf8(element.fieldName());
                    addColumn(bson.isArray() ? QStringLiteral("[") + name + QStringLiteral("]") : name);
                }
            }
        }
        endResetModel();
    }

    QVariant BsonTableModelProxy::data(const QModelIndex &index, int role) const
    {
        QVariant result;

        if (!index.isValid())
            return result;

        BsonTreeItem *node = QtUtils::item<BsonTreeItem *>(index);

        if (!node)
            return result;

        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            bool isCut = node->type() == mongo::String ||  node->type() == mongo::Code || node->type() == mongo::CodeWScope;  
            if (role == Qt::ToolTipRole) {
                result = node->toolTipValue();
            }
            else{
                result = node->displayValue(!isCut);
            }
        }
        else if (role == Qt::DecorationRole) {
            return BsonTreeModel::getIcon(node);
        }

        return result;
    }

    QVariant BsonTableModelProxy::headerData(int section, Qt::Orientation orientation, int role) const
    {
        if (role != Qt::DisplayRole)
            return QVariant();

        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            return column(section); 
        } else {
            return QString("%1").arg(section + 1);
        }
    }

    QString BsonTableModelProxy::column(int col) const
    {
        return col >= 0 && col < static_cast<int>(_columns.size()) ? _columns[col] : QString();
    }

    size_t BsonTableModelProxy::addColumn(const QString &col)
    {
        const auto existing = _columnIndexes.constFind(col);
        if (existing != _columnIndexes.constEnd())
            return existing.value();
        const int index = static_cast<int>(_columns.size());
        _columns.push_back(col);
        _columnIndexes.insert(col, index);
        return index;
    }
}
