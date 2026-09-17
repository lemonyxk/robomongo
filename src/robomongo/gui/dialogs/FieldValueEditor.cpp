#include "robomongo/gui/dialogs/FieldValueEditor.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSizeGrip>
#include <QShowEvent>
#include <QTextDocument>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <limits>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/utils/BsonUtils.h"
#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"
#include "robomongo/gui/editors/JSLexer.h"

namespace Robomongo
{
    FieldValueEditor::FieldValueEditor(const QString &fieldPath,
                                     const mongo::BSONElement &original, QWidget *parent)
        : QDialog(parent, Qt::Tool | Qt::FramelessWindowHint),
          _originalOwner(original.wrap().getOwned()), _original(_originalOwner.firstElement())
    {
        setObjectName("fieldValueEditor");
        setWindowTitle(tr("Edit Field"));
        setWindowModality(Qt::NonModal);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setStyleSheet(QStringLiteral(R"qss(
QDialog#fieldValueEditor { background: #efefef; border: 1px solid #c8c8c8; border-radius: 6px; }
QFrame#fieldEditorCard { background: #efefef; border: none; border-radius: 6px; }
QScrollArea#fieldEditorScroll, QWidget#fieldEditorBody { background: transparent; border: none; }
QLabel#fieldEditTitle { color: #333333; }
QLabel#fieldPath { color: #555555; }
QLabel#fieldType { background: transparent; color: #555555; padding: 3px 0; }
QLabel#fieldValueLabel, QLabel#fieldEditHint { color: #555555; }
QFrame#fieldValueInput {
    background: transparent; color: #333333; border: none;
    selection-background-color: #dedede; selection-color: #333333;
}
QFrame#fieldValueInput:focus { background: #ffffff; border-color: #999999; }
QLabel#fieldEditError { background: #f4eaea; color: #8c3f47; border-radius: 4px; padding: 7px 9px; }
QDialogButtonBox#fieldEditButtons QPushButton {
    background: #ffffff; color: #333333; border: 1px solid #d6d6d6;
    min-width: 70px; padding: 6px 12px; border-radius: 3px;
}
QDialogButtonBox#fieldEditButtons QPushButton:default { background: #ededed; border-color: #aaaaaa; }
QDialogButtonBox#fieldEditButtons QPushButton:hover { background: #f7f7f7; border-color: #999999; }
QDialogButtonBox#fieldEditButtons QPushButton:pressed { background: #dedede; border-color: #999999; }
QDialogButtonBox#fieldEditButtons QPushButton:focus { border-color: #999999; }
QDialogButtonBox#fieldEditButtons QPushButton:disabled { background: #ededea; color: #777777; border-color: #d5d5d1; }
)qss"));

        auto *card = new QFrame(this);
        card->setObjectName("fieldEditorCard");
        _body = new QWidget(card);
        _body->setObjectName("fieldEditorBody");

        auto *title = new QLabel(tr("Edit value"), _body);
        title->setObjectName("fieldEditTitle");
        QFont titleFont = font();
        titleFont.setBold(true);
        title->setFont(titleFont);

        auto *path = new QLabel(fieldPath, _body);
        path->setObjectName("fieldPath");
        path->setTextFormat(Qt::PlainText);
        path->setWordWrap(true);
        path->setMinimumWidth(0);
        auto wrapPolicy = path->sizePolicy();
        wrapPolicy.setHorizontalPolicy(QSizePolicy::Ignored);
        path->setSizePolicy(wrapPolicy);
        path->setTextInteractionFlags(Qt::TextSelectableByMouse);
        const auto *settings = AppRegistry::instance().settingsManager();
        auto *type = new QLabel(BsonUtils::BSONTypeToString(original.type(),
            original.type() == mongo::BinData ? original.binDataType() : mongo::BinDataGeneral,
            settings->uuidEncoding()), _body);
        type->setObjectName("fieldType");
        type->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        auto *heading = new QHBoxLayout;
        heading->addWidget(title);
        heading->addStretch();
        heading->addWidget(type);

        _input = new RoboScintilla(_body);
        _input->setObjectName("fieldValueInput");
        _input->setFont(GuiRegistry::instance().font());

