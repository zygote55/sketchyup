#include "app/presentations_dialog.hpp"
#include "app/keypad_decimal.hpp"
#include "app/presentation_scene_picker.hpp"
#include "app/viewport.hpp"
#include "core/scenes.hpp"
#include <QAbstractSpinBox>
#include <QAction>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>

namespace sketchy {
namespace {

// The words the Camera paths list uses for a clash (§17.2, the engine's own text).
constexpr auto kPathDuplicate = "Camera path names must be unique";
constexpr auto kPresetDuplicate = "Render preset names must be unique";

// The first of "stem", "stem 2", "stem 3", … that no name in `taken` uses.
QString uniqueName(const QString &stem, const QStringList &taken) {
    if (!taken.contains(stem))
        return stem;
    for (int suffix = 2;; ++suffix) {
        const auto candidate = stem + " " + QString::number(suffix);
        if (!taken.contains(candidate))
            return candidate;
    }
}

// "Path 1", "Path 2", … : the first number that no name in `taken` uses.
QString numberedName(const QString &stem, const QStringList &taken) {
    for (int number = 1;; ++number) {
        const auto candidate = stem + " " + QString::number(number);
        if (!taken.contains(candidate))
            return candidate;
    }
}

// Seconds for a frame count, one decimal, as the list and the live line show it (§17.2).
QString secondsText(size_t frames, int framesPerSecond) {
    return QString::number(double(frames) / framesPerSecond, 'f', 1);
}

// Why a path of `scenes` keyframes at `timing` cannot be kept, or an empty string when it can. The
// words are §17.8's, not the planner's; the formula is the planner's own (§17.2, §17.11 gap 3).
QString limitProblem(const QString &name, size_t scenes, CameraPathTiming timing) {
    if (scenes > cameraPathKeyframeLimit)
        return "A path needs two to thirty-two scenes.";
    const auto frames = cameraPathFrameCount(scenes, timing);
    if (frames <= cameraPathFrameLimit)
        return {};
    if (scenesThatFit(timing) == 0)
        return QString("%1 would render %2 frames; the limit is %3. At this timing, no path of two "
                       "scenes fits. Shorten the transition or the hold.")
            .arg(name)
            .arg(frames)
            .arg(cameraPathFrameLimit);
    return QString(
               "%1 would render %2 frames; the limit is %3. At this timing, %4 scenes is the most. "
               "Remove a scene, or shorten the transition or the hold.")
        .arg(name)
        .arg(frames)
        .arg(cameraPathFrameLimit)
        .arg(scenesThatFit(timing));
}

// The planner's arithmetic, shown in small type under a refusal (§17.2, §17.8).
QString limitFormula(size_t scenes, CameraPathTiming timing) {
    return QString("Camera path would plan %1 frames (1 + %2 x %3 + %4 x %5); the limit is %6")
        .arg(cameraPathFrameCount(scenes, timing))
        .arg(scenes)
        .arg(timing.holdSteps)
        .arg(scenes ? scenes - 1 : 0)
        .arg(timing.transitionSteps)
        .arg(cameraPathFrameLimit);
}

// The camera paths in the order the list shows them.
std::vector<Id> pathsInOrderIds(const Document &doc) {
    std::vector<std::pair<std::uint32_t, Id>> order;
    for (const auto &[id, path] : doc.cameraPaths())
        order.emplace_back(path->position, id);
    std::sort(order.begin(), order.end());
    std::vector<Id> ids;
    for (const auto &entry : order)
        ids.push_back(entry.second);
    return ids;
}

// The "what is wrong" words of a broken keyframe (§17.8): the first keyframe that names no usable
// scene.
QString brokenKeyframe(const CameraPathReferences &references, const std::vector<Id> &keyframes) {
    for (size_t index = 0; index < keyframes.size(); ++index) {
        const auto scene = keyframes[index];
        if (references.sceneMissing.contains(scene))
            return QString("keyframe %1 names a missing scene").arg(index + 1);
        if (references.sceneHasNoCamera.contains(scene))
            return QString("keyframe %1 has no camera").arg(index + 1);
    }
    return {};
}

// A spin box is named on its own control and on the line edit inside it, which the audit reaches
// too.
void nameSpin(QAbstractSpinBox *spin, const QString &name) {
    spin->setAccessibleName(name);
    if (auto *edit = spin->findChild<QLineEdit *>())
        edit->setAccessibleName(name);
}

QString engineText(RenderEngine engine) {
    return engine == RenderEngine::Eevee ? "Eevee preview" : "Cycles";
}

} // namespace

int transitionStepsFor(double seconds, int framesPerSecond) {
    return std::max(1, int(std::lround(seconds * framesPerSecond)));
}

int holdStepsFor(double seconds, int framesPerSecond) {
    return int(std::lround(seconds * framesPerSecond));
}

int scenesThatFit(CameraPathTiming timing) {
    int fit = 0;
    for (int scenes = 2; scenes <= int(cameraPathKeyframeLimit); ++scenes)
        if (cameraPathFrameCount(size_t(scenes), timing) <= cameraPathFrameLimit)
            fit = scenes;
    return fit;
}

QString presentationNameProblem(const QString &name, const QStringList &taken,
                                const QString &duplicate) {
    if (name.isEmpty())
        return "Give it a name.";
    if (name.startsWith(' ') || name.endsWith(' '))
        return "A name cannot start or end with a space.";
    for (const auto character : name)
        if (character.unicode() < 32 || character.unicode() == 127)
            return "A name cannot contain control characters.";
    if (name.toUtf8().size() > 1024)
        return "A name is at most 1024 bytes.";
    if (taken.contains(name))
        return duplicate;
    return {};
}

PresentationsDialog::PresentationsDialog(Document &doc, Viewport &view,
                                         std::function<RenderSettings()> currentSettings,
                                         QWidget *parent)
    : QDialog(parent), doc_(doc), view_(view), currentSettings_(std::move(currentSettings)),
      session_(doc.saveStamp()) {
    setObjectName("presentationsDialog");
    setWindowTitle("Presentations");
    setModal(false);
    resize(1100, 740);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("Camera paths and render presets for this model. Each change is one "
                             "undo step; the model stays open behind this window.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    // A tab bar over a stack of pages: the bar is the one Tab stop of the two tabs, and each page
    // is a plain widget (a QTabWidget's own focus reaches the bar only indirectly).
    tabs_ = new QTabBar;
    tabs_->setObjectName("presentationsTabs");
    tabs_->setAccessibleName("Presentations sections");
    tabs_->addTab("Camera paths");
    tabs_->addTab("Render presets");
    layout->addWidget(tabs_);
    pages_ = new QStackedWidget;
    pages_->setObjectName("presentationsPages");
    pages_->addWidget(buildPaths());
    pages_->addWidget(buildPresets());
    layout->addWidget(pages_, 1);
    connect(tabs_, &QTabBar::currentChanged, pages_, &QStackedWidget::setCurrentIndex);
    auto *footer = new QHBoxLayout;
    footerNote_ = new QLabel("Each change is one undo step (Ctrl+Z, or History).");
    footerNote_->setObjectName("presentationsFooterNote");
    footerNote_->setWordWrap(true);
    footer->addWidget(footerNote_, 1);
    exportBatch_ = new QPushButton("Export batch…");
    exportBatch_->setObjectName("presentationsExportBatch");
    exportBatch_->setAccessibleName("Export batch");
    exportBatch_->setEnabled(false);
    exportBatch_->setToolTip("Presentation batches are not available in this version yet.");
    exportBatch_->setAccessibleDescription(exportBatch_->toolTip());
    footer->addWidget(exportBatch_);
    close_ = new QPushButton("Close");
    close_->setObjectName("presentationsClose");
    close_->setAccessibleName("Close");
    // Close is not the default: Qt sends Enter from a field to the default button, and in this
    // non-modal editor Enter commits the field. Esc closes the dialog (§17.10).
    close_->setDefault(false);
    footer->addWidget(close_);
    layout->addLayout(footer);
    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
    // Enter in a field commits the field. A dialog with no default button passes Enter to its first
    // auto-default button, which would press New path, so the buttons here are not auto-default.
    for (auto *button : findChildren<QPushButton *>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    connect(&view_, &Viewport::changed, this, [this] { refresh(); });
    refresh();
}

void PresentationsDialog::detach() {
    view_.disconnect(this);
    blockSignals(true);
    for (auto *child : findChildren<QObject *>())
        child->blockSignals(true);
}

void PresentationsDialog::showCameraPaths() {
    tabs_->setCurrentIndex(0);
    show();
    raise();
    activateWindow();
    pathList_->setFocus();
}

void PresentationsDialog::showRenderPresets() {
    tabs_->setCurrentIndex(1);
    show();
    raise();
    activateWindow();
    presetList_->setFocus();
}

QWidget *PresentationsDialog::buildPaths() {
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    pathSplit_ = new QSplitter(Qt::Horizontal);
    pathSplit_->setObjectName("cameraPathSplit");
    pathSplit_->setAccessibleName("Camera paths and editor");
    pathSplit_->setChildrenCollapsible(false);
    outer->addWidget(pathSplit_, 1);

    // The list pane: the paths, with New path and Delete path under it (§17.2).
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    pathList_ = new QListWidget;
    pathList_->setObjectName("cameraPathList");
    pathList_->setAccessibleName("Camera paths");
    leftLayout->addWidget(pathList_, 1);
    auto *pathButtons = new QHBoxLayout;
    newPath_ = new QPushButton("New path");
    newPath_->setObjectName("cameraPathNew");
    newPath_->setAccessibleName("New path");
    pathButtons->addWidget(newPath_);
    deletePath_ = new QPushButton("Delete path");
    deletePath_->setObjectName("cameraPathDelete");
    deletePath_->setAccessibleName("Delete path");
    pathButtons->addWidget(deletePath_);
    leftLayout->addLayout(pathButtons);
    pathCap_ = new QLabel;
    pathCap_->setObjectName("cameraPathCap");
    pathCap_->setWordWrap(true);
    leftLayout->addWidget(pathCap_);
    auto *help = new QLabel("Help: a path plays its scenes in the order listed. The keyframe's own "
                            "camera is the view that was saved in its scene.");
    help->setWordWrap(true);
    leftLayout->addWidget(help);
    pathSplit_->addWidget(left);

    // The editor pane. It scrolls at a large text size, so the footer stays in view (§17.10).
    pathEditor_ = new QWidget;
    auto *editor = new QVBoxLayout(pathEditor_);
    editor->setContentsMargins(4, 0, 4, 0);
    auto *nameRow = new QHBoxLayout;
    auto *nameLabel = new QLabel("Name");
    nameRow->addWidget(nameLabel);
    pathName_ = new QLineEdit;
    pathName_->setObjectName("cameraPathName");
    pathName_->setAccessibleName("Camera path name");
    pathName_->setMaxLength(1024);
    nameRow->addWidget(pathName_, 1);
    editor->addLayout(nameRow);
    pathStatus_ = new QLabel;
    pathStatus_->setObjectName("cameraPathDraftStatus");
    pathStatus_->setWordWrap(true);
    editor->addWidget(pathStatus_);
    pathError_ = new QLabel;
    pathError_->setObjectName("cameraPathError");
    pathError_->setWordWrap(true);
    pathError_->setVisible(false);
    editor->addWidget(pathError_);
    auto *keyframeHeading = new QLabel("Keyframes (scenes play in this order; a scene may repeat)");
    keyframeHeading->setStyleSheet("font-weight: 600;");
    editor->addWidget(keyframeHeading);
    keyframeList_ = new QListWidget;
    keyframeList_->setObjectName("cameraPathKeyframes");
    keyframeList_->setAccessibleName("Keyframes, in playing order");
    keyframeList_->setMinimumHeight(140);
    editor->addWidget(keyframeList_, 1);
    auto *keyframeButtons = new QHBoxLayout;
    addScene_ = new QPushButton("Add scene…");
    addScene_->setObjectName("cameraPathAddScene");
    addScene_->setAccessibleName("Add scene");
    keyframeButtons->addWidget(addScene_);
    moveEarlier_ = new QPushButton("Move earlier");
    moveEarlier_->setObjectName("cameraPathMoveEarlier");
    moveEarlier_->setAccessibleName("Move keyframe earlier");
    keyframeButtons->addWidget(moveEarlier_);
    moveLater_ = new QPushButton("Move later");
    moveLater_->setObjectName("cameraPathMoveLater");
    moveLater_->setAccessibleName("Move keyframe later");
    keyframeButtons->addWidget(moveLater_);
    removeKeyframe_ = new QPushButton("Remove");
    removeKeyframe_->setObjectName("cameraPathRemoveKeyframe");
    removeKeyframe_->setAccessibleName("Remove keyframe");
    keyframeButtons->addWidget(removeKeyframe_);
    keyframeButtons->addStretch(1);
    editor->addLayout(keyframeButtons);
    keyframeNote_ = new QLabel("Alt+↑ / Alt+↓ move · Delete removes");
    keyframeNote_->setObjectName("cameraPathKeyframeNote");
    keyframeNote_->setWordWrap(true);
    editor->addWidget(keyframeNote_);

    auto *timing = new QFormLayout;
    timing->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *timingHeading = new QLabel("Timing");
    timingHeading->setStyleSheet("font-weight: 600;");
    editor->addWidget(timingHeading);
    framesPerSecond_ = new QSpinBox;
    framesPerSecond_->setObjectName("cameraPathFramesPerSecond");
    nameSpin(framesPerSecond_, "Frames per second");
    framesPerSecond_->setRange(1, 60);
    framesPerSecond_->setSuffix(" fps");
    framesPerSecond_->setKeyboardTracking(false);
    timing->addRow("Frames per second", framesPerSecond_);
    transition_ = new QDoubleSpinBox;
    transition_->setObjectName("cameraPathTransition");
    nameSpin(transition_, "Transition between scenes, seconds");
    transition_->setRange(0.05, 10);
    transition_->setDecimals(2);
    transition_->setSingleStep(0.05);
    transition_->setSuffix(" s");
    transition_->setKeyboardTracking(false);
    acceptKeypadDecimal(transition_);
    auto *transitionRow = new QHBoxLayout;
    transitionRow->addWidget(transition_);
    transitionSteps_ = new QLabel;
    transitionSteps_->setObjectName("cameraPathTransitionSteps");
    transitionRow->addWidget(transitionSteps_, 1);
    timing->addRow("Transition between scenes", transitionRow);
    hold_ = new QDoubleSpinBox;
    hold_->setObjectName("cameraPathHold");
    nameSpin(hold_, "Hold each scene, seconds");
    hold_->setRange(0, 5);
    hold_->setDecimals(2);
    hold_->setSingleStep(0.05);
    hold_->setSuffix(" s");
    hold_->setKeyboardTracking(false);
    acceptKeypadDecimal(hold_);
    auto *holdRow = new QHBoxLayout;
    holdRow->addWidget(hold_);
    holdSteps_ = new QLabel;
    holdSteps_->setObjectName("cameraPathHoldSteps");
    holdRow->addWidget(holdSteps_, 1);
    timing->addRow("Hold each scene", holdRow);
    editor->addLayout(timing);

    pathCount_ = new QLabel;
    pathCount_->setObjectName("cameraPathCount");
    pathCount_->setAccessibleName("Planned frames");
    pathCount_->setWordWrap(true);
    editor->addWidget(pathCount_);
    pathLimit_ = new QLabel;
    pathLimit_->setObjectName("cameraPathLimit");
    pathLimit_->setWordWrap(true);
    pathLimit_->setVisible(false);
    editor->addWidget(pathLimit_);
    auto *help2 = new QLabel("A pan takes the shorter turn. For a longer turn, add a scene between "
                             "the two. A switch between perspective and orthographic is a cut at "
                             "its keyframe.");
    help2->setWordWrap(true);
    editor->addWidget(help2);

    auto *draftButtons = new QHBoxLayout;
    draftButtons->addStretch(1);
    cancelDraft_ = new QPushButton("Cancel");
    cancelDraft_->setObjectName("cameraPathCancelDraft");
    cancelDraft_->setAccessibleName("Cancel the draft path");
    draftButtons->addWidget(cancelDraft_);
    createPath_ = new QPushButton("Create path");
    createPath_->setObjectName("cameraPathCreate");
    createPath_->setAccessibleName("Create path");
    draftButtons->addWidget(createPath_);
    editor->addLayout(draftButtons);
    editor->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setObjectName("cameraPathEditorScroll");
    scroll->setAccessibleName("Camera path editor");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(pathEditor_);
    pathSplit_->addWidget(scroll);
    pathSplit_->setStretchFactor(1, 1);

    connect(pathList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (!item)
                    return;
                if (item->data(Qt::UserRole + 1).toBool())
                    selectPath(0, true);
                else
                    selectPath(item->data(Qt::UserRole).toULongLong(), false);
            });
    connect(newPath_, &QPushButton::clicked, this, [this] { newPathDraft(); });
    connect(deletePath_, &QPushButton::clicked, this, [this] { deleteSelectedPath(); });
    connect(pathName_, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (draft_ && draftSelected_) {
            draft_->name = text;
            refreshPaths();
            updateDraftStatus();
        }
    });
    connect(pathName_, &QLineEdit::editingFinished, this, [this] { renameSelectedPath(); });
    connect(framesPerSecond_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { changeTiming(); });
    connect(transition_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { changeTiming(); });
    connect(hold_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { changeTiming(); });
    connect(addScene_, &QPushButton::clicked, this, [this] { addSceneToPath(); });
    connect(moveEarlier_, &QPushButton::clicked, this, [this] { moveKeyframe(-1); });
    connect(moveLater_, &QPushButton::clicked, this, [this] { moveKeyframe(1); });
    connect(removeKeyframe_, &QPushButton::clicked, this, [this] { removeKeyframe(); });
    connect(cancelDraft_, &QPushButton::clicked, this, [this] { cancelDraftPath(); });
    connect(createPath_, &QPushButton::clicked, this, [this] { createDraftPath(); });

    // The keyboard path of the keyframe list (§17.10): Alt+Up and Alt+Down move the scene, Delete
    // removes it. The shortcuts belong to the list, as in Saved scenes.
    const std::pair<QString, std::pair<QKeySequence, std::function<void()>>> bindings[] = {
        {"Move keyframe earlier", {QKeySequence("Alt+Up"), [this] { moveKeyframe(-1); }}},
        {"Move keyframe later", {QKeySequence("Alt+Down"), [this] { moveKeyframe(1); }}},
        {"Remove keyframe", {QKeySequence("Delete"), [this] { removeKeyframe(); }}},
    };
    for (const auto &[text, binding] : bindings) {
        auto *action = new QAction(text, keyframeList_);
        action->setShortcut(binding.first);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        keyframeList_->addAction(action);
        connect(action, &QAction::triggered, this, binding.second);
    }
    return page;
}

QWidget *PresentationsDialog::buildPresets() {
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    presetSplit_ = new QSplitter(Qt::Horizontal);
    presetSplit_->setObjectName("renderPresetSplit");
    presetSplit_->setAccessibleName("Render presets and editor");
    presetSplit_->setChildrenCollapsible(false);
    outer->addWidget(presetSplit_, 1);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    presetList_ = new QListWidget;
    presetList_->setObjectName("renderPresetList");
    presetList_->setAccessibleName("Render presets");
    leftLayout->addWidget(presetList_, 1);
    auto *presetButtons = new QHBoxLayout;
    newPreset_ = new QPushButton("New preset");
    newPreset_->setObjectName("renderPresetNew");
    newPreset_->setAccessibleName("New preset");
    presetButtons->addWidget(newPreset_);
    duplicatePreset_ = new QPushButton("Duplicate");
    duplicatePreset_->setObjectName("renderPresetDuplicate");
    duplicatePreset_->setAccessibleName("Duplicate preset");
    presetButtons->addWidget(duplicatePreset_);
    deletePreset_ = new QPushButton("Delete");
    deletePreset_->setObjectName("renderPresetDelete");
    deletePreset_->setAccessibleName("Delete preset");
    presetButtons->addWidget(deletePreset_);
    leftLayout->addLayout(presetButtons);
    fromCurrent_ = new QPushButton("New from current settings");
    fromCurrent_->setObjectName("renderPresetFromCurrent");
    fromCurrent_->setAccessibleName("New preset from current settings");
    leftLayout->addWidget(fromCurrent_);
    presetCap_ = new QLabel;
    presetCap_->setObjectName("renderPresetCap");
    presetCap_->setWordWrap(true);
    leftLayout->addWidget(presetCap_);
    presetSplit_->addWidget(left);

    presetEditor_ = new QWidget;
    auto *editor = new QVBoxLayout(presetEditor_);
    editor->setContentsMargins(4, 0, 4, 0);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    presetName_ = new QLineEdit;
    presetName_->setObjectName("renderPresetName");
    presetName_->setAccessibleName("Preset name");
    presetName_->setMaxLength(1024);
    form->addRow("Name", presetName_);
    auto *engineRow = new QHBoxLayout;
    cycles_ = new QRadioButton("Cycles");
    cycles_->setObjectName("renderPresetCycles");
    cycles_->setAccessibleName("Cycles engine");
    engineRow->addWidget(cycles_);
    eevee_ = new QRadioButton("Eevee preview (needs the OpenGL device)");
    eevee_->setObjectName("renderPresetEevee");
    eevee_->setAccessibleName("Eevee preview engine, needs the OpenGL device");
    engineRow->addWidget(eevee_);
    engineRow->addStretch(1);
    form->addRow("Engine", engineRow);
    width_ = new QSpinBox;
    width_->setObjectName("renderPresetWidth");
    nameSpin(width_, "Width in pixels");
    width_->setRange(64, 4096);
    width_->setSuffix(" px");
    width_->setKeyboardTracking(false);
    form->addRow("Width (pixels)", width_);
    height_ = new QSpinBox;
    height_->setObjectName("renderPresetHeight");
    nameSpin(height_, "Height in pixels");
    height_->setRange(64, 4096);
    height_->setSuffix(" px");
    height_->setKeyboardTracking(false);
    form->addRow("Height (pixels)", height_);
    samples_ = new QSpinBox;
    samples_->setObjectName("renderPresetSamples");
    nameSpin(samples_, "Samples");
    samples_->setRange(1, 1024);
    samples_->setKeyboardTracking(false);
    form->addRow("Samples", samples_);
    seed_ = new QSpinBox;
    seed_->setObjectName("renderPresetSeed");
    nameSpin(seed_, "Seed, Cycles only");
    seed_->setRange(0, 1000000);
    seed_->setKeyboardTracking(false);
    form->addRow("Seed", seed_);
    editor->addLayout(form);
    auto *sizeHint = new QLabel("64 to 4096 pixels. Even sizes are asked for because a batch that "
                                "writes video needs them. Samples: 1 to 1024. Seed: Cycles only; "
                                "Eevee ignores the seed.");
    sizeHint->setWordWrap(true);
    editor->addWidget(sizeHint);
    presetError_ = new QLabel;
    presetError_->setObjectName("renderPresetError");
    presetError_->setWordWrap(true);
    presetError_->setVisible(false);
    editor->addWidget(presetError_);
    auto *saved =
        new QLabel("Saved with this model: presets travel with the file and undo like any "
                   "edit. The render sheet's values stay on this computer.");
    saved->setObjectName("renderPresetSaved");
    saved->setWordWrap(true);
    editor->addWidget(saved);
    auto *batchNote =
        new QLabel("A batch keeps the settings it started with. Changing or deleting a "
                   "preset later does not change a batch already run.");
    batchNote->setWordWrap(true);
    editor->addWidget(batchNote);
    editor->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setObjectName("renderPresetEditorScroll");
    scroll->setAccessibleName("Render preset editor");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(presetEditor_);
    presetSplit_->addWidget(scroll);
    presetSplit_->setStretchFactor(1, 1);

    connect(presetList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    selectPreset(item->data(Qt::UserRole).toULongLong());
            });
    connect(newPreset_, &QPushButton::clicked, this,
            [this] { newPresetFrom(nextPresetName(), RenderSettings{}); });
    connect(duplicatePreset_, &QPushButton::clicked, this, [this] { duplicateSelectedPreset(); });
    connect(deletePreset_, &QPushButton::clicked, this, [this] { deleteSelectedPreset(); });
    connect(fromCurrent_, &QPushButton::clicked, this, [this] {
        newPresetFrom(nextPresetName(), currentSettings_ ? currentSettings_() : RenderSettings{});
    });
    connect(presetName_, &QLineEdit::editingFinished, this, [this] { renameSelectedPreset(); });
    connect(cycles_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked)
            changePresetSettings();
    });
    connect(eevee_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked)
            changePresetSettings();
    });
    connect(width_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { changePresetSettings(); });
    connect(height_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { changePresetSettings(); });
    connect(samples_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { changePresetSettings(); });
    connect(seed_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { changePresetSettings(); });
    return page;
}

