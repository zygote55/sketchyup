#include "app/presentation_batch_dialog.hpp"
#include "app/viewport.hpp"
#include "core/presentation_records.hpp"
#include "integrations/blender_job.hpp"
#include "io/presentation_io.hpp"
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QVBoxLayout>
#include <algorithm>
#include <optional>
#include <set>

namespace sketchy {
namespace {

constexpr qint64 mib = 1024 * 1024, gibibyte = 1024 * mib;
// The disk reserve the engine adds to a batch's need (UX_DESIGN 17.4).
constexpr qint64 diskReserve = 256 * mib;

const QStringList cycleBackends{"CPU", "CUDA", "OPTIX", "HIP", "ONEAPI", "METAL"};

QString gib(qint64 bytes) {
    return QString::number(double(bytes) / double(gibibyte), 'f', 2) + " GiB";
}

QString mibText(qint64 bytes) { return QLocale().toString(bytes / mib) + " MiB"; }

QString settingsText(const RenderSettings &settings) {
    return QString("%1 · %2 × %3 · %4 samples")
        .arg(settings.engine == RenderEngine::Eevee ? "Eevee preview" : "Cycles")
        .arg(settings.width)
        .arg(settings.height)
        .arg(settings.samples);
}

bool isEevee(const RenderSettings &settings) { return settings.engine == RenderEngine::Eevee; }

// The "what is wrong" words of a broken path's first keyframe (UX_DESIGN 17.8).
QString brokenReason(const CameraPathReferences &references, const std::vector<Id> &keyframes) {
    for (size_t index = 0; index < keyframes.size(); ++index) {
        if (references.sceneMissing.contains(keyframes[index]))
            return QString("keyframe %1 names a missing scene").arg(index + 1);
        if (references.sceneHasNoCamera.contains(keyframes[index]))
            return QString("keyframe %1 has no camera").arg(index + 1);
    }
    return {};
}

// Camera paths in their saved order, as the Presentations dialog lists them.
std::vector<Id> pathsInOrder(const Document &doc) {
    std::vector<std::pair<std::uint32_t, Id>> order;
    for (const auto &[id, path] : doc.cameraPaths())
        order.emplace_back(path->position, id);
    std::sort(order.begin(), order.end());
    std::vector<Id> ids;
    for (const auto &entry : order)
        ids.push_back(entry.second);
    return ids;
}

// A combo's item is enabled or disabled through its model; the combo has no setter of its own.
void setComboItemEnabled(QComboBox *combo, int index, bool on) {
    if (auto *model = qobject_cast<QStandardItemModel *>(combo->model()))
        if (auto *item = model->item(index))
            item->setEnabled(on);
}

bool comboItemEnabled(const QComboBox *combo, int index) {
    const auto *model = qobject_cast<QStandardItemModel *>(combo->model());
    const auto *item = model ? model->item(index) : nullptr;
    return item && item->isEnabled();
}

// A budget card: a small title, the figure and a line of words beneath it.
QFrame *card(const QString &title, const QString &name, QLabel *&value, QLabel *&detail) {
    auto *frame = new QFrame;
    frame->setObjectName(name);
    frame->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QVBoxLayout(frame);
    auto *heading = new QLabel(title.toUpper());
    heading->setStyleSheet("color: #5b6b5f; font-size: small;");
    layout->addWidget(heading);
    value = new QLabel;
    value->setObjectName(name + "Value");
    QFont big = value->font();
    big.setBold(true);
    big.setPointSizeF(big.pointSizeF() * 1.4);
    value->setFont(big);
    layout->addWidget(value);
    detail = new QLabel;
    detail->setObjectName(name + "Detail");
    detail->setWordWrap(true);
    layout->addWidget(detail);
    return frame;
}

} // namespace

PresentationBatchDialog::PresentationBatchDialog(PresentationBatches &batches, Document &document,
                                                 Viewport &view, QWidget *parent)
    : QDialog(parent), batches_(batches), doc_(document), view_(view) {
    setObjectName("presentationBatchDialog");
    setWindowTitle("Export presentation batch");
    setModal(true);
    resize(1040, 760);
    auto *outer = new QVBoxLayout(this);
    auto *title = new QLabel("Export presentation batch");
    title->setObjectName("batchTitle");
    QFont heading = title->font();
    heading.setBold(true);
    heading.setPointSizeF(heading.pointSizeF() * 1.3);
    title->setFont(heading);
    outer->addWidget(title);
    auto *subline =
        new QLabel("Renders one image sequence per preset along a camera path, into a new "
                   "folder. The model is not changed. For one set of frames from the "
                   "current settings, use Export scene animation…");
    subline->setObjectName("batchSubline");
    subline->setWordWrap(true);
    outer->addWidget(subline);

    columns_ = new QBoxLayout(QBoxLayout::LeftToRight);
    outer->addLayout(columns_, 1);
    auto *left = new QVBoxLayout;
    auto *leftPane = new QWidget;
    leftPane->setObjectName("batchLeft");
    leftPane->setLayout(left);
    columns_->addWidget(leftPane, 3);

    setup_ = new QFrame;
    setup_->setObjectName("batchSetup");
    setup_->setFrameShape(QFrame::StyledPanel);
    auto *setupLayout = new QVBoxLayout(setup_);
    setupText_ = new QLabel;
    setupText_->setObjectName("batchSetupText");
    setupText_->setWordWrap(true);
    setupLayout->addWidget(setupText_);
    auto *setupButtons = new QHBoxLayout;
    chooseBlender_ = new QPushButton("Choose executable…");
    chooseBlender_->setObjectName("batchChooseBlender");
    checkBlender_ = new QPushButton("Check again");
    checkBlender_->setObjectName("batchCheckBlender");
    setupButtons->addWidget(chooseBlender_);
    setupButtons->addWidget(checkBlender_);
    setupButtons->addStretch();
    setupLayout->addLayout(setupButtons);
    left->addWidget(setup_);
    setup_->hide();

    group_ = new QFrame;
    group_->setObjectName("batchGroup");
    auto *groupLayout = new QVBoxLayout(group_);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    left->addWidget(group_);

    // Camera path (§17.4)
    auto *pathHeading = new QLabel("Camera path");
    pathHeading->setStyleSheet("font-weight: bold;");
    groupLayout->addWidget(pathHeading);
    auto *pathForm = new QFormLayout;
    path_ = new QComboBox;
    path_->setObjectName("batchPath");
    path_->setAccessibleName("Camera path");
    path_->setModel(new QStandardItemModel(path_));
    pathForm->addRow("Path", path_);
    groupLayout->addLayout(pathForm);
    pathNote_ = new QLabel;
    pathNote_->setObjectName("batchPathNote");
    pathNote_->setWordWrap(true);
    groupLayout->addWidget(pathNote_);

    // Presets (§17.4)
    auto *presetHeading = new QLabel("Presets (up to four · one device family)");
    presetHeading->setStyleSheet("font-weight: bold;");
    groupLayout->addWidget(presetHeading);
    presets_ = new QListWidget;
    presets_->setObjectName("batchPresets");
    presets_->setAccessibleName("Presets, up to four");
    presets_->setMaximumHeight(170);
    groupLayout->addWidget(presets_);
    presetNote_ = new QLabel;
    presetNote_->setObjectName("batchPresetNote");
    presetNote_->setWordWrap(true);
    groupLayout->addWidget(presetNote_);

    // Device (§17.4)
    auto *deviceHeading = new QLabel("Device");
    deviceHeading->setStyleSheet("font-weight: bold;");
    groupLayout->addWidget(deviceHeading);
    auto *deviceForm = new QFormLayout;
    backend_ = new QComboBox;
    backend_->setObjectName("batchBackend");
    backend_->setAccessibleName("Device backend");
    backend_->addItems(cycleBackends + QStringList{"OPENGL"});
    deviceForm->addRow("Device backend", backend_);
    device_ = new QComboBox;
    device_->setObjectName("batchDevice");
    device_->setAccessibleName("Device");
    deviceForm->addRow("Device", device_);
    groupLayout->addLayout(deviceForm);
    fallback_ = new QCheckBox("Retry once on CPU if a GPU fails (per frame)");
    fallback_->setObjectName("batchFallback");
    fallback_->setChecked(true);
    groupLayout->addWidget(fallback_);

    // Output (§17.4)
    auto *outputHeading = new QLabel("Output");
    outputHeading->setStyleSheet("font-weight: bold;");
    groupLayout->addWidget(outputHeading);
    auto *outputForm = new QFormLayout;
    folder_ = new QLineEdit;
    folder_->setObjectName("batchFolder");
    folder_->setAccessibleName("New folder");
    folder_->setPlaceholderText("A new folder inside an existing one");
    outputForm->addRow("New folder", folder_);
    chooseFolder_ = new QPushButton("Choose parent folder…");
    chooseFolder_->setObjectName("batchChooseFolder");
    outputForm->addRow({}, chooseFolder_);
    groupLayout->addLayout(outputForm);
    folderNote_ = new QLabel(
        "A new folder inside an existing one. An existing folder is never used or changed.");
    folderNote_->setObjectName("batchFolderNote");
    folderNote_->setWordWrap(true);
    groupLayout->addWidget(folderNote_);

    // Video (§17.4, §17.5)
    auto *videoHeading = new QLabel("Video");
    videoHeading->setStyleSheet("font-weight: bold;");
    groupLayout->addWidget(videoHeading);
    video_ = new QCheckBox("Write H.264 video for each preset (MP4, one per preset)");
    video_->setObjectName("batchVideo");
    groupLayout->addWidget(video_);
    auto *codecForm = new QFormLayout;
    codec_ = new QComboBox;
    codec_->setObjectName("batchCodec");
    codec_->setAccessibleName("Video codec");
    codecForm->addRow("Codec", codec_);
    groupLayout->addLayout(codecForm);
    videoState_ = new QLabel;
    videoState_->setObjectName("batchVideoState");
    videoState_->setWordWrap(true);
    groupLayout->addWidget(videoState_);
    recheckVideo_ = new QPushButton("Check again");
    recheckVideo_->setObjectName("batchVideoRecheck");
    recheckVideo_->setAccessibleName("Check the video encoder again");
    groupLayout->addWidget(recheckVideo_, 0, Qt::AlignLeft);
    left->addStretch();

    // The live budget (§17.4), on the right.
    auto *right = new QVBoxLayout;
    auto *rightPane = new QWidget;
    rightPane->setObjectName("batchRight");
    rightPane->setLayout(right);
    columns_->addWidget(rightPane, 2);
    auto *budgetHeading = new QLabel("What the batch will do");
    budgetHeading->setStyleSheet("font-weight: bold;");
    right->addWidget(budgetHeading);
    frameCard_ = card("Frames", "batchFrames", frameValue_, frameDetail_);
    right->addWidget(frameCard_);
    outputCard_ = card("Output, at most", "batchOutput", outputValue_, outputDetail_);
    right->addWidget(outputCard_);
    spaceCard_ = card("Free space on this disk", "batchSpace", spaceValue_, spaceDetail_);
    right->addWidget(spaceCard_);
    QLabel *timeValue{}, *timeDetail{};
    right->addWidget(card("Time", "batchTime", timeValue, timeDetail));
    timeValue->setText("Not estimated");
    timeDetail->setText(
        "Each frame is a separate Blender render. In R099.b's measurement a 1024 × 768 "
        "frame at 32 samples took about 26 s on CPU.");
    blenderCard_ = card("Blender", "batchBlender", blenderValue_, blenderDetail_);
    right->addWidget(blenderCard_);
    right->addStretch();

    // The footer (§17.4, §17.8)
    refusal_ = new QLabel;
    refusal_->setObjectName("batchRefusal");
    refusal_->setWordWrap(true);
    refusal_->setTextFormat(Qt::PlainText);
    refusal_->hide();
    outer->addWidget(refusal_);
    auto *footer = new QHBoxLayout;
    footerNote_ =
        new QLabel("Start checks Blender again, then renders. The batch works from a copy of the "
                   "model as it is now: no save prompt.");
    footerNote_->setObjectName("batchFooterNote");
    footerNote_->setWordWrap(true);
    footer->addWidget(footerNote_, 1);
    startLine_ = new QLabel;
    startLine_->setObjectName("batchStartLine");
    startLine_->setWordWrap(true);
    footer->addWidget(startLine_, 1);
    cancel_ = new QPushButton("Cancel");
    cancel_->setObjectName("batchCancel");
    cancel_->setAutoDefault(false);
    footer->addWidget(cancel_);
    start_ = new QPushButton("Start batch");
    start_->setObjectName("batchStart");
    start_->setDefault(true);
    footer->addWidget(start_);
    // Start is the dialog's default, even while it is disabled. A button that is not Start is not
    // auto-default, so Qt does not give the default to a setup button when Start is disabled
    // (§17.10).
    for (auto *button : {chooseFolder_, recheckVideo_, chooseBlender_, checkBlender_})
        button->setAutoDefault(false);
    outer->addLayout(footer);

    // The lists and the choices, then the probes. Each probe reports through a signal, so the
    // dialog never waits on a nested event loop.
    fillPaths();
    fillPresets();
    connect(path_, &QComboBox::currentIndexChanged, this, [this] {
        // A folder the dialog suggested follows the path; one typed or chosen by hand stays.
        if (folder_->text().trimmed().isEmpty() || folder_->text() == suggestion_) {
            suggestion_ = defaultFolder();
            folder_->setText(suggestion_);
        }
        updateAll();
    });
    connect(presets_, &QListWidget::itemChanged, this, [this] { updateAll(); });
    connect(backend_, &QComboBox::currentIndexChanged, this, [this] {
        presetRowChanged();
        probeDevices();
    });
    connect(device_, &QComboBox::currentIndexChanged, this, [this] { updateAll(); });
    connect(fallback_, &QCheckBox::toggled, this, [this] { updateAll(); });
    connect(folder_, &QLineEdit::textChanged, this, [this] { updateAll(); });
    connect(chooseFolder_, &QPushButton::clicked, this, [this] { chooseParent(); });
    connect(video_, &QCheckBox::toggled, this, [this] { updateAll(); });
    connect(codec_, &QComboBox::currentIndexChanged, this, [this] { updateAll(); });
    connect(recheckVideo_, &QPushButton::clicked, this, [this] { refreshVideo(true); });
    connect(chooseBlender_, &QPushButton::clicked, this, [this] { chooseExecutable(); });
    connect(checkBlender_, &QPushButton::clicked, this, [this] {
        probeDevices();
        refreshVideo(true);
    });
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    connect(start_, &QPushButton::clicked, this, [this] { startBatch(); });
    connect(&batches_.videoChecks(), &PresentationVideoChecks::finished, this,
            [this] { videoFinished(); });
    connect(&batches_, &PresentationBatches::changed, this, [this] { updateAll(); });

    // Two presets start checked, as the mockup shows, when the document has them.
    stackForText();
    probeDevices();
    refreshVideo(false);
    updateAll();
}

PresentationBatchDialog::~PresentationBatchDialog() {
    if (devices_) {
        QObject::disconnect(devices_, nullptr, this, nullptr);
        devices_->deleteLater();
    }
}

void PresentationBatchDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    stackForText();
}

