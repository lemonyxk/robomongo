#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include <QApplication>
#include <QDialogButtonBox>
#include <QFocusEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"
#include <QPushButton>

#include "robomongo/core/bson/Bson.h"
#include "robomongo/gui/dialogs/FieldValueEditor.h"

using namespace Robomongo;

namespace {

struct FieldSaveRequest {
    QString id;
    mongo::BSONObj value;
};

class FieldValueEditorTest : public testing::Test {
protected:
    std::vector<FieldSaveRequest> requests;
    int savedCount = 0;
    std::unique_ptr<FieldValueEditor> editor;
    RoboScintilla *input = nullptr;
    QLabel *error = nullptr;
    QDialogButtonBox *buttons = nullptr;

    bool open(const mongo::BSONObj &document)
    {
        editor = std::make_unique<FieldValueEditor>(
            QStringLiteral("profile.value"), document["value"]);
        input = editor->findChild<RoboScintilla*>("fieldValueInput");
        error = editor->findChild<QLabel*>("fieldEditError");
        buttons = editor->findChild<QDialogButtonBox*>("fieldEditButtons");
        QObject::connect(editor.get(), &FieldValueEditor::saveRequested,
                         editor.get(), [this](const QString &id, const mongo::BSONObj &value) {
            requests.push_back({id, value.getOwned()});
        });
        QObject::connect(editor.get(), &FieldValueEditor::saved,
                         editor.get(), [this] { ++savedCount; });
        editor->show();
        return input && error && buttons &&
            buttons->button(QDialogButtonBox::Ok) &&
            buttons->button(QDialogButtonBox::Cancel);
    }
};

TEST_F(FieldValueEditorTest, ShowsFieldPathTypeAndOriginalString)
{
    ASSERT_TRUE(open(BSON("value" << "original value")));
    auto *path = editor->findChild<QLabel*>("fieldPath");
    auto *type = editor->findChild<QLabel*>("fieldType");
    ASSERT_NE(path, nullptr);
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(path->text(), QStringLiteral("profile.value"));
    EXPECT_FALSE(type->text().isEmpty());
    EXPECT_EQ(input->text(), QStringLiteral("original value"));
}

TEST_F(FieldValueEditorTest, CancelAndWindowCloseDiscardInputWithoutSaving)
{
    for (bool closeWindow : {false, true}) {
        SCOPED_TRACE(closeWindow);
        ASSERT_TRUE(open(BSON("value" << "original")));
        input->setText("changed but discarded");
        if (closeWindow)
            editor->close();
        else
            buttons->button(QDialogButtonBox::Cancel)->click();
        EXPECT_FALSE(editor->isVisible());
        EXPECT_EQ(editor->result(), QDialog::Rejected);
        EXPECT_TRUE(requests.empty());
        EXPECT_EQ(savedCount, 0);
    }
}

TEST_F(FieldValueEditorTest, OutsideClickDiscardsInputWithoutSaving)
{
    ASSERT_TRUE(open(BSON("value" << "original")));
    input->setText("discard this edit");
    QWidget outside;
    QMouseEvent click(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&outside, &click);
    EXPECT_FALSE(editor->isVisible());
    EXPECT_EQ(editor->result(), QDialog::Rejected);
    EXPECT_TRUE(requests.empty());
}

TEST_F(FieldValueEditorTest, ExternalFocusDiscardsInputWithoutSaving)
{
    ASSERT_TRUE(open(BSON("value" << "original")));
    input->setText("discard this edit");
    QWidget outside;
    QFocusEvent focus(QEvent::FocusIn, Qt::OtherFocusReason);
    QApplication::sendEvent(&outside, &focus);
    EXPECT_FALSE(editor->isVisible());
    EXPECT_TRUE(requests.empty());
}

TEST_F(FieldValueEditorTest, InternalButtonAndContextMenuFocusKeepEditorOpen)
{
    ASSERT_TRUE(open(BSON("value" << "original")));
    input->setText("still editing");
    QFocusEvent buttonFocus(QEvent::FocusIn, Qt::TabFocusReason);
    QApplication::sendEvent(buttons->button(QDialogButtonBox::Cancel), &buttonFocus);
    EXPECT_TRUE(editor->isVisible());
    QMenu menu(input);
    QFocusEvent menuFocus(QEvent::FocusIn, Qt::PopupFocusReason);
    QApplication::sendEvent(&menu, &menuFocus);
    EXPECT_TRUE(editor->isVisible());
    EXPECT_EQ(input->text(), QStringLiteral("still editing"));
    EXPECT_TRUE(requests.empty());
}

TEST_F(FieldValueEditorTest, ApplicationDeactivationDiscardsInputWithoutSaving)
{
    ASSERT_TRUE(open(BSON("value" << "original")));
    input->setText("discard this edit");
    QEvent deactivate(QEvent::ApplicationDeactivate);
    QApplication::sendEvent(qApp, &deactivate);
    EXPECT_FALSE(editor->isVisible());
    EXPECT_TRUE(requests.empty());
}

TEST_F(FieldValueEditorTest, LosingFocusDoesNotCancelAnAlreadySentWrite)
{
    ASSERT_TRUE(open(BSON("value" << 1)));
    input->setText("2");
    editor->accept();
    ASSERT_EQ(requests.size(), 1u);
    QWidget outside;
    QFocusEvent focus(QEvent::FocusIn, Qt::OtherFocusReason);
    QApplication::sendEvent(&outside, &focus);
    EXPECT_TRUE(editor->isVisible());
    EXPECT_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    EXPECT_EQ(savedCount, 0);
    editor->finishSave(requests.front().id, QString());
    EXPECT_FALSE(editor->isVisible());
    EXPECT_EQ(editor->result(), QDialog::Accepted);
    EXPECT_EQ(savedCount, 1);
}

TEST_F(FieldValueEditorTest, OwnsOriginalBsonAfterSourceDocumentIsReleased)
{
    {
        const auto document = BSON("value" << "first\r\nsecond");
        ASSERT_TRUE(open(document));
    }
    editor->accept();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests.front().value["value"].String(), "first\r\nsecond");
}