void PresentationsDialog::refresh() {
    // A different document (a new or opened model) starts with nothing selected and no draft.
    if (!doc_.owns(session_)) {
        draft_.reset();
        draftSelected_ = false;
        selectedPath_ = 0;
        selectedPreset_ = 0;
    }
    session_ = doc_.saveStamp();
    if (!doc_.cameraPaths().contains(selectedPath_))
        selectedPath_ = 0;
    if (!doc_.renderPresets().contains(selectedPreset_))
        selectedPreset_ = 0;
    if (!draft_)
        draftSelected_ = false;
    if (!draftSelected_ && !selectedPath_ && !doc_.cameraPaths().empty())
        selectedPath_ = pathsInOrderIds(doc_).front();
    if (!draftSelected_ && !selectedPath_ && draft_)
        draftSelected_ = true;
    if (!selectedPreset_ && !doc_.renderPresets().empty())
        selectedPreset_ = doc_.renderPresets().begin()->first;
    refreshPaths();
    refreshPresets();
    showPath();
    showPreset();
}

void PresentationsDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    stackAtLargeText();
}

void PresentationsDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange)
        stackAtLargeText();
}

void PresentationsDialog::stackAtLargeText() {
    // §17.10: at a large text size the list stacks above its editor. A side-by-side editor needs
    // about forty characters beside the list.
    const int editorWidth = fontMetrics().horizontalAdvance(QString(40, 'M'));
    const auto orientation = width() < 300 + editorWidth ? Qt::Vertical : Qt::Horizontal;
    if (pathSplit_->orientation() != orientation)
        pathSplit_->setOrientation(orientation);
    if (presetSplit_->orientation() != orientation)
        presetSplit_->setOrientation(orientation);
}

