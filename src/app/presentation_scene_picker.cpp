#include "app/presentation_scene_picker.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace sketchy {

PresentationScenePicker::PresentationScenePicker(const QString &pathName,
                                                 std::vector<PickerScene> scenes,
                                                 std::function<QString(Id)> add, QWidget *parent)
    : QDialog(parent), scenes_(std::move(scenes)), add_(std::move(add)) {
    setObjectName("presentationScenePicker");
    setWindowTitle("Add scenes to " + pathName);
    setModal(true);
    resize(520, 470);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("Enter adds a scene. Scenes are listed in their saved order.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    list_ = new QListWidget;
    list_->setObjectName("presentationSceneList");
    list_->setAccessibleName("Saved scenes in their saved order");
    layout->addWidget(list_, 1);
    for (const auto &scene : scenes_) {
        auto *item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(scene.id));
        describe(item, scene);
    }
    if (list_->count())
        list_->setCurrentRow(0);
    connect(list_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { addCurrent(item); });
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) { addCurrent(item); });
    status_ = new QLabel("Enter adds · Esc is Done");
    status_->setObjectName("presentationPickerStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *done = new QPushButton("Done");
    done->setObjectName("presentationPickerDone");
    done->setAccessibleName("Done");
    done->setDefault(true);
    buttons->addWidget(done);
    layout->addLayout(buttons);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    list_->setFocus();
}

void PresentationScenePicker::addCurrent(QListWidgetItem *item) {
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;
    const auto id = item->data(Qt::UserRole).toULongLong();
    for (auto &scene : scenes_) {
        if (scene.id != id)
            continue;
        const auto refusal = add_(id);
        if (!refusal.isEmpty()) {
            status_->setText(refusal);
            return;
        }
        ++scene.inPath;
        describe(item, scene);
        status_->setText("Added " + scene.name + ". Enter adds · Esc is Done");
        return;
    }
}

void PresentationScenePicker::describe(QListWidgetItem *item, const PickerScene &scene) const {
    if (!scene.camera) {
        item->setText(scene.name + "\nno camera: give it a camera in Saved scenes first");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        item->setData(Qt::AccessibleTextRole, scene.name + ", no camera; not selectable");
        return;
    }
    if (scene.inPath)
        item->setText(scene.name + "\ncamera · in path " + QString::number(scene.inPath) + "×");
    else
        item->setText(scene.name + "\ncamera");
    item->setData(Qt::AccessibleTextRole,
                  scene.name + ", camera" +
                      (scene.inPath ? ", in the path " + QString::number(scene.inPath) + " times"
                                    : QString()));
}

} // namespace sketchy
