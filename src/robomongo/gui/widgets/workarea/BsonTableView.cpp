#include "robomongo/gui/widgets/workarea/BsonTableView.h"

#include <QHeaderView>
#include <QFontMetrics>
#include <QAction>
#include <QMenu>
#include <QKeyEvent>

#include "robomongo/gui/widgets/workarea/BsonTreeItem.h"
#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/core/utils/QtUtils.h"

namespace Robomongo
{
    BsonTableView::BsonTableView(MongoShell *shell, const MongoQueryInfo &queryInfo, QWidget *parent) 
        :BaseClass(parent), _notifier(this, shell, queryInfo)
    {
#if defined(Q_OS_MAC)
        setAttribute(Qt::WA_MacShowFocusRect, false);
#endif
        GuiRegistry::instance().setAlternatingColor(this);
        setWordWrap(false);
        setTextElideMode(Qt::ElideRight);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

        verticalHeader()->setDefaultAlignment(Qt::AlignLeft);
        verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        verticalHeader()->setDefaultSectionSize(qMax(28, fontMetrics().height() + 10));
        horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
        horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        horizontalHeader()->setDefaultSectionSize(180);
        horizontalHeader()->setMinimumSectionSize(64);

        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setSelectionBehavior(QAbstractItemView::SelectItems);
        setContextMenuPolicy(Qt::CustomContextMenu);
        VERIFY(connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&))));
    }

    void BsonTableView::keyPressEvent(QKeyEvent *event)
    {
        if (event->key() == Qt::Key_Delete) {
            _notifier.onDeleteDocuments();
        }
        return BaseClass::keyPressEvent(event);
    }

    QModelIndex BsonTableView::selectedIndex() const
    {
        QModelIndexList indexes = detail::uniqueRows(selectionModel()->selectedIndexes());

        if (indexes.count() != 1)
            return QModelIndex();

        return indexes[0];
    }

    QModelIndexList BsonTableView::selectedIndexes() const
    {
        return detail::uniqueRows(selectionModel()->selectedIndexes());
    }

    void BsonTableView::showContextMenu( const QPoint &point )
    {
        QPoint menuPoint = mapToGlobal(point);
        menuPoint.setY(menuPoint.y() + horizontalHeader()->height());
        menuPoint.setX(menuPoint.x() + verticalHeader()->width());

        QModelIndexList indexes = selectedIndexes();
        if (detail::isMultiSelection(indexes)) {
            QMenu menu(this);
            _notifier.initMultiSelectionMenu(&menu);
            menu.exec(menuPoint);
        }
        else{
            QModelIndex selectedInd = selectedIndex();
            BsonTreeItem *documentItem = QtUtils::item<BsonTreeItem*>(selectedInd);
            QMenu menu(this);
            _notifier.initMenu(&menu, documentItem);
            menu.exec(menuPoint);
        }
    }

}
