#pragma once

#include <QStyle>
#include <QProxyStyle>
#include <QFont>

namespace Robomongo
{
    namespace AppStyleUtils
    {
        void initStyle();
        void applyStyle(const QString &styleName);
        void applyAppearanceSettings();
        const QFont &defaultInterfaceFont();
        QStringList getSupportedStyles();
    }

    class AppStyle : public QProxyStyle
    {
        Q_OBJECT

    public:
        static const QString StyleName;
        int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                      const QWidget *widget = nullptr, QStyleHintReturn *returnData = nullptr) const override;
        virtual void drawControl(ControlElement element, const QStyleOption * option, QPainter * painter, const QWidget * widget) const;
        virtual void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const;
        virtual QRect subElementRect( SubElement element, const QStyleOption * option, const QWidget * widget = 0 ) const;
    };
}
