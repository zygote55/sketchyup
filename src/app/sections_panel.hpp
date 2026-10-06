#pragma once
#include "app/viewport.hpp"
#include <QLabel>
#include <QListWidget>
namespace sketchy {
class SectionsPanel : public QWidget {
  public:
    SectionsPanel(Document &doc, Viewport &view, QWidget *parent = nullptr);
    void refresh();

  private:
    Document &doc_;
    Viewport &view_;
    QListWidget *list_{};
    QLabel *details_{}, *error_{};
    Document::SaveStamp session_;
    Id selected() const;
    void choose(Id id);
    void describe();
    void edit(bool create);
    void attempt(const std::function<void()> &operation);
};
} // namespace sketchy
