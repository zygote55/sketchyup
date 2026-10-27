#pragma once
#include "app/assistant_panel.hpp"
#include "app/organization_panel.hpp"
#include "app/recovery_controller.hpp"
#include "app/ifc_availability.hpp"
#include "app/import_report.hpp"
#include "app/shortcut_bindings.hpp"
#include "app/theme.hpp"
#include "app/viewport.hpp"
#include "io/dxf_export.hpp"
#include "io/obj_source.hpp"
#include "io/stl_export.hpp"
#include <QLabel>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
class QActionGroup;
class QTabWidget;
class QSplitter;
class QDialog;
class QPushButton;
class QHBoxLayout;
class QStackedWidget;
class QTimer;
class QToolBar;
class QToolButton;
class QPrinter;
namespace sketchy {
class ComponentParametersSection;
class ComponentDefinitionSection;
class ComponentDriversSection;
class DesktopInspection;
class RenderPanel;
class PresentationsDialog;
class DrawingViewRenderCache;
class SheetWorkspace;
class SheetsTray;
class SheetExportDialog;
class TerrainImportDialog;
class TerrainToolStrip;
struct SheetExport;
struct SheetExportOptions;
struct SheetExportPreflight;
class Window : public QMainWindow {
    Q_OBJECT
  public:
    explicit Window(QWidget *parent = nullptr, AssistantPanel::HostServices assistantServices = {});
    ~Window() override;
    void startUnits();
    void startRecovery(const QString &root = {});
    void showRecovery(bool onlyIfPresent = false);
    Document &document() { return doc_; }
    Viewport *viewport() { return viewport_; }
    RenderPanel *renderPanel() { return render_; }
    AssistantPanel *assistantPanel() { return assistant_; }
    // ADR 0164 §7 (R091.e): the Model and Sheets workspaces, always built (R091.h).
    enum class Workspace { Model, Sheets };
    Workspace workspace() const { return workspace_; }
    void setWorkspace(Workspace workspace);
    SheetWorkspace *sheetWorkspace() { return sheets_; }
    SheetsTray *sheetsTray() { return sheetsTray_; }
    DrawingViewRenderCache *sheetEvaluations() { return sheetCache_.get(); }
    QJsonObject inspect(const QJsonObject &request);
    void demo();
    void openPath(const QString &path);
    // R105.a (ADR 0179 section 7.3): a file the user picked that is a refused type (a ZIP archive,
    // COLLADA, USDZ or FBX) is named and refused before any importer runs; true when the import
    // must stop.
    bool refuseUnsupportedImport(const QString &path);
    void importFormlinePath(const QString &path);
    void importGltfPath(const QString &path);
    void importObjPath(const QString &path, ObjImportOptions options);
    void exportObjPath(const QString &path, ObjImportOptions options);
    void importStlPath(const QString &path, StlCoordinateOptions options, StlRepairOptions repairs);
    void exportStlPath(const QString &path, StlCoordinateOptions options, StlEncoding encoding);
    void importDxfPath(const QString &path, DxfOptions options = {}, unsigned segments = 96);
    void exportDxfPath(const QString &path, double metresPerUnit, bool siteCoordinates = false);
    void importDxfDialog();
    // R095.d: File > Import terrain… (window_terrain.cpp). Reads a DXF or point file and opens
    // the import dialog, which adds the terrain to this document in one undoable edit.
    void importTerrainDialog();
    // R095.e2: File > Site settings….
    void siteSettings();
    void importTerrainPath(const QString &path);
    // R095.e1 (window_terrain.cpp): the terrain actions on the selection (Create terrain from
    // selection, Terrain settings…, Regenerate terrain, Explode terrain…), the viewport context
    // menu and the tool refusal's Explode link in the status bar.
    void showViewportMenu(const QPoint &position);
    void exportDxfDialog();
    void importStlDialog();
    void exportStlDialog();
    void importObjDialog();
    void exportObjDialog();
    // R097.e1: File › Import IFC… / Show IFC import report… / Export IFC… (window_ifc.cpp).
    // The heavy work is the helper's (sketchyup-ifc-worker); the import runs on a worker
    // thread, so the UI is never blocked. A missing helper is "not installed"; a missing
    // kernel is "limited" (ADR 0171 §16.7). Export is R097.e2, so Export IFC… opens the
    // availability sheet only.
    void importIfc();
    void exportIfc();
    void showIfcImportReport(); // the one "show last report" path, the IFC face (import_report.hpp)
    // R098.a3: File › Import STEP… (window_step.cpp). The heavy work is the same helper's
    // (sketchyup-ifc-worker, ADR 0172 section 8: one component, one helper, two directions);
    // the import runs on a worker thread (StepImportRunner), the IFC import's own runner.
    // A missing helper is the same availability sheet; the STEP import needs no kernel, so
    // the helper's Limited state (no OpenCASCADE kernel) is not a limitation for STEP.
    void importStep();
    void showStepImportReport(); // the same path, the STEP face
    // R098.c2: File › Export STEP… (window_step.cpp). The plan dialog (the schema, the written and
    // not-written lines, the size warning), then the system Save dialog, then the export on a worker
    // thread from a copy of the document (the same helper; ADR 0172 section 8). Export needs the helper,
    // not the kernel: a missing or broken helper is the availability sheet, a Limited one is not.
    void exportStep();
    // R102.c: File › Import .skp file… (window_skp.cpp, ADR 0176 section 9). The parser runs only
    // in sketchyup-skp-worker; the import runs on a worker thread (SkpImportRunner). A missing helper is
    // the availability sheet. The import has no options, so the file picker leads to the progress sheet.
    void importSkp();
    void showSkpImportReport(); // the same path, the SketchUp face
    void clearSkpReportState(); // a new model ends the SketchUp chip's lifetime, as the STEP chip's
    void setSkpWorkerPath(const QString &path); // tests, and a future "where the helper is"
    QString skpWorkerPath();
    void setSkpDeadlineSeconds(int seconds); // tests; the CLI's --skp-deadline is the public option
    // The probe, on demand (the import is opened): a missing helper is the availability sheet, a
    // broken one "cannot start" (ADR 0171 §16.7). It has its own deadline and is the user's
    // action, so it runs on the UI thread; the conversion itself is on a worker thread (16.4).
    void probeIfcNow();
    // The report chip's lifetime (16.1): it belongs to the imported model; any other document
    // clears it. Called by every document replacement and at the start of an import. One
    // shared clear for both formats (the screens refactor); the chips' words follow the face.
    void clearIfcReportState();
    void clearStepReportState(); // R098.a3: the STEP chip's lifetime is the IFC chip's own
    void clearImportReportState(ImportReportState &state, QAction *action, QPushButton *chip);
    void setIfcWorkerPath(const QString &path); // tests, and a future "where the helper is"
    QString ifcWorkerPath();
    IfcProbe ifcProbe();
    void exportRasterDialog();
    void exportMeasuredDialog();
    // R092.b: File › Export sheets… and Print… (window_sheet_export.cpp). Both open the
    // preflight dialog; Export writes a new PDF off the UI thread and shows the report sheet,
    // Print paints every page at actual size on the printer's paper.
    void exportSheetsDialog();
    void printSheetsDialog();
    // The printer and paper Print uses; tests point it at a PDF file.
    QPrinter *sheetPrinter();
    void libraryDialog(bool templates);
    // R100.f3 (UX_DESIGN §18.5, §18.1): File > Asset catalogs…, and File > Export asset catalog….
    void catalogDialog();
    void exportCatalogDialog();
    void extensionsDialog();
    // R099.d2 (UX_DESIGN §17.1): Camera > Presentations…, the dialog on its Camera paths tab.
    void presentationsDialog();
    void saveLibraryDialog(bool component);
    void openTemplatePath(const QString &path);
    void openTemplateBundle(const struct TemplateBundle &bundle);

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    bool closePending_{};
    bool fileOperation_{};
    Document doc_;
    Viewport *viewport_{};
    RenderPanel *render_{};
    PresentationsDialog *presentations_{};
    AssistantPanel *assistant_{};
    QHBoxLayout *content_{};
    QTabWidget *sideTabs_{};
    QPointer<QDialog> diagnosticsSheet_;
    // R094.c: Entity info's placement parameters and the Measurements box routed to them.
    ComponentParametersSection *parameters_{};
    bool parameterMeasurement_{};
    void syncParameterMeasurement();
    // R094.d (ADR 0166 §2, §3; window_components.cpp): the definition context's sections, the
    // Preview strip under the banner, the rail's Add driver and the Measurements box "Plane".
    ComponentDefinitionSection *definitionSection_{};
    ComponentDriversSection *driversSection_{};
    bool planeMeasurement_{};
    QWidget *previewStrip_{};
    QLabel *previewError_{};
    QTimer *previewTimer_{};
    QString previewSignature_, previewKey_;
    Id previewScope_{}, statusScope_{};
    std::vector<std::pair<QJsonObject, QWidget *>> previewFields_;
    QAction *railSeparator_{}, *railAddDriver_{};
    void setupDefinitionAuthoring();
    void syncPreviewStrip(Id scope, bool parametric);
    void previewDefaults();
    void preview();
    void addDriver();
    // R094.e (ADR 0166 §§4, 5; window_components.cpp): the "has its own values" dialog, the
    // overrides suspended while a definition is edited at its defaults, the changed-since-bound
    // strip and the §5 messages' actions.
    struct ParameterSuspension {
        std::string document;
        Id placement{};
        QJsonObject values; // the placement's overrides, SI values by parameter name
        QJsonObject labels; // their labels, for a parameter the definition no longer has
        QString name;       // the placement's name, for labels and the status bar
        Document::AmendStamp stamp;
    };
    std::optional<ParameterSuspension> suspension_;
    Id templateScope_{};
    bool templateScopeOwn_{}, parameterStatesBusy_{}, parameterStatesDetached_{};
    QWidget *statusStrip_{};
    QLabel *statusStripText_{};
    QPushButton *statusRebind_{}, *statusUndo_{}, *previewAction_{};
    Id previewActionDriver_{}, previewActionDefinition_{};
    std::string stripDocument_;
    Id stripDefinition_{};
    std::uint64_t stripRevision_{};
    std::size_t stripPosition_{};
    std::set<Id> stripChanged_;
    QJsonArray stripDrivers_;
    void setupParameterStates();
    void detachParameterStates();
    bool openPlacement(Id placement);
    void syncParameterStates();
    void finishSuspension();
    void syncStatusStrip(Id scope, bool parametric);
    void placeParameterStrips();
    void parameterAction(int action, Id target, Id definition);
    void showGeometryDiagnostics();
    bool assistantShown_{}, assistantFenced_{};
    int assistantRecoveryInterval_{};
    std::map<QAction *, bool> assistantActionStates_;
    std::vector<std::pair<QWidget *, bool>> assistantWidgetStates_;
    void layoutAssistant();
    // R094.a: the user-resizable sidebar (window_sidebar.cpp). The workspace, the side panel and,
    // in the wide layout, the Assistant column share one splitter. Widths are kept per layout in
    // logical pixels at 100% interface text (0 = default) and persist in QSettings.
    enum SidebarWidth { SidebarTray, SidebarTabbed, SidebarAssistant };
    QSplitter *sidebarSplitter_{};
    std::array<int, 3> sidebarWidths_{};
    bool sidebarLayoutQueued_{};
    void setupSidebar(QWidget *workspace);
    void detachSidebar();
    void layoutSidebar();
    void queueSidebarLayout();
    void rememberSidebar(int handle);
    void storeSidebarWidth(QWidget *column, int width);
    void stepSidebar(int direction);
    void resetSidebar();
    void themeSidebar(const ThemeColors &colors);
    // Stores a sidebar column's width (logical pixels at the current text size) at 100% text.
    void storeSidebarColumn(int column, int width);
    // Narrow layout (UX_DESIGN §3.3; window_narrow.cpp). Below 1100 px the side panel is an
    // icon strip at the window's right edge that opens the Model panel or the Assistant in an
    // overlay sheet over the workspace, and the menu bar is one Menu button.
    enum class PanelKey { None, Info, Outliner, Materials, Tags, History, Assistant, More };
    QToolBar *panelStrip_{};
    QWidget *panelOverlay_{}, *overlayHandle_{}, *menuButton_{};
    // The sheet's heading: the open panel's name, in place of the Tray's "MODEL".
    QLabel *overlayHeading_{};
    // Owns the narrow layout's connections and the overlay's dismiss filter; deleted first in
    // ~Window so none of them reaches the window while it is torn down.
    QObject *narrowContext_{}, *overlayDismiss_{};
    std::vector<std::pair<PanelKey, QAction *>> stripActions_;
    QPointer<QWidget> overlayReturn_;
    PanelKey overlayKey_{PanelKey::None};
    bool overlayOpen_{}, overlayDismissPending_{};
    bool narrowLayout() const { return width() < 1100; }
    void setupNarrow(QWidget *header);
    void detachNarrow();
    void layoutNarrow();
    // The tool rail's groups (UX_DESIGN §2 item 2, §3.3; window_narrow.cpp). Every action on the
    // rail has one group, assigned where the rail is built (window.cpp). Below 800 px the rail
    // shows one group behind a group button; from 800 px up it is unchanged.
    enum RailGroup {
        RailSelect,
        RailDraw,
        RailModify,
        RailMeasure,
        RailTerrain,
        RailCamera,
        RailGroupCount
    };
    // The actions the rail holds in rail order, each with its group (-1: a separator, on the
    // wide rail only), and the group button's action.
    std::vector<std::pair<QAction *, int>> railTools_;
    QAction *railGroupAction_{};
    QToolButton *railGroupButton_{};
    QMenu *railGroupMenu_{};
    int railGroup_{RailSelect};
    bool railCompact_{};
    bool compactRail() const { return width() < 800; }
    void setupRailGroups();
    void showRailGroup(int group);
    void labelRailGroup();
    void layoutRail();
    void themeRail(const ThemeColors &colors);
    // Sizes and anchors the overlay sheet (window_sidebar.cpp): the remembered tabbed width,
    // clamped to the window, against the workspace's right edge.
    void placeOverlay();
    QWidget *createOverlayHandle(QWidget *overlay);
    void resizeOverlay(int panelWidth);
    // `returnTo`: where focus goes back when the sheet closes, when not the current focus.
    void openOverlay(PanelKey key, QWidget *returnTo = nullptr);
    void closeOverlay(bool restoreFocus = true);
    void focusOverlay();
    void syncPanelStrip();
    // The sheet shows the Tray's tab row only for More panels; a strip panel shows its name as
    // the heading instead. Docked, the Tray has its row and its own heading.
    void syncOverlayChrome();
    void themeNarrow(const ThemeColors &colors);
    PanelKey organizationKey() const;
    void stripClicked(PanelKey key);
    bool overlayFilter(QObject *object, QEvent *event);
    void toggleAssistant();
    void assistantFence(bool uncertain);
    std::unique_ptr<DesktopInspection> inspection_;
    QTreeWidget *outliner_{};
    OrganizationPanel *organization_{};
    QLabel *info_{};
    QLabel *status_{};
    QLabel *measurementUnits_{};
    static DisplayUnit preferredUnits();
    static int preferredPrecision();
    void unitsSettings(bool firstRun = false);
    QLabel *title_{};
    QLabel *breadcrumb_{};
    QLabel *componentBanner_{};
    QLineEdit *measurements_{};
    QWidget *tray_{};
    QString path_;
    RecoveryController *recovery_{};
    RecoveryContext recoveryContext_;
    QLabel *recoveryStatus_{};
    QString recoveredName_, saveFailure_;
    QWidget *saveBanner_{};
    QLabel *saveBannerText_{};
    void syncRecovery();
    void recoverySettings();
    void clearRecovery();
    void resetRecoveryContext();
    QAction *undo_{};
    QAction *redo_{};
    std::vector<QAction *> publicActions_;
    void initializeShortcuts();
    void applyShortcuts(const class ShortcutBindings &bindings);
    void shortcutSettings();
    void sync();
    void addHostedActions(QMenu *menu);
    void hostedGlueDialog();
    void hostedOptionsDialog();
    void syncHostedActions();
    void addComponentActions(QMenu *menu);
    void componentDialog(const QString &operation);
    void syncComponentActions();
    void measurementError(bool invalid);
    void applyTheme();
    int themeMode_{0}; // System, light, dark.
    void run(const std::function<void()> &action, bool viewOnly = false);
    bool save(bool saveAs = false);
    bool canReplace();
    void palette();
    void focusRegion(bool previous = false);
    // Sheets workspace (window_sheets.cpp).
    Workspace workspace_{Workspace::Model};
    std::unique_ptr<DrawingViewRenderCache> sheetCache_;
    QStackedWidget *workspaceStack_{}, *railStack_{};
    QTabWidget *modelTabs_{};
    QToolBar *modelRail_{}, *sheetRail_{};
    SheetWorkspace *sheets_{};
    SheetsTray *sheetsTray_{};
    QLabel *activeTool_{};
    QWidget *workspaceToggle_{};
    std::vector<QWidget *> modelTrayWidgets_;
    std::vector<QWidget *> modelRegions_, sheetRegions_;
    std::set<QAction *> workspaceSuspended_;
    QString modelStatus_, modelPlaceholder_;
    void setupSheets(QWidget *header, QMenu *view);
    void syncSheets();
    void themeSheets(const ThemeColors &colors);
    void syncWorkspaceActions();
    void sheetTool(int tool);
    QJsonObject sheetCommands(const QJsonArray &commands, const QString &label);
    // R091.f: the canvas's commits. One labelled sheet-only entry, or an amendment of the
    // previous one when the amend key repeats and nothing changed since (ADR 0164 §5).
    QJsonObject sheetCommit(const QJsonArray &commands, const QString &label,
                            const QString &amendKey);
    QString sheetAmendKey_, sheetCommitError_, sheetInstruction_;
    Document::AmendStamp sheetAmendStamp_;
    QLabel *measurementStep_{};
    void refreshSheetStep();
    // Enables the sheet actions for the page, the selection and edit-view.
    void syncSheetActions();
    void refreshPlacement();
    void chooseViewScene(Id view);
    void deleteSheetView();
    // R091.g: page items.
    void deleteSheetItem(Id item);
    void restackSheetItem(Id item, int delta);
    void insertTitleBlock();
    void saveViewScene(Id view);
    void showViewScene(Id view);
    void addSheetPage();
    void moveSheetPage(Id sheet, int delta);
    // R092.b: the export and print flow (window_sheet_export.cpp).
    QPointer<SheetExportDialog> sheetExport_;
    QPointer<TerrainImportDialog> terrainImport_;
    void setupTerrainActions();
    void syncTerrainActions();
    QPushButton *statusExplode_{};
    Id statusExplodeTerrain_{};
    QString terrainStatusSaved_, terrainStatusShown_;
    // R096.f1 (UX_DESIGN §15.1): the Terrain menu, the rail's Terrain slot (the last terrain tool
    // used; "Terrain" until one is), the tool options strip and the terrain tool actions.
    QMenu *terrainMenu_{};
    QAction *terrainRail_{}, *terrainHeading_{};
    TerrainToolStrip *terrainStrip_{};
    Viewport::Tool lastTerrainTool_{Viewport::Tool::Sculpt};
    bool terrainToolUsed_{}, terrainMeasurement_{};
    void setupTerrainTools(QActionGroup *group, QToolBar *rail);
    void setupTerrainMenu();
    void syncTerrainTools();
    static QString measurementPlaceholder(Viewport::Tool tool);
    std::unique_ptr<QPrinter> sheetPrinter_;
    ThemeColors sheetColors_{themeColors(false)};
    void sheetExportFlow(bool print);
    void exportSheetsTo(const QString &path, SheetExportOptions options,
                        const SheetExportPreflight &preflight);
    void printSheetsTo(QPrinter &printer, SheetExportOptions options,
                       const SheetExportPreflight &preflight);
    void sheetExportReport(const QString &path, const SheetExport &exported);
    void goToSheetView(Id view);
    void chooseSheetItemFont(Id item);
    void focusWorkspace();
    bool sheetMeasurements(const QString &text);
    ShortcutScope actionScope(const QAction *action) const;
    void rememberPath(const QString &path);
    QStringList recentFiles_;
    std::vector<QWidget *> focusRegions_;
    // R097.e1: the IFC import's state (window_ifc.cpp).
    QString ifcWorkerPath_;
    IfcProbe ifcProbe_;
    // The import reports (the screens refactor): one state type, one chip function and one
    // show path for the IFC and STEP imports (import_report.hpp). The report chip is a button
    // on the bottom status row (the design's mockup), hidden until an import has produced it;
    // a hidden widget takes no space, so the window's layout is untouched until then. A new
    // format (R102.c, the .skp import) adds one state and its strings.
    ImportReportState ifcImportReport_;
    ImportReportState stepImportReport_;
    QPushButton *ifcReportChip_{};
    QAction *ifcReportAction_{};
    QPushButton *stepReportChip_{};
    QAction *stepReportAction_{};
    // R102.c: the SketchUp import's state (window_skp.cpp): the helper's path (empty: found beside the
    // application), the helper's deadline, and the report, chip and action as the STEP's own.
    QString skpWorkerPath_;
    int skpDeadlineSeconds_{120};
    ImportReportState skpImportReport_;
    QPushButton *skpReportChip_{};
    // R100.f1 (UX_DESIGN §18.3): the edge texture chip, shown while the model's edge stroke names a
    // texture that is missing. It opens the Styles tab.
    QPushButton *edgeTextureChip_{};
    QAction *skpReportAction_{};
    void tool(Viewport::Tool tool, const QString &instruction);
    // Scope defaults to Model for model tools and viewport-only keys, otherwise Global; the
    // same shortcut may be used once per non-overlapping scope (ADR 0164 §4).
    QAction *action(const QString &id, const QString &title, const QKeySequence &shortcut,
                    const std::function<void()> &fn,
                    std::optional<ShortcutScope> scope = std::nullopt);
};
} // namespace sketchy
