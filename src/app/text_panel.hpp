#pragma once
#include "app/viewport.hpp"
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QThread>
namespace sketchy {
class TextPanel : public QWidget {
  public:
    TextPanel(Document &doc, Viewport &view, QWidget *parent = nullptr);
    ~TextPanel() override;
    void refresh();

  private:
    Document &doc_;
    Viewport &view_;
    QListWidget *list_{};
    QLabel *details_{}, *error_{};
    Document::SaveStamp session_;
    std::vector<QPointer<QThread>> workers_;
    Id selected() const;
    void choose(Id id);
    void describe();
    void edit(bool create);
    void bake();
    void attempt(const std::function<void()> &operation);
};
} // namespace sketchy
