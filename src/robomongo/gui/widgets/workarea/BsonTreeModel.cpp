#include "robomongo/gui/widgets/workarea/BsonTreeModel.h"
#include <QApplication>

#include "robomongo/core/mongodb/MongoConnection.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/utils/BsonUtils.h"
#include "robomongo/core/domain/MongoDocument.h"
#include "robomongo/gui/widgets/workarea/BsonTreeItem.h"
#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/gui/GuiRegistry.h"

namespace
{
    using namespace Robomongo;

    QString arrayValue(int itemsCount) {
        QString elements = itemsCount == 1 ? "element" : "elements";
        return QString("[ %1 %2 ]").arg(itemsCount).arg(elements);
    }

    QString objectValue(int itemsCount) {
        QString fields = itemsCount == 1 ? "field" : "fields";
        return QString("{ %1 %2 }").arg(itemsCount).arg(fields);
    }

    void parseDocument(BsonTreeItem *root, const mongo::BSONObj &doc, bool isArray)
    {
            const auto *settings = AppRegistry::instance().settingsManager();
            const auto uuidEncoding = settings->uuidEncoding();
            const auto timeZone = settings->timeZone();
            mongo::BSONObjIterator iterator(doc);
            while (iterator.more())
            {
                mongo::BSONElement element = iterator.next();                
                BsonTreeItem *childItemInner = new BsonTreeItem(doc, root);
                childItemInner->setElement(element, uuidEncoding, timeZone);
                std::string fieldName = std::string(element.fieldName());
                childItemInner->setFieldName(fieldName);

                QString uiFieldName = QtUtils::toQString(fieldName);
                childItemInner->setKey(uiFieldName);

                if (isArray) {
                    // When we iterate array, show field names in square brackets
                    // In this case field names are numeric, starting from 0.
                    childItemInner->setKey("[" + uiFieldName + "]");
                }

                if (BsonUtils::isArray(element)) {
                    int itemsCount = element.embeddedFieldCount();
                    childItemInner->setUnfetchedChildCount(itemsCount);
                    childItemInner->setValue(arrayValue(itemsCount));
                }
                else if (BsonUtils::isDocument(element)) {
                    int count = element.embeddedFieldCount();
                    childItemInner->setUnfetchedChildCount(count);
                    childItemInner->setValue(objectValue(count));
                }
                childItemInner->setType(element.type());
                if (element.type() == mongo::BinData) {
                    childItemInner->setBinType(element.binDataType());
                }
                root->addChild(childItemInner);
            }            
    }
}

namespace Robomongo
{
    BsonTreeModel::BsonTreeModel(const std::vector<MongoDocumentPtr> &documents, QObject *parent) :
        BaseClass(parent),
        _root(new BsonTreeItem(this))
    {
        const auto *settings = AppRegistry::instance().settingsManager();
        for (int i = 0; i < documents.size(); ++i) {
            MongoDocumentPtr doc = documents[i]; 
            BsonTreeItem *child = new BsonTreeItem(doc->bsonObj(), _root);

            QString idValue;
            const mongo::BSONElement id = doc->bsonObj().getField("_id");
            if (!id.eoo()) {
                if (id.isABSONObj()) {
                    idValue = id.type() == mongo::Array ? arrayValue(id.embeddedFieldCount())
                                                       : objectValue(id.embeddedFieldCount());
                } else {
                    std::string result;
                    BsonUtils::buildJsonString(id, result, settings->uuidEncoding(), settings->timeZone());
                    idValue = QtUtils::toQString(result).left(300);
                }
            }

            child->setKey(QString("(%1) %2").arg(i + 1).arg(idValue));

            const int count = doc->bsonObj().nFields();
            child->setUnfetchedChildCount(count);

            if (doc->bsonObj().isArray()) {
                child->setValue(arrayValue(count));
                child->setType(mongo::Array);
            } else {
                child->setValue(objectValue(count));
                child->setType(mongo::Object);
            }
            _root->addChild(child);
        }
    }

    void BsonTreeModel::fetchMore(const QModelIndex &parent)
    {
        if (!canFetchMore(parent))
            return;
        BsonTreeItem *node = QtUtils::item<BsonTreeItem*>(parent);
        const mongo::BSONObj document = node->childrenDocument();
        const int count = node->unfetchedChildCount();
        beginInsertRows(parent, 0, count - 1);
        node->setUnfetchedChildCount(0);
        parseDocument(node, document, node->type() == mongo::Array);
        endInsertRows();
    }

    bool BsonTreeModel::canFetchMore(const QModelIndex &parent) const
    {
        if (!parent.isValid() || parent.column() != 0)
            return false;
        const BsonTreeItem *node = QtUtils::item<BsonTreeItem*>(parent);
        return node && node->unfetchedChildCount() > 0;
    }

    bool BsonTreeModel::hasChildren(const QModelIndex &parent) const
    {
        if (parent.isValid() && parent.column() != 0)
            return false;
        const BsonTreeItem *node = parent.isValid() ? QtUtils::item<BsonTreeItem*>(parent) : _root;
        return node && (node->childrenCount() > 0 || node->unfetchedChildCount() > 0);
    }

