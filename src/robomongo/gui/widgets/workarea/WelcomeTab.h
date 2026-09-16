#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QScrollArea;
QT_END_NAMESPACE

namespace Robomongo
{
    class WelcomeTab : public QWidget
    {
        Q_OBJECT

    public:
        explicit WelcomeTab(QScrollArea *parent = nullptr);
        QScrollArea *getParent() const { return _parent; }

    private:
        QScrollArea *_parent;
    };
}