CameraPathPtr PresentationsDialog::selectedRecord() const {
    if (draftSelected_ || !selectedPath_)
        return nullptr;
    const auto found = doc_.cameraPaths().find(selectedPath_);
    return found == doc_.cameraPaths().end() ? nullptr : found->second;
}

RenderPresetPtr PresentationsDialog::selectedPresetRecord() const {
    if (!selectedPreset_)
        return nullptr;
    const auto found = doc_.renderPresets().find(selectedPreset_);
    return found == doc_.renderPresets().end() ? nullptr : found->second;
}

std::vector<Id> PresentationsDialog::keyframesInEditor() const {
    if (draftSelected_ && draft_)
        return draft_->keyframes;
    const auto record = selectedRecord();
    return record ? record->keyframes : std::vector<Id>{};
}

CameraPathTiming PresentationsDialog::timingInEditor() const {
    if (draftSelected_ && draft_)
        return draft_->timing;
    const auto record = selectedRecord();
    return record ? record->timing : CameraPathTiming{};
}

QStringList PresentationsDialog::pathNames(Id except) const {
    QStringList names;
    for (const auto &[id, path] : doc_.cameraPaths())
        if (id != except)
            names << QString::fromStdString(path->name);
    return names;
}

QStringList PresentationsDialog::presetNames(Id except) const {
    QStringList names;
    for (const auto &[id, preset] : doc_.renderPresets())
        if (id != except)
            names << QString::fromStdString(preset->name);
    return names;
}

