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
class QHBoxLayout;
class QStackedWidget;
class QToolBar;
class QPrinter;
namespace sketchy {
class ComponentParametersSection;
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
    QPointer<QDialog> assistantSheet_;
    QPointer<QDialog> diagnosticsSheet_;
    // R094.c: Entity info's placement parameters and the Measurements box routed to them.
    ComponentParametersSection *parameters_{};
    bool parameterMeasurement_{};
    void syncParameterMeasurement();
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
