#include "robomongo/gui/widgets/workarea/PagingWidget.h"

#include <QIntValidator>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>

#include <limits>

#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/gui/GuiRegistry.h"

namespace
{
    QPushButton *createButtonWithIcon(const QIcon &icon, const QString &description)
    {
        QPushButton *button = new QPushButton;
        button->setIcon(icon);
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(28, 28);
        button->setFlat(true);
        button->setToolTip(description);
        button->setAccessibleName(description);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    }
}

namespace Robomongo
{
    PagingWidget::PagingWidget(QWidget *parent)
        : BaseClass(parent)
    {
        setObjectName("resultPaging");
        setStyleSheet(
            "QWidget#resultPaging QLineEdit { background: white; color: #243247;"
                "border: 1px solid #dce3ec; border-radius: 4px; padding: 2px 5px; min-height: 0; }"
            "QWidget#resultPaging QLineEdit:focus { border-color: #247c68; }"
            "QWidget#resultPaging QPushButton { padding: 0; min-width: 0; min-height: 0; }"
        );
        _skipEdit = new QLineEdit(this);
        _batchSizeEdit = new QLineEdit(this);
        _skipEdit->setAlignment(Qt::AlignCenter);
        _skipEdit->setToolTip(tr("Documents to skip — press Enter to apply"));
        _skipEdit->setAccessibleName(tr("Documents to skip"));
        _skipEdit->setPlaceholderText(tr("Skip"));
        _batchSizeEdit->setAlignment(Qt::AlignCenter);
        _batchSizeEdit->setToolTip(tr("Documents per page — press Enter to apply"));
        _batchSizeEdit->setAccessibleName(tr("Documents per page"));
        _batchSizeEdit->setPlaceholderText(tr("Page size"));
        _skipEdit->setClearButtonEnabled(false);
        _batchSizeEdit->setClearButtonEnabled(false);

        const int width = qMax(68, _skipEdit->fontMetrics().horizontalAdvance(QStringLiteral("000000")) + 16);
        const int maximum = std::numeric_limits<int>::max();
        QLocale inputLocale = QLocale::c();
        inputLocale.setNumberOptions(QLocale::RejectGroupSeparator);
        auto *skipValidator = new QIntValidator(0, maximum, _skipEdit);
        auto *batchValidator = new QIntValidator(1, maximum, _batchSizeEdit);
        skipValidator->setLocale(inputLocale);
        batchValidator->setLocale(inputLocale);
        _skipEdit->setValidator(skipValidator);
        _batchSizeEdit->setValidator(batchValidator);
        _skipEdit->setFixedSize(width, 26);
        _batchSizeEdit->setFixedSize(width, 26);

        _leftButton = createButtonWithIcon(GuiRegistry::instance().leftIcon(), tr("Previous page"));
        _rightButton = createButtonWithIcon(GuiRegistry::instance().rightIcon(), tr("Next page"));
        VERIFY(connect(_leftButton, SIGNAL(clicked()), this, SLOT(leftButton_clicked())));
        VERIFY(connect(_rightButton, SIGNAL(clicked()), this, SLOT(rightButton_clicked())));
        VERIFY(connect(_batchSizeEdit, SIGNAL(returnPressed()), this, SLOT(refresh())));
        VERIFY(connect(_skipEdit, SIGNAL(returnPressed()), this, SLOT(refresh())));
        connect(_skipEdit, &QLineEdit::textChanged, this, [this]() { updateNavigation(); });
        connect(_batchSizeEdit, &QLineEdit::textChanged, this, [this]() { updateNavigation(); });

        auto *layout = new QHBoxLayout(this);
        layout->setSpacing(4);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(_leftButton);
        layout->addWidget(_skipEdit);
        layout->addWidget(_batchSizeEdit);
        layout->addWidget(_rightButton);
        updateNavigation();
    }

    void PagingWidget::setSkip(int skip)
    {
        const QString text = QString::number(qMax(0, skip));
        if (_skipEdit->text() != text)
            _skipEdit->setText(text);
        show();
    }

    void PagingWidget::setBatchSize(int batchSize)
    {
        if (batchSize <= 0)
            batchSize = AppRegistry::instance().settingsManager()->batchSize();

        const QString text = QString::number(qMax(1, batchSize));
        if (_batchSizeEdit->text() != text)
            _batchSizeEdit->setText(text);
        show();
    }

    bool PagingWidget::pageValues(int &skip, int &limit) const
    {
        bool skipValid = false;
        bool limitValid = false;
        skip = _skipEdit->text().toInt(&skipValid);
        limit = _batchSizeEdit->text().toInt(&limitValid);
        return skipValid && limitValid && skip >= 0 && limit > 0;
    }

    void PagingWidget::updateNavigation()
    {
        int skip = 0;
        int limit = 0;
        const bool valid = pageValues(skip, limit);
        _leftButton->setEnabled(valid && skip > 0);
        _rightButton->setEnabled(valid && skip <= std::numeric_limits<int>::max() - limit);
    }

    void PagingWidget::refresh()
    {
        int skip = 0;
        int limit = 0;
        if (pageValues(skip, limit))
            emit refreshed(skip, limit);
    }

    void PagingWidget::leftButton_clicked()
    {
        int skip = 0;
        int limit = 0;
        if (pageValues(skip, limit) && skip > 0)
            emit leftClicked(skip, limit);
    }

    void PagingWidget::rightButton_clicked()
    {
        int skip = 0;
        int limit = 0;
        if (pageValues(skip, limit) && skip <= std::numeric_limits<int>::max() - limit)
            emit rightClicked(skip, limit);
    }
}