QString PresentationsDialog::draftProblem() const {
    if (!draft_)
        return {};
    auto problem = presentationNameProblem(draft_->name, pathNames(0), kPathDuplicate);
    if (problem.isEmpty() && draft_->keyframes.size() < 2)
        return "Add at least two saved scenes. The path plays them in this order.";
    if (problem.isEmpty())
        problem = limitProblem(draft_->name, draft_->keyframes.size(), draft_->timing);
    return problem;
}

QString PresentationsDialog::nextPathName() const { return numberedName("Path", pathNames(0)); }

QString PresentationsDialog::nextPresetName() const {
    return numberedName("Preset", presetNames(0));
}

QString PresentationsDialog::commit(const QJsonArray &commands, const QString &label) {
    try {
        view_.editPresentations(commands, label);
        return {};
    } catch (const std::exception &error) {
        return QString::fromUtf8(error.what());
    }
}

void PresentationsDialog::showRefusal(QLabel *label, const QString &text) {
    label->setText("✕ Refused · " + text + " Nothing was changed.");
    label->setVisible(true);
}

void PresentationsDialog::selectPath(Id path, bool draft) {
    selectedPath_ = path;
    draftSelected_ = draft && draft_.has_value();
    pathError_->setVisible(false);
    showPath();
}

void PresentationsDialog::selectPreset(Id preset) {
    selectedPreset_ = preset;
    presetError_->setVisible(false);
    showPreset();
}

