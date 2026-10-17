#include "app/parameters_scope_dialog.hpp"
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
namespace sketchy {
QString ParametersScopeDialog::listed(const QStringList &items) {
    if (items.size() <= 1)
        return items.value(0);
    return QStringList(items.begin(), items.end() - 1).join(", ") + " and " + items.back();
}
ParametersScopeDialog::ParametersScopeDialog(const QString &placement, const QString &values,
                                             int count, const QString &parent, QWidget *owner)
    : Settled<QDialog>(owner) {
    setObjectName("parametersScopeDialog");
    const auto title = placement + " has its own values";
    setWindowTitle(title);
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);
    heading_ = new QLabel(title);
    heading_->setObjectName("parametersScopeHeading");
    heading_->setTextFormat(Qt::PlainText);
    heading_->setWordWrap(true);
    layout->addWidget(heading_);
    // The design's sentence (States artboard); a nested placement says where its values
    // come from and is edited only on a unique copy (§4.1).
    QString body = values.isEmpty()
                       ? QString{}
                       : values + (count == 1 ? " differs" : " differ") + " from the defaults. ";
    body += parent.isEmpty() ? QString("Shape edits are made on the shared definition at its "
                                       "default values, or on a unique copy.")
                             : "Its values come from " + parent + ".";
    text_ = new QLabel(body);
    text_->setObjectName("parametersScopeText");
    text_->setTextFormat(Qt::PlainText);
    text_->setWordWrap(true);
    layout->addWidget(text_);
    error_ = new ParameterMessageLabel;
    error_->setObjectName("parametersScopeError");
    error_->hide();
    layout->addWidget(error_);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch();
    cancel_ = new QPushButton("Cancel");
    cancel_->setObjectName("parametersScopeCancel");
    cancel_->setAutoDefault(false);
    unique_ = new QPushButton("Make unique and edit");
    unique_->setObjectName("parametersScopeUnique");
    unique_->setAutoDefault(false);
    defaults_ = new QPushButton("Edit definition at defaults");
    defaults_->setObjectName("parametersScopeDefaults");
    defaults_->setAutoDefault(false);
    defaults_->setProperty("accentFill", true); // The shared focus rule (R091.h).
    buttons->addWidget(cancel_);
    buttons->addWidget(unique_);
    buttons->addWidget(defaults_);
    layout->addLayout(buttons);
    footnote_ = new QLabel("Make unique keeps the parameters: this placement's values become the "
                           "copy's defaults.");
    footnote_->setObjectName("parametersScopeNote");
    footnote_->setTextFormat(Qt::PlainText);
    footnote_->setWordWrap(true);
    layout->addWidget(footnote_);
    // A nested placement cannot be reset: its values come from its parent (§4.1).
    if (!parent.isEmpty()) {
        defaults_->hide();
        unique_->setProperty("accentFill", true);
        unique_->setDefault(true);
    } else {
        defaults_->setDefault(true);
    }
    setAccessibleName(title);
    setAccessibleDescription(body);
    connect(cancel_, &QPushButton::clicked, this, [this] { done(Cancel); });
    connect(unique_, &QPushButton::clicked, this, [this] { choose(MakeUnique); });
    connect(defaults_, &QPushButton::clicked, this, [this] { choose(EditAtDefaults); });
    // The design's 620 px card, within the screen.
    const auto *screen = owner ? owner->screen() : QGuiApplication::primaryScreen();
    const int available = screen ? screen->availableGeometry().width() - 32 : 620;
    setMaximumWidth(std::max(280, std::min(available, 620)));
    setMinimumWidth(std::min(std::max(280, available), 440));
    // Focus starts on the default button, so Enter takes it and Esc cancels.
    (parent.isEmpty() ? defaults_ : unique_)->setFocus(Qt::OtherFocusReason);
}
QString ParametersScopeDialog::text() const { return text_->text(); }
void ParametersScopeDialog::choose(int choice) {
    if (check_) {
        const auto refused = check_(choice);
        if (!refused.sentence.isEmpty()) {
            error_->setTone(refused.tone);
            error_->setText(refused.sentence);
            error_->setToolTip(refused.detail);
            error_->show();
            // The sentence joins the dialog's description, so a screen reader hears it.
            setAccessibleDescription(text_->text() + " " + refused.sentence);
            return;
        }
    }
    done(choice);
}
} // namespace sketchy
