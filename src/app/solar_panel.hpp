#pragma once
#include "app/viewport.hpp"
#include <QLabel>
namespace sketchy {
class SolarPanel : public QWidget {
  public:
    SolarPanel(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    void refresh();

  private:
    Document &doc_;
    Viewport &view_;
    QLabel *details_{};
    void edit();
};
} // namespace sketchy