void PresentationsDialog::refreshPaths() {
    const QSignalBlocker blocked(pathList_);
    pathList_->clear();
    if (draft_) {
        auto *item = new QListWidgetItem(draft_->name + "\ndraft · not saved", pathList_);
        item->setData(Qt::UserRole + 1, true);
        item->setData(Qt::AccessibleTextRole, draft_->name + ", draft, not saved");
        auto font = item->font();
        font.setItalic(true);
        item->setFont(font);
        if (draftSelected_)
            pathList_->setCurrentItem(item);
    }
    for (const auto id : pathsInOrderIds(doc_)) {
        const auto &path = doc_.cameraPaths().at(id);
        const auto references = cameraPathReferences(doc_.scenes(), *path);
        const auto name = QString::fromStdString(path->name);
        const auto count = path->keyframes.size();
        const auto frames = cameraPathFrameCount(count, path->timing);
        auto subtitle = QString("%1 scenes · %2 frames · %3 s")
                            .arg(count)
                            .arg(frames)
                            .arg(secondsText(frames, path->timing.framesPerSecond));
        auto accessible = QString("%1, %2 scenes, %3 frames").arg(name).arg(count).arg(frames);
        if (!references.empty()) {
            const auto reason = brokenKeyframe(references, path->keyframes);
            subtitle = "✕ broken: " + reason;
            accessible += ", broken: " + reason;
        }
        auto *item = new QListWidgetItem(name + "\n" + subtitle, pathList_);
        item->setData(Qt::UserRole, qulonglong(id));
        item->setData(Qt::AccessibleTextRole, accessible);
        if (!draftSelected_ && id == selectedPath_)
            pathList_->setCurrentItem(item);
    }
}

void PresentationsDialog::refreshPresets() {
    const QSignalBlocker blocked(presetList_);
    presetList_->clear();
    for (const auto &[id, preset] : doc_.renderPresets()) {
        const auto &settings = preset->settings;
        const auto name = QString::fromStdString(preset->name);
        auto *item = new QListWidgetItem(name + "\n" +
                                             QString("%1 · %2 × %3 · %4 samples")
                                                 .arg(engineText(settings.engine))
                                                 .arg(settings.width)
                                                 .arg(settings.height)
                                                 .arg(settings.samples),
                                         presetList_);
        item->setData(Qt::UserRole, qulonglong(id));
        item->setData(Qt::AccessibleTextRole, QString("%1, %2, %3 by %4, %5 samples")
                                                  .arg(name)
                                                  .arg(engineText(settings.engine))
                                                  .arg(settings.width)
                                                  .arg(settings.height)
                                                  .arg(settings.samples));
        if (id == selectedPreset_)
            presetList_->setCurrentItem(item);
    }
}

