#pragma once
// R099.d2 (UX_DESIGN §17.2, §17.10): the scene picker of the camera path's create flow. It lists
// the saved scenes in their saved order. Enter or a double click adds a scene through `add`, and
// the picker stays open for more; Done or Esc returns. A scene without a camera is listed dimmed.
#include "core/model.hpp"
#include <QDialog>
#include <functional>
#include <vector>

class QLabel;
class QListWidget;
class QListWidgetItem;

namespace sketchy {

struct PickerScene {
    Id id{};
    QString name;
    bool camera{};
    int inPath{}; // How many times the path already keeps this scene.
};

class PresentationScenePicker : public QDialog {
  public:
    // `add` appends one keyframe. It returns the words for a refusal, or an empty string when the
    // scene was added; the picker shows the refusal and stays open.
    PresentationScenePicker(const QString &pathName, std::vector<PickerScene> scenes,
                            std::function<QString(Id)> add, QWidget *parent = nullptr);

  private:
    std::vector<PickerScene> scenes_;
    std::function<QString(Id)> add_;
    QListWidget *list_{};
    QLabel *status_{};
    void addCurrent(QListWidgetItem *item);
    void describe(QListWidgetItem *item, const PickerScene &scene) const;
};

} // namespace sketchy
