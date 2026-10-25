#include "app/terrain_replay_job.hpp"
#include "app/window.hpp"
#include "app/accessible_names.hpp"
#include "app/component_drivers_section.hpp"
#include "app/component_parameters_section.hpp"
#include "app/components_tab.hpp"
#include "app/context_label.hpp"
#include "app/file_operation.hpp"
#include "app/focus_ring.hpp"
#include "app/inspection_service.hpp"
#include "app/interface_preferences.hpp"
#include "app/keyboard_navigation.hpp"
#include "app/keypad_decimal.hpp"
#include "app/render_panel.hpp"
#include "app/settled_dialog.hpp"
#include "app/sheet_evaluation.hpp"
#include "app/sheet_export_dialog.hpp"
#include "app/terrain_import_dialog.hpp"
#include "app/terrain_tool_strip.hpp"
#include "app/sheet_workspace.hpp"
#include "app/sheets_tray.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QWidgetAction>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <map>
#include <tuple>
#include <wayland-client.h>
namespace sketchy {
namespace {
class ElidedLabel final : public QLabel {
  public:
    explicit ElidedLabel(const QString &text = {}) : QLabel(text) {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(contentsRect(), alignment(),
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};
void resizeContextLabels(QWidget *window) {
    auto *breadcrumb = window->findChild<QLabel *>("contextBreadcrumb");
    auto *banner = window->findChild<QLabel *>("componentScopeBanner");
    if (breadcrumb && banner) {
        breadcrumb->ensurePolished();
        breadcrumb->setMaximumWidth(std::max(100, breadcrumb->parentWidget()->width() - 32));
        breadcrumb->adjustSize();
        banner->ensurePolished();
        banner->setFixedWidth(
            std::max(100, std::min(640, breadcrumb->parentWidget()->width() - 32)));
        banner->move(16, breadcrumb->geometry().bottom() + 8);
        banner->adjustSize();
    }
    if (auto *hint = window->findChild<QLabel *>("hint")) {
        hint->ensurePolished();
        hint->setMinimumHeight(hint->heightForWidth(hint->width()));
    }
}
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
// R091.h: a modal prompt or the file-operation surface owned by the window hands activation
// back to it when it closes. Without a window manager (X11 under Xvfb) nothing else does: the
// application is left with no active window and no focus widget, so keys go nowhere. The
// window's own focus child (the viewport or the page canvas) gets focus with the activation.
// Another dialog still open (a prompt that follows another, or the window that opened it)
// keeps activation.
class ModalReturn final : public QObject {
  public:
    explicit ModalReturn(QWidget *window) : QObject(window), window_(window) {
        qApp->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() != QEvent::Hide)
            return false;
        auto *dialog = qobject_cast<QDialog *>(object);
        if (!dialog || !dialog->isWindow() || !dialog->parentWidget() ||
            dialog->parentWidget()->window() != window_ ||
            (!dialog->isModal() && dialog->windowModality() == Qt::NonModal))
            return false;
        QTimer::singleShot(0, window_, [window = window_] {
            if (!window->isVisible() || window->isMinimized() ||
                QApplication::activeModalWidget() || QApplication::activePopupWidget())
                return;
            // Another of the application's windows still open (a non-modal sheet such as Render
            // that opened the prompt) keeps activation.
            for (auto *other : QApplication::topLevelWidgets())
                if (other != window && other->isVisible() &&
                    (other->windowType() == Qt::Dialog || other->windowType() == Qt::Window ||
                     other->windowType() == Qt::Sheet))
                    return;
            window->activateWindow();
        });
        return false;
    }

  private:
    QWidget *window_;
};
} // namespace
QAction *Window::action(const QString &id, const QString &title, const QKeySequence &shortcut,
                        const std::function<void()> &fn, std::optional<ShortcutScope> scope) {
    // Model tools and viewport-only keys (plain and Shift-only) belong to the Model workspace;
    // everything else is global unless the caller names a scope (ADR 0164 §4).
    const bool viewportKey =
        !shortcut.isEmpty() &&
        !(shortcut[0].keyboardModifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
        !(shortcut[0].key() >= Qt::Key_F1 && shortcut[0].key() <= Qt::Key_F35);
    const auto resolved = scope.value_or(
        id.startsWith("tool.") || viewportKey ? ShortcutScope::Model : ShortcutScope::Global);
    for (auto *existing : publicActions_) {
        if (existing->objectName() == id ||
            (!shortcut.isEmpty() && existing->shortcut() == shortcut &&
             shortcutScopesOverlap(actionScope(existing), resolved)))
            throw std::logic_error("Duplicate public action identity or shortcut");
    }
    auto *a = new QAction(title, this);
    a->setObjectName(id);
    a->setProperty("shortcutScope", int(resolved));
    a->setProperty("shortcutScopeExplicit", scope.has_value());
    a->setProperty("category", id.section('.', 0, 0));
    a->setShortcut(shortcut);
    a->setProperty("defaultShortcut", QVariant::fromValue(shortcut));
    addAction(a);
    publicActions_.push_back(a);
    connect(a, &QAction::triggered, this, [this, fn, id] { run(fn, id.startsWith("view.")); });
    return a;
}
Window::Window(QWidget *parent, AssistantPanel::HostServices assistantServices)
    : QMainWindow(parent), doc_(preferredUnits(), preferredPrecision()) {
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
    title_ = new ElidedLabel;
    headerLayout->addWidget(title_, 1);
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
    tools->setAccessibleName("Modeling tools");
    tools->setOrientation(Qt::Vertical);
    tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tools->setFixedWidth(88);
    modelRail_ = tools;
    // ADR 0164 §7: stacks are built here, so the GL viewport is never reparented later.
    railStack_ = new QStackedWidget;
    railStack_->setObjectName("toolRailStack");
    railStack_->addWidget(tools);
    content->addWidget(railStack_);
    viewport_ = new Viewport(doc_);
    inspection_ = std::make_unique<DesktopInspection>(*viewport_);
    auto *modelTabs = new QTabWidget;
    modelTabs->setObjectName("modelTabs");
    modelTabs->setAccessibleName("Model and render views");
    modelTabs->setTabBarAutoHide(true);
    modelTabs->setTabsClosable(true);
    modelTabs->addTab(viewport_, "Model");
    modelTabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    modelTabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    modelTabs_ = modelTabs;
    sheetCache_ = std::make_unique<DrawingViewRenderCache>(doc_);
    workspaceStack_ = new QStackedWidget;
    workspaceStack_->setObjectName("workspaceStack");
    workspaceStack_->addWidget(modelTabs);
    sheets_ = new SheetWorkspace(doc_, *sheetCache_);
    workspaceStack_->addWidget(sheets_);
    breadcrumb_ = new QLabel(viewport_);
    breadcrumb_->setObjectName("contextBreadcrumb");
    breadcrumb_->setAccessibleName("Editing context breadcrumb");
    breadcrumb_->setTextFormat(Qt::RichText);
    breadcrumb_->setWordWrap(true);
    breadcrumb_->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                         Qt::LinksAccessibleByKeyboard);
    breadcrumb_->setFocusPolicy(Qt::StrongFocus);
    breadcrumb_->setProperty("focusHalo", true);
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
    componentBanner_->setProperty("focusHalo", true);
    connect(componentBanner_, &QLabel::linkActivated, this, [this](const QString &link) {
        // ADR 0166 §3.1: Close definition (Esc) returns to the placement's own context.
        if (link == "close") {
            const auto scope = viewport_->componentScope();
            run([this, scope] {
                viewport_->enterContext(scope && doc_.bodies().contains(scope)
                                            ? doc_.bodies().at(scope)->parent
                                            : 0);
                viewport_->setFocus();
            });
            return;
        }
        run([this] { viewport_->makeComponentUnique(true); });
    });
    viewport_->setContextOverlay(breadcrumb_, componentBanner_);
    backtabOutOfLinks(breadcrumb_);
    backtabOutOfLinks(componentBanner_);
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
    modelTrayWidgets_ = {heading, info_, organization_, hint};
    sheetsTray_ = new SheetsTray(doc_, *viewport_, *sheetCache_);
    sheetsTray_->hide();
    trayLayout->addWidget(sheetsTray_, 1);
    sideTabs_ = new QTabWidget;
    sideTabs_->setObjectName("assistantSideTabs");
    sideTabs_->setAccessibleName("Model panels and assistant");
    sideTabs_->setTabBarAutoHide(true);
    sideTabs_->addTab(tray_, "Model");
    assistant_ = new AssistantPanel(doc_, *viewport_, std::move(assistantServices), root);
    assistant_->hide();
    // R094.a: the workspace and the side panel share a splitter, built here so the GL viewport
    // is never reparented later.
    setupSidebar(workspaceStack_);
    vertical->addLayout(content, 1);
    auto *bottom = new QWidget;
    bottom->setObjectName("footer");
    auto *bottomLayout = new QHBoxLayout(bottom);
    activeTool_ = new QLabel;
    activeTool_->setObjectName("activeToolLabel");
    activeTool_->setAccessibleName("Active tool");
    activeTool_->hide();
    bottomLayout->addWidget(activeTool_);
    status_ = new ElidedLabel("Select a tool to begin");
    status_->setObjectName("statusMessage");
    status_->setMinimumWidth(80);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bottomLayout->addWidget(status_, 1);
    // R097.e1: the IFC report chip (ADR 0171 §16, the design's mockup `after`): on this row,
    // after the status text, hidden until an import has produced it. A hidden widget takes no
    // space, so the row's height is the row's before any import; the chip never grows the bar.
    ifcReportChip_ = new QPushButton;
    ifcReportChip_->setObjectName("ifcImportChip");
    ifcReportChip_->setCursor(QCursor(Qt::PointingHandCursor));
    ifcReportChip_->setMaximumHeight(24); // the row already holds a text edit of that height; the
                                        // chip, when shown, must never grow the row
    ifcReportChip_->hide();
    QObject::connect(ifcReportChip_, &QPushButton::clicked, this, &Window::showIfcImportReport);
    bottomLayout->addWidget(ifcReportChip_);
    // R098.a3: the STEP report chip, the IFC chip's own for STEP (the same lifetime rules).
    stepReportChip_ = new QPushButton;
    stepReportChip_->setObjectName("stepImportChip");
    stepReportChip_->setCursor(QCursor(Qt::PointingHandCursor));
    stepReportChip_->setMaximumHeight(24);
    stepReportChip_->hide();
    QObject::connect(stepReportChip_, &QPushButton::clicked, this, &Window::showStepImportReport);
    bottomLayout->addWidget(stepReportChip_);
    auto *renderChip = new QPushButton;
    bottomLayout->addWidget(renderChip);
    render_ = new RenderPanel(doc_, *viewport_, *modelTabs, *renderChip, *this);
    measurementUnits_ = new QLabel;
    measurementUnits_->setObjectName("measurementUnits");
    bottomLayout->addWidget(measurementUnits_);
    // The Sheets workspace names the Measurements step ("Size", "Scale"); ADR 0164 §4.
    measurementStep_ = new QLabel;
    measurementStep_->setObjectName("measurementStep");
    measurementStep_->hide();
    bottomLayout->addWidget(measurementStep_);
    measurements_ = new QLineEdit;
    measurements_->setObjectName("measurements");
    measurements_->installEventFilter(this);
    acceptKeypadDecimal(measurements_);
    measurements_->setFixedWidth(150);
    measurements_->setPlaceholderText("width, depth");
    measurements_->setAccessibleName("Measurements");
    measurementStep_->setBuddy(measurements_);
    bottomLayout->addWidget(measurements_);
    vertical->addWidget(bottom);
    setCentralWidget(root);
    auto *file = menuBar()->addMenu("&File");
    file->addAction(action("file.new", "New", QKeySequence::New, [this] {
        if (canReplace()) {
            doc_ = Document(preferredUnits(), preferredPrecision());
            clearIfcReportState(); // a new model: the IFC report chip's lifetime ends (16.1)
            clearStepReportState(); // a new model: the STEP report chip's lifetime ends too
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
    file->addAction(
        action("file.newFromTemplate", "New from template…", {}, [this] { libraryDialog(true); }));
    file->addAction(action("file.saveTemplate", "Save template to library…", {},
                           [this] { saveLibraryDialog(false); }));
    file->addAction(action("file.importDxf", "Import 2D DXF…", {}, [this] { importDxfDialog(); }));
    file->addAction(action("file.exportMeasured", "Export measured PDF/SVG…", {},
                           [this] { exportMeasuredDialog(); }));
    // R092.b: the drawing set. Sheets has its own entries (Sheets scope); Model offers Export
    // sheets… while the document has sheets. Only one of each pair shows at a time.
    file->addAction(action(
        "file.exportSheets", "Export sheets…", {}, [this] { exportSheetsDialog(); },
        ShortcutScope::Model));
    file->addAction(action(
        "sheet.export", "Export sheets…", {}, [this] { exportSheetsDialog(); },
        ShortcutScope::Sheets));
    file->addAction(action(
        "sheet.print", "Print…", {}, [this] { printSheetsDialog(); }, ShortcutScope::Sheets));
    for (const char *id : {"sheet.export", "sheet.print"})
        findChild<QAction *>(id)->setVisible(false); // The Sheets workspace shows them.
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
    // R097.e1: IFC exchange (ADR 0171 §16). Import IFC… after Import GLB/glTF…;
    // Show IFC import report… directly below it (enabled only after an import).
    file->addAction(action("file.importIfc", "Import IFC…", {}, [this] { importIfc(); }));
    ifcReportAction_ = action("file.ifcImportReport", "Show IFC import report…", {},
                               [this] { showIfcImportReport(); });
    ifcReportAction_->setEnabled(false);
    file->addAction(ifcReportAction_);
    // R098.a3: STEP exchange (ADR 0172 section 8). Import STEP… after the IFC pair; Show
    // STEP import report… directly below it (enabled only after a STEP import).
    file->addAction(action("file.importStep", "Import STEP…", {}, [this] { importStep(); }));
    stepReportAction_ = action("file.stepImportReport", "Show STEP import report…", {},
                                 [this] { showStepImportReport(); });
    stepReportAction_->setEnabled(false);
    file->addAction(stepReportAction_);
    file->addAction(action("file.importFormline", "Import Formline…", {}, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Import Formline v1", {},
                                                       "Formline models (*.formline *.json)");
        if (!path.isEmpty())
            importFormlinePath(path);
    }));
    file->addAction(
        action("file.importTerrain", "Import terrain…", {}, [this] { importTerrainDialog(); }));
    setupTerrainActions();
    file->addAction(action("file.save", "Save", QKeySequence::Save, [this] { save(); }));
    file->addAction(
        action("file.saveAs", "Save as…", QKeySequence::SaveAs, [this] { save(true); }));
    file->addAction(
        action("file.exportRaster", "Export view as PNG…", {}, [this] { exportRasterDialog(); }));
    file->addAction(
        action("file.exportObj", "Export OBJ package…", {}, [this] { exportObjDialog(); }));
    // R097.e1: Export IFC… after Export OBJ package… (the export screen is R097.e2).
    file->addAction(action("file.exportIfc", "Export IFC…", {}, [this] { exportIfc(); }));
    // R098.c2: Export STEP… after Export IFC… (the STEP twin of the IFC export screen).
    file->addAction(action("file.exportStep", "Export STEP…", {}, [this] { exportStep(); }));
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
    // R095.e2: the site origin, its CRS label and provenance; Re-anchor and Set origin.
    file->addAction(action("file.siteSettings", "Site settings…", {}, [this] { siteSettings(); }));
    file->addAction(action("file.quit", "Quit", QKeySequence::Quit, [this] { close(); }));
    auto *edit = menuBar()->addMenu("&Edit");
    edit->addAction(
        action("edit.shortcuts", "Keyboard shortcuts…", {}, [this] { shortcutSettings(); }));
    undo_ = action("edit.undo", "Undo", QKeySequence::Undo, [this] {
        viewport_->cancel();
        doc_.undo();
        sync();
        focusWorkspace();
    });
    redo_ = action("edit.redo", "Redo", QKeySequence::Redo, [this] {
        viewport_->cancel();
        doc_.redo();
        sync();
        focusWorkspace();
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
            viewport_->openContext(viewport_->selectedBody());
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
        QColorDialog picker(Qt::white, this);
        picker.setWindowTitle("Material color");
        nameColorDialog(picker);
        const auto c = picker.exec() == QDialog::Accepted ? picker.selectedColor() : QColor{};
        if (c.isValid()) {
            viewport_->paintSelection({float(c.redF()), float(c.greenF()), float(c.blueF())});
            sync();
        }
    }));
    auto *draw = menuBar()->addMenu("&Draw");
    auto *group = new QActionGroup(this);
    // The tool rail's groups (UX_DESIGN §2 item 2): the one place that says which group each
    // rail tool belongs to. Within a group the rail keeps its order; the wide rail is unchanged.
    // A tool added to the rail without a group here throws when the window is built.
    const std::map<Viewport::Tool, int> railGroups{
        {Viewport::Tool::Select, RailSelect},      {Viewport::Tool::Paint, RailSelect},
        {Viewport::Tool::Line, RailDraw},          {Viewport::Tool::Rectangle, RailDraw},
        {Viewport::Tool::Circle, RailDraw},        {Viewport::Tool::Extrude, RailModify},
        {Viewport::Tool::Offset, RailModify},      {Viewport::Tool::Sweep, RailModify},
        {Viewport::Tool::Intersect, RailModify},   {Viewport::Tool::Boolean, RailModify},
        {Viewport::Tool::Orientation, RailModify}, {Viewport::Tool::Move, RailModify},
        {Viewport::Tool::Rotate, RailModify},      {Viewport::Tool::Scale, RailModify},
        {Viewport::Tool::Tape, RailMeasure},       {Viewport::Tool::Orbit, RailCamera},
        {Viewport::Tool::Pan, RailCamera}};
    // The rail's visible label, where the full name does not fit the 88 px rail at 100 % text.
    // Only the button text changes: the action's text (menus, command search), its tooltip and
    // the accessible name stay the full name. A tool listed here must have a full name that elides.
    const std::map<Viewport::Tool, QString> railLabels{{Viewport::Tool::Rectangle, "Rect"},
                                                       {Viewport::Tool::Sweep, "Follow"},
                                                       {Viewport::Tool::Boolean, "Solids"},
                                                       {Viewport::Tool::Orientation, "Orient"},
                                                       {Viewport::Tool::Tape, "Tape"}};
    auto addTool = [&](const QString &name, const QString &key, Viewport::Tool mode,
                       const QString &instruction, bool inRail = true) {
        auto *a = action("tool." + QString::number(int(mode)), name, QKeySequence(key),
                         [this, mode, instruction] { tool(mode, instruction); });
        a->setCheckable(true);
        group->addAction(a);
        if (inRail) {
            if (const auto label = railLabels.find(mode); label != railLabels.end())
                a->setIconText(label->second);
            tools->addAction(a);
            railTools_.emplace_back(a, railGroups.at(mode));
        }
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
            "Select two solids or their faces · Choose operation and originals in Draw");
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
            "Select faces to reverse, or one reference face to orient its connected surface");
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
        for (auto *field : {origin, normal, axis})
            acceptKeypadDecimal(field);
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
    newFace->setIconText("New face"); // The rail label only; the full name is in menus and tooltip.
    tools->addAction(newFace);
    // An option toggle: it belongs to Push/pull's group, Modify, though it sits after Tape measure.
    railTools_.emplace_back(newFace, RailModify);
    connect(viewport_, &Viewport::pushPullModeChanged, newFace, &QAction::setChecked);
    // R096.f1: the terrain tools and the rail's Terrain slot, in its own group after Tape.
    setupTerrainTools(group, tools);
    // The separator before Camera shows on the wide rail only (group -1: in no group).
    railTools_.emplace_back(tools->addSeparator(), -1);
    addTool("Orbit", "O", Viewport::Tool::Orbit, "Drag to orbit · Shift-drag to pan");
    addTool("Pan", "H", Viewport::Tool::Pan, "Drag to pan");
    addTool("Zoom", "Z", Viewport::Tool::Zoom, "Drag up to zoom in · Drag down to zoom out", false);
    setupRailGroups();
    // One Tab stop on the active tool; arrows move between tools (R084.v).
    installToolbarNavigation(tools, group);
    // R096.f1 (UX_DESIGN §15.1): the Terrain menu sits between Draw and Camera.
    setupTerrainMenu();
    auto *cameraMenu = menuBar()->addMenu("&Camera");
    cameraMenu->addAction(addTool(
        "Look around", "", Viewport::Tool::LookAround,
        "Drag or use arrows to look from a fixed eye · Wheel changes field of view", false));
    cameraMenu->addAction(addTool(
        "Walk", "", Viewport::Tool::Walk,
        "WASD/arrows walk · Q/E change height · Shift moves faster · Drag looks · Esc exits",
        false));
    cameraMenu->addAction(action("camera.render", "Render…", {}, [this] { render_->showSetup(); }));
    auto *extensions = menuBar()->addMenu("E&xtensions");
    extensions->addAction(
        action("extensions.manage", "Manage extensions…", {}, [this] { extensionsDialog(); }));
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
    // R095.e2: the site origin marker, always (checked) or only while a terrain or Site
    // settings is selected (unchecked); a preference like the navigation choice.
    auto *siteOrigin = action("view.siteOrigin", "Site origin", {}, [this] {
        const bool shown = findChild<QAction *>("view.siteOrigin")->isChecked();
        viewport_->setSiteOriginVisible(shown);
        QSettings("SketchyUp", "SketchyUp").setValue("siteOriginVisible", shown);
    });
    siteOrigin->setCheckable(true);
    siteOrigin->setChecked(
        QSettings("SketchyUp", "SketchyUp").value("siteOriginVisible", false).toBool());
    viewport_->setSiteOriginVisible(siteOrigin->isChecked());
    view->addAction(siteOrigin);
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
        SettledInputDialog input(this);
        input.setWindowTitle("Field of view");
        input.setLabelText("Vertical angle in degrees");
        input.setInputMode(QInputDialog::DoubleInput);
        input.setDoubleRange(5, 120);
        input.setDoubleDecimals(1);
        input.setDoubleValue(viewport_->fieldOfView());
        if (auto *angle = input.findChild<QDoubleSpinBox *>())
            angle->setAccessibleName(input.labelText());
        nameInternalEditors(&input);
        if (input.exec() == QDialog::Accepted) {
            const auto degrees = input.doubleValue();
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
    // Every panel action reveals the Model panel, selects its tab and moves keyboard focus into
    // that tab's content, so a menu or palette command is never a dead end for keyboard users.
    auto showPanel = [this](void (OrganizationPanel::*show)()) {
        // Model panels live in the Model workspace's Tray. Focus returns to where it was when
        // the narrow layout's sheet closes, not to the canvas setWorkspace focuses.
        const QPointer<QWidget> before = focusWidget();
        setWorkspace(Workspace::Model);
        sideTabs_->show();
        sideTabs_->setCurrentWidget(tray_);
        tray_->show();
        findChild<QAction *>("view.tray")->setChecked(true);
        (organization_->*show)();
        // The narrow layout opens the panel in its overlay sheet (UX_DESIGN §3.3).
        if (narrowLayout())
            openOverlay(organizationKey(), before);
        organization_->setFocus(Qt::ShortcutFocusReason);
    };
    view->addAction(action("view.outliner", "Outliner", {},
                           [showPanel] { showPanel(&OrganizationPanel::showOutliner); }));
    view->addAction(action("view.tags", "Tags and folders", {},
                           [showPanel] { showPanel(&OrganizationPanel::showTags); }));
    view->addAction(action("view.info", "Entity info", {},
                           [showPanel] { showPanel(&OrganizationPanel::showInfo); }));
    view->addAction(action("view.materials", "Materials", {},
                           [showPanel] { showPanel(&OrganizationPanel::showMaterials); }));
    view->addAction(action("view.history", "History", {},
                           [showPanel] { showPanel(&OrganizationPanel::showHistory); }));
    view->addAction(action("view.scenes", "Saved scenes", {},
                           [showPanel] { showPanel(&OrganizationPanel::showScenes); }));
    view->addAction(action("view.sections", "Section planes", {},
                           [showPanel] { showPanel(&OrganizationPanel::showSections); }));
    view->addAction(action("view.annotations", "Dimensions and labels", {},
                           [showPanel] { showPanel(&OrganizationPanel::showAnnotations); }));
    view->addAction(action("view.text", "3D text", {},
                           [showPanel] { showPanel(&OrganizationPanel::showText); }));
    view->addAction(action("view.solar", "Sun and shadows", {},
                           [showPanel] { showPanel(&OrganizationPanel::showSolar); }));
    view->addAction(action("view.reference_images", "Reference images", {},
                           [showPanel] { showPanel(&OrganizationPanel::showReferenceImages); }));
    view->addAction(action("view.components", "Components", {},
                           [showPanel] { showPanel(&OrganizationPanel::showComponents); }));
    auto *reducedMotion = action("view.reduced_motion", "Reduced camera motion", {}, [this] {
        const auto enabled = findChild<QAction *>("view.reduced_motion")->isChecked();
        QSettings().setValue("reducedMotion", enabled);
        viewport_->setReducedMotion(enabled);
    });
    reducedMotion->setCheckable(true);
    reducedMotion->setChecked(navigationSettings.value("reducedMotion", false).toBool());
    viewport_->setReducedMotion(reducedMotion->isChecked());
    view->addAction(reducedMotion);
    view->addAction(action("view.styles", "Model styles", {},
                           [showPanel] { showPanel(&OrganizationPanel::showStyles); }));
    view->addAction(action("view.assistant", "Assistant", QKeySequence("Ctrl+J"),
                           [this] { toggleAssistant(); }));
    auto *panel = action("view.tray", "Model panel", QKeySequence("Ctrl+Shift+T"), [this] {
        if (narrowLayout()) {
            // The strip's More panels: the whole panel in the overlay sheet, or closed again.
            if (overlayOpen_ && overlayKey_ == PanelKey::More) {
                closeOverlay();
                return;
            }
            sideTabs_->setCurrentWidget(tray_);
            openOverlay(PanelKey::More);
            return;
        }
        sideTabs_->setVisible(findChild<QAction *>("view.tray")->isChecked());
    });
    panel->setCheckable(true);
    panel->setChecked(true);
    view->addAction(panel);
    // R094.a: keyboard equivalents of dragging the sidebar's edge.
    view->addAction(action("view.sidebar.wider", "Widen sidebar", QKeySequence("Ctrl+Alt+]"),
                           [this] { stepSidebar(1); }));
    view->addAction(action("view.sidebar.narrower", "Narrow sidebar", QKeySequence("Ctrl+Alt+["),
                           [this] { stepSidebar(-1); }));
    view->addAction(
        action("view.sidebar.reset", "Reset sidebar width", {}, [this] { resetSidebar(); }));
    view->addAction(action("view.textSize", "Interface text size…", {}, [this] {
        const QStringList sizes{"75%", "100%", "125%", "150%", "175%", "200%"};
        bool accepted = false;
        const auto choice =
            QInputDialog::getItem(this, "Interface text size", "Text size", sizes,
                                  (interfaceTextPercent() - 75) / 25, false, &accepted);
        if (accepted) {
            QSettings preferences("SketchyUp", "SketchyUp");
            preferences.setValue("interfaceTextPercent", 75 + 25 * sizes.indexOf(choice));
            preferences.sync();
            if (preferences.status() != QSettings::NoError)
                throw std::runtime_error("Could not save interface text size");
            applyTheme();
        }
        activateWindow();
        viewport_->setFocus();
    }));
    auto *themes = view->addMenu("Theme");
    auto *themeGroup = new QActionGroup(this);
    const QStringList themeNames{"System theme", "Light theme", "Dark theme"};
    for (int mode = 0; mode < themeNames.size(); ++mode) {
        auto *entry =
            action("view.theme." + QString::number(mode), themeNames[mode], {}, [this, mode] {
                themeMode_ = mode;
                QSettings("SketchyUp", "SketchyUp").setValue("theme", mode);
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
    modelRegions_ = {search, tools, viewport_, organization_, assistant_, measurements_};
    focusRegions_ = modelRegions_;
    setupSheets(header, view);
    setupNarrow(header);
    // The rails sit in a stack, which put them late in the focus chain: Tab from command search
    // skipped them. Move the stack, with both rails, back to right after the search. Qt moves
    // only a focusable widget, so the stack is focusable just for the call.
    railStack_->setFocusPolicy(Qt::TabFocus);
    QWidget::setTabOrder(search, railStack_);
    railStack_->setFocusPolicy(Qt::NoFocus);
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
    // Single-letter modeling shortcuts must never consume typing in text fields. Sheet tools
    // are scoped to the page canvas the same way.
    for (auto *a : publicActions_)
        if (a->shortcut().count() == 1 && a->shortcut()[0].keyboardModifiers() == Qt::NoModifier &&
            a->shortcut()[0].key() != Qt::Key_F6) {
            a->setShortcutContext(Qt::WidgetShortcut);
            removeAction(a);
            if (actionScope(a) == ShortcutScope::Sheets)
                sheets_->canvas()->addAction(a);
            else
                viewport_->addAction(a);
        }
    initializeShortcuts();
    connect(viewport_, &Viewport::toolChanged, this, [this](int mode) {
        if (workspace_ == Workspace::Model)
            measurements_->setEnabled(mode != int(Viewport::Tool::Paint));
        if (mode == int(Viewport::Tool::Paint))
            organization_->showMaterials();
        if (auto *toolAction = findChild<QAction *>("tool." + QString::number(mode)))
            toolAction->setChecked(true);
        syncTerrainTools();
        measurements_->setPlaceholderText(measurementPlaceholder(Viewport::Tool(mode)));
        if (Viewport::terrainTool(Viewport::Tool(mode)))
            showRailGroup(RailTerrain);
        syncParameterMeasurement();
    });
    connect(viewport_, &Viewport::terrainToolChanged, this, [this] {
        syncTerrainTools();
        syncParameterMeasurement();
    });
    connect(viewport_, &Viewport::changed, this, &Window::sync);
    connect(viewport_, &Viewport::assemblyPlacementEnded, this,
            [this] { syncParameterMeasurement(); });
    connect(viewport_, &Viewport::message, this, [this](const QString &text) {
        status_->setText(text);
        // R096.f2 (UX §15.3): a terrain tool's refusal at the pointer reads in the failure style.
        const bool failure = !text.isEmpty() && text == viewport_->terrainFailureStatus();
        if (status_->property("terrainFailure").toBool() != failure) {
            status_->setProperty("terrainFailure", failure);
            status_->style()->unpolish(status_);
            status_->style()->polish(status_);
        }
    });
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
    // ADR 0166 §1.3 (R094.c): with the Select tool and one parametric placement, the box sets
    // the parameter Entity info names; re-entry amends the entry.
    parameters_ = organization_->findChild<ComponentParametersSection *>();
    connect(parameters_, &ComponentParametersSection::measurementChanged, this,
            &Window::syncParameterMeasurement);
    // The section's keys replace the status text while focus is in it (§1.2). Focus changes
    // while the window is torn down, so the handler holds no pointer into the window: it lives
    // as long as the section and checks the status label.
    connect(qApp, &QApplication::focusChanged, parameters_,
            [section = parameters_, status = QPointer<QLabel>(status_),
             saved = std::make_shared<QString>()](QWidget *old, QWidget *now) {
                const auto inside = [section](QWidget *widget) {
                    return widget && section->isAncestorOf(widget);
                };
                if (!status)
                    return;
                if (inside(now) && !inside(old)) {
                    *saved = status->text();
                    status->setText(ComponentParametersSection::hint());
                } else if (inside(old) && !inside(now) &&
                           status->text() == ComponentParametersSection::hint()) {
                    status->setText(*saved);
                }
            });
    setupDefinitionAuthoring();
    connect(measurements_, &QLineEdit::returnPressed, this, [this] {
        if (assistant_ && assistant_->uncertain())
            return;
        // ADR 0166 §3.4: "Plane" moves the stretch plane; it does not commit the driver.
        if (workspace_ == Workspace::Model && planeMeasurement_) {
            QString error;
            if (driversSection_->setPlaneText(measurements_->text(), error)) {
                measurementError(false);
                measurements_->clear();
                viewport_->setFocus();
                syncParameterMeasurement();
            } else {
                status_->setText(error);
                measurementError(true);
                measurements_->selectAll();
            }
            return;
        }
        if (workspace_ == Workspace::Model && parameterMeasurement_) {
            QString error;
            if (parameters_->measurements(measurements_->text(), error)) {
                measurementError(false);
                measurements_->clear();
                viewport_->setFocus();
                syncParameterMeasurement();
            } else {
                status_->setText(error);
                measurementError(true);
                measurements_->selectAll();
            }
            return;
        }
        if (workspace_ == Workspace::Sheets) {
            if (sheetMeasurements(measurements_->text())) {
                measurementError(false);
                focusWorkspace();
            } else {
                measurementError(true);
                measurements_->selectAll();
            }
            return;
        }
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
        // Esc in the Assistant: the overlay sheet closes and focus returns to where it was.
        if (overlayOpen_) {
            closeOverlay();
            return;
        }
        activateWindow();
        focusWorkspace();
    });
    new ModalReturn(this);
    installFractionalFocusRings();
    keyboardTabBars(this);
    keepPagesAfterTabBar(modelTabs);
    keepPagesAfterTabBar(sideTabs_);
    applyTheme();
    sync();
}
void Window::applyTheme() {
    const auto textPercent = interfaceTextPercent();
    const bool dark =
        themeMode_ == 2 ||
        (themeMode_ == 0 && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    const auto colors = themeColors(dark);
    auto style = QStringLiteral(R"(
QMainWindow,QWidget {background:$surface;color:$ink;font-family:'DejaVu Sans';font-size:$textPx;}
QMenuBar {padding:5px;background:$surface;border-bottom:1px solid $border;}
QMenuBar::item {padding:5px 10px;} QMenuBar::item:selected,QMenu::item:selected {background:$selected;}
QMenu::item:disabled {color:$muted;}
QMenu {border:1px solid $border;padding:5px;} QMenu::item {padding:7px 22px;}
#header {border-bottom:1px solid $border;} #brand {font-weight:800;letter-spacing:2px;padding:8px 12px;}
#tray {border-left:1px solid $border;} #section,#overlayHeading {font-size:$sectionPx;font-weight:700;letter-spacing:2px;padding:14px 0 8px;}
#hint {color:$muted;font-size:$hintPx;padding:12px 0;} #footer {border-top:1px solid $border;}
#statusMessage[terrainFailure="true"] {color:$failure;font-weight:700;}
#terrainReplayChip {border:1px solid $border;border-radius:4px;background:$input;}
#terrainEditsNotice {background:$warningSurface;border:1px solid $warningBorder;border-radius:4px;}
#terrainEditsShowReport {border:0;padding:0 2px;color:$accent;text-decoration:underline;background:transparent;}
QToolBar {border:1px solid transparent;border-right:1px solid $border;spacing:6px;padding:9px 5px 9px 4px;}
QToolButton {padding:11px 4px;border:1px solid transparent;border-radius:4px;}
#railGroupButton {padding:11px 0;}
/* R096.f1: rail buttons are 1 px shorter, so the full rail (with the Terrain slot) fits a 1000 px window. */
#toolRail QToolButton {padding:10px 4px 11px 4px;} #toolRail #railGroupButton {padding:10px 0 11px 0;}
QToolButton:checked {background:$selected;color:$ink;}
QToolButton:hover,QPushButton:hover {background:$hover;}
QPushButton {border:1px solid $border;padding:7px 11px;border-radius:4px;}
QLineEdit {background:$input;border:1px solid $border;border-radius:4px;padding:7px;selection-background-color:$accent;selection-color:$selectionInk;}
QComboBox,QAbstractSpinBox,QPlainTextEdit,QTextEdit {background:$input;border:1px solid $border;border-radius:4px;padding:3px 5px;selection-background-color:$accent;selection-color:$selectionInk;}
QAbstractSpinBox::up-button {subcontrol-origin:border;subcontrol-position:top right;width:18px;border:0;}
QAbstractSpinBox::down-button {subcontrol-origin:border;subcontrol-position:bottom right;width:18px;border:0;}
QComboBox::drop-down {subcontrol-origin:border;subcontrol-position:center right;width:18px;border:0;}
QAbstractSpinBox::up-arrow {image:url(:/sketchyup/control-arrow-up-$arrowTheme.png);width:9px;height:5px;}
QAbstractSpinBox::down-arrow,QComboBox::down-arrow {image:url(:/sketchyup/control-arrow-down-$arrowTheme.png);width:9px;height:5px;}
QCheckBox,QRadioButton {border:1px solid transparent;border-radius:4px;padding:1px 2px;}
QTabWidget::pane {border:1px solid $border;}
/* D-023 (c): scroll bars in the theme: the panel's background for the track, the muted ink for the handle
   (3:1 or more against it) and the ink under the pointer; $barPx thick; no arrow buttons. */
QScrollBar:vertical {background:$surface;border:0;margin:0;width:$barPx;}
QScrollBar:horizontal {background:$surface;border:0;margin:0;height:$barPx;}
QScrollBar::handle {background:$muted;border:0;border-radius:4px;}
QScrollBar::handle:vertical {min-height:$barHandlePx;} QScrollBar::handle:horizontal {min-width:$barHandlePx;}
QScrollBar::handle:hover,QScrollBar::handle:pressed {background:$ink;}
QScrollBar::add-line,QScrollBar::sub-line {width:0;height:0;border:0;background:none;}
QScrollBar::add-page,QScrollBar::sub-page {background:none;}
QAbstractScrollArea::corner {background:$surface;border:0;}
QTabBar::tab {padding:6px 12px;border:1px solid transparent;border-radius:4px;}
QTabBar::tab:selected {background:$selected;color:$ink;} QTabBar::tab:hover:!selected {background:$hover;}
QSlider,QScrollArea,#assistantContainer {border:1px solid transparent;border-radius:4px;}
/* D-023: where a control borders the 3D canvas, whose colour is the document's, its ring is two-tone:
   the ring (the border) inside a halo in the control's own background, painted as the margin's
   background. Qt anti-aliases the margin's rounded outer edge, so the margin is 2 px: one blended
   pixel and the halo's own full pixel. The outer size is unchanged: the margin takes the padding. */
*[focusHalo="true"] {margin:2px;background-clip:margin;}
#contextBreadcrumb,#componentScopeBanner {border:1px solid transparent;border-radius:4px;padding:0 2px;}
QListWidget,QTreeWidget {border:1px solid transparent;background:transparent;}
QTreeWidget::item {padding:3px 1px;}
QTreeWidget::item:selected {background:$selected;color:$ink;}
QListWidget::item {padding:9px 5px;} QListWidget::item:selected {background:$selected;color:$ink;}
QLineEdit:focus,QComboBox:focus,QAbstractSpinBox:focus,QPlainTextEdit:focus,QTextEdit:focus,QCheckBox:focus,QRadioButton:focus,QToolButton:focus,QTabBar::tab:selected:focus,QSlider:focus,QScrollArea:focus,#assistantContainer:focus,#contextBreadcrumb:focus,#componentScopeBanner:focus,QListWidget:focus,QTreeWidget:focus,QToolBar:focus,QPushButton:focus {border:1px solid $accent;}
QLineEdit[invalid="true"] {border:2px solid #bc4343;}
#workspaceToggle QPushButton {border:1px solid $border;border-radius:0;padding:4px 14px;background:$input;}
#workspaceModel {border-top-left-radius:6px;border-bottom-left-radius:6px;border-right:0;}
#workspaceSheets {border-top-right-radius:6px;border-bottom-right-radius:6px;}
#workspaceToggle QPushButton:checked {background:$accent;color:$onAccent;font-weight:600;border:2px solid $accent;padding:3px 13px;}
#workspaceToggle QPushButton:focus {border:1px solid $ink;}
#workspaceToggle QPushButton:checked:focus {border:2px solid $surface;}
#sheetTopBar {border-bottom:1px solid $border;} #sheetPageBar {border-top:1px solid $border;}
#sheetTraySeparator {background:$border;}
#sectionHeader {text-align:left;font-weight:600;border:1px solid transparent;border-radius:4px;padding:4px 8px;background:transparent;}
#sectionHeader:hover {background:$hover;} #sectionHeader:focus {border:1px solid $accent;}
#sheetInfoCaption,#sheetPaperLine,#sheetInfoEmpty {color:$muted;}
#activeToolLabel,#measurementStep,#sheetPlaceHeading {font-weight:600;}
#sheetPlaceHeading {font-size:$sectionHeadingPx;}
#sheetPlace {background:$accent;color:$onAccent;font-weight:600;border-radius:6px;padding:3px 7px;border:2px solid $accent;}
#sheetPlace:disabled {background:$hover;color:$muted;border-color:$hover;}
#sheetPlaceSceneRow {border-radius:4px;} #sheetPlaceSceneRow[chosen="true"] {background:$selected;}
#sheetPlaceSceneRow QRadioButton,#sheetPlaceSceneRow QLabel {background:transparent;}
#sheetPlaceSceneRow QRadioButton::indicator {width:12px;height:12px;border:1px solid $muted;border-radius:7px;background:$input;}
#sheetPlaceSceneRow QRadioButton::indicator:checked {border:1px solid $accent;background:qradialgradient(cx:0.5,cy:0.5,radius:0.5,fx:0.5,fy:0.5,stop:0 $accent,stop:0.5 $accent,stop:0.6 $input,stop:1 $input);}
#sheetPlaceSceneRow QRadioButton::indicator:disabled {border:1px solid $border;}
#sheetPlaceCancel,#sheetInfoActions QPushButton {border-radius:6px;padding:4px 10px;background:$input;}
#sheetRail QToolButton {padding:6px;border-radius:6px;}
#panelStrip {border:1px solid transparent;border-left:1px solid $border;padding:9px $stripPad;}
#panelStrip QToolButton {padding:$stripButtonPad;border-radius:6px;}
#menuButton {padding:$stripButtonPad;border-radius:6px;} #menuButton::menu-indicator {image:none;width:0;}
#sheetPageTabs::tab {border:1px solid $border;border-radius:6px;padding:3px 10px;background:$input;margin:1px 5px 1px 1px;}
#sheetPageTabs::tab:selected {border:1px solid $accent;background:$selected;}
#sheetPageTabs::tab:selected:focus {border:2px solid $ink;margin:0 4px 0 0;}
#sheetAddPage {border:1px dashed $border;background:transparent;padding:3px 10px;border-radius:6px;}
#sheetAddPage:focus,#sheetFitPage:focus,#sheetBreadcrumb:focus {border:1px solid $accent;}
#sheetFitPage {padding:3px 10px;border-radius:6px;background:$input;}
#sheetBreadcrumb {border:1px solid transparent;border-radius:4px;padding:2px 4px;}
*[accentFill="true"] {border:2px solid $accent;}
*[accentFill="true"]:focus,#sheetPlace:focus {border:2px solid $surface;}
#parameterLabel {font-weight:600;}
#parameterCaption,#parameterNote,#parameterMessage,#sectionHeaderDetail {color:$muted;}
#parameterCaption[override="true"] {color:$information;}
QLineEdit[parameterField="true"],QSpinBox[parameterField="true"] {font-family:'DejaVu Sans Mono';}
*[parameterField="true"][override="true"] {border:2px solid $information;background:$informationSurface;}
*[parameterField="true"][override="true"]:focus {border:2px solid $accent;}
*[parameterField="true"][invalid="true"],*[parameterField="true"][invalid="true"]:focus {border:2px solid $failure;background:$failureSurface;}
#parameterError,#parameterErrorIcon {color:$failure;}
#parameterFewer,#parameterMore,*[parameterReset="true"] {padding:2px 8px;}
#componentScopeBanner[parametric="true"] {background:$informationSurface;color:$information;border:1px solid $information;padding:2px 6px;}
#componentScopeBanner[parametric="true"]:focus {border:2px solid $accent;padding:1px 5px;}
#parameterPreviewStrip {background:$input;border:1px solid $border;border-radius:8px;}
#parameterPreviewFields,#parameterPreviewPair,#parameterPreviewStrip QLabel,#parameterPreviewStrip QCheckBox {background:transparent;}
#parameterPreviewTitle,#parameterEditorTitle,#driverEditorTitle {font-weight:600;}
#parameterPreviewNote,#parameterEditorNote,#definitionParametersEmpty,#driversEmpty,#driverMoving {color:$muted;}
#parameterPreviewError,#parameterEditorError,#driverEditorError,#formulaCaret {color:$failure;font-weight:600;}
#formulaCaret {font-family:'DejaVu Sans Mono';}
#parameterEditor,#driverEditor {border:1px solid $border;border-radius:6px;}
#driverChangedState,#driverChangedNote {color:$warning;font-weight:600;}
#driverRowRebind {padding:1px 8px;border:1px solid $warning;}
#assemblyValuesTitle,#assembliesAboutTitle {font-weight:600;}
#assemblyValuesNote,#assembliesAboutTitle,#componentsShowLabel,#componentsMessage,#assemblyCancelKey {color:$muted;}
#assemblyCancelKey {font-family:'DejaVu Sans Mono';}
#componentCards {background:transparent;border:1px solid transparent;border-radius:4px;}
#componentCards:focus {border:1px solid $accent;}
#parametersScopeError {color:$failure;font-weight:600;}
#parametersScopeError[tone="warning"],#parameterError[tone="warning"],#parameterErrorIcon[tone="warning"],#parameterPreviewError[tone="warning"],#parameterEditorError[tone="warning"],#driverEditorError[tone="warning"] {color:$warning;}
#parametersScopeHeading {font-size:$sectionHeadingPx;font-weight:700;}
#parametersScopeNote {color:$muted;}
#parametersScopeDefaults,#parametersScopeUnique[accentFill="true"] {background:$accent;color:$onAccent;font-weight:600;border:2px solid $accent;}
#parametersScopeDefaults:focus,#parametersScopeUnique[accentFill="true"]:focus {border:2px solid $surface;}
#parameterStatusStrip {background:$warningSurface;border:1px solid $warningBorder;border-radius:6px;}
#parameterStatusStrip QLabel,#parameterStatusIcon {background:transparent;color:$warning;}
#parameterStatusStrip QPushButton {background:$input;}
#parameterStatusRebind {border:1px solid $warning;}
#parameterStatusUndo QLabel {color:$ink;} #parameterStatusUndo QLabel#parameterStatusShortcut {color:$muted;font-family:'DejaVu Sans Mono';}
#parameterStatusStrip QPushButton:focus {border:2px solid $accent;padding:6px 10px;}
/* R095.e1: the Terrain section's amber "Changed since generated" chip and the terrain dialogs. */
#terrainChangedChip {background:$warningSurface;border:1px solid $warningBorder;border-radius:6px;}
#terrainChangedChip QLabel {background:transparent;color:$warning;font-weight:600;}
#terrainRegenerate {background:$input;border:1px solid $warning;padding:3px 10px;}
#terrainRegenerate:focus {border:2px solid $accent;padding:2px 9px;}
#terrainDialogTitle {font-size:$sectionHeadingPx;font-weight:700;}
#terrainDialogNote,#terrainBudgetCaption,#terrainSettingsStatus,#terrainSelectionStatus {color:$muted;}
#terrainSettingsStatus[refused="true"],#terrainSelectionStatus[refused="true"] {color:$failure;font-weight:600;}
QFrame#terrainDialogBanner {border:2px solid $failure;border-radius:8px;background:$failureSurface;}
QFrame#terrainDialogBanner[tone="notice"] {border:1px solid $warningBorder;background:$warningSurface;}
#terrainDialogBannerText {background:transparent;color:$ink;}
#terrainExplodeFaces,#terrainExplodeTriangles {font-family:'DejaVu Sans Mono';}
)");
    style.replace("$textPx", QString::number(interfaceExtent(12, textPercent)) + "px");
    style.replace("$sectionPx", QString::number(interfaceExtent(10, textPercent)) + "px");
    style.replace("$sectionHeadingPx", QString::number(interfaceExtent(14, textPercent)) + "px");
    style.replace("$hintPx", QString::number(interfaceExtent(11, textPercent)) + "px");
    style.replace("$arrowTheme", dark ? "dark" : "light");
    style.replace("$barPx", QString::number(interfaceExtent(10, textPercent)) + "px");
    style.replace("$barHandlePx", QString::number(interfaceExtent(24, textPercent)) + "px");
    // UX_DESIGN §3.3: the panel strip is 46 px with 36 px buttons and 24 px icons, all scaled by
    // text size; these paddings centre them.
    style.replace("$stripPad", QString::number((interfaceExtent(46, textPercent) - 2 -
                                                interfaceExtent(36, textPercent)) /
                                               2) +
                                   "px");
    style.replace("$stripButtonPad", QString::number((interfaceExtent(36, textPercent) - 2 -
                                                      interfaceExtent(24, textPercent)) /
                                                     2) +
                                         "px");
    style.replace("$selectionInk", dark ? colors.surface.name() : colors.input.name());
    style.replace("$onAccent", dark ? "#172019" : "#ffffff");
    style.replace("$surface", colors.surface.name());
    style.replace("$ink", colors.ink.name());
    style.replace("$border", colors.border.name());
    style.replace("$selected", colors.selected.name());
    style.replace("$muted", colors.muted.name());
    style.replace("$hover", colors.hover.name());
    style.replace("$input", colors.input.name());
    style.replace("$accent", colors.accent.name());
    // R094.c (ADR 0166 §1.2): longer token names first.
    style.replace("$informationSurface", colors.informationSurface.name());
    style.replace("$information", colors.information.name());
    style.replace("$warningSurface", colors.warningSurface.name());
    style.replace("$warningBorder", colors.warningBorder.name());
    style.replace("$warning", colors.warning.name());
    style.replace("$failureSurface", colors.failureSurface.name());
    style.replace("$failure", colors.failure.name());
    setStyleSheet(style);
    findChild<QToolBar *>("toolRail")->setFixedWidth(interfaceExtent(88, textPercent));
    sheetRail_->setFixedWidth(interfaceExtent(46, textPercent));
    railStack_->setFixedWidth(railStack_->currentWidget()->maximumWidth());
    measurements_->setFixedWidth(interfaceExtent(150, textPercent));
    if (property("interfaceTextPercent").toInt() != textPercent) {
        setProperty("interfaceTextPercent", textPercent);
        layoutAssistant();
    }
    updateCommandSearchLabel(this);
    QTimer::singleShot(0, this, [this] { resizeContextLabels(this); });
    viewport_->setTheme(colors);
    setContextLabelColor(breadcrumb_, colors.accent);
    setContextLabelColor(componentBanner_, colors.accent);
    themeSheets(colors);
    themeSidebar(colors);
    themeNarrow(colors);
    themeRail(colors);
    if (terrainStrip_)
        terrainStrip_->setColors(colors);
    // R096.f1: the Terrain menu's headings are muted labels.
    if (terrainMenu_)
        for (auto *entry : terrainMenu_->actions())
            if (auto *widget = qobject_cast<QWidgetAction *>(entry))
                if (auto *label = widget->defaultWidget())
                    label->setStyleSheet(QString("color:%1;background:transparent;")
                                             .arg(colors.muted.name()));
    if (auto *components = findChild<ComponentsTab *>())
        components->setTheme(colors);
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
void Window::syncParameterMeasurement() {
    // R096.f1 (UX §15.2): a terrain tool names its step: Radius before a stroke, Amount or
    // Strength while a spot or region waits for its value and after a commit (amend).
    if (workspace_ == Workspace::Model && Viewport::terrainTool(viewport_->tool())) {
        const auto label = viewport_->terrainMeasurementLabel();
        parameterMeasurement_ = planeMeasurement_ = false;
        terrainMeasurement_ = true;
        measurementStep_->setText(label);
        measurementStep_->setVisible(true);
        measurements_->setAccessibleName(label + ", Measurements in " +
                                         unitName(doc_.displayUnits()).toLower());
        return;
    }
    if (terrainMeasurement_) {
        terrainMeasurement_ = false;
        measurementStep_->clear();
        measurementStep_->hide();
        measurements_->setAccessibleName("Measurements in " +
                                         unitName(doc_.displayUnits()).toLower());
    }
    if (!parameters_ || workspace_ != Workspace::Model) {
        parameterMeasurement_ = planeMeasurement_ = false;
        return;
    }
    // While an assembly is placed (or re-entry can amend it) the box is the viewport's (R094.f).
    const bool select = viewport_->tool() == Viewport::Tool::Select && !assistantFenced_ &&
                        !viewport_->assemblyMeasurementsActive();
    // The Stretch form's plane (R094.d) comes before the placement parameters (R094.c): in a
    // definition context no placement is selected outside it.
    const bool plane = select && driversSection_ && driversSection_->planeActive();
    const auto label = plane ? QString("Plane") : select ? parameters_->measurementLabel() : QString{};
    const bool routed = !label.isEmpty();
    if (!routed && !parameterMeasurement_ && !planeMeasurement_)
        return;
    planeMeasurement_ = plane;
    parameterMeasurement_ = routed && !plane;
    measurementStep_->setText(label);
    measurementStep_->setVisible(routed);
    measurements_->setAccessibleName((routed ? label + ", " : QString{}) + "Measurements in " +
                                     unitName(doc_.displayUnits()).toLower());
    if (!measurements_->hasFocus())
        measurements_->setText(plane    ? driversSection_->planeText()
                               : routed ? parameters_->measurementValue()
                                        : QString{});
}
void Window::measurementError(bool invalid) {
    measurements_->setProperty("invalid", invalid);
    measurements_->setAccessibleDescription(invalid ? status_->text() : QString{});
    measurements_->style()->unpolish(measurements_);
    measurements_->style()->polish(measurements_);
}
bool Window::eventFilter(QObject *object, QEvent *event) {
    // Esc in the Preview strip goes back to the defaults and clears the ghost (ADR 0166 §2);
    // it never reaches the viewport, where it would close the context.
    if (previewStrip_ && object->isWidgetType() &&
        previewStrip_->isAncestorOf(static_cast<QWidget *>(object)) &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        event->accept();
        if (event->type() == QEvent::KeyPress)
            previewDefaults();
        return true;
    }
    if (object == measurements_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        if (workspace_ == Workspace::Model)
            measurements_->clear();
        measurementError(false);
        focusWorkspace();
        syncParameterMeasurement();
        return true;
    }
    return QMainWindow::eventFilter(object, event);
}
// The Measurements box's placeholder for a tool (also after a tool ends itself, R096.f2 Drape).
QString Window::measurementPlaceholder(Viewport::Tool t) {
    return t == Viewport::Tool::Paint             ? "Choose a material and side"
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
        : t == Viewport::Tool::Contours ? "elevation, [x,y] or @E,N"
        : t == Viewport::Tool::Pad      ? "elevation or slope, or [x,y] or @E,N"
        : t == Viewport::Tool::Drape    ? "click or Enter"
        : Viewport::terrainTool(t) ? "radius, [x,y] or @E,N, then a value"
                                                                     : "width, depth";
}
void Window::tool(Viewport::Tool t, const QString &text) {
    status_->setText(text);
    if (t == Viewport::Tool::HostedPlacement)
        viewport_->startHostedPlacement();
    else
        viewport_->setTool(t);
    viewport_->setFocus();
    measurements_->setPlaceholderText(measurementPlaceholder(t));
}
void Window::sync() {
    if (assistant_)
        assistant_->refresh();
    if (render_)
        render_->refreshProvenance();
    if (workspace_ == Workspace::Sheets) {
        // Bare numbers are paper millimetres in Sheets (ADR 0164 §4).
        measurementUnits_->setText("Paper units: mm");
        measurements_->setAccessibleName("Measurements in paper millimetres");
    } else {
        measurementUnits_->setText("Measurements · " +
                                   QString::fromLatin1(unitCode(doc_.displayUnits()).data()));
        measurements_->setAccessibleName("Measurements in " +
                                         unitName(doc_.displayUnits()).toLower());
    }
    viewport_->refresh();
    // An open export or print dialog checks the changed document again (R092.b).
    if (sheetExport_)
        sheetExport_->documentChanged();
    if (terrainImport_)
        terrainImport_->documentChanged();
    syncTerrainActions();
    if (auto *exportSheets = findChild<QAction *>("file.exportSheets");
        exportSheets && workspace_ == Workspace::Model && !assistantFenced_)
        exportSheets->setEnabled(!doc_.drawingSheets().empty());
    title_->setText(
        (path_.isEmpty() ? (recoveredName_.isEmpty() ? QString("Untitled") : recoveredName_)
                         : QFileInfo(path_).fileName()) +
        (workspace_ == Workspace::Sheets ? "  ·  Sheets" : "") +
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
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        auto name = QString::fromStdString(doc_.bodies().at(*it)->name);
        // A parametric definition's context reads "Definition (definition)" (ADR 0166 §3.1).
        if (const auto instance = doc_.instances().find(*it); instance != doc_.instances().end())
            if (const auto &definition = doc_.definitions().at(instance->second->definition);
                definition->parameters)
                name = QString::fromStdString(definition->name) + " (definition)";
        breadcrumb += QString(" &rsaquo; <a href=\"%1\">%2</a>").arg(*it).arg(name.toHtmlEscaped());
    }
    setContextLabelText(breadcrumb_, breadcrumb);
    breadcrumb_->setToolTip(path.empty() ? "Editing the model"
                                         : "Click an ancestor to close nested contexts");
    breadcrumb_->setMaximumWidth(std::max(100, viewport_->width() - 32));
    breadcrumb_->adjustSize();
    breadcrumb_->raise();
    if (terrainStrip_)
        terrainStrip_->place(0);
    syncComponentActions();
    organization_->refresh();
    syncParameterMeasurement();
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
                                                2, doc_.displayUnits(), doc_.displayPrecision());
    }
    info_->setText(text);
    info_->setAccessibleDescription(text);
    syncSheets();
    syncWorkspaceActions();
    if (assistantFenced_) {
        for (auto &[action, wasEnabled] : assistantActionStates_)
            action->setEnabled(false);
        measurements_->setEnabled(false);
    }
}
bool Window::save(bool saveAs) {
    if (fileOperation_) {
        status_->setText("Wait for the current file operation to finish.");
        return false;
    }
    QScopedValueRollback busy(fileOperation_, true);
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
        const auto stamp = doc_.saveStamp();
        const auto revision = doc_.revision();
        runFileOperation(this, "Saving model…", [snapshot = doc_.readSnapshot(), target]() mutable {
            saveDocument(snapshot, target);
        });
        if (!doc_.markSaved(stamp))
            throw std::runtime_error("Saved the prior model; the active document session changed");
        path_ = target;
        recoveryContext_ = {QFileInfo(target).absoluteFilePath(), revision,
                            QDateTime::currentDateTimeUtc()};
        recoveredName_.clear();
        saveFailure_.clear();
        saveBanner_->hide();
        if (!doc_.dirty())
            clearRecovery();
        rememberPath(target);
        sync();
        status_->setText(doc_.dirty() ? "Saved snapshot; newer edits remain unsaved."
                                      : "Saved locally");
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
    // R096.f2: a terrain replay belongs to this document; replacing it cancels the replay.
    if (auto *job = TerrainReplayJob::running())
        job->cancel();
    if (fileOperation_) {
        status_->setText("Wait for the current file operation to finish.");
        return false;
    }
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
        if (choice == QMessageBox::Save && doc_.dirty()) {
            status_->setText("New edits arrived while saving. Review them before continuing.");
            return false;
        }
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
    if (fileOperation_)
        throw std::runtime_error("Wait for the current file operation to finish");
    // Validate before asking to discard the current document.
    auto loaded = [&] {
        QScopedValueRollback busy(fileOperation_, true);
        return runFileOperation(this, "Opening model…", [path] { return loadDocument(path); });
    }();
    if (!canReplace())
        return;
    doc_ = std::move(loaded);
    clearIfcReportState(); // a new model: the IFC report chip's lifetime ends (16.1)
    clearStepReportState(); // a new model: the STEP report chip's lifetime ends too
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
    updateCommandSearchLabel(this);
    QTimer::singleShot(0, this, [this] { resizeContextLabels(this); });
    if (sideTabs_)
        layoutAssistant();
    if (auto *panel = findChild<QAction *>("view.tray"); panel && !narrowLayout())
        panel->setChecked(true);
    setProperty("layoutClass", width() < 800    ? "compact"
                               : width() < 1100 ? "standard"
                               : width() < 1500 ? "expanded"
                                                : "wide");
}
void Window::focusRegion(bool previous) {
    std::vector<QWidget *> visible;
    int current = -1;
    auto *focused = QApplication::focusWidget();
    // R096.f1 (UX §15.1): while a terrain tool's options strip shows, it follows the viewport and
    // the Measurements box follows it, so F6 moves between the strip and the box.
    auto regions = focusRegions_;
    if (terrainStrip_ && terrainStrip_->isVisible() && workspace_ == Workspace::Model) {
        std::erase(regions, measurements_);
        const auto view = std::find(regions.begin(), regions.end(), viewport_);
        if (view != regions.end())
            regions.insert(std::next(view), {terrainStrip_, measurements_});
    }
    for (auto *region : regions) {
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
    if (visible[next] == terrainStrip_)
        terrainStrip_->firstField()->setFocus(Qt::ShortcutFocusReason);
    else
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
    query->setAccessibleName("Command and model search");
    query->setPlaceholderText("Commands, objects, recent files, or face BODY/FACE…");
    layout->addWidget(query);
    auto *list = new QListWidget;
    list->setObjectName("paletteResults");
    list->setAccessibleName("Matching commands and model objects");
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
                // Model objects are selected in the Model workspace.
                setWorkspace(Workspace::Model);
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
    if (dialog.exec() == QDialog::Accepted && chosen) {
        activateWindow();
        chosen();
    }
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
    clearIfcReportState(); // a new model: the IFC report chip's lifetime ends (16.1)
    clearStepReportState(); // a new model: the STEP report chip's lifetime ends too
    resetRecoveryContext();
    path_.clear();
    viewport_->cancel();
    viewport_->setSelection(0);
    viewport_->fit();
    sync();
    status_->setText("Example courtyard · Original editable face geometry");
}
} // namespace sketchy