void PresentationsDialog::showPath() {
    const bool draft = draftSelected_ && draft_.has_value();
    const auto record = selectedRecord();
    const bool any = draft || record != nullptr;
    {
        const QSignalBlocker name(pathName_), fps(framesPerSecond_), transition(transition_),
            hold(hold_);
        const auto timing = timingInEditor();
        pathName_->setText(draft    ? draft_->name
                           : record ? QString::fromStdString(record->name)
                                    : QString());
        framesPerSecond_->setValue(timing.framesPerSecond);
        transition_->setValue(double(timing.transitionSteps) / timing.framesPerSecond);
        hold_->setValue(double(timing.holdSteps) / timing.framesPerSecond);
    }
    const int previousRow = keyframeList_->currentRow();
    keyframeList_->clear();
    const auto scenes = keyframesInEditor();
    for (size_t index = 0; index < scenes.size(); ++index) {
        const auto scene = scenes[index];
        const auto found = doc_.scenes().find(scene);
        const bool repeat = std::find(scenes.begin(), scenes.begin() + std::ptrdiff_t(index),
                                      scene) != scenes.begin() + std::ptrdiff_t(index);
        const auto position = QString::number(index + 1);
        const auto count = QString::number(scenes.size());
        QString name, status, accessible;
        if (found == doc_.scenes().end()) {
            name = "Missing scene, old ID " + QString::number(qulonglong(scene));
            status = "missing: remove or replace it";
            accessible =
                "Keyframe " + position + " of " + count + ", scene missing; remove or replace it";
        } else if (!found->second->snapshot.camera) {
            name = QString::fromStdString(found->second->name);
            status = "no camera";
            accessible = "Keyframe " + position + " of " + count + ", " + name +
                         ", no camera; give it a camera in Saved scenes first";
        } else {
            name = QString::fromStdString(found->second->name);
            status = repeat ? "camera (repeat)" : "camera";
            accessible = "Keyframe " + position + " of " + count + ", " + name + ", has a camera";
        }
        auto *item = new QListWidgetItem(position + "  " + name + " · " + status, keyframeList_);
        item->setData(Qt::UserRole, qulonglong(scene));
        item->setData(Qt::AccessibleTextRole, accessible);
    }
    if (keyframeList_->count())
        keyframeList_->setCurrentRow(std::clamp(previousRow, 0, keyframeList_->count() - 1));
    const int row = keyframeList_->currentRow();
    const int count = int(scenes.size());
    pathEditor_->setEnabled(any);
    pathStatus_->setVisible(draft);
    cancelDraft_->setVisible(draft);
    createPath_->setVisible(draft);
    if (draft)
        updateDraftStatus();
    const bool full = doc_.cameraPaths().size() >= cameraPathCountLimit;
    newPath_->setEnabled(!draft_ && !full);
    deletePath_->setEnabled(record != nullptr);
    pathCap_->setText(draft_ ? "New path is disabled until this draft is created or cancelled."
                      : full
                          ? QString("%1 of %2 paths. A document holds up to %2 paths; delete one "
                                    "to add another.")
                                .arg(doc_.cameraPaths().size())
                                .arg(cameraPathCountLimit)
                          : QString("A document holds up to %1 paths. At %1, New path is disabled "
                                    "and says why.")
                                .arg(cameraPathCountLimit));
    addScene_->setEnabled(any && count < int(cameraPathKeyframeLimit));
    moveEarlier_->setEnabled(any && row > 0);
    moveLater_->setEnabled(any && row >= 0 && row + 1 < count);
    removeKeyframe_->setEnabled(any && row >= 0 && count > 2);
    keyframeNote_->setText(
        count < 2    ? "Add at least two saved scenes. The path plays them in this order."
        : count == 2 ? "A path needs two to thirty-two scenes. Add a scene before you remove one."
                     : "Alt+↑ / Alt+↓ move · Delete removes");
    updatePathCount();
    updateStepLabels();
}

void PresentationsDialog::updateStepLabels() {
    const auto fps = framesPerSecond_->value();
    transitionSteps_->setText(
        QString("= %1 steps · 0.05 to 10 s").arg(transitionStepsFor(transition_->value(), fps)));
    holdSteps_->setText(QString("= %1 steps · 0 to 5 s").arg(holdStepsFor(hold_->value(), fps)));
}

void PresentationsDialog::updatePathCount() {
    const auto timing = timingInEditor();
    const auto scenes = keyframesInEditor();
    const auto name = draftSelected_ && draft_ ? draft_->name
                      : selectedRecord()       ? QString::fromStdString(selectedRecord()->name)
                                               : QString();
    const auto frames = cameraPathFrameCount(scenes.size(), timing);
    const auto fps = timing.framesPerSecond;
    if (frames > cameraPathFrameLimit) {
        pathCount_->setText("✕ Too long · " + limitProblem(name, scenes.size(), timing));
        pathLimit_->setText(limitFormula(scenes.size(), timing));
        pathLimit_->setVisible(true);
        return;
    }
    pathLimit_->setVisible(false);
    const auto summary = QString("%1 of %2 frames · %3 scenes · %4 s at %5 fps.")
                             .arg(frames)
                             .arg(cameraPathFrameLimit)
                             .arg(scenes.size())
                             .arg(secondsText(frames, fps))
                             .arg(fps);
    const auto fit = QString(" At this timing, up to %1 scenes fit in %2 frames.")
                         .arg(scenesThatFit(timing))
                         .arg(cameraPathFrameLimit);
    pathCount_->setText(scenes.size() >= 2 ? "✓ Plans · " + summary + fit : summary + fit);
}

void PresentationsDialog::updateDraftStatus() {
    if (!draft_)
        return;
    const auto problem = draftProblem();
    const auto frames = cameraPathFrameCount(draft_->keyframes.size(), draft_->timing);
    createPath_->setEnabled(problem.isEmpty());
    pathStatus_->setText(
        problem.isEmpty() ? QString("%1 is not saved yet. It has %2 scenes, %3 frames. Create path "
                                    "adds it to this model.")
                                .arg(draft_->name)
                                .arg(draft_->keyframes.size())
                                .arg(frames)
                          : QString("%1 is not saved yet. %2 Create path is disabled until then.")
                                .arg(draft_->name)
                                .arg(problem));
}

