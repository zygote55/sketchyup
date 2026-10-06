#pragma once
#include "app/viewport.hpp"
#include <QLabel>
#include <QListWidget>
namespace sketchy {
class ScenesPanel : public QWidget {
  public:
    ScenesPanel(Document &doc, Viewport &view, QWidget *parent = nullptr);
    void refresh();

  private:
    Document &doc_;
    Viewport &view_;
    QListWidget *list_{};
    QLabel *details_{}, *error_{};
    std::vector<QWidget *> selectedControls_;
    Document::SaveStamp session_;
    Id selected() const;
    void choose(Id scene);
    void describe();
    void edit(bool create, bool renameOnly = false);
    void reorder(int direction);
    void attempt(const std::function<void()> &operation);
};
} // namespace sketchy
