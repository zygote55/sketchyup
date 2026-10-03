#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QToolBar>
#include <QVBoxLayout>
namespace sketchy {
QAction *Window::action(const QString &id, const QString &title, const QKeySequence &shortcut,
                        const std::function<void()> &fn) {
    for (auto *existing : publicActions_) {
        if (existing->objectName() == id ||
            (!shortcut.isEmpty() && existing->shortcut() == shortcut))
            throw std::logic_error("Duplicate public action identity or shortcut");
    }
    auto *a = new QAction(title, this);
    a->setObjectName(id);
    a->setProperty("category", id.section('.', 0, 0));
    a->setShortcut(shortcut);
    addAction(a);
    publicActions_.push_back(a);
    connect(a, &QAction::triggered, this, [this, fn] { run(fn); });
    return a;
}
Window::Window(QWidget *parent) : QMainWindow(parent) {
    QSettings preferences("SketchyUp", "SketchyUp");
    recentFiles_ = preferences.value("recentFiles").toStringList().mid(0, 10);
    themeMode_ = std::clamp(preferences.value("theme", 0).toInt(), 0, 2);
    setWindowTitle("SketchyUp — Native modeling spike");
    resize(1200, 800);
    setMinimumSize(640, 480);
    auto *root = new QWidget(this);
    auto *vertical = new QVBoxLayout(root);
    vertical->setContentsMargins(0, 0, 0, 0);
    vertical->setSpacing(0);
    auto *header = new QWidget;
    header->setObjectName("header");
    auto *headerLayout = new QHBoxLayout(header);
    auto *brand = new QLabel("SKETCHYUP");
    brand->setObjectName("brand");
    headerLayout->addWidget(brand);
    title_ = new QLabel;
    headerLayout->addWidget(title_);
    headerLayout->addStretch();
    auto *search = new QPushButton("Commands  Ctrl+K");
    search->setObjectName("commandSearch");
    headerLayout->addWidget(search);
    connect(search, &QPushButton::clicked, this, &Window::palette);
    vertical->addWidget(header);
    auto *content = new QHBoxLayout;
    content->setSpacing(0);
    auto *tools = new QToolBar;
    tools->setObjectName("toolRail");
    tools->setFocusPolicy(Qt::StrongFocus);
    tools->setOrientation(Qt::Vertical);
    tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tools->setFixedWidth(78);
    content->addWidget(tools);
    viewport_ = new Viewport(doc_);
    content->addWidget(viewport_, 1);
    tray_ = new QWidget;
    tray_->setObjectName("tray");
    tray_->setFixedWidth(248);
    auto *trayLayout = new QVBoxLayout(tray_);
    auto *heading = new QLabel("MODEL");
    heading->setObjectName("section");
    trayLayout->addWidget(heading);
    info_ = new QLabel("Draw a face to begin.");
    info_->setWordWrap(true);
    trayLayout->addWidget(info_);
    auto *outlinerLabel = new QLabel("OUTLINER");
    outlinerLabel->setObjectName("section");
    trayLayout->addWidget(outlinerLabel);
    outliner_ = new QListWidget;
    outliner_->setAccessibleName("Model objects");
    trayLayout->addWidget(outliner_, 1);
    auto *hint = new QLabel("Native development spike\n\nRectangle or circle → select face → "
                            "Extrude → enter distance.\n\nMiddle drag: orbit\nRight drag: "
                            "pan\nWheel: zoom\n\nSave explicitly. Recovery is not implemented.");
    hint->setWordWrap(true);
    hint->setObjectName("hint");
    trayLayout->addWidget(hint);
    content->addWidget(tray_);
    auto *assistant = new QWidget;
    assistant->setObjectName("assistantContainer");
    assistant->hide();
    content->addWidget(assistant);
    vertical->addLayout(content, 1);
    auto *bottom = new QWidget;
    bottom->setObjectName("footer");
    auto *bottomLayout = new QHBoxLayout(bottom);
    status_ = new QLabel("Select a tool to begin");
    status_->setMinimumWidth(80);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bottomLayout->addWidget(status_, 1);
    bottomLayout->addWidget(new QLabel("Measurements · m"));
    measurements_ = new QLineEdit;
    measurements_->setObjectName("measurements");
    measurements_->setFixedWidth(150);
    measurements_->setPlaceholderText("width, depth");
    measurements_->setAccessibleName("Measurements in meters");
    bottomLayout->addWidget(measurements_);
    vertical->addWidget(bottom);
    setCentralWidget(root);
    auto *file = menuBar()->addMenu("&File");
    file->addAction(action("file.new", "New", QKeySequence::New, [this] {
        if (canReplace()) {
            doc_ = Document();
            path_.clear();
            viewport_->cancel();
            viewport_->refresh();
            viewport_->fit();
            sync();
        }
    }));
    file->addAction(action("file.open", "Open…", QKeySequence::Open, [this] {
        auto p = QFileDialog::getOpenFileName(
            this, "Open model", {},
            "SketchyUp models (*.sketchyup);;Previous saves (*.sketchyup.bak);;All files (*)");
        if (!p.isEmpty())
            openPath(p);
    }));
    file->addAction(action("file.save", "Save", QKeySequence::Save, [this] { save(); }));
    file->addAction(
        action("file.saveAs", "Save as…", QKeySequence::SaveAs, [this] { save(true); }));
    file->addSeparator();
    file->addAction(action("file.example", "Example courtyard", {}, [this] {
        if (canReplace())
            demo();
    }));
    file->addSeparator();
    file->addAction(action("file.quit", "Quit", QKeySequence::Quit, [this] { close(); }));
    auto *edit = menuBar()->addMenu("&Edit");
    undo_ = action("edit.undo", "Undo", QKeySequence::Undo, [this] {
        doc_.undo();
        sync();
    });
    redo_ = action("edit.redo", "Redo", QKeySequence::Redo, [this] {
        doc_.redo();
        sync();
    });
    edit->addAction(undo_);
    edit->addAction(redo_);
    auto *remove = action("edit.delete", "Delete selection", QKeySequence::Delete, [this] {
        if (auto id = viewport_->selectedBody()) {
            doc_.erase(id);
            sync();
        }
    });
    edit->addAction(remove);
    edit->addAction(action("edit.move", "Move selection…", QKeySequence("M"), [this] {
        auto id = viewport_->selectedBody();
        if (!id)
            return;
        bool ok = false;
        auto text = QInputDialog::getText(this, "Move", "Translation in meters: x, y, z",
                                          QLineEdit::Normal, "0, 0, 0", &ok);
        if (!ok)
            return;
        auto values = text.split(',');
        if (values.size() != 3)
            throw std::runtime_error("Enter x, y, z");
        Vec3 delta;
        double *fields[] = {&delta.x, &delta.y, &delta.z};
        for (int i = 0; i < 3; ++i) {
            *fields[i] = values[i].trimmed().toDouble(&ok);
            if (!ok)
                throw std::runtime_error("Invalid translation");
        }
        doc_.move(id, delta);
        sync();
    }));
    edit->addAction(action("edit.paint", "Paint selection…", QKeySequence("B"), [this] {
        auto id = viewport_->selectedBody();
        if (!id)
            return;
        auto c = QColorDialog::getColor(Qt::white, this, "Material color");
        if (c.isValid()) {
            doc_.paint(id, {float(c.redF()), float(c.greenF()), float(c.blueF())});
            sync();
        }
    }));
    auto *draw = menuBar()->addMenu("&Draw");
    auto *group = new QActionGroup(this);
    auto addTool = [&](const QString &name, const QString &key, Viewport::Tool mode,
                       const QString &instruction) {
        auto *a = action("tool." + QString::number(int(mode)), name, QKeySequence(key),
                         [this, mode, instruction] { tool(mode, instruction); });
        a->setCheckable(true);
        group->addAction(a);
        tools->addAction(a);
        draw->addAction(a);
        return a;
    };
    addTool("Select", "Space", Viewport::Tool::Select, "Select a face · Delete removes its object")
        ->setChecked(true);
    addTool("Rectangle", "R", Viewport::Tool::Rectangle,
            "Click first corner · Then click opposite corner or enter width, depth");
    addTool("Circle", "C", Viewport::Tool::Circle,
            "Click center · Then click radius or enter a radius");
    addTool("Extrude", "P", Viewport::Tool::Extrude,
            "Select a face · Enter push/pull distance in meters");
    tools->addSeparator();
    addTool("Orbit", "O", Viewport::Tool::Orbit, "Drag to orbit · Shift-drag to pan");
    addTool("Pan", "H", Viewport::Tool::Pan, "Drag to pan");
    auto *view = menuBar()->addMenu("&View");
    view->addAction(
        action("view.fit", "Fit model", QKeySequence("Shift+Z"), [this] { viewport_->fit(); }));
    view->addAction(action("view.perspective", "Perspective", QKeySequence("1"),
                           [this] { viewport_->standardView(0); }));
    view->addAction(
        action("view.top", "Top", QKeySequence("2"), [this] { viewport_->standardView(1); }));
    view->addAction(
        action("view.front", "Front", QKeySequence("3"), [this] { viewport_->standardView(2); }));
    auto *panel = action("view.tray", "Model panel", QKeySequence("Ctrl+Shift+T"), [this] {
        tray_->setVisible(findChild<QAction *>("view.tray")->isChecked());
    });
    panel->setCheckable(true);
    panel->setChecked(true);
    view->addAction(panel);
    auto *themes = view->addMenu("Theme");
    auto *themeGroup = new QActionGroup(this);
    const QStringList themeNames{"System theme", "Light theme", "Dark theme"};
    for (int mode = 0; mode < themeNames.size(); ++mode) {
        auto *entry =
            action("view.theme." + QString::number(mode), themeNames[mode], {}, [this, mode] {
                themeMode_ = mode;
                applyTheme();
            });
        entry->setCheckable(true);
        entry->setChecked(mode == themeMode_);
        themeGroup->addAction(entry);
        themes->addAction(entry);
    }
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (themeMode_ == 0)
            applyTheme();
    });
    action("view.commands", "Commands…", QKeySequence("Ctrl+K"), [this] { palette(); });
    focusRegions_ = {search, tools, viewport_, outliner_, measurements_};
    for (const auto &binding :
         std::vector<std::pair<QString, QString>>{{"edit.move", "geometry.translate"},
                                                  {"edit.paint", "material.color"},
                                                  {"edit.delete", "geometry.delete"}}) {
        auto *entry = findChild<QAction *>(binding.first);
        entry->setProperty("command", binding.second);
        entry->setProperty("requiresSelection", true);
    }
    findChild<QAction *>("edit.move")->setProperty("form", QStringList{"x", "y", "z"});
    findChild<QAction *>("edit.paint")->setProperty("form", QStringList{"color"});
    action("view.nextRegion", "Next region", QKeySequence("F6"), [this] { focusRegion(); });
    action("view.previousRegion", "Previous region", QKeySequence("Shift+F6"),
           [this] { focusRegion(true); });
    // Single-letter modeling shortcuts must never consume typing in text fields.
    for (auto *a : publicActions_)
        if (a->shortcut().count() == 1 && a->shortcut()[0].keyboardModifiers() == Qt::NoModifier &&
            a->shortcut()[0].key() != Qt::Key_F6) {
            a->setShortcutContext(Qt::WidgetShortcut);
            removeAction(a);
            viewport_->addAction(a);
        }
    connect(viewport_, &Viewport::changed, this, &Window::sync);
    connect(viewport_, &Viewport::message, status_, &QLabel::setText);
    connect(viewport_, &Viewport::selected, this, [this](qulonglong, qulonglong) { sync(); });
    connect(measurements_, &QLineEdit::returnPressed, this, [this] {
        if (viewport_->measurements(measurements_->text())) {
            measurements_->clear();
            viewport_->setFocus();
        } else {
            measurements_->selectAll();
        }
    });
    connect(outliner_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            viewport_->setSelection(item->data(Qt::UserRole).toULongLong());
    });
    applyTheme();
    sync();
}
void Window::applyTheme() {
    const bool dark =
        themeMode_ == 2 ||
        (themeMode_ == 0 && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    const auto colors = themeColors(dark);
    auto style = QStringLiteral(R"(
QMainWindow,QWidget {background:$surface;color:$ink;font-family:'DejaVu Sans';font-size:12px;}
QMenuBar {padding:5px;background:$surface;border-bottom:1px solid $border;}
QMenuBar::item {padding:5px 10px;} QMenuBar::item:selected,QMenu::item:selected {background:$selected;}
QMenu {border:1px solid $border;padding:5px;} QMenu::item {padding:7px 22px;}
#header {border-bottom:1px solid $border;} #brand {font-weight:800;letter-spacing:2px;padding:8px 12px;}
#tray {border-left:1px solid $border;} #section {font-size:10px;font-weight:700;letter-spacing:2px;padding:14px 0 8px;}
#hint {color:$muted;font-size:11px;padding:12px 0;} #footer {border-top:1px solid $border;}
QToolBar {border:0;border-right:1px solid $border;spacing:6px;padding:10px 5px;}
QToolButton {padding:12px 5px;border-radius:4px;} QToolButton:checked {background:$selected;color:$ink;}
QToolButton:hover,QPushButton:hover {background:$hover;}
QPushButton {border:1px solid $border;padding:7px 11px;border-radius:4px;}
QLineEdit {background:$input;border:1px solid $border;border-radius:4px;padding:7px;selection-background-color:$accent;}
QLineEdit:focus {border:1px solid $accent;}
QListWidget {border:0;background:transparent;}
QListWidget:focus,QToolBar:focus,QPushButton:focus {border:1px solid $accent;} QListWidget::item {padding:9px 5px;} QListWidget::item:selected {background:$selected;color:$ink;}
)");
    style.replace("$surface", colors.surface.name());
    style.replace("$ink", colors.ink.name());
    style.replace("$border", colors.border.name());
    style.replace("$selected", colors.selected.name());
    style.replace("$muted", colors.muted.name());
    style.replace("$hover", colors.hover.name());
    style.replace("$input", colors.input.name());
    style.replace("$accent", colors.accent.name());
    setStyleSheet(style);
    viewport_->setTheme(colors);
}
void Window::run(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const std::exception &e) {
        status_->setText(e.what());
        QMessageBox::warning(this, "Could not complete edit", e.what());
    }
}
void Window::tool(Viewport::Tool t, const QString &text) {
    viewport_->setTool(t);
    viewport_->setFocus();
    status_->setText(text);
    measurements_->setPlaceholderText(t == Viewport::Tool::Extrude  ? "distance"
                                      : t == Viewport::Tool::Circle ? "radius"
                                                                    : "width, depth");
}
void Window::sync() {
    viewport_->refresh();
    title_->setText(
        (path_.isEmpty() ? "Untitled" : QFileInfo(path_).fileName()) +
        (path_.isEmpty() ? "  ·  Not saved" : (doc_.dirty() ? "  •  Edited" : "  ·  Saved")));
    undo_->setEnabled(doc_.canUndo());
    redo_->setEnabled(doc_.canRedo());
    for (const auto &id : {"edit.move", "edit.paint", "edit.delete"})
        findChild<QAction *>(id)->setEnabled(doc_.bodies().contains(viewport_->selectedBody()));
    QSignalBlocker block(outliner_);
    outliner_->clear();
    for (const auto &[id, b] : doc_.bodies()) {
        auto *item = new QListWidgetItem(
            QString::fromStdString(b->name) + "  #" + QString::number(id), outliner_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
        if (id == viewport_->selectedBody())
            outliner_->setCurrentItem(item);
    }
    auto it = doc_.bodies().find(viewport_->selectedBody());
    if (it != doc_.bodies().end()) {
        const auto &b = *it->second;
        QString text = QString::fromStdString(b.name) + QString("\n%1 vertices · %2 faces")
                                                            .arg(b.surface.vertices.size())
                                                            .arg(b.surface.faces.size());
        if (b.surface.faces.contains(viewport_->selectedFace()))
            text += QString("\nFace area %1 m²")
                        .arg(doc_.worldArea(b.id, viewport_->selectedFace()), 0, 'f', 3);
        info_->setText(text);
    } else
        info_->setText(
            QString("%1 objects\nClick a face to inspect it.").arg(doc_.bodies().size()));
}
bool Window::save(bool saveAs) {
    auto target = path_;
    if (saveAs || target.isEmpty()) {
        QFileDialog dialog(this, "Save model", target.isEmpty() ? "Untitled.sketchyup" : target,
                           "SketchyUp models (*.sketchyup)");
        dialog.setObjectName("saveModelDialog");
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setDefaultSuffix("sketchyup");
        // Confirm the final path ourselves, including suffix normalization. Native
        // backends differ in when they append defaultSuffix during confirmation.
        dialog.setOption(QFileDialog::DontConfirmOverwrite);
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
            return false;
        target = dialog.selectedFiles().front();
        if (!target.endsWith(".sketchyup", Qt::CaseInsensitive))
            target += ".sketchyup";
        if (QFileInfo::exists(target) &&
            QMessageBox::question(
                this, "Replace model?", QString("Replace the existing file %1?").arg(target),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return false;
    }
    if (target.isEmpty())
        return false;
    try {
        saveDocument(doc_, target);
        path_ = target;
        rememberPath(target);
        sync();
        status_->setText("Saved locally");
        return true;
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Save failed", e.what());
        status_->setText("Save failed. Edits remain in memory; recovery is unavailable.");
        return false;
    }
}
bool Window::canReplace() {
    if (!doc_.dirty())
        return true;
    auto choice = QMessageBox::warning(
        this, "Unsaved changes", "Save this model before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel)
        return false;
    if (choice == QMessageBox::Save)
        return save();
    return true;
}
void Window::openPath(const QString &path) {
    // Validate before asking to discard the current document.
    auto loaded = loadDocument(path);
    if (!canReplace())
        return;
    doc_ = std::move(loaded);
    path_ = path;
    rememberPath(path);
    viewport_->cancel();
    viewport_->setSelection(0);
    viewport_->fit();
    sync();
}
void Window::closeEvent(QCloseEvent *e) {
    if (canReplace())
        e->accept();
    else
        e->ignore();
}
void Window::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (tray_)
        tray_->setVisible(width() >= 800);
    if (auto *panel = findChild<QAction *>("view.tray"))
        panel->setChecked(width() >= 800);
    setProperty("layoutClass", width() < 800    ? "compact"
                               : width() < 1100 ? "standard"
                               : width() < 1500 ? "expanded"
                                                : "wide");
}
void Window::focusRegion(bool previous) {
    std::vector<QWidget *> visible;
    int current = -1;
    auto *focused = QApplication::focusWidget();
    for (auto *region : focusRegions_) {
        if (!region->isVisible() || !region->isEnabled())
            continue;
        if (region == focused || region->isAncestorOf(focused))
            current = int(visible.size());
        visible.push_back(region);
    }
    if (visible.empty())
        return;
    const int count = int(visible.size());
    const auto next = current < 0 ? 0 : (current + (previous ? count - 1 : 1)) % count;
    visible[next]->setFocus(Qt::ShortcutFocusReason);
}
void Window::rememberPath(const QString &path) {
    const auto absolute = QFileInfo(path).absoluteFilePath();
    recentFiles_.removeAll(absolute);
    recentFiles_.prepend(absolute);
    recentFiles_ = recentFiles_.mid(0, 10);
    QSettings("SketchyUp", "SketchyUp").setValue("recentFiles", recentFiles_);
}
void Window::palette() {
    QDialog dialog(this);
    dialog.setObjectName("commandPalette");
    dialog.setWindowTitle("Commands and model search");
    dialog.resize(500, 430);
    auto *layout = new QVBoxLayout(&dialog);
    auto *query = new QLineEdit;
    query->setObjectName("paletteQuery");
    query->setPlaceholderText("Commands, objects, recent files, or face BODY/FACE…");
    layout->addWidget(query);
    auto *list = new QListWidget;
    list->setObjectName("paletteResults");
    layout->addWidget(list);
    auto populate = [&] {
        list->clear();
        auto add = [&](const QString &label, const QString &kind, QVariant value,
                       QVariant extra = {}) {
            if (list->count() >= 200)
                return;
            auto *item = new QListWidgetItem(label, list);
            item->setData(Qt::UserRole, kind);
            item->setData(Qt::UserRole + 1, value);
            item->setData(Qt::UserRole + 2, extra);
        };
        for (auto *a : publicActions_)
            if (a->isEnabled() && a->text().contains(query->text(), Qt::CaseInsensitive))
                add(a->text() + "    " + a->shortcut().toString(), "action", a->objectName());
        for (const auto &[id, body] : doc_.bodies()) {
            const auto name = QString::fromStdString(body->name) + " #" + QString::number(id);
            if (name.contains(query->text(), Qt::CaseInsensitive))
                add("Select object: " + name, "entity", QVariant::fromValue<qulonglong>(id));
        }
        const auto faceQuery =
            QRegularExpression("^face ([1-9][0-9]*)/([1-9][0-9]*)$").match(query->text().trimmed());
        if (faceQuery.hasMatch()) {
            bool bodyOk = false, faceOk = false;
            const auto body = faceQuery.captured(1).toULongLong(&bodyOk);
            const auto face = faceQuery.captured(2).toULongLong(&faceOk);
            if (bodyOk && faceOk && doc_.bodies().contains(body) &&
                doc_.bodies().at(body)->surface.faces.contains(face))
                add("Select " + faceQuery.captured(), "entity",
                    QVariant::fromValue<qulonglong>(body), QVariant::fromValue<qulonglong>(face));
        }
        for (const auto &path : recentFiles_)
            if (path.contains(query->text(), Qt::CaseInsensitive))
                add("Open recent: " + path, "recent", path);
        if (list->count())
            list->setCurrentRow(0);
    };
    populate();
    connect(query, &QLineEdit::textChanged, &dialog, [&] { populate(); });
    std::function<void()> chosen;
    auto accept = [&] {
        auto *item = list->currentItem();
        if (!item)
            return;
        const auto kind = item->data(Qt::UserRole).toString();
        const auto value = item->data(Qt::UserRole + 1);
        if (kind == "action") {
            auto *a = findChild<QAction *>(value.toString());
            chosen = [a] {
                if (a && a->isEnabled())
                    a->trigger();
            };
        } else if (kind == "entity") {
            const auto body = value.toULongLong();
            const auto face = item->data(Qt::UserRole + 2).toULongLong();
            chosen = [this, body, face] {
                viewport_->setSelection(body, face);
                viewport_->setFocus();
            };
        } else {
            const auto path = value.toString();
            chosen = [this, path] { run([&] { openPath(path); }); };
        }
        dialog.accept();
    };
    connect(query, &QLineEdit::returnPressed, &dialog, accept);
    connect(list, &QListWidget::itemActivated, &dialog, [&](QListWidgetItem *) { accept(); });
    query->setFocus();
    if (dialog.exec() == QDialog::Accepted && chosen)
        chosen();
}
void Window::demo() {
    Document d;
    auto box = [&](double x, double y, double w, double h, double z, const char *name,
                   std::array<float, 3> color) {
        auto id = d.addFace({{{x, y, 0}, {x + w, y, 0}, {x + w, y + h, 0}, {x, y + h, 0}}}, name);
        d.extrude(id, d.bodies().at(id)->surface.faces.begin()->first, z);
        d.paint(id, color);
    };
    box(-4, -3, 8, 6, .15, "Courtyard slab", {.80f, .78f, .69f});
    box(-4, -3, .25, 6, 2.7, "West wall", {.86f, .85f, .77f});
    box(-4, 2.75, 8, .25, 2.7, "North wall", {.86f, .85f, .77f});
    box(-2, 1.8, 1.5, .55, .6, "Planter", {.45f, .59f, .41f});
    box(1, 1.8, 1.5, .55, .6, "Planter", {.45f, .59f, .41f});
    box(-1, -.6, 2, 1.1, .8, "Table", {.67f, .48f, .30f});
    box(-1, -1.6, 2, .4, .45, "Bench", {.67f, .48f, .30f});
    doc_ = std::move(d);
    path_.clear();
    viewport_->cancel();
    viewport_->setSelection(0);
    viewport_->fit();
    sync();
    status_->setText("Example courtyard · Original editable face geometry");
}
} // namespace sketchy
