#pragma once
#include "app/assistant_panel.hpp"
#include "app/organization_panel.hpp"
#include "app/recovery_controller.hpp"
#include "app/viewport.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <functional>
class QTabWidget;
class QDialog;
class QHBoxLayout;
namespace sketchy {
class DesktopInspection;
class RenderPanel;
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
    QJsonObject inspect(const QJsonObject &request);
    void demo();
    void openPath(const QString &path);
    void importFormlinePath(const QString &path);

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    Document doc_;
    Viewport *viewport_{};
    RenderPanel *render_{};
    AssistantPanel *assistant_{};
    QHBoxLayout *content_{};
    QTabWidget *sideTabs_{};
    QPointer<QDialog> assistantSheet_;
    bool assistantShown_{}, assistantFenced_{};
    int assistantRecoveryInterval_{};
    std::map<QAction *, bool> assistantActionStates_;
    std::vector<std::pair<QWidget *, bool>> assistantWidgetStates_;
    void layoutAssistant();
    void toggleAssistant();
    void assistantFence(bool uncertain);
    std::unique_ptr<DesktopInspection> inspection_;
    QTreeWidget *outliner_{};
    OrganizationPanel *organization_{};
    QLabel *info_{};
    QLabel *status_{};
    QLabel *measurementUnits_{};
    static DisplayUnit preferredUnits();
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
    void sync();
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
    void rememberPath(const QString &path);
    QStringList recentFiles_;
    std::vector<QWidget *> focusRegions_;
    void tool(Viewport::Tool tool, const QString &instruction);
    QAction *action(const QString &id, const QString &title, const QKeySequence &shortcut,
                    const std::function<void()> &fn);
};
} // namespace sketchy
