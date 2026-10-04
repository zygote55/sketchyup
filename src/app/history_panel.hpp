#pragma once
#include "app/viewport.hpp"
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
namespace sketchy {
class HistoryPanel : public QWidget {
  public:
    HistoryPanel(Document &, Viewport &, QWidget *parent = nullptr);
    void refresh(bool force = false);

  private:
    Document &doc_;
    Viewport &view_;
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