        auto *lexer = new JSLexer(_input);
        lexer->setDefaultColor(QColor("#262626"));
        _input->setLexer(lexer);
        _input->setMarginsForegroundColor(QColor("#666666"));
        _input->setCaretForegroundColor(QColor("#262626"));
        _input->setSelectionBackgroundColor(QColor("#dcdcd7"));
        _input->setSelectionForegroundColor(QColor("#202020"));
        _input->setAppropriateBraceMatching();
        _input->setAutoIndent(true);
        _input->setIndentationWidth(4);
        _input->setIndentationsUseTabs(false);
        _input->setIndentationGuides(true);
        _input->setCaretLineVisible(true);
        _input->setFolding(QsciScintilla::BoxedTreeFoldStyle);
        _input->setWrapMode(QsciScintilla::WrapNone);
        if (original.type() == mongo::String)
            _input->setText(QString::fromStdString(original.String()));
        else if (original.type() == mongo::NumberDecimal)
            _input->setText(QString::fromStdString(original.numberDecimal().toString()));
        else
            _input->setText(QString::fromStdString(BsonUtils::jsonString(original,
                mongo::TenGen, false, 1, settings->uuidEncoding(), settings->timeZone(), false, true)));
        _initialText = _input->text();
        const bool structured = original.type() == mongo::Object || original.type() == mongo::Array;
        if (structured) {
            _input->setIndentationWidth(4);
            _input->setAutoIndent(true);
        }
        const int lines = qBound(structured ? 6 : 3, _input->lines(), 12);
        const int lineHeight = _input->fontMetrics().lineSpacing();
        _input->setMinimumHeight(qMax(120, qBound(120, lines * lineHeight + 28, 320)));
        _input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        _input->setAccessibleName(tr("Field value"));

        auto *valueLabel = new QLabel(original.type() == mongo::String
            ? tr("Value · plain text, no quotes needed") : tr("Value · keep the original type"), _body);
        valueLabel->setObjectName("fieldValueLabel");
        valueLabel->setWordWrap(true);
        valueLabel->setSizePolicy(wrapPolicy);

