#pragma once

#include <QIcon>
#include <QColor>
#include <QWidget>

namespace Robomongo
{
    class Indicator : public QWidget
    {
        Q_OBJECT

    public:
        Indicator(const QIcon &icon, const QString &text = QString());
        QString text() const { return _text; }
        void setText(const QString &text);
        QColor textColor() const { return _textColor; }
        void setTextColor(const QColor &color);
        QSize sizeHint() const override;
        QSize minimumSizeHint() const override;

    protected:
        void paintEvent(QPaintEvent *event) override;

    private:
        QIcon _icon;
        QString _text;
        QColor _textColor = QColor("#6d6d6d");
    };
}