TEST_F(FieldValueEditorTest, JsonLookingStringRemainsLiteralString)
{
    ASSERT_TRUE(open(BSON("value" << "original")));
    const QString text = QStringLiteral("{\"enabled\":false,\"count\":42}\nNumberLong(7)");
    input->setText(text);
    buttons->button(QDialogButtonBox::Ok)->click();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_FALSE(requests.front().id.isEmpty());
    EXPECT_EQ(requests.front().value.nFields(), 1);
    ASSERT_EQ(requests.front().value["value"].type(), mongo::String);
    EXPECT_EQ(requests.front().value["value"].String(), text.toStdString());
    EXPECT_TRUE(editor->isVisible());
    EXPECT_EQ(savedCount, 0);
}

TEST_F(FieldValueEditorTest, UnchangedStringKeepsOriginalLineSeparators)
{
    const std::string original = QStringLiteral("first\r\nsecond\rthird\u2029last").toStdString();
    ASSERT_TRUE(open(BSON("value" << original)));
    editor->accept();
    ASSERT_EQ(requests.size(), 1u);
    ASSERT_EQ(requests.front().value["value"].type(), mongo::String);
    EXPECT_EQ(requests.front().value["value"].String(), original);
}

TEST_F(FieldValueEditorTest, Int64BeyondJavascriptPrecisionIsExact)
{
    ASSERT_TRUE(open(BSON("value" << 9007199254740993LL)));
    EXPECT_EQ(input->text(), QStringLiteral("9007199254740993"));
    input->setText("9223372036854775807");
    editor->accept();
    ASSERT_EQ(requests.size(), 1u);
    ASSERT_EQ(requests.front().value["value"].type(), mongo::NumberLong);
    EXPECT_EQ(requests.front().value["value"].Long(), 9223372036854775807LL);
}

TEST_F(FieldValueEditorTest, InvalidInt32InputDoesNotSubmit)
{
    ASSERT_TRUE(open(BSON("value" << 1)));
    for (const QString &text : {QStringLiteral("2147483648"),
                                QStringLiteral("-2147483649"),
                                QStringLiteral("1.5"),
                                QStringLiteral("true"),
                                QStringLiteral("")}) {
        SCOPED_TRACE(text.toStdString());
        input->setText(text);
        editor->accept();
        EXPECT_TRUE(requests.empty());
        EXPECT_TRUE(editor->isVisible());
        EXPECT_EQ(input->text(), text);
        EXPECT_FALSE(error->text().isEmpty());
        EXPECT_TRUE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    }
}

