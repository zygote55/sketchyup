#pragma once
// R099.d2 (UX_DESIGN §17.1 to §17.3, §17.8, §17.10; ADR 0173 §2, §3, §7): the Presentations dialog.
// It is a non-modal window with two tabs, Camera paths and Render presets. Every edit is one
// camera_path.* or render_preset.* command through Viewport::editPresentations, so each one is one
// undo step. A new path is a draft in the dialog until it is valid; the model does not change until
// Create path.
#include "core/model.hpp"
#include "core/presentation_records.hpp"
#include <QDialog>
#include <QJsonArray>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QResizeEvent;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QTabBar;

namespace sketchy {

class Viewport;

// The steps of a typed number of seconds (§17.2, the one-shot sheet's mapping): a transition is
// max(1, round(seconds × fps)) steps and a hold is round(seconds × fps).
int transitionStepsFor(double seconds, int framesPerSecond);
int holdStepsFor(double seconds, int framesPerSecond);

// The most keyframes a path can keep at a timing and still plan 240 frames or fewer (2 to 32). Zero
// when not even two fit.
int scenesThatFit(CameraPathTiming timing);

// Why a name cannot be used (§17.2, §17.3), or an empty string when it can: empty, leading or
// trailing spaces, control characters, more than 1024 bytes, or a clash with `taken`.
QString presentationNameProblem(const QString &name, const QStringList &taken,
                                const QString &duplicate);

class PresentationsDialog : public QDialog {
  public:
    // `currentSettings` gives the render sheet's values for "New from current settings".
    PresentationsDialog(Document &doc, Viewport &view,
                        std::function<RenderSettings()> currentSettings, QWidget *parent = nullptr);
    void showCameraPaths();
    void showRenderPresets();
    // Re-reads the model: called when the model changes, including undo and redo.
    void refresh();
    // Stops every signal of the dialog before its document goes (the Window's destructor does
    // this).
    void detach();
    // R099.d3: the footer's Export batch… opens the batch dialog. Until it is set, the button is disabled.
    void setExportBatch(std::function<void()> open);

  protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    void stackAtLargeText();
    struct Draft {
        QString name;
        std::vector<Id> keyframes;
        CameraPathTiming timing;
    };
    Document &doc_;
    Viewport &view_;
    std::function<RenderSettings()> currentSettings_;
    Document::SaveStamp session_;
    std::optional<Draft> draft_;
    bool draftSelected_{};
    Id selectedPath_{}, selectedPreset_{};
    QTabBar *tabs_{};
    QStackedWidget *pages_{};
    QSplitter *pathSplit_{}, *presetSplit_{};
    // Camera paths (§17.2)
    QListWidget *pathList_{}, *keyframeList_{};
    QPushButton *newPath_{}, *deletePath_{}, *addScene_{}, *moveEarlier_{}, *moveLater_{};
    QPushButton *removeKeyframe_{}, *cancelDraft_{}, *createPath_{};
    QLabel *pathCap_{}, *pathStatus_{}, *pathError_{}, *pathCount_{}, *pathLimit_{},
        *keyframeNote_{};
    QLabel *transitionSteps_{}, *holdSteps_{};
    QLineEdit *pathName_{};
    QSpinBox *framesPerSecond_{};
    QDoubleSpinBox *transition_{}, *hold_{};
    QWidget *pathEditor_{};
    // Render presets (§17.3)
    QListWidget *presetList_{};
    QPushButton *newPreset_{}, *duplicatePreset_{}, *deletePreset_{}, *fromCurrent_{};
    QLabel *presetCap_{}, *presetError_{};
    QLineEdit *presetName_{};
    QRadioButton *cycles_{}, *eevee_{};
    QSpinBox *width_{}, *height_{}, *samples_{}, *seed_{};
    QWidget *presetEditor_{};
    // The footer
    QLabel *footerNote_{};
    QPushButton *exportBatch_{}, *close_{};

    QWidget *buildPaths();
    QWidget *buildPresets();
    void refreshPaths();
    void refreshPresets();
    void showPath();
    void showPreset();
    void updatePathCount();
    void updateDraftStatus();
    void updateStepLabels();
    // Runs one batch of camera_path.* or render_preset.* commands. Returns the refusal, or an empty
    // string when the edit was made.
    QString commit(const QJsonArray &commands, const QString &label);
    void showRefusal(QLabel *label, const QString &text);
    void presetRefused(const QString &text);
    void selectPath(Id path, bool draft);
    void selectPreset(Id preset);
    void newPathDraft();
    void createDraftPath();
    void cancelDraftPath();
    void deleteSelectedPath();
    void renameSelectedPath();
    void changeTiming();
    // Returns the refusal for a new set of keyframes, or an empty string when the path now has
    // them.
    QString replaceKeyframes(const std::vector<Id> &keyframes);
    void addSceneToPath();
    void moveKeyframe(int direction);
    void removeKeyframe();
    void newPresetFrom(const QString &name, RenderSettings settings);
    void duplicateSelectedPreset();
    void deleteSelectedPreset();
    void renameSelectedPreset();
    void changePresetSettings();
    CameraPathPtr selectedRecord() const;
    RenderPresetPtr selectedPresetRecord() const;
    std::vector<Id> keyframesInEditor() const;
    CameraPathTiming timingInEditor() const;
    QStringList pathNames(Id except) const;
    QStringList presetNames(Id except) const;
    QString draftProblem() const;
    QString nextPathName() const;
    QString nextPresetName() const;
    RenderSettings presetSettingsInEditor() const;
};

} // namespace sketchy