void PresentationBatchDialog::stackForText() {
    // At 200% the columns stack, so the budget cards come below the form (§17.10). The unit is the
    // width of an "M" in the dialog's own font, which the Interface text size sets.
    const bool stacked = QFontMetrics(font()).horizontalAdvance('M') * 40 > width() / 2;
    columns_->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}

QString PresentationBatchDialog::executable() const {
    QSettings preferences("SketchyUp", "SketchyUp");
    const auto configured = preferences.value("render/blenderPath").toString().trimmed();
    return configured.isEmpty() ? QStandardPaths::findExecutable("blender") : configured;
}

BlenderJob::Options PresentationBatchDialog::options() const {
    BlenderJob::Options result;
    result.executable = executable();
    result.backend = backend_->currentText();
    result.deviceId = device_->currentData().toString();
    result.allowCpuFallback = fallback_->isChecked();
    return result;
}

BlenderJob::Options PresentationBatchDialog::probeOptions() const {
    auto result = options();
    result.deviceId = "probe-only";
    return result;
}

PresentationBatchDialog::Choice PresentationBatchDialog::choice() const {
    Choice result;
    const auto pathValue = path_->currentData();
    if (pathValue.isValid())
        result.path = pathValue.toULongLong();
    for (int row = 0; row < presets_->count(); ++row) {
        const auto *item = presets_->item(row);
        if (item->checkState() == Qt::Checked)
            result.presets.push_back(item->data(Qt::UserRole).toULongLong());
    }
    result.video = video_->isChecked();
    result.codec = PresentationVideoCodec(codec_->currentData().toInt());
    return result;
}

