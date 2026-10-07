#include "app/scenes_panel.hpp"
#include "core/scenes.hpp"
#include "io/scenes_io.hpp"
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
namespace sketchy {
ScenesPanel::ScenesPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    setObjectName("scenesPanel");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("Save selected view properties. Double-click a scene or use the "
                            "viewport tabs to recall it.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *timing = new QFormLayout;
    auto *duration = new QSpinBox;
    duration->setObjectName("sceneTransitionDuration");
    duration->setAccessibleName("Camera transition duration in milliseconds");
    duration->findChild<QLineEdit *>()->setAccessibleName(
        "Camera transition duration in milliseconds");
    duration->setRange(0, 10000);
    duration->setSuffix(" ms");
    duration->setValue(std::clamp(QSettings().value("sceneTransitionMs", 160).toInt(), 0, 10000));
    view_.setSceneTransitionDuration(duration->value());
    timing->addRow("Camera transition", duration);
    layout->addLayout(timing);
    connect(duration, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        view_.setSceneTransitionDuration(value);
        QSettings().setValue("sceneTransitionMs", value);
    });
    list_ = new QListWidget;
    list_->setObjectName("savedScenesList");
    list_->setAccessibleName("Saved scenes in presentation order");
    layout->addWidget(list_, 1);
    setFocusProxy(list_);
    auto *controls = new QGridLayout;
    layout->addLayout(controls);
    auto button = [&](const QString &id, const QString &text, int row, int column, bool selected,
                      const QKeySequence &shortcut, const std::function<void()> &operation) {
        auto *action = new QAction(text, this);
        action->setObjectName(id);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(action);
        connect(action, &QAction::triggered, this, [this, operation] { attempt(operation); });
        auto *button = new QPushButton(text);
        button->setObjectName(id + "Button");
        button->setToolTip(shortcut.isEmpty() ? text : text + " (" + shortcut.toString() + ")");
        controls->addWidget(button, row, column);
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        if (selected) {
            selectedControls_.push_back(button);
            connect(list_, &QListWidget::currentRowChanged, action,
                    [this, action] { action->setEnabled(this->selected() != 0); });
            action->setEnabled(false);
        }
    };
    button("sceneNew", "New…", 0, 0, false, QKeySequence("Ctrl+Shift+N"), [this] { edit(true); });
    button("sceneUpdate", "Update…", 0, 1, true, {}, [this] { edit(false); });
    button("sceneRecall", "Recall", 1, 0, true, QKeySequence("Return"), [this] {
        if (selected())
            view_.recallSavedScene(selected());
    });
    button("sceneDelete", "Delete", 1, 1, true, QKeySequence("Delete"), [this] {
        if (selected())
            view_.editSavedScenes({QJsonObject{{"command", "saved_scene.delete"},
                                               {"scene", QString::number(selected())}}});
    });
    button("sceneEarlier", "Move earlier", 2, 0, true, QKeySequence("Alt+Up"),
           [this] { reorder(-1); });
    button("sceneLater", "Move later", 2, 1, true, QKeySequence("Alt+Down"),
           [this] { reorder(1); });
    button("sceneRename", "Rename…", 3, 0, true, QKeySequence("F2"), [this] { edit(false, true); });
    details_ = new QLabel;
    details_->setObjectName("sceneDetails");
    details_->setWordWrap(true);
    layout->addWidget(details_);
    error_ = new QLabel;
    error_->setObjectName("sceneError");
    error_->setWordWrap(true);
    layout->addWidget(error_);
    auto *history = new QLabel("Undo restores saved scene edits, model style and model visibility. "
                               "Named section activation is undoable. Camera, temporary hiding and free clipping are view navigation.");
    history->setWordWrap(true);
    layout->addWidget(history);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { describe(); });
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this] { attempt([this] { view_.recallSavedScene(selected()); }); });
    connect(&view_, &Viewport::changed, this, [this] { refresh(); });
    connect(&view_, &Viewport::sceneRecalled, this, [this](qulonglong id) { choose(id); });
    refresh();
}
Id ScenesPanel::selected() const {
    return list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toULongLong() : 0;
}
void ScenesPanel::choose(Id id) {
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toULongLong() == id) {
            list_->setCurrentRow(i);
            break;
        }
    describe();
}
void ScenesPanel::refresh() {
    const auto previous = doc_.owns(session_) ? selected() : 0;
    session_ = doc_.saveStamp();
    {
        const QSignalBlocker blocked(list_);
        list_->clear();
        for (auto id : orderedScenes(doc_)) {
            auto *item =
                new QListWidgetItem(QString::fromStdString(doc_.scenes().at(id)->name), list_);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
        }
        if (list_->count())
            list_->setCurrentRow(0);
    }
    choose(previous);
    describe();
    for (auto *action : actions())
        if (action->objectName() != "sceneNew")
            action->setEnabled(selected() != 0);
}
void ScenesPanel::describe() {
    const auto id = selected();
    for (auto *control : selectedControls_)
        control->setEnabled(id != 0);
    if (!id || !doc_.scenes().contains(id)) {
        details_->setText("No saved scenes yet.");
        return;
    }
    const auto &snapshot = doc_.scenes().at(id)->snapshot;
    QStringList properties;
    if (snapshot.camera)
        properties << "camera";
    if (snapshot.visibility)
        properties << "visibility";
    if (snapshot.style)
        properties << "style";
    if (snapshot.section)
        properties << "section";
    const auto missing = missingSceneReferences(doc_, snapshot);
    auto text = "Controls: " + properties.join(", ") + ".";
    if (!missing.empty())
        text += QString("\nMissing references: %1 bodies, %2 tags, %3 entities, %4 sections. Recall skips them; "
                        "Update captures current references.")
                    .arg(missing.bodies.size())
                    .arg(missing.tags.size())
                    .arg(missing.entities.size())
                    .arg(missing.sections.size());
    details_->setText(text);
}
void ScenesPanel::attempt(const std::function<void()> &operation) {
    try {
        operation();
        error_->clear();
    } catch (const std::exception &error) {
        error_->setText(error.what());
    }
}
void ScenesPanel::reorder(int direction) {
    auto order = orderedScenes(doc_);
    const auto id = selected();
    const auto found = std::find(order.begin(), order.end(), id);
    if (found == order.end())
        return;
    const auto index = std::distance(order.begin(), found), target = index + direction;
    if (target < 0 || target >= std::ssize(order))
        return;
    std::swap(order[size_t(index)], order[size_t(target)]);
    QJsonArray values;
    for (auto scene : order)
        values.append(QString::number(scene));
    view_.editSavedScenes({QJsonObject{{"command", "saved_scene.reorder"}, {"order", values}}});
    choose(id);
}
void ScenesPanel::edit(bool create, bool renameOnly) {
    const auto id = create ? 0 : selected();
    if (!create && !id)
        return;
    const auto record = create ? ScenePtr{} : doc_.scenes().at(id);
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    QDialog dialog(this);
    dialog.setObjectName("savedSceneDialog");
    dialog.setWindowTitle(create       ? "New saved scene"
                          : renameOnly ? "Rename saved scene"
                                       : "Update saved scene");
    auto *form = new QFormLayout(&dialog);
    auto *name = new QLineEdit;
    name->setObjectName("savedSceneName");
    name->setMaxLength(1024);
    if (record)
        name->setText(QString::fromStdString(record->name));
    else {
        size_t number = 1;
        std::set<std::string> used;
        for (const auto &[key, scene] : doc_.scenes())
            used.insert(scene->name);
        while (used.contains("Scene " + std::to_string(number)))
            ++number;
        name->setText("Scene " + QString::number(number));
    }
    form->addRow("Name", name);
    auto flag = [&](const char *label, const char *object, bool checked) {
        auto *box = new QCheckBox(label);
        box->setObjectName(object);
        box->setChecked(checked);
        form->addRow(box);
        return box;
    };
    auto *camera =
        flag("Camera", "savedSceneCamera", !record || record->snapshot.camera.has_value());
    auto *visibility = flag("Visibility", "savedSceneVisibility",
                            !record || record->snapshot.visibility.has_value());
    auto *style =
        flag("Model style", "savedSceneStyle", !record || record->snapshot.style.has_value());
    auto *solar = flag("Sun study", "savedSceneSolar", !record || record->snapshot.solar.has_value());
    auto *section = flag("Section clipping", "savedSceneSection",
                         !record || record->snapshot.section.has_value());
    auto *hint = new QLabel("Checked properties are captured from the current view. Unchecked "
                            "properties are left unchanged when this scene is recalled.");
    hint->setWordWrap(true);
    form->addRow(hint);
    if (renameOnly) {
        for (auto *box : {camera, visibility, style, section, solar})
            box->hide();
        hint->setText("Change the name of this saved view.");
    }
    auto *error = new QLabel;
    error->setObjectName("savedSceneDialogError");
    error->setWordWrap(true);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp))
                throw std::runtime_error("Document changed; reopen the scene editor");
            const auto snapshot =
                renameOnly
                    ? record->snapshot
                    : view_.captureSceneSnapshot(camera->isChecked(), visibility->isChecked(),
                                                 style->isChecked(), section->isChecked(), solar->isChecked());
            QJsonArray commands;
            if (create)
                commands.append(QJsonObject{{"command", "saved_scene.create"},
                                            {"name", name->text()},
                                            {"snapshot", encodeSceneSnapshot(snapshot)}});
            else {
                if (record->name != name->text().toStdString())
                    commands.append(QJsonObject{{"command", "saved_scene.rename"},
                                                {"scene", QString::number(id)},
                                                {"name", name->text()}});
                if (record->snapshot != snapshot)
                    commands.append(QJsonObject{{"command", "saved_scene.update"},
                                                {"scene", QString::number(id)},
                                                {"snapshot", encodeSceneSnapshot(snapshot)}});
            }
            Id selected = id;
            if (!commands.isEmpty()) {
                const auto result = view_.editSavedScenes(commands);
                if (create)
                    selected = result["createdScenes"].toArray()[0].toString().toULongLong();
            }
            choose(selected);
            dialog.accept();
        } catch (const std::exception &e) {
            error->setText(e.what());
        }
    });
    name->selectAll();
    name->setFocus();
    dialog.exec();
}
} // namespace sketchy
