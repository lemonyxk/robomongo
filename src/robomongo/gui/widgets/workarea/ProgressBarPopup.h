#pragma once
#include <QFrame>

QT_BEGIN_NAMESPACE
class QLabel;
class QMovie;
class QShowEvent;
class QHideEvent;
QT_END_NAMESPACE

namespace Robomongo
{
    class ProgressBarPopup : public QFrame
    {
        Q_OBJECT

    public:
        ProgressBarPopup(QWidget *parent = NULL);
        enum {heightProgress = 16, widthProgress = 164, height = heightProgress+28, width = widthProgress+28};

    protected:
        void showEvent(QShowEvent *event) override;
        void hideEvent(QHideEvent *event) override;

    private:
        QLabel *_progressLabel;
        QMovie *_movie;
    };
}

