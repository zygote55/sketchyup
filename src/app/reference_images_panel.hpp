#pragma once
#include "app/viewport.hpp"
#include <QLabel>
class QPushButton;
namespace sketchy {
class ReferenceImagesPanel : public QWidget {
  public:
    ReferenceImagesPanel(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    void refresh();
    void importFile(const QString &path);

  private:
    Document &doc_;
    Viewport &view_;
    QLabel *details_{};
    QPushButton *edit_{}, *calibrate_{};
    Id selectedImage() const;
    void edit(bool calibrate);
};
} // namespace sketchy
