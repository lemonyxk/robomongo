#pragma once

#include <QDialog>
#include <QRect>
#include <QPoint>
#include "robomongo/core/bson/Bson.h"

class QCloseEvent;
class QDialogButtonBox;
class QLabel;
class QScrollArea;
class QShowEvent;
class QMouseEvent;

namespace Robomongo
{
    class RoboScintilla;

    class FieldValueEditor : public QDialog
    {
        Q_OBJECT

    public:
        FieldValueEditor(const QString &fieldPath, const mongo::BSONElement &original,
                         QWidget *parent = nullptr);
        void showAt(const QRect &anchor);

    public Q_SLOTS:
        void accept() override;
        void reject() override;
        void finishSave(const QString &requestId, const QString &errorMessage);
        void connectionClosed();

    Q_SIGNALS:
        void saveRequested(const QString &requestId, const mongo::BSONObj &value);
        void saved();

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override;
        void showEvent(QShowEvent *event) override;
        void closeEvent(QCloseEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;

    private:
        mongo::BSONObj parseValue() const;
        void setSaving(bool saving);
        void showError(const QString &message);
        bool ownsFocusObject(const QObject *object) const;
        void checkFocusLater();
        void updatePopupGeometry();

        mongo::BSONObj _originalOwner;
        mongo::BSONElement _original;
        RoboScintilla *_input;
        QLabel *_error;
        QLabel *_hint;
        QDialogButtonBox *_buttons;
        QWidget *_body;
        QScrollArea *_scrollArea;
        QRect _anchor;
        int _preferredWidth;
        QString _initialText;
        QString _requestId;
        bool _saving = false;
        bool _focusCheckQueued = false;
        QPoint _dragPosition;
        bool _dragging = false;
    };
}