void PresentationBatchDialog::fillPaths() {
    auto *model = qobject_cast<QStandardItemModel *>(path_->model());
    QSignalBlocker blocker(path_);
    const auto previous = path_->currentData();
    model->clear();
    const auto broken = brokenCameraPaths(doc_);
    int firstValid = -1;
    for (const auto id : pathsInOrder(doc_)) {
        const auto &record = *doc_.cameraPaths().at(id);
        const auto frames = cameraPathFrameCount(record.keyframes.size(), record.timing);
        auto text = QString("%1 · %2 scenes · %3 frames")
                        .arg(QString::fromStdString(record.name))
                        .arg(record.keyframes.size())
                        .arg(frames);
        auto *item = new QStandardItem;
        item->setData(QVariant::fromValue<qulonglong>(id), Qt::UserRole);
        const auto found = broken.find(id);
        if (found != broken.end()) {
            item->setText(text + " · ✕ broken: " + brokenReason(found->second, record.keyframes));
            item->setEnabled(false);
        } else {
            item->setText(text);
            if (firstValid < 0)
                firstValid = int(model->rowCount());
        }
        model->appendRow(item);
    }
    if (previous.isValid()) {
        const int restored = path_->findData(previous);
        if (restored >= 0 && model->item(restored)->isEnabled()) {
            path_->setCurrentIndex(restored);
            return;
        }
    }
    path_->setCurrentIndex(firstValid);
}

