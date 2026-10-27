#pragma once
#include "app/viewport.hpp"
#include <QLabel>
#include <QPushButton>
namespace sketchy {
// R107.a (ADR 0181 §2.1, §2.6): the sun study's location and the site's geographic location are
// separate until the user uses the site location. The panel shows the grid convergence (read
// only), a warning when the two differ by more than 1 m, and Use site location (one edit).
class SolarPanel : public QWidget {
  public:
    SolarPanel(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    void refresh();

  private:
    Document &doc_;
    Viewport &view_;
    QLabel *details_{}, *convergence_{}, *warning_{};
    QPushButton *useSite_{};
    void edit();
    void useSiteLocation();
};
} // namespace sketchy
