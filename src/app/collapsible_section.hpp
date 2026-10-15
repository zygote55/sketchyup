#pragma once
#include <QAccessible>
#include <QBoxLayout>
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
    bool expanded() const { return header_->isChecked(); }
    void setExpanded(bool expanded) { header_->setChecked(expanded); }
    QPushButton *header() const { return header_; }
    QWidget *content() const { return content_; }
    // The stretch this section takes in its parent's box layout while expanded.
    void setExpandedStretch(int stretch) {
        stretch_ = stretch;
        refreshHeader();
    }

  private:
    QPushButton *header_{};
    QWidget *content_{};
    QString title_;
    int stretch_{0};
    void refreshHeader() {
        const bool open = header_->isChecked();
        header_->setText(QString(open ? "▾ " : "▸ ") + title_);
        header_->setAccessibleName(title_);
        header_->setAccessibleDescription(open ? "Expanded" : "Collapsed");
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
