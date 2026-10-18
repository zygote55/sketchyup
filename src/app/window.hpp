#pragma once
#include "app/assistant_panel.hpp"
#include "app/organization_panel.hpp"
#include "app/recovery_controller.hpp"
#include "app/shortcut_bindings.hpp"
#include "app/theme.hpp"
#include "app/viewport.hpp"
#include "io/dxf_export.hpp"
#include "io/obj_source.hpp"
#include "io/stl_export.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
class QTabWidget;
class QSplitter;
class QDialog;
class QPushButton;
class QHBoxLayout;
class QStackedWidget;
class QTimer;
class QToolBar;
class QPrinter;
namespace sketchy {
class ComponentParametersSection;
class ComponentDefinitionSection;
class ComponentDriversSection;
class DesktopInspection;
class RenderPanel;
class DrawingViewRenderCache;
class SheetWorkspace;
class SheetsTray;
class SheetExportDialog;
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
    void importFormlinePath(const QString &path);
    void importGltfPath(const QString &path);
    void importObjPath(const QString &path, ObjImportOptions options);
    void exportObjPath(const QString &path, ObjImportOptions options);
    void importStlPath(const QString &path, StlCoordinateOptions options, StlRepairOptions repairs);
    void exportStlPath(const QString &path, StlCoordinateOptions options, StlEncoding encoding);
    void importDxfPath(const QString &path, DxfOptions options = {}, unsigned segments = 96);
    void exportDxfPath(const QString &path, double metresPerUnit);
    void importDxfDialog();
    void exportDxfDialog();
    void importStlDialog();
    void exportStlDialog();
    void importObjDialog();
    void exportObjDialog();
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
    void extensionsDialog();
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
    void tool(Viewport::Tool tool, const QString &instruction);
    // Scope defaults to Model for model tools and viewport-only keys, otherwise Global; the
    // same shortcut may be used once per non-overlapping scope (ADR 0164 §4).
    QAction *action(const QString &id, const QString &title, const QKeySequence &shortcut,
                    const std::function<void()> &fn,
                    std::optional<ShortcutScope> scope = std::nullopt);
};
} // namespace sketchy