void PresentationBatchDialog::fillPresets() {
    QSignalBlocker blocker(presets_);
    const bool first = presets_->count() == 0;
    std::set<Id> checked;
    for (int row = 0; row < presets_->count(); ++row)
        if (presets_->item(row)->checkState() == Qt::Checked)
            checked.insert(presets_->item(row)->data(Qt::UserRole).toULongLong());
    presets_->clear();
    int index = 0;
    for (const auto &[id, preset] : doc_.renderPresets()) {
        auto *item = new QListWidgetItem(QString::fromStdString(preset->name) + " · " +
                                             settingsText(preset->settings),
                                         presets_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool on = first ? index < 2 : checked.contains(id);
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        ++index;
    }
}

QString PresentationBatchDialog::presetMismatch(const Choice &selected) const {
    const auto backend = backend_->currentText();
    for (const auto id : selected.presets) {
        const auto &preset = *doc_.renderPresets().at(id);
        const auto name = QString::fromStdString(preset.name);
        if (isEevee(preset.settings) && backend != "OPENGL")
            return QString("%1 uses Eevee, which needs the OpenGL device. The batch uses %2. "
                           "Uncheck %1, "
                           "or choose OPENGL.")
                .arg(name, backend);
        if (!isEevee(preset.settings) && backend == "OPENGL")
            return QString(
                       "%1 uses Cycles, which needs the CPU or a compute device. The batch uses "
                       "OPENGL. Uncheck %1, or choose a Cycles device.")
                .arg(name);
    }
    return {};
}

void PresentationBatchDialog::presetRowChanged() {
    QSignalBlocker blocker(presets_);
    const auto backend = backend_->currentText();
    int checkedCount = 0;
    for (int row = 0; row < presets_->count(); ++row)
        checkedCount += presets_->item(row)->checkState() == Qt::Checked;
    QStringList notes;
    for (int row = 0; row < presets_->count(); ++row) {
        auto *item = presets_->item(row);
        const auto id = item->data(Qt::UserRole).toULongLong();
        const auto &preset = *doc_.renderPresets().at(id);
        const bool checked = item->checkState() == Qt::Checked;
        QString reason;
        if (isEevee(preset.settings) && backend != "OPENGL")
            reason = QString("Off: %1 needs the OpenGL device, and the device is %2.")
                         .arg(QString::fromStdString(preset.name), backend);
        else if (!isEevee(preset.settings) && backend == "OPENGL")
            reason = QString("Off: %1 needs the CPU or a compute device, and the device is OPENGL.")
                         .arg(QString::fromStdString(preset.name));
        else if (!checked && checkedCount >= 4)
            reason = "Off: up to four presets in one batch.";
        // A preset that cannot be checked is disabled and says why. One that is checked stays
        // checkable, so it can be unchecked; Start then names the mismatch.
        const bool enabled = checked || reason.isEmpty();
        item->setFlags(enabled ? (item->flags() | Qt::ItemIsEnabled)
                               : (item->flags() & ~Qt::ItemIsEnabled));
        item->setToolTip(reason);
        if (!reason.isEmpty())
            notes << reason;
    }
    presetNote_->setText(notes.join("\n"));
}

void PresentationBatchDialog::probeDevices() {
    if (devices_) {
        QObject::disconnect(devices_, nullptr, this, nullptr);
        devices_->deleteLater();
        devices_.clear();
    }
    devicesReady_ = false;
    devicesChecking_ = false;
    blenderVersion_.clear();
    blenderFailure_.clear();
    {
        QSignalBlocker blocker(device_);
        device_->clear();
    }
    const auto found = executable();
    const QFileInfo file(found);
    blenderMissing_ =
        found.isEmpty() || !file.isAbsolute() || !file.isFile() || !file.isExecutable();
    if (blenderMissing_) {
        setBlenderState();
        updateAll();
        return;
    }
    devices_ = new BlenderJob(this);
    connect(devices_, &BlenderJob::changed, this, [this] { devicesFinished(); });
    devicesChecking_ = true;
    try {
        devices_->probe(probeOptions());
    } catch (const std::exception &error) {
        devicesChecking_ = false;
        blenderFailure_ = QString::fromUtf8(error.what());
    }
    setBlenderState();
    updateAll();
}

void PresentationBatchDialog::devicesFinished() {
    if (!devices_ || !devices_->done())
        return;
    devicesChecking_ = false;
    const auto report = devices_->report();
    if (devices_->phase() == BlenderJob::Phase::Succeeded) {
        const auto worker = report.value("worker").toObject();
        blenderVersion_ = worker.value("blenderVersionString").toString();
        QSignalBlocker blocker(device_);
        device_->clear();
        for (const auto &value : worker.value("devices").toArray()) {
            const auto item = value.toObject();
            device_->addItem(item.value("name").toString(), item.value("id").toString());
        }
        devicesReady_ = device_->count() > 0;
        if (!devicesReady_)
            blenderFailure_ =
                "No device is available for this backend. Choose CPU or another backend.";
    } else {
        blenderFailure_ = "Blender check did not finish. " + report.value("message").toString();
    }
    setBlenderState();
    updateAll();
}

void PresentationBatchDialog::setBlenderState() {
    const bool problem = blenderMissing_ || !blenderFailure_.isEmpty();
    setup_->setVisible(problem);
    group_->setEnabled(!problem);
    if (blenderMissing_) {
        const auto configured =
            QSettings("SketchyUp", "SketchyUp").value("render/blenderPath").toString();
        const auto detected =
            configured.trimmed().isEmpty()
                ? QString("Detected: Not found on PATH")
                : QString("Detected: %1 is not an executable file").arg(configured);
        setupText_->setText(
            "Presentation batches render each frame with Blender 5.2 LTS and encode video "
            "with its FFmpeg. Renders and the one-shot export use the same Blender.\n" +
            detected + "\nOn Arch, install the blender package, then check again.");
    } else if (!blenderFailure_.isEmpty()) {
        setupText_->setText(blenderFailure_ + "\nChoose Blender 5.2 LTS and check again.");
    }
}

void PresentationBatchDialog::chooseExecutable() {
    const auto selected = QFileDialog::getOpenFileName(this, "Choose Blender 5.2 LTS");
    if (selected.isEmpty())
        return;
    QSettings("SketchyUp", "SketchyUp").setValue("render/blenderPath", selected);
    probeDevices();
    refreshVideo(false);
}

void PresentationBatchDialog::chooseParent() {
    const auto parent = QFileDialog::getExistingDirectory(this, "Choose batch parent folder");
    if (parent.isEmpty())
        return;
    folder_->setText(QDir(parent).filePath(pathName()));
}

QString PresentationBatchDialog::pathName() const {
    const auto selected = choice();
    return doc_.cameraPaths().contains(selected.path)
               ? QString::fromStdString(doc_.cameraPaths().at(selected.path)->name)
               : QString("Presentation");
}

QString PresentationBatchDialog::defaultFolder() const {
    const auto movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    return QDir(movies.isEmpty() ? QDir::homePath() : movies).filePath(pathName());
}

void PresentationBatchDialog::refreshVideo(bool force) {
    if (blenderMissing_) {
        videoChecking_ = false;
        updateAll();
        return;
    }
    auto &checks = batches_.videoChecks();
    checks.refresh(options(), force);
    // Nothing is cached while a probe runs; its finished() clears this flag.
    videoChecking_ = !checks.cached(options()).has_value();
    updateAll();
}

void PresentationBatchDialog::videoFinished() {
    videoChecking_ = false;
    updateAll();
    // A Start that waits on its check goes on with the answer; startBatch clears the wait.
    if (pendingStart_)
        startBatch();
}

void PresentationBatchDialog::updatePathNote() const {
    const auto selected = choice();
    if (!doc_.cameraPaths().contains(selected.path)) {
        pathNote_->setText(
            "Only valid paths are listed; a broken one is shown disabled, with its reason.");
        return;
    }
    const auto &record = *doc_.cameraPaths().at(selected.path);
    const auto frames = cameraPathFrameCount(record.keyframes.size(), record.timing);
    pathNote_->setText(QString("%1 s at %2 fps. Only valid paths are listed; a broken one is shown "
                               "disabled, with its reason.")
                           .arg(double(frames) / record.timing.framesPerSecond, 0, 'f', 1)
                           .arg(record.timing.framesPerSecond));
}

bool PresentationBatchDialog::videoAvailable() const {
    if (blenderMissing_ || videoChecking_)
        return false;
    const auto answer = batches_.videoChecks().cached(options());
    if (!answer || !answer->checked)
        return false;
    const auto id = presentationVideoCodecId(choice().codec);
    return answer->codecs.contains(id);
}

void PresentationBatchDialog::updateVideoState() {
    const auto selected = choice();
    QString text, accessible;
    bool h264 = false, mpeg4 = false, notChecked = false, checking = false;
    std::optional<PresentationVideoCheck> answer;
    if (!blenderMissing_)
        answer = batches_.videoChecks().cached(options());
    if (blenderMissing_) {
        text = "No Blender found, so video cannot be checked.";
        accessible = "Video encoder: not checked, no Blender";
        notChecked = true;
    } else if (videoChecking_) {
        text = "Checking the video encoder…";
        accessible = "Video encoder: checking";
        checking = true;
    } else if (!answer || !answer->checked) {
        text = "The check did not finish.";
        accessible = "Video encoder: not checked";
        notChecked = true;
    } else {
        h264 = answer->codecs.contains("H264");
        mpeg4 = answer->codecs.contains("MPEG4");
    }
    const auto version =
        blenderVersion_.isEmpty() ? QString("this Blender") : "Blender " + blenderVersion_;
    if (!checking && !notChecked) {
        // The codec list chooses the video's codec: H.264 by default, and MPEG-4 Part 2 only when
        // H.264 is missing and the owner has chosen it (UX_DESIGN 17.12 choice 1).
        const auto previous = codec_->currentData().isValid() ? codec_->currentData().toInt()
                                                              : int(PresentationVideoCodec::H264);
        QSignalBlocker blocker(codec_);
        codec_->clear();
        codec_->addItem("H.264", int(PresentationVideoCodec::H264));
        setComboItemEnabled(codec_, 0, h264);
        if (!h264 && mpeg4)
            codec_->addItem("MPEG-4 Part 2 in MP4 (explicit fallback)",
                            int(PresentationVideoCodec::MPEG4));
        const int restored = codec_->findData(previous);
        codec_->setCurrentIndex(restored >= 0 && codec_->itemData(restored).isValid()
                                    ? restored
                                    : (h264 ? 0 : codec_->count() - 1));
        const bool chosenH264 = codec_->currentData().toInt() == int(PresentationVideoCodec::H264);
        const bool available = chosenH264 ? h264 : mpeg4;
        if (available) {
            const auto name = chosenH264 ? QString("H.264") : QString("MPEG-4 Part 2");
            text = QString("✓ %1 available in %2. Each video encode stops after 15:00.")
                       .arg(name, version);
            accessible = QString("Video encoder: available, %1").arg(name);
        } else {
            text = QString("⚠ H.264 is not in this Blender's FFmpeg build. Frames still render; "
                           "video is off.%1")
                       .arg(mpeg4 ? " MPEG-4 Part 2 is available: choose it under Codec." : "");
            accessible = "Video encoder: unavailable, H.264 not in this Blender";
        }
    } else {
        QSignalBlocker blocker(codec_);
        codec_->clear();
        codec_->addItem("H.264", int(PresentationVideoCodec::H264));
        setComboItemEnabled(codec_, 0, false);
        codec_->setCurrentIndex(0);
    }
    videoState_->setText(text);
    videoState_->setAccessibleName(accessible);
    recheckVideo_->setVisible(notChecked);
    const bool chosenAvailable = !checking && !notChecked && codec_->currentData().isValid() &&
                                 comboItemEnabled(codec_, codec_->currentIndex());
    video_->setEnabled(chosenAvailable);
    // A Start that waits for its check keeps the owner's Write video choice: the batch starts with
    // video when the answer has H.264, and the choice is cleared once a refusal is shown.
    if (!chosenAvailable && !pendingStart_) {
        QSignalBlocker blocker(video_);
        video_->setChecked(false);
    }
    const auto codecName = selected.codec == PresentationVideoCodec::MPEG4
                               ? QString("MPEG-4 Part 2")
                               : QString("H.264");
    video_->setText(selected.codec == PresentationVideoCodec::MPEG4
                        ? "Write MPEG-4 Part 2 video for each preset (MP4, one per preset)"
                        : "Write H.264 video for each preset (MP4, one per preset)");
    video_->setToolTip(chosenAvailable ? QString() : videoState_->text());
    blenderDetail_->setText(
        QString("Video: %1")
            .arg(checking ? QString("checking")
                          : (chosenAvailable ? codecName + " available." : "not available.")));
}

int PresentationBatchDialog::plannedFrames(const Choice &selected) const {
    if (!doc_.cameraPaths().contains(selected.path))
        return 0;
    const auto &record = *doc_.cameraPaths().at(selected.path);
    return int(cameraPathFrameCount(record.keyframes.size(), record.timing));
}

qint64 PresentationBatchDialog::outputCeiling(const Choice &selected) const {
    const auto frames = qint64(plannedFrames(selected));
    qint64 total = 0;
    for (const auto id : selected.presets)
        total += frames * presentationFrameBudget(doc_.renderPresets().at(id)->settings) *
                 (selected.video ? 2 : 1);
    return std::min(total, presentationOutputLimit);
}

QString PresentationBatchDialog::folderProblem() const {
    const auto folder = folder_->text().trimmed();
    if (folder.isEmpty())
        return "Choose a new batch folder.";
    const QFileInfo target(folder);
    if (target.exists() || target.isSymLink())
        return "Choose a new batch folder that does not exist yet.";
    const QFileInfo parent(target.absolutePath());
    if (!target.isAbsolute() || !parent.isDir() || parent.isSymLink())
        return "Choose a new batch folder within an existing directory.";
    return {};
}

QString PresentationBatchDialog::oddSizeProblem(const Choice &selected) const {
    if (!selected.video)
        return {};
    for (const auto id : selected.presets) {
        const auto &preset = *doc_.renderPresets().at(id);
        if (preset.settings.width % 2 || preset.settings.height % 2)
            return QString("A video needs even width and height in every preset: %1 is %2 × %3.")
                .arg(QString::fromStdString(preset.name))
                .arg(preset.settings.width)
                .arg(preset.settings.height);
    }
    return {};
}

QString PresentationBatchDialog::diskProblem(const Choice &selected) const {
    const auto folder = folder_->text().trimmed();
    const auto where = folder.isEmpty() ? defaultFolder() : QFileInfo(folder).absolutePath();
    const auto available = QStorageInfo(where).bytesAvailable();
    const auto needed = outputCeiling(selected) + diskReserve;
    if (available >= needed)
        return {};
    return QString("Not enough free disk space for this batch: about %1 needed, %2 free.")
        .arg(mibText(needed), mibText(available));
}

void PresentationBatchDialog::updateBudget(const Choice &selected) {
    const auto frames = plannedFrames(selected);
    frameValue_->setText(QLocale().toString(frames * int(selected.presets.size())));
    frameDetail_->setText(QString("%1 frames × %2 %3")
                              .arg(frames)
                              .arg(selected.presets.size())
                              .arg(selected.presets.size() == 1 ? "preset" : "presets"));
    const auto ceiling = outputCeiling(selected);
    outputValue_->setText(gib(ceiling));
    outputDetail_->setText(
        selected.video ? "Largest possible, with video: the 2.0 GiB budget caps it."
                       : "Largest possible; real frames are usually smaller. The batch stops "
                         "when its output reaches 2.0 GiB.");
    const auto folder = folder_->text().trimmed();
    const auto where = folder.isEmpty() ? defaultFolder() : QFileInfo(folder).absolutePath();
    const auto available = QStorageInfo(where).bytesAvailable();
    const auto needed = ceiling + diskReserve;
    spaceValue_->setText(gib(available));
    spaceDetail_->setText(
        available >= needed
            ? QString("Enough: the batch needs about %1, with a 256 MiB reserve.").arg(gib(needed))
            : QString("Not enough: the batch needs about %1, with a 256 MiB reserve.")
                  .arg(gib(needed)));
    if (blenderMissing_) {
        blenderValue_->setText("Not found");
    } else if (devicesChecking_) {
        blenderValue_->setText("Checking Blender…");
    } else if (devicesReady_) {
        blenderValue_->setText(QString("✓ Blender %1 · %2 device ready")
                                   .arg(blenderVersion_, backend_->currentText()));
    } else {
        blenderValue_->setText("Not checked");
    }
}

QString PresentationBatchDialog::startProblem() const {
    if (blenderMissing_)
        return "Presentation batches need an installed Blender 5.2 LTS; install it or choose its "
               "executable.";
    if (devicesChecking_)
        return "Checking Blender…";
    if (!blenderFailure_.isEmpty() || !devicesReady_)
        return blenderFailure_.isEmpty() ? QString("Checking Blender…") : blenderFailure_;
    const auto selected = choice();
    if (!doc_.cameraPaths().contains(selected.path))
        return "Choose a camera path.";
    if (selected.presets.empty())
        return "Check at least one preset.";
    if (const auto mismatch = presetMismatch(selected); !mismatch.isEmpty())
        return mismatch;
    if (selected.video && videoChecking_)
        return "Checking the video encoder…";
    // No answer for this Blender is not a refusal: Start checks the encoder again first, and
    // startBatch starts the batch when the answer arrives (UX_DESIGN 17.5). A probe that did not
    // finish, or a build without the codec, is refused with its own reason.
    if (selected.video) {
        if (const auto answer = batches_.videoChecks().cached(options())) {
            if (!answer->checked)
                return answer->message +
                       ". Check again, or clear Write video to render the frames only.";
            if (!videoAvailable())
                return presentationVideoRefusal(selected.codec);
        }
    }
    if (const auto odd = oddSizeProblem(selected); !odd.isEmpty())
        return odd;
    if (batches_.running())
        return "Another presentation batch is running; wait for it to finish.";
    if (view_.inspectionBusy())
        return "Finish the current modeling gesture before starting the batch.";
    if (const auto folder = folderProblem(); !folder.isEmpty())
        return folder;
    return diskProblem(selected);
}

void PresentationBatchDialog::updateAll() {
    if (!path_ || !start_)
        return;
    presetRowChanged();
    updatePathNote();
    updateVideoState();
    const auto selected = choice();
    updateBudget(selected);
    updateStart();
}

void PresentationBatchDialog::updateStart() {
    const auto problem = startProblem();
    start_->setEnabled(problem.isEmpty());
    startLine_->setText(problem.isEmpty() ? QString("Start checks Blender again, then renders.")
                                          : problem);
    startLine_->setStyleSheet(problem.isEmpty() ? QString() : QString("color: #9b2c2c;"));
}

void PresentationBatchDialog::showRefusal(const QString &text) {
    refusal_->setText("✕ Refused · " + text);
    refusal_->show();
}

void PresentationBatchDialog::startBatch() {
    const bool waiting = pendingStart_;
    pendingStart_ = false;
    refusal_->hide();
    // A Start that waited for its check gives its reason on the refusal line. The Write video
    // choice is cleared once the refusal shows (updateVideoState), so the start line would lose it.
    if (const auto problem = startProblem(); !problem.isEmpty()) {
        if (waiting) {
            showRefusal(problem);
            updateAll();
        }
        return;
    }
    const auto selected = choice();
    const auto batchOptions = options();
    // Start re-checks the encoder when the Blender changed since the dialog opened
    // (UX_DESIGN 17.5). The check runs in the background, and the batch starts when its answer
    // arrives. One re-check is made per Start, so an answer that never arrives cannot start a loop
    // of probes.
    if (selected.video && !batches_.videoChecks().cached(batchOptions)) {
        if (waiting) {
            showRefusal("The video encoder has not been checked yet. Check again, then Start.");
            updateAll();
            return;
        }
        pendingStart_ = true;
        refreshVideo(true);
        return;
    }
    try {
        const auto plan =
            batches_.planFor(selected.path, selected.presets, selected.video, selected.codec);
        const auto id = batches_.start(plan, folder_->text().trimmed(), batchOptions);
        emit started(id);
        accept();
    } catch (const std::exception &error) {
        showRefusal(QString::fromUtf8(error.what()));
    }
}

} // namespace sketchy