    const QIcon &BsonTreeModel::getIcon(BsonTreeItem *item)
    {
        switch(item->type()) {
        case mongo::NumberDouble: return GuiRegistry::instance().bsonDoubleIcon();
        case mongo::NumberDecimal: return GuiRegistry::instance().bsonNumberDecimalIcon();
        case mongo::String: return GuiRegistry::instance().bsonStringIcon();
        case mongo::Object: return GuiRegistry::instance().bsonObjectIcon();
        case mongo::Array: return GuiRegistry::instance().bsonArrayIcon();
        case mongo::BinData: return GuiRegistry::instance().bsonBinaryIcon();
        case mongo::Undefined: return GuiRegistry::instance().circleIcon();
        case mongo::jstOID: return GuiRegistry::instance().circleIcon();
        case mongo::Bool: return GuiRegistry::instance().bsonBooleanIcon();
        case mongo::Date: return GuiRegistry::instance().bsonDateTimeIcon();
        case mongo::jstNULL: return GuiRegistry::instance().bsonNullIcon();
        case mongo::RegEx: return GuiRegistry::instance().circleIcon();
        case mongo::DBRef: return GuiRegistry::instance().circleIcon();
        case mongo::Code: case mongo::CodeWScope: return GuiRegistry::instance().circleIcon();
        case mongo::NumberInt: return GuiRegistry::instance().bsonIntegerIcon();
        case mongo::bsonTimestamp: return GuiRegistry::instance().bsonDateTimeIcon();
        case mongo::NumberLong: return GuiRegistry::instance().bsonIntegerIcon();
        default: return GuiRegistry::instance().circleIcon();
        }
    }

    QVariant BsonTreeModel::data(const QModelIndex &index, int role) const
    {
        QVariant result;

        if (!index.isValid())
            return result;

        BsonTreeItem *node = QtUtils::item<BsonTreeItem*>(index);

        if (!node)
            return result;  

        int col = index.column();        

        if (role == Qt::DecorationRole && col == BsonTreeItem::eKey ) {
            return getIcon(node);
        }

        if (role == Qt::ForegroundRole && col == BsonTreeItem::eType) {
            return QApplication::palette().color(QPalette::PlaceholderText);
        }

        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            if (col == BsonTreeItem::eKey) {
                if (role == Qt::DisplayRole) {
                    result = node->key();
                }
            }
            else if (col == BsonTreeItem::eValue) {
                bool isCut = node->type() == mongo::String ||  node->type() == mongo::Code || node->type() == mongo::CodeWScope;  
                if (role == Qt::ToolTipRole) {
                    result = node->toolTipValue();
                }
                else{
                    result = node->displayValue(isCut);
                }
            }
            else if (col == BsonTreeItem::eType) {
                result = BsonUtils::BSONTypeToString(node->type(), node->binType(), AppRegistry::instance().settingsManager()->uuidEncoding());
            }
        }       

        return result;
    }

    Qt::ItemFlags BsonTreeModel::flags(const QModelIndex &index) const
    {
        Qt::ItemFlags result = Qt::NoItemFlags;
        if (index.isValid()) {
            result = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        }
        return result;
    }

    int BsonTreeModel::rowCount(const QModelIndex &parent) const
    {
        if (parent.isValid() && parent.column() != 0)
            return 0;
        const BsonTreeItem *parentItem = NULL;
        if (parent.isValid())
            parentItem = QtUtils::item<BsonTreeItem*>(parent);
        else
            parentItem = _root;

        return parentItem->childrenCount();
    }

    int BsonTreeModel::columnCount(const QModelIndex &parent) const
    {
        return BsonTreeItem::eCountColumns;
    }

    QVariant BsonTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
    {
        if (role != Qt::DisplayRole)
            return QVariant();

        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            if (section == BsonTreeItem::eKey) {
                return "Key";
            }
            else if (section == BsonTreeItem::eValue) {
                return "Value";
            }
            else {
                return "Type";
            }
        }

        return BaseClass::headerData(section, orientation, role);
    }

    QModelIndex BsonTreeModel::parent(const QModelIndex& index) const
    {
        QModelIndex result;
        if (index.isValid()) {
            BsonTreeItem *const childItem = QtUtils::item<BsonTreeItem*const>(index);
            BsonTreeItem *const parentItem = static_cast<BsonTreeItem*const>(childItem->parent());
            if (parentItem && parentItem != _root) {
                result = createIndex(parentItem->row(), 0, parentItem);
            }
        }
        return result;
    }

    QModelIndex BsonTreeModel::index(int row, int column, const QModelIndex &parent) const
    {
        QModelIndex index;
        if (hasIndex(row, column, parent)) {
            const BsonTreeItem * parentItem = NULL;
            if (!parent.isValid()) {
                parentItem = _root;
            } else {
                parentItem = QtUtils::item<BsonTreeItem*>(parent);
            }

            BsonTreeItem *childItem = parentItem->child(row);
            if (childItem) {
                index = createIndex(row, column, childItem);
            }
        }
        return index;
    }

    void BsonTreeModel::insertItem(BsonTreeItem *parent, BsonTreeItem *children)
    {
        QModelIndex index = parent == _root ? QModelIndex() : createIndex(parent->row(), 0, parent);
        unsigned child_count = parent->childrenCount();
        beginInsertRows(index, child_count, child_count);
        parent->addChild(children);
        endInsertRows();
    }

    void BsonTreeModel::removeitem(BsonTreeItem *children)
    {
        BsonTreeItem *parent = static_cast<BsonTreeItem *>(children->parent());
        if (parent) {
            QModelIndex index = parent == _root ? QModelIndex() : createIndex(parent->row(), 0, parent);
            int row = parent->indexOf(children);
            if (row < 0)
                return;
            beginRemoveRows(index, row, row);
            parent->removeChild(children);
            endRemoveRows();
        }
    }
}
