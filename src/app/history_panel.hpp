#pragma once
#include "app/viewport.hpp"
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTreeWidget>
namespace sketchy {
class HistoryPanel : public QWidget {
  public:
    HistoryPanel(Document &, Viewport &, QWidget *parent = nullptr);
    void refresh(bool force = false);
    // Where keyboard focus goes after restoring a step; the viewport when unset. The Sheets
    // Tray's copy returns focus to the page canvas, because the Sheets workspace hides the
    // viewport.
    void setFocusTarget(QWidget *target) { focusTarget_ = target; }

  private:
    Document &doc_;
    Viewport &view_;
    QPointer<QWidget> focusTarget_;
    QTreeWidget *steps_{};
    QPlainTextEdit *details_{};
    QLabel *summary_{}, *error_{};
    QPushButton *undo_{}, *redo_{}, *older_{}, *newer_{};
    HistoryPage page_;
    Document::SaveStamp stamp_;
    std::uint64_t revision_{};
    size_t offset_{};
    bool syncing_{}, pending_{};
    void navigate(size_t position);
    void details();
};
} // namespace sketchy
