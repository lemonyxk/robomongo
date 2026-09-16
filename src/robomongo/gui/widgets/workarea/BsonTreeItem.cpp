#include "robomongo/gui/widgets/workarea/BsonTreeItem.h"
#include <QtAlgorithms>
#include "robomongo/core/utils/BsonUtils.h"
#include "robomongo/core/utils/QtUtils.h"

namespace
{
    const Robomongo::BsonTreeItem *findSuperRoot(const Robomongo::BsonTreeItem *const item)
    {
        Robomongo::BsonTreeItem *parent = qobject_cast<Robomongo::BsonTreeItem *>(item->parent());
        if (parent) {
            Robomongo::BsonTreeItem *grParent = qobject_cast<Robomongo::BsonTreeItem *>(parent->parent());
            if (grParent) {
               return findSuperRoot(parent);
            }
        }
        return item;
    }
}
namespace Robomongo
{
    BsonTreeItem::BsonTreeItem(QObject *parent) 
        :BaseClass(parent)
    {
       
    }

    BsonTreeItem::BsonTreeItem(const mongo::BSONObj &bsonObjRoot, QObject *parent)
        :BaseClass(parent),
        _root(bsonObjRoot)
    {

    }

    unsigned BsonTreeItem::childrenCount() const
    {
        return _items.size();
    }

    void BsonTreeItem::clear()
    {
        qDeleteAll(_items);
        _items.clear();
        _itemsByKey.clear();
    }

    void BsonTreeItem::addChild(BsonTreeItem *item)
    {
        item->_row = static_cast<int>(_items.size());
        _items.push_back(item);
        // Keep the first occurrence for BSON documents with duplicate keys.
        if (!_itemsByKey.contains(item->key()))
            _itemsByKey.insert(item->key(), item);
    }

    BsonTreeItem* BsonTreeItem::child(unsigned pos) const
    {
        return _items[pos];
    }

    BsonTreeItem* BsonTreeItem::childSafe(unsigned pos) const
    {
        if (childrenCount() > pos) {
            return _items[pos];
        }
        else {
            return NULL;
        }
    }

    BsonTreeItem* BsonTreeItem::childByKey(const QString &val)
    {
        return _itemsByKey.value(val, nullptr);
    }

    const BsonTreeItem *BsonTreeItem::superParent() const
    {
        return findSuperRoot(this);
    }

    mongo::BSONObj BsonTreeItem::superRoot() const
    {
        return superParent()->root();
    }

    mongo::BSONObj BsonTreeItem::root() const
    {
        return _root;
    }

    int BsonTreeItem::indexOf(BsonTreeItem *item) const
    {
        return item && item->_row >= 0 && childSafe(item->_row) == item ? item->_row : -1;
    }

    mongo::BSONObj BsonTreeItem::childrenDocument() const
    {
        return _element.isABSONObj() ? _element.Obj() : _root;
    }

    void BsonTreeItem::setElement(const mongo::BSONElement &element, UUIDEncoding uuidEncoding, SupportedTimes timeZone)
    {
        _element = element;
        _uuidEncoding = uuidEncoding;
        _timeZone = timeZone;
        _valueReady = false;
        _displayValueReady = false;
    }

    QString BsonTreeItem::key() const
    {
        return _fields._key;
    }

    QString BsonTreeItem::value() const
    {
        if (!_valueReady) {
            std::string result;
            BsonUtils::buildJsonString(_element, result, _uuidEncoding, _timeZone);
            _fields._value = QtUtils::toQString(result);
            _valueReady = true;
        }
        return _fields._value;
    }

    QString BsonTreeItem::displayValue(bool simplify) const
    {
        const QString text = value();
        if (!simplify)
            return text.left(300);
        if (!_displayValueReady) {
            // Stop at the preview boundary instead of simplifying megabytes of
            // text for every paint or size hint. Full values remain available
            // to the copy/edit actions through value().
            _displayValue.clear();
            _displayValue.reserve(qMin<qsizetype>(text.size(), 300));
            bool pendingSpace = false;
            for (const QChar character : text) {
                if (character.isSpace()) {
                    pendingSpace = !_displayValue.isEmpty();
                    continue;
                }
                if (pendingSpace) {
                    _displayValue.append(QLatin1Char(' '));
                    pendingSpace = false;
                }
                if (_displayValue.size() == 300)
                    break;
                _displayValue.append(character);
                if (_displayValue.size() == 300)
                    break;
            }
            _displayValueReady = true;
        }
        return _displayValue;
    }

    QString BsonTreeItem::toolTipValue() const
    {
        return value().left(500);
    }

    mongo::BSONType BsonTreeItem::type() const
    {
        return _fields._type;
    }

    void BsonTreeItem::setKey(const QString &key)
    {
        _fields._key = key;
    }

    void BsonTreeItem::setValue(const QString &value)
    {
        _fields._value = value;
        _valueReady = true;
        _displayValueReady = false;
    }

    void BsonTreeItem::setType(mongo::BSONType type)
    {
       _fields._type = type;
    }

    mongo::BinDataType BsonTreeItem::binType() const
    {
        return _fields._binType;
    }

    void BsonTreeItem::setBinType(mongo::BinDataType type)
    {
        _fields._binType = type;
    }

    void BsonTreeItem::removeChild(BsonTreeItem *item)
    {
        const int row = indexOf(item);
        if (row < 0)
            return;
        const QString key = item->key();
        _items.erase(_items.begin() + row);
        _itemsByKey.remove(key);
        for (int i = 0; i < static_cast<int>(_items.size()); ++i) {
            _items[i]->_row = i;
            if (_items[i]->key() == key && !_itemsByKey.contains(key))
                _itemsByKey.insert(key, _items[i]);
        }
        delete item;
    }
}