TEST_F(FieldValueEditorTest, Int64OverflowDoesNotSubmit)
{
    ASSERT_TRUE(open(BSON("value" << 1LL)));
    for (const QString &text : {QStringLiteral("9223372036854775808"),
                                QStringLiteral("-9223372036854775809")}) {
        SCOPED_TRACE(text.toStdString());
        input->setText(text);
        editor->accept();
        EXPECT_TRUE(requests.empty());
        EXPECT_TRUE(editor->isVisible());
        EXPECT_EQ(input->text(), text);
        EXPECT_FALSE(error->text().isEmpty());
    }
}

TEST_F(FieldValueEditorTest, ChangingBooleanTypeDoesNotSubmit)
{
    ASSERT_TRUE(open(BSON("value" << true)));
    input->setText("\"false\"");
    editor->accept();
    EXPECT_TRUE(requests.empty());
    EXPECT_TRUE(editor->isVisible());
    EXPECT_FALSE(error->text().isEmpty());
    input->setText("false");
    editor->accept();
    ASSERT_EQ(requests.size(), 1u);
    ASSERT_EQ(requests.front().value["value"].type(), mongo::Bool);
    EXPECT_FALSE(requests.front().value["value"].Bool());
}

TEST_F(FieldValueEditorTest, FailurePreservesTextAndAllowsANewSaveRequest)
{
    ASSERT_TRUE(open(BSON("value" << "original")));
    input->setText("keep this unsaved text");
    editor->accept();
    ASSERT_EQ(requests.size(), 1u);
    const QString firstId = requests.front().id;
    editor->finishSave(firstId, "Permission denied");
    EXPECT_TRUE(editor->isVisible());
    EXPECT_EQ(savedCount, 0);
    EXPECT_EQ(input->text(), QStringLiteral("keep this unsaved text"));
    EXPECT_TRUE(error->text().contains("Permission denied"));
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Cancel)->isEnabled());

    input->setText("retry with this text");
    editor->accept();
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_NE(requests.back().id, firstId);
    EXPECT_EQ(requests.back().value["value"].String(), "retry with this text");
    // A delayed reply to the failed attempt must not complete the retry.
    editor->finishSave(firstId, QString());
    EXPECT_TRUE(editor->isVisible());
    EXPECT_EQ(savedCount, 0);
    EXPECT_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    editor->finishSave(requests.back().id, QString());
    EXPECT_FALSE(editor->isVisible());
    EXPECT_EQ(editor->result(), QDialog::Accepted);
    EXPECT_EQ(savedCount, 1);
}

TEST_F(FieldValueEditorTest, PendingSaveDisablesButtonsAndIgnoresDuplicateConfirmation)
{
    ASSERT_TRUE(open(BSON("value" << 1)));
    input->setText("2");
    editor->accept();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    EXPECT_FALSE(buttons->button(QDialogButtonBox::Cancel)->isEnabled());
    buttons->button(QDialogButtonBox::Ok)->click();
    editor->accept();
    EXPECT_EQ(requests.size(), 1u);
    EXPECT_TRUE(editor->isVisible());
    EXPECT_EQ(savedCount, 0);
}

TEST_F(FieldValueEditorTest, OnlyMatchingSuccessfulReplyAcceptsDialogOnce)
{
    ASSERT_TRUE(open(BSON("value" << 1)));
    input->setText("2");
    editor->accept();
    ASSERT_EQ(requests.size(), 1u);
    editor->finishSave("unrelated request", QString());
    EXPECT_TRUE(editor->isVisible());
    EXPECT_EQ(savedCount, 0);
    EXPECT_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    EXPECT_TRUE(error->text().isEmpty());
    editor->finishSave("unrelated request", "unrelated error");
    EXPECT_TRUE(error->text().isEmpty());

    const QString requestId = requests.front().id;
    editor->finishSave(requestId, QString());
    EXPECT_FALSE(editor->isVisible());
    EXPECT_EQ(editor->result(), QDialog::Accepted);
    EXPECT_EQ(savedCount, 1);
    editor->finishSave(requestId, QString());
    EXPECT_EQ(savedCount, 1);
}

} // namespace