        _error = new QLabel(_body);
        _error->setObjectName("fieldEditError");
        _error->setTextFormat(Qt::PlainText);
        _error->setWordWrap(true);
        _error->setSizePolicy(wrapPolicy);
        _error->setTextInteractionFlags(Qt::TextSelectableByMouse);
        _error->hide();
        _buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, card);
        _buttons->setObjectName("fieldEditButtons");
        auto *confirm = _buttons->button(QDialogButtonBox::Ok);
        confirm->setObjectName("fieldConfirm");
        confirm->setText(tr("Confirm"));
        confirm->setDefault(true);
        connect(_buttons, &QDialogButtonBox::accepted, this, &FieldValueEditor::accept);
        connect(_buttons, &QDialogButtonBox::rejected, this, &FieldValueEditor::reject);

        _hint = new QLabel(tr("Click outside or press Esc to cancel."), _body);
        _hint->setObjectName("fieldEditHint");
        _hint->setWordWrap(true);
        _hint->setSizePolicy(wrapPolicy);
        const QKeySequence saveKeys(Qt::CTRL | Qt::Key_Return);
        confirm->setToolTip(tr("Save changes (%1)").arg(saveKeys.toString(QKeySequence::NativeText)));
        for (const auto &keys : {saveKeys, QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
            auto *shortcut = new QShortcut(keys, this);
            connect(shortcut, &QShortcut::activated, this, &FieldValueEditor::accept);
        }

        auto *content = new QVBoxLayout(_body);
        content->setContentsMargins(0, 0, 0, 0);
        content->setSpacing(10);
        content->addLayout(heading);
        content->addWidget(path);
        content->addSpacing(2);
        content->addWidget(valueLabel);
        content->addWidget(_input, 1);
        content->addWidget(_error);
        content->addWidget(_hint);
        content->setAlignment(Qt::AlignTop);

        _scrollArea = new QScrollArea(card);
        _scrollArea->setObjectName("fieldEditorScroll");
        _scrollArea->setFrameShape(QFrame::NoFrame);
        _scrollArea->setWidgetResizable(true);
        _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _scrollArea->setAlignment(Qt::AlignTop);
        _scrollArea->setWidget(_body);
        _scrollArea->setMinimumSize(0, 0);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 16, 16, 16);
        cardLayout->setSpacing(10);
        cardLayout->setSizeConstraint(QLayout::SetNoConstraint);
        cardLayout->addWidget(_scrollArea, 1);
        // Confirmation and cancellation stay reachable while the content scrolls.
        cardLayout->addWidget(_buttons);

    auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSizeConstraint(QLayout::SetNoConstraint);
        layout->addWidget(card);
        _preferredWidth = qBound(440, fontMetrics().height() * 28, 720);
        const int editorLines = qMax(1, _input->lines());
        const int editorLineHeight = _input->fontMetrics().lineSpacing();
        const int preferredHeight = qBound(360, editorLines * editorLineHeight + 250, 760);
        resize(_preferredWidth, preferredHeight);
        qApp->installEventFilter(this);
        _input->setFocus();
    }

    void FieldValueEditor::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton) {
            _dragging = true;
            _dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
            return;
        }

        QDialog::mousePressEvent(event);
    }

    void FieldValueEditor::mouseMoveEvent(QMouseEvent *event)
    {
        if (_dragging && (event->buttons() & Qt::LeftButton)) {
            move(event->globalPosition().toPoint() - _dragPosition);
            event->accept();
            return;
        }

        QDialog::mouseMoveEvent(event);
    }

    void FieldValueEditor::mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton)
            _dragging = false;

        QDialog::mouseReleaseEvent(event);
    }

    void FieldValueEditor::showAt(const QRect &anchor)
    {
        _anchor = anchor;
        updatePopupGeometry();
        show();
        raise();
        activateWindow();
        _input->setFocus(Qt::OtherFocusReason);
    }

    void FieldValueEditor::updatePopupGeometry()
    {
        if (!_anchor.isValid())
            return;
        QScreen *targetScreen = QGuiApplication::screenAt(_anchor.center());
        if (!targetScreen)
            targetScreen = screen();
        const QRect available = targetScreen->availableGeometry().adjusted(8, 8, -8, -8);
        const int popupWidth = qMin(_preferredWidth, available.width());
        // Account for card padding, its border and a possible vertical scrollbar
        // before asking wrapping labels for their height at this width.
        const int bodyWidth = qMax(1, popupWidth - 62 - _scrollArea->verticalScrollBar()->sizeHint().width());
        _body->layout()->invalidate();
        int bodyHeight = _body->layout()->totalHeightForWidth(bodyWidth);
        if (bodyHeight < 0)
            bodyHeight = _body->sizeHint().height();
        const int popupHeight = bodyHeight + _buttons->sizeHint().height() + 68;
        resize(popupWidth, qMin(popupHeight, available.height()));
        int y = _anchor.bottom() + 4;
        if (y + height() > available.bottom())
            y = _anchor.top() - height() - 4;
        move(qBound(available.left(), _anchor.left() - 12, qMax(available.left(), available.right() - width() + 1)),
             qBound(available.top(), y, qMax(available.top(), available.bottom() - height() + 1)));
    }

    void FieldValueEditor::showEvent(QShowEvent *event)
    {
        QDialog::showEvent(event);
        _input->setFocus(Qt::OtherFocusReason);
    }

    bool FieldValueEditor::ownsFocusObject(const QObject *object) const
    {
        // A text editor's context menu is a separate window, but remains our
        // QObject descendant. Treat it like an internal focus change.
        for (; object; object = object->parent())
            if (object == this)
                return true;
        return false;
    }

    void FieldValueEditor::checkFocusLater()
    {
        if (_focusCheckQueued)
            return;
        _focusCheckQueued = true;
        QTimer::singleShot(0, this, [this] {
            _focusCheckQueued = false;
            if (!isVisible() || _saving || ownsFocusObject(QApplication::activePopupWidget()) ||
                ownsFocusObject(QApplication::focusWidget()))
                return;
            // Focus can briefly be null while a native menu is opening or an
            // internal widget is receiving it. Wait for an actual external focus.
            if (QApplication::activeWindow() != this)
                reject();
        });
    }

    bool FieldValueEditor::eventFilter(QObject *watched, QEvent *event)
    {
        if (!isVisible() || _saving)
            return QDialog::eventFilter(watched, event);
        const bool inside = ownsFocusObject(watched);
        if (event->type() == QEvent::ApplicationDeactivate ||
            (event->type() == QEvent::ApplicationStateChange &&
             QApplication::applicationState() != Qt::ApplicationActive)) {
            reject();
        } else if ((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn) &&
                   qobject_cast<QWidget *>(watched) && !inside &&
                   !ownsFocusObject(QApplication::activePopupWidget())) {
            reject();
        } else if (inside && (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate)) {
            checkFocusLater();
        }
        return QDialog::eventFilter(watched, event);
    }

    mongo::BSONObj FieldValueEditor::parseValue() const
    {
        const QString input = _input->text();
        const QString trimmed = input.trimmed();
        mongo::BSONObjBuilder builder;
        // QTextDocument normalizes line separators. Confirming an unchanged
        // display must retain the original BSON bytes, including string newlines.
        if (input == _initialText) {
            builder.appendAs(_original, "value");
            return builder.obj();
        }
        switch (_original.type()) {
        case mongo::String:
            // Strings are plain text: whitespace, quotes and empty strings are data.
            builder.append("value", input.toStdString());
            return builder.obj();
        case mongo::NumberInt:
        case mongo::NumberLong: {
            bool ok = false;
            const auto number = trimmed.toLongLong(&ok, 10);
            if (!ok || (_original.type() == mongo::NumberInt &&
                (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())))
                throw std::runtime_error("Enter an integer within the original field type's range.");
            if (_original.type() == mongo::NumberInt)
                builder.append("value", static_cast<int>(number));
            else
                builder.append("value", static_cast<long long>(number));
            return builder.obj();
        }
        case mongo::NumberDouble: {
            bool ok = false;
            double number = trimmed.toDouble(&ok);
            if (trimmed == "NaN") {
                number = std::numeric_limits<double>::quiet_NaN();
                ok = true;
            } else if (trimmed == "Infinity" || trimmed == "-Infinity") {
                number = std::numeric_limits<double>::infinity() * (trimmed.startsWith('-') ? -1 : 1);
                ok = true;
            }
            if (!ok)
                throw std::runtime_error("Enter a valid Double value.");
            builder.append("value", number);
            return builder.obj();
        }
        case mongo::NumberDecimal:
            builder.append("value", mongo::Decimal128(trimmed.toStdString()));
            return builder.obj();
        default:
            break;
        }

        mongo::BSONObjBuilder original;
        original.appendAs(_original, "value");
        const std::string json = "{\"value\":" + input.toStdString() + "}";
        const auto parsed = mongo::Robomongo::fromjson(json.c_str(), nullptr, original.obj());
        const auto value = parsed.getField("value");
        if (parsed.nFields() != 1 || value.type() != _original.type() ||
            (value.type() == mongo::BinData && value.binDataType() != _original.binDataType()))
            throw std::runtime_error("Keep the original field type when editing this value.");
        return parsed;
    }

    void FieldValueEditor::accept()
    {
        if (_saving)
            return;
        mongo::BSONObj value;
        try {
            value = parseValue();
        } catch (const std::exception &error) {
            showError(QString::fromUtf8(error.what()));
            return;
        }
        _error->hide();
        _requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        setSaving(true);
        Q_EMIT saveRequested(_requestId, value);
    }

    void FieldValueEditor::finishSave(const QString &requestId, const QString &errorMessage)
    {
        if (!_saving || requestId != _requestId)
            return;
        setSaving(false);
        if (!errorMessage.isEmpty()) {
            showError(errorMessage);
            return;
        }
        const QPointer<FieldValueEditor> guard(this);
        Q_EMIT saved();
        if (guard)
            QDialog::accept();
    }

    void FieldValueEditor::connectionClosed()
    {
        if (_saving)
            finishSave(_requestId, tr("The connection was closed. Check the saved value before retrying."));
    }

    void FieldValueEditor::setSaving(bool saving)
    {
        _saving = saving;
        _input->setReadOnly(saving);
        _buttons->setEnabled(!saving);
        _buttons->button(QDialogButtonBox::Ok)->setText(saving ? tr("Saving...") : tr("Confirm"));
        _hint->setText(saving ? tr("Saving changes. Waiting for the database...")
                             : tr("Click outside or press Esc to cancel."));
        updatePopupGeometry();
    }

    void FieldValueEditor::showError(const QString &message)
    {
        _error->setText(message);
        _error->show();
        updatePopupGeometry();
        _input->setFocus();
        _scrollArea->ensureWidgetVisible(_error);
    }

    void FieldValueEditor::reject()
    {
        if (!_saving)
            QDialog::reject();
    }

    void FieldValueEditor::closeEvent(QCloseEvent *event)
    {
        if (_saving)
            event->ignore();
        else
            QDialog::closeEvent(event);
    }
}
