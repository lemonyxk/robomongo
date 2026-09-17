#include "robomongo/gui/widgets/workarea/WelcomeTab.h"

#include <QFrame>
#include <QGridLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "robomongo/gui/MainWindow.h"

namespace Robomongo
{
    WelcomeTab::WelcomeTab(QScrollArea *parent) :
        QWidget(parent), _parent(parent)
    {
        setObjectName("welcomePage");
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(
            "QWidget#welcomePage { background: #efefef; }"
            "QFrame#welcomeCard { background: #ffffff; border: 1px solid #d6d6d6; border-radius: 12px; }"
            "QLabel { background: transparent; color: #6d6d6d; }"
            "QLabel#welcomeBrand { color: #666666; font-weight: bold; }"
            "QLabel#welcomeTitle { color: #444444; }"
            "QLabel#welcomeStep { color: #666666; font-weight: bold; }"
            "QLabel#welcomeStepTitle { color: #444444; font-weight: bold; }"
            "QLabel#welcomeShortcut { color: #444444; background: #efefef; border-radius: 4px; padding: 5px 8px; }"
            "QPushButton { min-height: 28px; padding: 5px 14px; border-radius: 6px; }"
            "QPushButton#welcomeConnect { color: #333333; background: #ededed; border: 1px solid #aaaaaa; }"
            "QPushButton#welcomeConnect:hover { background: #e3e3e3; border-color: #999999; }"
            "QPushButton#welcomeConnect:pressed { background: #d9d9d9; }"
            "QPushButton#welcomeConnect:focus { border-color: #666666; }"
        );

        auto *card = new QFrame(this);
        card->setObjectName("welcomeCard");
        card->setMaximumWidth(800);
        auto *content = new QVBoxLayout(card);
        content->setContentsMargins(32, 30, 32, 30);
        content->setSpacing(14);

        auto *brand = new QLabel(tr("ROBO 3T  /  WORKSPACE"), card);
        brand->setObjectName("welcomeBrand");
        content->addWidget(brand);

        auto *title = new QLabel(tr("Welcome to Robo 3T"), card);
        title->setObjectName("welcomeTitle");
        QFont titleFont = title->font();
        titleFont.setPointSize(26);
        titleFont.setBold(true);
        title->setFont(titleFont);
        title->setWordWrap(true);
        content->addWidget(title);

        auto *instructions = new QLabel(tr(
            "Connect to MongoDB, explore your collections, and turn queries into answers."), card);
        instructions->setWordWrap(true);
        content->addWidget(instructions);
        content->addSpacing(8);

        auto *actions = new QHBoxLayout;
        actions->setSpacing(10);
        auto *connectButton = new QPushButton(tr("Connect to MongoDB"), card);
        connectButton->setObjectName("welcomeConnect");
        connectButton->setCursor(Qt::PointingHandCursor);
        connectButton->setAccessibleName(tr("Manage MongoDB connections"));
        connect(connectButton, &QPushButton::clicked, this, [this]() {
            if (auto *mainWindow = qobject_cast<MainWindow *>(window()))
                mainWindow->manageConnections();
        });
        actions->addWidget(connectButton);

        actions->addStretch();
        content->addLayout(actions);
        content->addSpacing(16);

        auto *steps = new QGridLayout;
        steps->setHorizontalSpacing(16);
        steps->setVerticalSpacing(7);
        const QString titles[] = {tr("Connect"), tr("Explore"), tr("Query")};
        const QString descriptions[] = {
            tr("Choose a saved connection or create a new one."),
            tr("Browse databases and double-click a collection."),
            tr("Run a script and inspect results as text, a tree, or a table.")
        };
        for (int i = 0; i < 3; ++i) {
            auto *number = new QLabel(QStringLiteral("0%1").arg(i + 1), card);
            number->setObjectName("welcomeStep");
            auto *stepTitle = new QLabel(titles[i], card);
            stepTitle->setObjectName("welcomeStepTitle");
            auto *description = new QLabel(descriptions[i], card);
            description->setWordWrap(true);
            steps->addWidget(number, i * 3, 0, Qt::AlignTop);
            steps->addWidget(stepTitle, i * 3, 1);
            steps->addWidget(description, i * 3 + 1, 1);
            steps->setRowMinimumHeight(i * 3 + 2, 8);
        }
        steps->setColumnStretch(1, 1);
        content->addLayout(steps);

        auto *shortcuts = new QHBoxLayout;
        shortcuts->setSpacing(8);
        auto addShortcut = [card, shortcuts](const QString &label, const QKeySequence &key) {
            auto *hint = new QLabel(label + QStringLiteral("  ") + key.toString(QKeySequence::NativeText), card);
            hint->setObjectName("welcomeShortcut");
            shortcuts->addWidget(hint);
        };
        addShortcut(tr("Run"), QKeySequence(Qt::CTRL | Qt::Key_Return));
        addShortcut(tr("New shell"), QKeySequence(Qt::CTRL | Qt::Key_T));
        addShortcut(tr("Focus editor"), QKeySequence(Qt::Key_F6));
        shortcuts->addStretch();
        content->addLayout(shortcuts);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(32, 36, 32, 36);
        layout->addStretch(1);
        layout->addWidget(card, 0, Qt::AlignHCenter);
        layout->addStretch(2);
    }
}