void PresentationsDialog::showPreset() {
    const auto record = selectedPresetRecord();
    const auto count = doc_.renderPresets().size();
    const bool full = count >= renderPresetCountLimit;
    presetEditor_->setEnabled(record != nullptr);
    deletePreset_->setEnabled(record != nullptr);
    duplicatePreset_->setEnabled(record != nullptr && !full);
    newPreset_->setEnabled(!full);
    fromCurrent_->setEnabled(!full);
    presetCap_->setText(
        full ? QString("%1 of %2 presets. A document holds up to %2 presets; delete one "
                       "to add another.")
                   .arg(count)
                   .arg(renderPresetCountLimit)
             : QString("%1 of %2 presets. At %2, New preset is disabled and says why.")
                   .arg(count)
                   .arg(renderPresetCountLimit));
    const QSignalBlocker name(presetName_), cycles(cycles_), eevee(eevee_), width(width_),
        height(height_), samples(samples_), seed(seed_);
    if (!record) {
        presetName_->clear();
        return;
    }
    const auto &settings = record->settings;
    presetName_->setText(QString::fromStdString(record->name));
    cycles_->setChecked(settings.engine == RenderEngine::Cycles);
    eevee_->setChecked(settings.engine == RenderEngine::Eevee);
    width_->setValue(settings.width);
    height_->setValue(settings.height);
    samples_->setValue(settings.samples);
    seed_->setValue(settings.seed);
}

RenderSettings PresentationsDialog::presetSettingsInEditor() const {
    RenderSettings settings;
    settings.width = width_->value();
    settings.height = height_->value();
    settings.samples = samples_->value();
    settings.seed = seed_->value();
    settings.engine = eevee_->isChecked() ? RenderEngine::Eevee : RenderEngine::Cycles;
    return settings;
}

void PresentationsDialog::presetRefused(const QString &text) { showRefusal(presetError_, text); }

void PresentationsDialog::newPathDraft() {
    if (draft_ || doc_.cameraPaths().size() >= cameraPathCountLimit)
        return;
    draft_ = Draft{nextPathName(), {}, CameraPathTiming{}};
    draftSelected_ = true;
    pathError_->setVisible(false);
    refreshPaths();
    showPath();
    pathName_->setFocus();
    pathName_->selectAll();
}

void PresentationsDialog::createDraftPath() {
    if (!draft_ || !draftSelected_ || !draftProblem().isEmpty())
        return;
    const auto saved = *draft_;
    QJsonArray scenes;
    for (const auto scene : saved.keyframes)
        scenes.append(QString::number(qulonglong(scene)));
    const QJsonArray commands{
        QJsonObject{{"command", "camera_path.create"},
                    {"name", saved.name},
                    {"keyframes", scenes},
                    {"timing", QJsonObject{{"framesPerSecond", saved.timing.framesPerSecond},
                                           {"transitionSteps", saved.timing.transitionSteps},
                                           {"holdSteps", saved.timing.holdSteps}}}}};
    draft_.reset();
    draftSelected_ = false;
    const auto refusal = commit(commands, "Create camera path");
    if (!refusal.isEmpty()) {
        draft_ = saved;
        draftSelected_ = true;
        refreshPaths();
        showPath();
        showRefusal(pathError_, refusal);
        return;
    }
    for (const auto &[id, path] : doc_.cameraPaths())
        if (path->name == saved.name.toStdString())
            selectedPath_ = id;
    refreshPaths();
    showPath();
}

void PresentationsDialog::cancelDraftPath() {
    draft_.reset();
    draftSelected_ = false;
    pathError_->setVisible(false);
    refresh();
}

void PresentationsDialog::deleteSelectedPath() {
    const auto record = selectedRecord();
    if (!record)
        return;
    // The next path in the list is selected after the delete.
    const auto order = pathsInOrderIds(doc_);
    const auto index = std::find(order.begin(), order.end(), record->id) - order.begin();
    const auto neighbour = index + 1 < std::ssize(order) ? order[size_t(index + 1)]
                           : index > 0                   ? order[size_t(index - 1)]
                                                         : Id{};
    const auto before = selectedPath_;
    selectedPath_ = neighbour;
    const auto refusal =
        commit(QJsonArray{QJsonObject{{"command", "camera_path.delete"},
                                      {"path", QString::number(qulonglong(record->id))}}},
               "Delete camera path");
    if (!refusal.isEmpty()) {
        selectedPath_ = before;
        showRefusal(pathError_, refusal);
        showPath();
    }
}

void PresentationsDialog::renameSelectedPath() {
    const auto name = pathName_->text();
    if (draftSelected_ && draft_) {
        if (draft_->name != name) {
            draft_->name = name;
            refreshPaths();
            updateDraftStatus();
        }
        return;
    }
    const auto record = selectedRecord();
    if (!record || name == QString::fromStdString(record->name))
        return;
    const auto problem = presentationNameProblem(name, pathNames(record->id), kPathDuplicate);
    const auto refusal =
        problem.isEmpty()
            ? commit(QJsonArray{QJsonObject{{"command", "camera_path.update"},
                                            {"path", QString::number(qulonglong(record->id))},
                                            {"name", name}}},
                     "Rename camera path")
            : problem;
    if (!refusal.isEmpty()) {
        showRefusal(pathError_, refusal);
        showPath();
    }
}

void PresentationsDialog::changeTiming() {
    const auto fps = framesPerSecond_->value();
    const CameraPathTiming timing{fps, transitionStepsFor(transition_->value(), fps),
                                  holdStepsFor(hold_->value(), fps)};
    if (draftSelected_ && draft_) {
        draft_->timing = timing;
        updatePathCount();
        updateStepLabels();
        updateDraftStatus();
        return;
    }
    const auto record = selectedRecord();
    if (!record || record->timing == timing) {
        updateStepLabels();
        return;
    }
    const auto problem =
        limitProblem(QString::fromStdString(record->name), record->keyframes.size(), timing);
    const auto refusal =
        problem.isEmpty()
            ? commit(QJsonArray{QJsonObject{
                         {"command", "camera_path.update"},
                         {"path", QString::number(qulonglong(record->id))},
                         {"timing", QJsonObject{{"framesPerSecond", timing.framesPerSecond},
                                                {"transitionSteps", timing.transitionSteps},
                                                {"holdSteps", timing.holdSteps}}}}},
                     "Change camera path timing")
            : problem;
    if (!refusal.isEmpty()) {
        showRefusal(pathError_, refusal);
        showPath();
    }
}

