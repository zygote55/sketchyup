#include "app/window.hpp"
#include "app/inspection_service.hpp"
#include "app/render_panel.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <tuple>
#include <wayland-client.h>
namespace sketchy {
namespace {
class WindowUnmapBarrier final : public QObject {
    wl_callback *callback_{};
    std::function<void()> finish_;

  public:
    WindowUnmapBarrier(QWidget &window, wl_display *display, std::function<void()> finish)
        : QObject(&window), finish_(std::move(finish)) {
        // Retain the surface while compositor focus events are drained.
        window.windowHandle()->hide();
        callback_ = wl_display_sync(display);
        if (!callback_) {
            // A disconnected display cannot acknowledge the unmap. Finish
            // shutdown outside the close handler without throwing through Qt.
            QTimer::singleShot(0, this, [this] {
                auto finish = std::move(finish_);
                deleteLater();
                finish();
            });
            return;
        }
        static const wl_callback_listener listener{
            [](void *data, wl_callback *callback, std::uint32_t) {
                auto *barrier = static_cast<WindowUnmapBarrier *>(data);
                wl_callback_destroy(callback);
                barrier->callback_ = nullptr;
                QTimer::singleShot(0, barrier, [barrier] {
                    auto finish = std::move(barrier->finish_);
                    barrier->deleteLater();
                    finish();
                });
            }};
        wl_callback_add_listener(callback_, &listener, this);
        wl_display_flush(display);
    }
    ~WindowUnmapBarrier() override {
        if (callback_)
            wl_callback_destroy(callback_);
    }
};
} // namespace
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
    connect(a, &QAction::triggered, this, [this, fn, id] { run(fn, id.startsWith("view.")); });
    return a;
}
Window::Window(QWidget *parent, AssistantPanel::HostServices assistantServices)
    : QMainWindow(parent), doc_(preferredUnits()) {
    QSettings preferences("SketchyUp", "SketchyUp");
    recentFiles_ = preferences.value("recentFiles").toStringList().mid(0, 10);
    themeMode_ = std::clamp(preferences.value("theme", 0).toInt(), 0, 2);
    setWindowTitle("SketchyUp — Native modeling");
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
    recoveryStatus_ = new QLabel;
    recoveryStatus_->setObjectName("recoveryStatus");
    recoveryStatus_->setTextFormat(Qt::PlainText);
    recoveryStatus_->setWordWrap(true);
    recoveryStatus_->setContentsMargins(12, 2, 12, 2);
    recoveryStatus_->hide();
    vertical->addWidget(recoveryStatus_);
    saveBanner_ = new QWidget;
    saveBanner_->setObjectName("saveFailureBanner");
    auto *saveBannerLayout = new QHBoxLayout(saveBanner_);
    saveBannerText_ = new QLabel;
    saveBannerText_->setTextFormat(Qt::PlainText);
    saveBannerText_->setWordWrap(true);
    saveBannerLayout->addWidget(saveBannerText_, 1);
    auto *retrySave = new QPushButton("Retry Save");
    retrySave->setObjectName("retrySave");
    connect(retrySave, &QPushButton::clicked, this, [this] { save(); });
    saveBannerLayout->addWidget(retrySave);
    auto *saveCopy = new QPushButton("Save as…");
    connect(saveCopy, &QPushButton::clicked, this, [this] { save(true); });
    saveBannerLayout->addWidget(saveCopy);
    saveBanner_->hide();
    vertical->addWidget(saveBanner_);
    auto *content = new QHBoxLayout;
    content_ = content;
    content->setSpacing(0);
    auto *tools = new QToolBar;
    tools->setObjectName("toolRail");
    tools->setFocusPolicy(Qt::StrongFocus);
    tools->setOrientation(Qt::Vertical);
    tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tools->setFixedWidth(78);
    content->addWidget(tools);
    viewport_ = new Viewport(doc_);
    inspection_ = std::make_unique<DesktopInspection>(*viewport_);
    auto *modelTabs = new QTabWidget;
    modelTabs->setObjectName("modelTabs");
    modelTabs->setTabBarAutoHide(true);
    modelTabs->setTabsClosable(true);
    modelTabs->addTab(viewport_, "Model");
    modelTabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    modelTabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    content->addWidget(modelTabs, 1);
    breadcrumb_ = new QLabel(viewport_);
    breadcrumb_->setObjectName("contextBreadcrumb");
    breadcrumb_->setAccessibleName("Editing context breadcrumb");
    breadcrumb_->setTextFormat(Qt::RichText);
    breadcrumb_->setWordWrap(true);
    breadcrumb_->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                         Qt::LinksAccessibleByKeyboard);
    breadcrumb_->setFocusPolicy(Qt::StrongFocus);
    breadcrumb_->move(16, 12);
    connect(breadcrumb_, &QLabel::linkActivated, this, [this](const QString &link) {
        run([&] {
            viewport_->enterContext(link.toULongLong());
            viewport_->setFocus();
        });
    });
    componentBanner_ = new QLabel(viewport_);
    componentBanner_->setObjectName("componentScopeBanner");
    componentBanner_->setAccessibleName("Shared component editing scope");
    componentBanner_->setTextFormat(Qt::RichText);
    componentBanner_->setWordWrap(true);
    componentBanner_->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                              Qt::LinksAccessibleByKeyboard);
    componentBanner_->setFocusPolicy(Qt::StrongFocus);
    connect(componentBanner_, &QLabel::linkActivated, this,
            [this] { run([this] { viewport_->makeComponentUnique(true); }); });
    tray_ = new QWidget;
    tray_->setObjectName("tray");
    tray_->setMinimumWidth(220);
    auto *trayLayout = new QVBoxLayout(tray_);
    auto *heading = new QLabel("MODEL");
    heading->setObjectName("section");
    trayLayout->addWidget(heading);
    info_ = new QLabel("Draw a face to begin.");
    info_->setWordWrap(true);
    trayLayout->addWidget(info_);
    organization_ = new OrganizationPanel(doc_, *viewport_);
    outliner_ = organization_->outliner();
    info_->setAccessibleName("Selection summary");
    trayLayout->addWidget(organization_, 1);
    auto *hint = new QLabel("Middle drag: orbit\nRight drag: pan · Wheel: zoom");
    hint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    hint->setWordWrap(true);
    hint->setObjectName("hint");
    trayLayout->addWidget(hint);
    sideTabs_ = new QTabWidget;
    sideTabs_->setObjectName("assistantSideTabs");
    sideTabs_->setTabBarAutoHide(true);
    sideTabs_->addTab(tray_, "Model");
    sideTabs_->setFixedWidth(248);
    content->addWidget(sideTabs_);
    assistant_ = new AssistantPanel(doc_, *viewport_, std::move(assistantServices), root);
    assistant_->hide();
    vertical->addLayout(content, 1);
    auto *bottom = new QWidget;
    bottom->setObjectName("footer");
    auto *bottomLayout = new QHBoxLayout(bottom);
    status_ = new QLabel("Select a tool to begin");
    status_->setMinimumWidth(80);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bottomLayout->addWidget(status_, 1);
    auto *renderChip = new QPushButton;
    bottomLayout->addWidget(renderChip);
    render_ = new RenderPanel(doc_, *viewport_, *modelTabs, *renderChip, *this);
    measurementUnits_ = new QLabel;
    measurementUnits_->setObjectName("measurementUnits");
    bottomLayout->addWidget(measurementUnits_);
    measurements_ = new QLineEdit;
    measurements_->setObjectName("measurements");
    measurements_->installEventFilter(this);
    measurements_->setFixedWidth(150);
    measurements_->setPlaceholderText("width, depth");
    measurements_->setAccessibleName("Measurements");
    bottomLayout->addWidget(measurements_);
    vertical->addWidget(bottom);
    setCentralWidget(root);
    auto *file = menuBar()->addMenu("&File");
    file->addAction(action("file.new", "New", QKeySequence::New, [this] {
        if (canReplace()) {
            doc_ = Document(preferredUnits());
            resetRecoveryContext();
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
    file->addAction(action("file.importDxf", "Import 2D DXF…", {}, [this] { importDxfDialog(); }));
    file->addAction(action("file.exportMeasured", "Export measured PDF/SVG…", {},
                           [this] { exportMeasuredDialog(); }));
    file->addAction(action("file.exportDxf", "Export 2D DXF…", {}, [this] { exportDxfDialog(); }));
    file->addAction(action("file.importStl", "Import STL…", {}, [this] { importStlDialog(); }));
    file->addAction(action("file.exportStl", "Export STL…", {}, [this] { exportStlDialog(); }));
    file->addAction(action("file.importObj", "Import OBJ/MTL…", {}, [this] { importObjDialog(); }));
    file->addAction(action("file.importGltf", "Import GLB/glTF…", {}, [this] {
        const auto path =
            QFileDialog::getOpenFileName(this, "Import GLB/glTF", {}, "glTF models (*.glb *.gltf)");
        if (!path.isEmpty())
            importGltfPath(path);
    }));
    file->addAction(action("file.importFormline", "Import Formline…", {}, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Import Formline v1", {},
                                                       "Formline models (*.formline *.json)");
        if (!path.isEmpty())
            importFormlinePath(path);
    }));
    file->addAction(action("file.save", "Save", QKeySequence::Save, [this] { save(); }));
    file->addAction(
        action("file.saveAs", "Save as…", QKeySequence::SaveAs, [this] { save(true); }));
    file->addAction(
        action("file.exportRaster", "Export view as PNG…", {}, [this] { exportRasterDialog(); }));
    file->addAction(
        action("file.exportObj", "Export OBJ package…", {}, [this] { exportObjDialog(); }));
    file->addSeparator();
    file->addAction(action("file.recovery", "Recover work…", {}, [this] { showRecovery(); }));
    file->addAction(action("file.recoveryNow", "Save recovery now", {}, [this] {
        if (!recovery_)
            startRecovery();
        recovery_->checkpoint();
    }));
    file->addAction(
        action("file.recoverySettings", "Recovery settings…", {}, [this] { recoverySettings(); }));
    file->addSeparator();
    file->addAction(action("file.example", "Example courtyard", {}, [this] {
        if (canReplace())
            demo();
    }));
    file->addSeparator();
    file->addAction(action("file.units", "Document units…", {}, [this] { unitsSettings(); }));
    file->addAction(action("file.quit", "Quit", QKeySequence::Quit, [this] { close(); }));
    auto *edit = menuBar()->addMenu("&Edit");
    undo_ = action("edit.undo", "Undo", QKeySequence::Undo, [this] {
        viewport_->cancel();
        doc_.undo();
        sync();
        viewport_->setFocus();
    });
    redo_ = action("edit.redo", "Redo", QKeySequence::Redo, [this] {
        viewport_->cancel();
        doc_.redo();
        sync();
        viewport_->setFocus();
    });
    edit->addAction(undo_);
    edit->addAction(redo_);
    auto *remove = action("edit.delete", "Delete selection", QKeySequence::Delete,
                          [this] { viewport_->deleteSelection(); });
    edit->addAction(remove);
    auto *selectAll = action("edit.selectAll", "Select all eligible geometry",
                             QKeySequence::SelectAll, [this] { viewport_->selectAll(); });
    selectAll->setShortcutContext(Qt::WidgetShortcut);
    removeAction(selectAll);
    viewport_->addAction(selectAll);
    edit->addAction(selectAll);
    edit->addAction(action("selection.hide", "Hide selection in this view", {},
                           [this] { viewport_->hideSelection(); }));
    edit->addAction(action("selection.reveal", "Reveal hidden geometry in this view", {},
                           [this] { viewport_->revealHiddenGeometry(); }));
    edit->addAction(action("geometry.diagnose", "Geometry diagnostics…", {},
                           [this] { showGeometryDiagnostics(); }));
    auto *edgeAppearance = edit->addMenu("Edge appearance");
    const std::array<std::tuple<const char *, const char *, const char *, bool>, 6> edgeActions{
        {{"edge.hide", "Hide selected edges", "hidden", true},
         {"edge.reveal", "Reveal selected edges", "hidden", false},
         {"edge.soften", "Soften selected edges", "soft", true},
         {"edge.harden", "Harden selected edges", "soft", false},
         {"edge.smooth", "Smooth across selected edges", "smooth", true},
         {"edge.flat", "Use flat shading across selected edges", "smooth", false}}};
    for (const auto &[name, label, flag, value] : edgeActions) {
        auto *entry = action(name, label, {}, [this, flag, value] {
            viewport_->setSelectedEdgeAppearance(flag, value);
        });
        entry->setProperty("command", "geometry.edge_appearance");
        entry->setProperty("requiresSelection", true);
        edgeAppearance->addAction(entry);
    }
    edit->addAction(action("selection.lock", "Lock selected contexts in this view", {},
                           [this] { viewport_->lockSelection(); }));
    edit->addAction(action("selection.unlock", "Unlock all contexts in this view", {},
                           [this] { viewport_->unlockContexts(); }));
    edit->addAction(action("group.selection", "Make group", QKeySequence("Ctrl+G"),
                           [this] { viewport_->makeGroup(); }));
    edit->addAction(action("group.explode", "Explode selected groups", QKeySequence("Ctrl+Shift+G"),
                           [this] { viewport_->explodeGroups(); }));
    for (const auto &name : {"group.selection", "group.explode"}) {
        auto *groupAction = findChild<QAction *>(name);
        groupAction->setShortcutContext(Qt::WidgetShortcut);
        removeAction(groupAction);
        viewport_->addAction(groupAction);
        groupAction->setProperty("command", name);
        groupAction->setProperty("requiresSelection", true);
    }
    edit->addAction(action("geometry.merge_context", "Merge raw geometry in current context", {},
                           [this] { viewport_->mergeContextGeometry(); }));
    addComponentActions(edit);
    edit->addAction(action("group.hide", "Hide selected groups in document", {},
                           [this] { viewport_->setPersistentState(true, false); }));
    edit->addAction(action("group.lock", "Lock selected groups in document", {},
                           [this] { viewport_->setPersistentState(false, true); }));
    edit->addAction(action("group.reveal", "Reveal all document groups and contexts", {},
                           [this] { viewport_->revealPersistentEntities(); }));
    edit->addAction(action("group.unlock", "Unlock all document groups and contexts", {},
                           [this] { viewport_->unlockPersistentEntities(); }));
    for (const auto &name : {"group.hide", "group.lock", "group.reveal", "group.unlock"})
        findChild<QAction *>(name)->setProperty("command", "scene.state");
    edit->addAction(action("context.enter", "Edit selected context", {}, [this] {
        if (viewport_->selectedBody())
            viewport_->enterContext(viewport_->selectedBody());
    }));
    edit->addAction(action("context.leave", "Close editing context", {},
                           [this] { viewport_->leaveContext(); }));
    edit->addAction(action("edit.repeatPushPull", "Repeat push/pull distance", {},
                           [this] { viewport_->repeatPushPull(); }));
    edit->addAction(
        action("guides.clear", "Delete all guides", {}, [this] { viewport_->clearGuides(); }));
    edit->addAction(action("edit.move", "Move selection", {}, [this] {
        tool(Viewport::Tool::Move, "Choose a pivot and destination · Ctrl: copy");
    }));
    edit->addAction(action("edit.paint", "Set original face color…", {}, [this] {
        auto id = viewport_->selectedBody();
        if (!id)
            return;
        auto c = QColorDialog::getColor(Qt::white, this, "Material color");
        if (c.isValid()) {
            viewport_->paintSelection({float(c.redF()), float(c.greenF()), float(c.blueF())});
            sync();
        }
    }));
    auto *draw = menuBar()->addMenu("&Draw");
    auto *group = new QActionGroup(this);
    auto addTool = [&](const QString &name, const QString &key, Viewport::Tool mode,
                       const QString &instruction, bool inRail = true) {
        auto *a = action("tool." + QString::number(int(mode)), name, QKeySequence(key),
                         [this, mode, instruction] { tool(mode, instruction); });
        a->setCheckable(true);
        group->addAction(a);
        if (inRail)
            tools->addAction(a);
        draw->addAction(a);
        return a;
    };
    addTool("Select", "Space", Viewport::Tool::Select,
            "Click an entity · Ctrl adds · Shift toggles · Drag to select a window")
        ->setChecked(true);
    addTool("Paint", "B", Viewport::Tool::Paint,
            "Choose a material · Click a face · Alt-click samples the visible side");
    addTool("Line", "L", Viewport::Tool::Line, "Click first point · Click or drag to endpoint");
    addTool("Rectangle", "R", Viewport::Tool::Rectangle,
            "Click first corner · Then click opposite corner or enter width, depth");
    addTool("Circle", "C", Viewport::Tool::Circle,
            "Choose center and radius · Type 24s to set segments");
    addTool("Push/pull", "P", Viewport::Tool::Extrude,
            "Select a face · Drag or type distance · Ctrl: new face · Double-click: repeat");
    addTool("Offset", "F", Viewport::Tool::Offset,
            "Select a face · Preview or type distance · Positive: outward · Negative: inward");
    addTool(
        "Follow Me", "Shift+F", Viewport::Tool::Sweep,
        "Select one profile face and a connected edge path · Enter or click applies · Esc cancels");
    addTool("Intersect", "I", Viewport::Tool::Intersect,
            "Select target faces · Choose reference scope in Draw · Enter or click applies · Esc "
            "cancels");
    addTool("Solid tools", "Shift+B", Viewport::Tool::Boolean,
            "Select two solids or their faces · Choose operation and originals in Draw")
        ->setIconText("Solids");
    auto *booleans = draw->addMenu("Solid operation options");
    auto *booleanGroup = new QActionGroup(this);
    for (const auto &[operation, label] :
         {std::pair{QString("union"), QString("Union")},
          std::pair{QString("subtract"), QString("Subtract tool from target")},
          std::pair{QString("intersection"), QString("Intersection")},
          std::pair{QString("trim"), QString("Trim target (retain tool)")},
          std::pair{QString("split"), QString("Split into target, tool and overlap")},
          std::pair{QString("outer_shell"), QString("Outer shell (fill enclosed cavities)")}}) {
        auto *choice = action("boolean.operation." + operation, label, {}, [this, operation] {
            findChild<QAction *>("boolean.keep")
                ->setText(operation == "trim" ? "Keep target (tool always retained)"
                                              : "Keep originals");
            viewport_->setBooleanOperation(operation);
        });
        choice->setCheckable(true);
        choice->setChecked(operation == "union");
        booleanGroup->addAction(choice);
        booleans->addAction(choice);
    }
    auto *keepOriginals = action("boolean.keep", "Keep originals", {}, [this] {
        viewport_->setBooleanKeepOperands(findChild<QAction *>("boolean.keep")->isChecked());
    });
    keepOriginals->setCheckable(true);
    keepOriginals->setChecked(true);
    booleans->addAction(keepOriginals);
    booleans->addAction(action("boolean.swap", "Swap target and tool", {},
                               [this] { viewport_->swapBooleanOperands(); }));
    addTool("Face orientation", "Shift+O", Viewport::Tool::Orientation,
            "Select faces to reverse, or one reference face to orient its connected surface")
        ->setIconText("Orient");
    addTool("Attach component to face", "Shift+H", Viewport::Tool::HostedPlacement,
            "Select one component and one host face · Move to preview · Enter applies", false)
        ->setProperty("command", "component.attach");
    auto *orientation = draw->addMenu("Face orientation options");
    auto *orientationGroup = new QActionGroup(this);
    for (bool connected : {false, true}) {
        auto *choice = action(connected ? "orientation.connected" : "orientation.reverse",
                              connected ? "Orient connected faces to selected reference"
                                        : "Reverse selected faces",
                              {}, [this, connected] { viewport_->setOrientationMode(connected); });
        choice->setCheckable(true);
        choice->setChecked(!connected);
        orientationGroup->addAction(choice);
        orientation->addAction(choice);
    }
    auto *references = draw->addMenu("Intersection references");
    auto *referenceGroup = new QActionGroup(this);
    for (const auto &[mode, label] : {std::pair{QString("selected"), QString("Selected faces")},
                                      std::pair{QString("context"), QString("Active context")},
                                      std::pair{QString("model"), QString("Model")}}) {
        auto *choice = action("intersection.mode." + mode, label, {},
                              [this, mode] { viewport_->setIntersectionMode(mode); });
        choice->setCheckable(true);
        choice->setChecked(mode == "selected");
        referenceGroup->addAction(choice);
        references->addAction(choice);
    }
    addTool("Move", "M", Viewport::Tool::Move,
            "Select geometry · Choose pivot and destination · Type displacement · Ctrl: copy");
    addTool("Rotate", "Q", Viewport::Tool::Rotate,
            "Choose pivot and baseline, then angle · Type degrees · Ctrl: copy");
    addTool(
        "Scale", "S", Viewport::Tool::Scale,
        "Choose pivot and reference, then destination · Type one or three factors · Ctrl: copy");
    auto *copyTransform = action("transform.copy", "Copy when transforming", {}, [this] {
        viewport_->setTransformCopy(findChild<QAction *>("transform.copy")->isChecked());
    });
    copyTransform->setCheckable(true);
    edit->addAction(copyTransform);
    auto *localTransform = action("transform.local", "Use local transform axes", {}, [this] {
        viewport_->setTransformLocal(findChild<QAction *>("transform.local")->isChecked());
    });
    localTransform->setCheckable(true);
    edit->addAction(localTransform);
    connect(viewport_, &Viewport::transformOptionsChanged, this,
            [this, copyTransform, localTransform] {
                copyTransform->setChecked(viewport_->transformCopy());
                localTransform->setChecked(viewport_->transformLocal());
            });
    auto *flip = edit->addMenu("Flip about selection center");
    for (int axis = 0; axis < 3; ++axis)
        flip->addAction(action("transform.flip." + QString::number(axis),
                               QString("%1 axis").arg(QString("XYZ")[axis]), {},
                               [this, axis] { viewport_->flipSelection(axis); }));
    addTool("Regular polygon", "", Viewport::Tool::Polygon,
            "Choose center and radius · Type 6s to set sides", false);
    addTool("Freehand", "", Viewport::Tool::Freehand, "Press and draw a stroke · Release to commit",
            false);
    addTool("Rotated rectangle", "", Viewport::Tool::RotatedRectangle,
            "Choose first corner, baseline endpoint and height", false);
    addTool("Center arc", "A", Viewport::Tool::CenterArc,
            "Choose center, start and end direction · Or enter radius, angle · 24s sets segments",
            false);
    addTool("Two-point arc", "", Viewport::Tool::TwoPointArc,
            "Choose endpoints, then bulge point · Or enter a signed bulge · 24s sets segments",
            false);
    addTool("Three-point arc", "", Viewport::Tool::ThreePointArc,
            "Choose start, through and end points · 24s sets segments", false);
    addTool("Pie", "", Viewport::Tool::Pie,
            "Choose center, start and end direction · Or enter radius, angle · 24s sets segments",
            false);
    addTool("Tape measure", "T", Viewport::Tool::Tape,
            "Choose an edge for an offset guide, or two points for distance · Ctrl: measure only");
    addTool("Protractor", "", Viewport::Tool::Protractor,
            "Choose center, baseline and angle · Ctrl: measure only", false);
    auto *createGuides = action("guides.create", "Create guides when measuring", {}, [this] {
        viewport_->setGuideCreation(findChild<QAction *>("guides.create")->isChecked());
    });
    createGuides->setCheckable(true);
    createGuides->setChecked(true);
    draw->addAction(createGuides);
    connect(viewport_, &Viewport::guideCreationChanged, createGuides, &QAction::setChecked);
    auto *planes = draw->addMenu("Drawing plane");
    planes->addAction(action("drawing.plane.auto", "Automatic from hovered face", {},
                             [this] { viewport_->setDrawingPlane(std::nullopt); }));
    planes->addAction(action("drawing.plane.ground", "Ground (Z=0)", {},
                             [this] { viewport_->setDrawingPlane(DrawingPlane{}); }));
    planes->addAction(action("drawing.plane.selected", "Use selected face", {},
                             [this] { viewport_->useSelectedFacePlane(); }));
    planes->addAction(action("drawing.plane.custom", "Custom plane…", {}, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle("Drawing plane");
        dialog.setObjectName("drawingPlaneDialog");
        QFormLayout form(&dialog);
        const auto separator = QLocale().decimalPoint() == "," ? ";" : ",";
        auto *origin = new QLineEdit(QStringList{"0", "0", "0"}.join(separator));
        auto *normal = new QLineEdit(QStringList{"0", "0", "1"}.join(separator));
        auto *axis = new QLineEdit(QStringList{"1", "0", "0"}.join(separator));
        origin->setAccessibleName("Plane origin");
        normal->setAccessibleName("Plane normal direction");
        axis->setAccessibleName("Plane horizontal direction");
        form.addRow("Origin (x, y, z)", origin);
        form.addRow("Normal direction", normal);
        form.addRow("Horizontal direction", axis);
        auto *error = new QLabel;
        error->setWordWrap(true);
        form.addRow(error);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            try {
                auto vector = [&](const QString &text, bool length = false) {
                    const auto input = parseMeasurements(
                        text, length ? inputUnit(doc_.displayUnits()) : "m", QLocale());
                    if (input.kind != MeasurementKind::Values || input.values.size() != 3)
                        throw std::runtime_error("Enter three values for each plane vector");
                    return Vec3{input.values[0], input.values[1], input.values[2]};
                };
                viewport_->setDrawingPlane(DrawingPlane::make(
                    vector(origin->text(), true), vector(normal->text()), vector(axis->text())));
                dialog.accept();
            } catch (const std::exception &failure) {
                error->setText(failure.what());
            }
        });
        dialog.exec();
    }));
    auto *newFace =
        action("draw.pushPullNewFace", "Create new face when pushing/pulling", {}, [this] {
            viewport_->setPushPullNewFace(
                findChild<QAction *>("draw.pushPullNewFace")->isChecked());
        });
    newFace->setCheckable(true);
    tools->addAction(newFace);
    connect(viewport_, &Viewport::pushPullModeChanged, newFace, &QAction::setChecked);
    tools->addSeparator();
    addTool("Orbit", "O", Viewport::Tool::Orbit, "Drag to orbit · Shift-drag to pan");
    addTool("Pan", "H", Viewport::Tool::Pan, "Drag to pan");
    addTool("Zoom", "Z", Viewport::Tool::Zoom, "Drag up to zoom in · Drag down to zoom out", false);
    auto *cameraMenu = menuBar()->addMenu("&Camera");
    cameraMenu->addAction(action("camera.render", "Render…", {}, [this] { render_->showSetup(); }));
    auto *view = menuBar()->addMenu("&View");
    auto *hidden = action("selection.showHidden", "Show hidden geometry", {}, [this] {
        viewport_->showHiddenGeometry(findChild<QAction *>("selection.showHidden")->isChecked());
    });
    hidden->setCheckable(true);
    view->addAction(hidden);
    auto *showGuides = action("guides.visible", "Show guides", {}, [this] {
        viewport_->setGuidesVisible(findChild<QAction *>("guides.visible")->isChecked());
    });
    showGuides->setCheckable(true);
    showGuides->setChecked(true);
    view->addAction(showGuides);
    view->addAction(
        action("view.fit", "Fit model", QKeySequence("Shift+Z"), [this] { viewport_->fit(); }));
    view->addAction(action("view.perspective", "Perspective", QKeySequence("1"),
                           [this] { viewport_->setOrthographic(false); }));
    view->addAction(
        action("view.top", "Top", QKeySequence("2"), [this] { viewport_->standardView(1); }));
    view->addAction(
        action("view.front", "Front", QKeySequence("3"), [this] { viewport_->standardView(2); }));
    view->addAction(action("view.orthographic", "Orthographic", {},
                           [this] { viewport_->setOrthographic(true); }));
    for (const auto &[id, label, preset] : {std::tuple{"view.right", "Right", 3},
                                            {"view.back", "Back", 4},
                                            {"view.left", "Left", 5},
                                            {"view.bottom", "Bottom", 6},
                                            {"view.isometric", "Isometric", 7}})
        view->addAction(action(id, label, {}, [this, preset] { viewport_->standardView(preset); }));
    view->addAction(action("view.fov", "Field of view…", {}, [this] {
        bool ok = false;
        const auto degrees =
            QInputDialog::getDouble(this, "Field of view", "Vertical angle in degrees",
                                    viewport_->fieldOfView(), 5, 120, 1, &ok);
        if (ok) {
            viewport_->setFieldOfView(degrees);
            QSettings("SketchyUp", "SketchyUp").setValue("fieldOfView", degrees);
        }
    }));
    auto *projection = new QActionGroup(this);
    for (const auto *id : {"view.perspective", "view.orthographic"}) {
        auto *item = findChild<QAction *>(id);
        item->setCheckable(true);
        projection->addAction(item);
    }
    connect(viewport_, &Viewport::navigationChanged, this, [this] {
        findChild<QAction *>("view.perspective")->setChecked(!viewport_->orthographic());
        findChild<QAction *>("view.orthographic")->setChecked(viewport_->orthographic());
    });
    auto *navigation = view->addMenu("Navigation");
    auto *navigationGroup = new QActionGroup(this);
    for (const bool trackpad : {false, true}) {
        auto *item =
            action(trackpad ? "view.trackpad" : "view.mouse", trackpad ? "Trackpad" : "Mouse", {},
                   [this, trackpad] {
                       viewport_->setTrackpadNavigation(trackpad);
                       QSettings("SketchyUp", "SketchyUp").setValue("trackpadNavigation", trackpad);
                   });
        item->setCheckable(true);
        navigationGroup->addAction(item);
        navigation->addAction(item);
    }
    const QSettings navigationSettings("SketchyUp", "SketchyUp");
    const bool trackpad = navigationSettings.value("trackpadNavigation", false).toBool();
    findChild<QAction *>(trackpad ? "view.trackpad" : "view.mouse")->setChecked(true);
    viewport_->setTrackpadNavigation(trackpad);
    const auto fov = navigationSettings.value("fieldOfView", 45).toDouble();
    viewport_->setFieldOfView(std::isfinite(fov) && fov >= 5 && fov <= 120 ? fov : 45);
    view->addAction(action("view.history", "History", {}, [this] {
        tray_->show();
        findChild<QAction *>("view.tray")->setChecked(true);
        organization_->showHistory();
    }));
    view->addAction(action("view.scenes", "Saved scenes", {}, [this] {
        sideTabs_->show();
        sideTabs_->setCurrentWidget(tray_);
        tray_->show();
        findChild<QAction *>("view.tray")->setChecked(true);
        organization_->showScenes();
    }));
    view->addAction(action("view.sections", "Section planes", {}, [this] {
        sideTabs_->show();
        sideTabs_->setCurrentWidget(tray_);
        tray_->show();
        findChild<QAction *>("view.tray")->setChecked(true);
        organization_->showSections();
    }));
    view->addAction(action("view.annotations", "Dimensions and labels", {}, [this] {
        sideTabs_->show();
        sideTabs_->setCurrentWidget(tray_);
        tray_->show();
        findChild<QAction *>("view.tray")->setChecked(true);
        organization_->showAnnotations();
    }));
    view->addAction(action("view.text", "3D text", {}, [this] {
        sideTabs_->show();
        sideTabs_->setCurrentWidget(tray_);
        tray_->show();
        findChild<QAction *>("view.tray")->setChecked(true);
        organization_->showText();
    }));
    view->addAction(action("view.solar", "Sun and shadows", {}, [this] {
        sideTabs_->show();
        sideTabs_->setCurrentWidget(tray_);
        tray_->show();
        findChild<QAction *>("view.tray")->setChecked(true);
        organization_->showSolar();
    }));
    view->addAction(action("view.reference_images", "Reference images", {}, [this] {
        sideTabs_->show();
        sideTabs_->setCurrentWidget(tray_);
        tray_->show();
        findChild<QAction *>("view.tray")->setChecked(true);
        organization_->showReferenceImages();
    }));
    auto *reducedMotion = action("view.reduced_motion", "Reduced camera motion", {}, [this] {
        const auto enabled = findChild<QAction *>("view.reduced_motion")->isChecked();
        QSettings().setValue("reducedMotion", enabled);
        viewport_->setReducedMotion(enabled);
    });
    reducedMotion->setCheckable(true);
    reducedMotion->setChecked(navigationSettings.value("reducedMotion", false).toBool());
    viewport_->setReducedMotion(reducedMotion->isChecked());
    view->addAction(reducedMotion);
    view->addAction(action("view.styles", "Model styles", {}, [this] {
        sideTabs_->show();
        sideTabs_->setCurrentWidget(tray_);
        tray_->show();
        findChild<QAction *>("view.tray")->setChecked(true);
        organization_->showStyles();
    }));
    view->addAction(action("view.assistant", "Assistant", QKeySequence("Ctrl+J"),
                           [this] { toggleAssistant(); }));
    auto *panel = action("view.tray", "Model panel", QKeySequence("Ctrl+Shift+T"), [this] {
        sideTabs_->setVisible(findChild<QAction *>("view.tray")->isChecked());
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
    focusRegions_ = {search, tools, viewport_, organization_, assistant_, measurements_};
    for (const auto &binding :
         std::vector<std::pair<QString, QString>>{{"edit.move", "geometry.transform_selection"},
                                                  {"edit.paint", "material.color"},
                                                  {"edit.delete", "geometry.erase_selection"}}) {
        auto *entry = findChild<QAction *>(binding.first);
        entry->setProperty("command", binding.second);
        entry->setProperty("requiresSelection", true);
    }
    for (const auto &name : {"edit.move", "tool.17", "tool.18", "tool.19", "transform.flip.0",
                             "transform.flip.1", "transform.flip.2"}) {
        auto *entry = findChild<QAction *>(name);
        entry->setProperty("command", "geometry.transform_selection");
        entry->setProperty("form", QStringList{"pivot", "space", "copy", "measurements"});
    }
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
    connect(viewport_, &Viewport::toolChanged, this, [this](int mode) {
        measurements_->setEnabled(mode != int(Viewport::Tool::Paint));
        if (mode == int(Viewport::Tool::Paint))
            organization_->showMaterials();
        if (auto *toolAction = findChild<QAction *>("tool." + QString::number(mode)))
            toolAction->setChecked(true);
    });
    connect(viewport_, &Viewport::changed, this, &Window::sync);
    connect(viewport_, &Viewport::message, status_, &QLabel::setText);
    connect(viewport_, &Viewport::selected, this, [this](qulonglong, qulonglong) { sync(); });
    connect(viewport_, &Viewport::measurementsRequested, this, [this](const QString &text) {
        measurements_->setFocus();
        measurements_->setText(text);
        measurements_->setCursorPosition(text.size());
    });
    connect(viewport_, &Viewport::measurementPreview, this, [this](const QString &text) {
        if (!measurements_->hasFocus())
            measurements_->setText(text);
    });
    connect(measurements_, &QLineEdit::returnPressed, this, [this] {
        if (assistant_ && assistant_->uncertain())
            return;
        if (viewport_->measurements(measurements_->text())) {
            measurementError(false);
            measurements_->clear();
            viewport_->setFocus();
        } else {
            measurementError(true);
            measurements_->selectAll();
        }
    });
    connect(measurements_, &QLineEdit::textEdited, this, [this] { measurementError(false); });
    connect(assistant_, &AssistantPanel::modelChanged, this, &Window::sync);
    connect(assistant_, &AssistantPanel::fenceChanged, this, &Window::assistantFence);
    connect(assistant_, &AssistantPanel::focusViewport, this, [this] {
        if (assistantSheet_ && assistantSheet_->isVisible())
            assistantSheet_->reject();
        activateWindow();
        viewport_->setFocus();
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
QLineEdit[invalid="true"] {border:2px solid #bc4343;}
QListWidget,QTreeWidget {border:0;background:transparent;}
QTreeWidget:focus {border:1px solid $accent;} QTreeWidget::item {padding:3px 1px;}
QTreeWidget::item:selected {background:$selected;color:$ink;}
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
    auto linkPalette = breadcrumb_->palette();
    linkPalette.setColor(QPalette::Link, colors.accent);
    breadcrumb_->setPalette(linkPalette);
}
void Window::run(const std::function<void()> &fn, bool viewOnly) {
    if (!viewOnly && assistant_ && assistant_->uncertain()) {
        status_->setText("Reconcile the assistant outcome before editing this model.");
        return;
    }
    try {
        fn();
    } catch (const std::exception &e) {
        status_->setText(e.what());
        QMessageBox::warning(this, "Could not complete edit", e.what());
    }
}
void Window::measurementError(bool invalid) {
    measurements_->setProperty("invalid", invalid);
    measurements_->setAccessibleDescription(invalid ? status_->text() : QString{});
    measurements_->style()->unpolish(measurements_);
    measurements_->style()->polish(measurements_);
}
bool Window::eventFilter(QObject *object, QEvent *event) {
    if (object == measurements_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        measurements_->clear();
        measurementError(false);
        viewport_->setFocus();
        return true;
    }
    return QMainWindow::eventFilter(object, event);
}
void Window::tool(Viewport::Tool t, const QString &text) {
    status_->setText(text);
    if (t == Viewport::Tool::HostedPlacement)
        viewport_->startHostedPlacement();
    else
        viewport_->setTool(t);
    viewport_->setFocus();
    measurements_->setPlaceholderText(
        t == Viewport::Tool::Paint             ? "Choose a material and side"
        : t == Viewport::Tool::Move            ? "distance, dx,dy,dz or [x,y,z]"
        : t == Viewport::Tool::Rotate          ? "angle (deg) or [pivot / reference]"
        : t == Viewport::Tool::Scale           ? "factor or x,y,z factors"
        : t == Viewport::Tool::Tape            ? "distance or [x,y,z]"
        : t == Viewport::Tool::Protractor      ? "angle (deg) or [x,y,z]"
        : t == Viewport::Tool::Extrude         ? "distance"
        : t == Viewport::Tool::Offset          ? "signed offset distance (+ outward, - inward)"
        : t == Viewport::Tool::Sweep           ? "Enter applies the preview · Esc cancels"
        : t == Viewport::Tool::Boolean         ? "Enter applies the preview · Esc cancels"
        : t == Viewport::Tool::HostedPlacement ? "Host x,y,z anchor or inset length"
        : t == Viewport::Tool::Orientation     ? "Enter applies the preview · Esc cancels"
        : t == Viewport::Tool::Intersect       ? "Enter applies the preview · Esc cancels"
        : t == Viewport::Tool::Circle          ? "radius or 24s"
        : t == Viewport::Tool::CenterArc || t == Viewport::Tool::Pie ? "radius, angle (deg) or 24s"
        : t == Viewport::Tool::TwoPointArc                           ? "signed bulge or 24s"
        : t == Viewport::Tool::ThreePointArc                         ? "[x,y,z] or 24s"
                                                                     : "width, depth");
}
void Window::sync() {
    if (assistant_)
        assistant_->refresh();
    if (render_)
        render_->refreshProvenance();
    measurementUnits_->setText("Measurements · " +
                               QString::fromLatin1(unitCode(doc_.displayUnits()).data()));
    measurements_->setAccessibleName("Measurements in " + unitName(doc_.displayUnits()).toLower());
    viewport_->refresh();
    title_->setText(
        (path_.isEmpty() ? (recoveredName_.isEmpty() ? QString("Untitled") : recoveredName_)
                         : QFileInfo(path_).fileName()) +
        (!saveFailure_.isEmpty()
             ? "  ⚠  Not saved"
             : (path_.isEmpty() ? (recoveredName_.isEmpty() ? "  ·  Not saved" : "  •  Edited")
                                : (doc_.dirty() ? "  •  Edited" : "  ·  Saved"))));
    syncRecovery();
    undo_->setEnabled(doc_.canUndo());
    redo_->setEnabled(doc_.canRedo());
    const auto cursor = doc_.history(0, 1).position;
    auto historyAction = [&](QAction *action, const QString &verb, bool enabled, size_t offset) {
        const auto label =
            enabled ? QString::fromStdString(doc_.history(offset, 1).entries.front().label)
                    : QString{};
        const auto text = label.isEmpty() ? verb : verb + " " + label;
        action->setToolTip(text);
        action->setText(fontMetrics().elidedText(text, Qt::ElideRight, 320).replace("&", "&&"));
    };
    historyAction(undo_, "Undo", doc_.canUndo(), cursor ? cursor - 1 : 0);
    historyAction(redo_, "Redo", doc_.canRedo(), cursor);

    for (const auto &id : {"edit.paint", "context.enter"})
        findChild<QAction *>(id)->setEnabled(doc_.bodies().contains(viewport_->selectedBody()));
    for (const auto &id : {"edit.move", "edit.delete", "selection.hide", "selection.lock"})
        findChild<QAction *>(id)->setEnabled(!viewport_->selectionState().entities().empty());
    findChild<QAction *>("edit.repeatPushPull")->setEnabled(viewport_->canRepeatPushPull());
    findChild<QAction *>("context.leave")->setEnabled(viewport_->selectionState().context() != 0);
    findChild<QAction *>("selection.showHidden")
        ->setChecked(viewport_->selectionState().showingHidden());
    const auto &selection = viewport_->selectionState().entities();
    const bool selectedEdges = !selection.empty() && selection.size() <= 4096 &&
                               std::all_of(selection.begin(), selection.end(), [](auto entity) {
                                   return entity.kind == SelectionKind::Edge;
                               });
    for (const auto *name :
         {"edge.hide", "edge.reveal", "edge.soften", "edge.harden", "edge.smooth", "edge.flat"})
        findChild<QAction *>(name)->setEnabled(selectedEdges);
    findChild<QAction *>("geometry.diagnose")
        ->setEnabled(viewport_->selectedBody() || viewport_->selectionState().context());
    findChild<QAction *>("group.selection")->setEnabled(!selection.empty());
    const bool whole =
        !selection.empty() && std::all_of(selection.begin(), selection.end(),
                                          [](auto e) { return e.kind == SelectionKind::Body; });
    for (const auto &action : {"group.hide", "group.lock"})
        findChild<QAction *>(action)->setEnabled(whole);
    findChild<QAction *>("group.explode")
        ->setEnabled(whole && std::all_of(selection.begin(), selection.end(), [&](auto e) {
                         return doc_.bodies().at(e.body)->kind == BodyKind::Group;
                     }));
    std::vector<Id> path;
    for (auto context = viewport_->selectionState().context(); context;
         context = doc_.bodies().at(context)->parent)
        path.push_back(context);
    QString breadcrumb = "<a href=\"0\">Model</a>";
    for (auto it = path.rbegin(); it != path.rend(); ++it)
        breadcrumb += QString(" &rsaquo; <a href=\"%1\">%2</a>")
                          .arg(*it)
                          .arg(QString::fromStdString(doc_.bodies().at(*it)->name).toHtmlEscaped());
    breadcrumb_->setText(breadcrumb);
    breadcrumb_->setToolTip(path.empty() ? "Editing the model"
                                         : "Click an ancestor to close nested contexts");
    breadcrumb_->setMaximumWidth(std::max(100, viewport_->width() - 32));
    breadcrumb_->adjustSize();
    breadcrumb_->raise();
    syncComponentActions();
    organization_->refresh();
    QString text = viewport_->selectionSummary();
    if (viewport_->selectionState().context())
        text += QString("\nEditing context #%1").arg(viewport_->selectionState().context());
    const auto it = doc_.bodies().find(viewport_->selectedBody());
    if (it != doc_.bodies().end()) {
        const auto &body = *it->second;
        text += "\n" + QString::fromStdString(body.name);
        if (body.surface.faces.contains(viewport_->selectedFace()))
            text +=
                "\nFace area " + displayMeasure(doc_.worldArea(body.id, viewport_->selectedFace()),
                                                2, doc_.displayUnits());
    }
    info_->setText(text);
    if (assistantFenced_) {
        for (auto &[action, wasEnabled] : assistantActionStates_)
            action->setEnabled(false);
        measurements_->setEnabled(false);
    }
}
bool Window::save(bool saveAs) {
    if (assistant_ && assistant_->uncertain()) {
        status_->setText("Reconcile the assistant outcome before saving.");
        return false;
    }
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
        recoveryContext_ = {QFileInfo(target).absoluteFilePath(), doc_.revision(),
                            QDateTime::currentDateTimeUtc()};
        recoveredName_.clear();
        saveFailure_.clear();
        saveBanner_->hide();
        clearRecovery();
        rememberPath(target);
        sync();
        status_->setText("Saved locally");
        return true;
    } catch (const std::exception &e) {
        saveFailure_ = QString::fromUtf8(e.what());
        saveBannerText_->setText("Not saved: " + saveFailure_);
        saveBanner_->show();
        sync();
        QMessageBox::warning(this, "Save failed", e.what());
        status_->setText("Save failed. Edits remain in memory.");
        return false;
    }
}
bool Window::canReplace() {
    if (assistant_ && assistant_->uncertain()) {
        status_->setText("Reconcile the assistant outcome before replacing or closing this model.");
        return false;
    }
    if (doc_.dirty()) {
        const auto choice = QMessageBox::warning(
            this, "Unsaved changes", "Save this model before continuing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if ((choice != QMessageBox::Save && choice != QMessageBox::Discard) ||
            (choice == QMessageBox::Save && !save()))
            return false;
    }
    try {
        if (assistant_)
            assistant_->closeSession();
    } catch (const std::exception &e) {
        status_->setText(e.what());
        return false;
    }
    clearRecovery();
    return true;
}
void Window::openPath(const QString &path) {
    // Validate before asking to discard the current document.
    auto loaded = loadDocument(path);
    if (!canReplace())
        return;
    doc_ = std::move(loaded);
    resetRecoveryContext();
    recoveryContext_ = {QFileInfo(path).absoluteFilePath(), doc_.revision(),
                        QFileInfo(path).lastModified().toUTC()};
    path_ = path;
    rememberPath(path);
    viewport_->cancel();
    viewport_->setSelection(0);
    viewport_->fit();
    sync();
}
void Window::closeEvent(QCloseEvent *e) {
    if (closePending_ || !canReplace()) {
        e->ignore();
        return;
    }
#if QT_CONFIG(wayland)
    if (auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
        closePending_ = true;
        // Drain queued keyboard enter/leave events before destroying the main
        // wl_surface. Keep the original close event pending: Qt captured its
        // visible state before entering this handler and must use that event
        // for normal lastWindowClosed/quitOnLastWindowClosed behavior.
        const auto snapshot = doc_.saveStamp();
        QPointer<Window> alive(this);
        QEventLoop unmap;
        bool acknowledged = false;
        connect(this, &QObject::destroyed, &unmap, &QEventLoop::quit);
        QPointer<WindowUnmapBarrier> barrier =
            new WindowUnmapBarrier(*this, native->display(), [&] {
                acknowledged = true;
                unmap.quit();
            });
        unmap.exec();
        // Application termination may also exit the nested loop. Never leave
        // a callback holding its stack references after the close handler.
        delete barrier.data();
        if (!alive) {
            e->ignore();
            return;
        }
        closePending_ = false;
        if (!acknowledged || !doc_.isCurrentSnapshot(snapshot)) {
            show();
            status_->setText("Model changed while closing. Review it before closing again.");
            e->ignore();
            return;
        }
    }
#endif
    e->accept();
}
void Window::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (sideTabs_)
        layoutAssistant();
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
            if (viewport_->selectionState().selectable(doc_, {id, SelectionKind::Body, 0}) &&
                name.contains(query->text(), Qt::CaseInsensitive))
                add("Select object: " + name, "entity", QVariant::fromValue<qulonglong>(id));
        }
        const auto faceQuery =
            QRegularExpression("^face ([1-9][0-9]*)/([1-9][0-9]*)$").match(query->text().trimmed());
        if (faceQuery.hasMatch()) {
            bool bodyOk = false, faceOk = false;
            const auto body = faceQuery.captured(1).toULongLong(&bodyOk);
            const auto face = faceQuery.captured(2).toULongLong(&faceOk);
            if (bodyOk && faceOk && doc_.bodies().contains(body) &&
                viewport_->selectionState().selectable(doc_, {body, SelectionKind::Face, face}))
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
    resetRecoveryContext();
    path_.clear();
    viewport_->cancel();
    viewport_->setSelection(0);
    viewport_->fit();
    sync();
    status_->setText("Example courtyard · Original editable face geometry");
}
} // namespace sketchy
