#include "ProgressBarPopup.h"

#include <QHideEvent>
#include <QLabel>
#include <QMovie>
#include <QShowEvent>
#include <QVBoxLayout>

namespace Robomongo
{
    ProgressBarPopup::ProgressBarPopup(QWidget *parent) :
        QFrame(parent)
    {
        setObjectName("queryProgress");
        setAccessibleName(tr("Query in progress"));
        setStyleSheet(
            "QFrame#queryProgress { background: white; border: 1px solid #dce3ec; border-radius: 8px; }"
            "QFrame#queryProgress QLabel { background: transparent; border: none; }"
        );

        _movie = new QMovie(":robomongo/icons/progress_bar.gif", QByteArray(), this);
        _movie->setCacheMode(QMovie::CacheAll);
        _progressLabel = new QLabel(this);
        _progressLabel->setMovie(_movie);
        _progressLabel->setFixedSize(widthProgress, heightProgress);
        setFixedSize(width, height);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(0);
        layout->addWidget(_progressLabel);
        hide();
    }

    void ProgressBarPopup::showEvent(QShowEvent *event)
    {
        QFrame::showEvent(event);
        _movie->start();
    }

    void ProgressBarPopup::hideEvent(QHideEvent *event)
    {
        _movie->stop();
        QFrame::hideEvent(event);
    }
}
