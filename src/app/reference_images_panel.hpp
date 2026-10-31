#pragma once
#include "app/viewport.hpp"
#include <QLabel>
class QPushButton;
namespace sketchy {
class ReferenceImagesPanel : public QWidget {
    Q_OBJECT

  public:
    ReferenceImagesPanel(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    void refresh();
    void importFile(const QString &path);

  signals:
    // The status line's text after a georeferenced import or its cancellation (UX §21.2).
    void referenceStatus(const QString &text);

  private:
    Document &doc_;
    Viewport &view_;
    QLabel *details_{};
    QPushButton *edit_{}, *calibrate_{};
    // The note after a georeferenced import, shown in the details of the image it placed.
    Id georefNoteBody_{};
    QString georefNote_;
    Id selectedImage() const;
    void chooseImage();
    void importGeoreferenced(const QString &path);
    void edit(bool calibrate);
};
} // namespace sketchy
