#pragma once
// A layout that places its items left to right and wraps them onto further rows. Shared by the
// component Preview strip (ADR 0166 §3.1) and the terrain tool options strip (UX_DESIGN §15.1),
// whose fields wrap to a second row where one row does not fit.
#include <QLayout>
#include <QWidget>
#include <algorithm>
#include <vector>
namespace sketchy {
class FlowLayout final : public QLayout {
  public:
    explicit FlowLayout(QWidget *parent = nullptr, int gapX = 12, int gapY = 6)
        : QLayout(parent), gapX_(gapX), gapY_(gapY) {
        setContentsMargins(0, 0, 0, 0);
    }
    ~FlowLayout() override {
        while (auto *item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem *item) override { items_.push_back(item); }
    int count() const override { return int(items_.size()); }
    QLayoutItem *itemAt(int index) const override {
        return index >= 0 && index < count() ? items_[std::size_t(index)] : nullptr;
    }
    QLayoutItem *takeAt(int index) override {
        if (index < 0 || index >= count())
            return nullptr;
        auto *item = items_[std::size_t(index)];
        items_.erase(items_.begin() + index);
        return item;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return place(QRect(0, 0, width, 0), false); }
    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        place(rect, true);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize size;
        for (auto *item : items_)
            size = size.expandedTo(item->minimumSize());
        const auto margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }

  private:
    std::vector<QLayoutItem *> items_;
    int gapX_, gapY_;
    int place(const QRect &rect, bool apply) const {
        const int gapX = gapX_, gapY = gapY_;
        int x = rect.x(), y = rect.y(), line = 0;
        for (auto *item : items_) {
            if (item->isEmpty())
                continue;
            const auto hint = item->sizeHint();
            const int width = std::min(hint.width(), rect.width());
            if (x > rect.x() && x + width > rect.right() + 1) {
                x = rect.x();
                y += line + gapY;
                line = 0;
            }
            const int height = item->hasHeightForWidth() ? item->heightForWidth(width)
                                                         : hint.height();
            if (apply)
                item->setGeometry(QRect(QPoint(x, y), QSize(width, height)));
            x += width + gapX;
            line = std::max(line, height);
        }
        return y + line - rect.y();
    }
};
} // namespace sketchy
