#pragma once
#include "app/viewport.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
namespace sketchy {
class StylesPanel : public QWidget {
  public:
    StylesPanel(Document &doc, Viewport &view, QWidget *parent = nullptr);
    void refresh();

  private:
    Document &doc_;
    Viewport &view_;
    QComboBox *mode_{};
    QLabel *error_{};
    std::vector<std::pair<QCheckBox *, bool ModelStyle::*>> flags_;
    bool syncing_{};
    void change(const std::function<void(ModelStyle &)> &operation);
    void customize();
};
} // namespace sketchy
