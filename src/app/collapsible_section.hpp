#pragma once
#include <QAccessible>
#include <QBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
namespace sketchy {
// A Tray section with a 28 px header row that shows or hides its content (ADR 0164 §7). The
// header is one keyboard stop: Space toggles it, and its accessible description says whether
// the section is expanded.
class CollapsibleSection : public QWidget {
  public:
    CollapsibleSection(const QString &title, QWidget *content, bool expanded = true,
                       QWidget *parent = nullptr)
        : QWidget(parent), content_(content) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        header_ = new QPushButton;
        header_->setObjectName("sectionHeader");
        header_->setCheckable(true);
        header_->setChecked(expanded);
        header_->setFlat(true);
        header_->setAutoDefault(false);
        header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        header_->setMinimumHeight(28);
        // A narrow Tray elides the title instead of widening the section (ADR 0166 §8).
        header_->setMinimumWidth(48);
        header_->installEventFilter(this);
        layout->addWidget(header_);
        layout->addWidget(content_);
        QObject::connect(header_, &QPushButton::toggled, this, [this] { refreshHeader(); });
        title_ = title;
        refreshHeader();
    }
    void setTitle(const QString &title) {
        title_ = title;
        refreshHeader();
    }
    QString title() const { return title_; }
    // Muted text at the header's end, such as a count ("2 overridden"; ADR 0166 §1.1). It
    // follows the expanded state in the header's accessible description.
    void setDetail(const QString &detail) {
        if (!detail_) {
            // The header row becomes the header button and the detail beside it.
            auto *row = new QWidget;
            auto *box = new QHBoxLayout(row);
            box->setContentsMargins(0, 0, 8, 0);
            box->setSpacing(4);
            auto *column = static_cast<QVBoxLayout *>(layout());
            column->removeWidget(header_);
            column->insertWidget(0, row);
            box->addWidget(header_, 1);
            detail_ = new QLabel;
            detail_->setObjectName("sectionHeaderDetail");
            detail_->setTextFormat(Qt::PlainText);
            box->addWidget(detail_);
        }
        detail_->setText(detail);
        detail_->setVisible(!detail.isEmpty());
        refreshHeader();
    }
    QString detail() const { return detail_ ? detail_->text() : QString{}; }
    bool expanded() const { return header_->isChecked(); }
    void setExpanded(bool expanded) { header_->setChecked(expanded); }
    QPushButton *header() const { return header_; }
    QWidget *content() const { return content_; }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == header_ && event->type() == QEvent::Resize)
            showTitle();
        return QWidget::eventFilter(object, event);
    }
    // The stretch this section takes in its parent's box layout while expanded.
    void setExpandedStretch(int stretch) {
        stretch_ = stretch;
        refreshHeader();
    }

  private:
    QPushButton *header_{};
    QWidget *content_{};
    QLabel *detail_{};
    QString title_;
    int stretch_{0};
    void showTitle() {
        const auto full = QString(header_->isChecked() ? "▾ " : "▸ ") + title_;
        // The style sheet's header padding and border.
        const int room = header_->width() - 20;
        header_->setText(header_->isVisible() && room > 0
                             ? header_->fontMetrics().elidedText(full, Qt::ElideRight, room)
                             : full);
    }
    void refreshHeader() {
        const bool open = header_->isChecked();
        showTitle();
        header_->setAccessibleName(title_);
        const auto detail = this->detail();
        header_->setAccessibleDescription(QString(open ? "Expanded" : "Collapsed") +
                                          (detail.isEmpty() ? QString{} : ", " + detail));
        header_->setToolTip((open ? "Collapse " : "Expand ") + title_);
        content_->setVisible(open);
        // A collapsed section gives its stretch back to the open ones.
        if (auto *box =
                parentWidget() ? qobject_cast<QBoxLayout *>(parentWidget()->layout()) : nullptr)
            if (const int index = box->indexOf(this); index >= 0)
                box->setStretch(index, open ? stretch_ : 0);
        QAccessibleEvent event(header_, QAccessible::DescriptionChanged);
        QAccessible::updateAccessibility(&event);
    }
};
} // namespace sketchy
