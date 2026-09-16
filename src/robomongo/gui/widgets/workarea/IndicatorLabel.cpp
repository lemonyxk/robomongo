#include "robomongo/gui/widgets/workarea/IndicatorLabel.h"

#include <QPainter>

namespace Robomongo
{
    Indicator::Indicator(const QIcon &icon, const QString &text) :
        _icon(icon)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setText(text);
    }

    void Indicator::setText(const QString &text)
    {
        if (_text == text)
            return;

        _text = text;
        // Names are plain text, even when they contain HTML-looking characters.
        setToolTip(QStringLiteral("<qt>%1</qt>").arg(text.toHtmlEscaped()));
        setAccessibleName(text);
        updateGeometry();
        update();
    }

    void Indicator::setTextColor(const QColor &color)
    {
        if (_textColor == color)
            return;
        _textColor = color;
        update();
    }

    QSize Indicator::sizeHint() const
    {
        const int iconWidth = _icon.isNull() ? 0 : 24;
        return QSize(qMin(480, fontMetrics().horizontalAdvance(_text) + iconWidth + 2),
                     qMax(24, fontMetrics().height() + 4));
    }

    QSize Indicator::minimumSizeHint() const
    {
        return QSize(_icon.isNull() ? 24 : 44, sizeHint().height());
    }

    void Indicator::paintEvent(QPaintEvent *)
    {
        QPainter painter(this);
        const int iconTop = (height() - 16) / 2;
        const int textLeft = _icon.isNull() ? 0 : 24;
        if (!_icon.isNull())
            _icon.paint(&painter, 0, iconTop, 16, 16);
        painter.setPen(_textColor);
        const QRect textRect(textLeft, 0, qMax(0, width() - textLeft - 2), height());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(_text, Qt::ElideRight, textRect.width()));
    }
}