QString PresentationsDialog::replaceKeyframes(const std::vector<Id> &keyframes) {
    if (draftSelected_ && draft_) {
        if (keyframes.size() > cameraPathKeyframeLimit)
            return "A path needs two to thirty-two scenes.";
        draft_->keyframes = keyframes;
        refreshPaths();
        showPath();
        return {};
    }
    const auto record = selectedRecord();
    if (!record)
        return {};
    const auto name = QString::fromStdString(record->name);
    auto problem = limitProblem(name, keyframes.size(), record->timing);
    if (problem.isEmpty()) {
        // A broken path changes only when every keyframe left is valid (§17.2).
        CameraPathRecord candidate = *record;
        candidate.keyframes = keyframes;
        const auto references = cameraPathReferences(doc_.scenes(), candidate);
        if (!references.empty())
            problem = "This path has a broken keyframe: " + brokenKeyframe(references, keyframes) +
                      ". Remove or replace it, or give the scene a camera in Saved scenes, before "
                      "the path can change.";
    }
    if (!problem.isEmpty())
        return problem;
    QJsonArray scenes;
    for (const auto scene : keyframes)
        scenes.append(QString::number(qulonglong(scene)));
    return commit(QJsonArray{QJsonObject{{"command", "camera_path.update"},
                                         {"path", QString::number(qulonglong(record->id))},
                                         {"keyframes", scenes}}},
                  "Change camera path keyframes");
}

void PresentationsDialog::addSceneToPath() {
    const bool draft = draftSelected_ && draft_.has_value();
    const auto record = selectedRecord();
    if (!draft && !record)
        return;
    const auto keyframes = keyframesInEditor();
    std::vector<PickerScene> scenes;
    for (const auto id : orderedScenes(doc_)) {
        const auto &scene = doc_.scenes().at(id);
        scenes.push_back({id, QString::fromStdString(scene->name),
                          scene->snapshot.camera.has_value(),
                          int(std::count(keyframes.begin(), keyframes.end(), id))});
    }
    const auto title = draft ? draft_->name : QString::fromStdString(record->name);
    PresentationScenePicker picker(
        title, std::move(scenes),
        [this](Id scene) {
            auto next = keyframesInEditor();
            next.push_back(scene);
            return replaceKeyframes(next);
        },
        this);
    picker.exec();
    showPath();
}

void PresentationsDialog::moveKeyframe(int direction) {
    auto keyframes = keyframesInEditor();
    const int row = keyframeList_->currentRow();
    const int target = row + direction;
    if (row < 0 || target < 0 || target >= int(keyframes.size()))
        return;
    std::swap(keyframes[size_t(row)], keyframes[size_t(target)]);
    const auto refusal = replaceKeyframes(keyframes);
    if (!refusal.isEmpty()) {
        showRefusal(pathError_, refusal);
        showPath();
        return;
    }
    // The moved scene keeps the focus, at its new row.
    keyframeList_->setCurrentRow(target);
}

void PresentationsDialog::removeKeyframe() {
    auto keyframes = keyframesInEditor();
    const int row = keyframeList_->currentRow();
    if (row < 0 || row >= int(keyframes.size()) || keyframes.size() <= 2)
        return;
    keyframes.erase(keyframes.begin() + row);
    const auto refusal = replaceKeyframes(keyframes);
    if (!refusal.isEmpty()) {
        showRefusal(pathError_, refusal);
        showPath();
        return;
    }
    keyframeList_->setCurrentRow(std::min(row, int(keyframes.size()) - 1));
}

void PresentationsDialog::newPresetFrom(const QString &name, RenderSettings settings) {
    if (doc_.renderPresets().size() >= renderPresetCountLimit)
        return;
    const auto problem = presentationNameProblem(name, presetNames(0), kPresetDuplicate);
    if (!problem.isEmpty()) {
        presetRefused(problem);
        return;
    }
    const auto refusal =
        commit(QJsonArray{QJsonObject{
                   {"command", "render_preset.create"},
                   {"name", name},
                   {"width", settings.width},
                   {"height", settings.height},
                   {"samples", settings.samples},
                   {"seed", settings.seed},
                   {"engine", settings.engine == RenderEngine::Eevee ? "eevee" : "cycles"}}},
               "Create render preset");
    if (!refusal.isEmpty()) {
        presetRefused(refusal);
        return;
    }
    for (const auto &[id, preset] : doc_.renderPresets())
        if (preset->name == name.toStdString())
            selectedPreset_ = id;
    refreshPresets();
    showPreset();
}

void PresentationsDialog::duplicateSelectedPreset() {
    const auto record = selectedPresetRecord();
    if (!record)
        return;
    newPresetFrom(uniqueName(QString::fromStdString(record->name) + " copy", presetNames(0)),
                  record->settings);
}

void PresentationsDialog::deleteSelectedPreset() {
    const auto record = selectedPresetRecord();
    if (!record)
        return;
    const auto &presets = doc_.renderPresets();
    const auto next = presets.upper_bound(record->id);
    const Id neighbour = next != presets.end() ? next->first
                         : presets.size() > 1  ? std::prev(next)->first
                                               : Id{};
    const auto before = selectedPreset_;
    selectedPreset_ = neighbour;
    const auto refusal =
        commit(QJsonArray{QJsonObject{{"command", "render_preset.delete"},
                                      {"preset", QString::number(qulonglong(record->id))}}},
               "Delete render preset");
    if (!refusal.isEmpty()) {
        selectedPreset_ = before;
        presetRefused(refusal);
        showPreset();
    }
}

void PresentationsDialog::renameSelectedPreset() {
    const auto record = selectedPresetRecord();
    if (!record)
        return;
    const auto name = presetName_->text();
    if (name == QString::fromStdString(record->name))
        return;
    const auto problem = presentationNameProblem(name, presetNames(record->id), kPresetDuplicate);
    const auto refusal =
        problem.isEmpty()
            ? commit(QJsonArray{QJsonObject{{"command", "render_preset.update"},
                                            {"preset", QString::number(qulonglong(record->id))},
                                            {"name", name}}},
                     "Rename render preset")
            : problem;
    if (!refusal.isEmpty()) {
        presetRefused(refusal);
        showPreset();
    }
}

void PresentationsDialog::changePresetSettings() {
    const auto record = selectedPresetRecord();
    if (!record)
        return;
    const auto settings = presetSettingsInEditor();
    if (settings == record->settings)
        return;
    const auto refusal =
        commit(QJsonArray{QJsonObject{
                   {"command", "render_preset.update"},
                   {"preset", QString::number(qulonglong(record->id))},
                   {"width", settings.width},
                   {"height", settings.height},
                   {"samples", settings.samples},
                   {"seed", settings.seed},
                   {"engine", settings.engine == RenderEngine::Eevee ? "eevee" : "cycles"}}},
               "Change render preset settings");
    if (!refusal.isEmpty()) {
        presetRefused(refusal);
        showPreset();
    }
}

} // namespace sketchy
