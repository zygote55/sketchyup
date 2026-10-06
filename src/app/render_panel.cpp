#include "app/render_panel.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <future>
namespace sketchy {
namespace {
class ImageView : public QWidget {
    QImage image_;

  public:
    explicit ImageView(QImage image) : image_(std::move(image)) {
        setObjectName("renderImage");
        setAccessibleName("Verified Blender render");
        setMinimumSize(120, 120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        const auto size = image_.size().scaled(this->size(), Qt::KeepAspectRatio);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(
            QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size),
            image_);
    }
};
QString phaseText(BlenderJob::Phase phase) {
    using P = BlenderJob::Phase;
    switch (phase) {
    case P::Idle:
        return "Ready";
    case P::Probing:
        return "Checking Blender";
    case P::Rendering:
        return "Rendering";
    case P::Verifying:
        return "Verifying image";
    case P::Canceling:
        return "Canceling";
    case P::Succeeded:
        return "Render ready";
    case P::Failed:
        return "Render failed";
    case P::Unavailable:
        return "Blender unavailable";
    case P::Canceled:
        return "Render canceled";
    case P::TimedOut:
        return "Render timed out";
    }
    return {};
}
void saveImage(const BlenderResult &result, const QString &path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(result.png) != result.png.size() ||
        !file.commit())
        throw std::runtime_error("Could not save the render image");
}
} // namespace
struct RenderPanel::Impl {
    RenderPanel &owner;
    Document &document;
    Viewport &view;
    QTabWidget &tabs;
    QPushButton &chip;
    QWidget &window;
    QPointer<QDialog> dialog;
    QLineEdit *path{};
    QComboBox *camera{}, *backend{}, *device{};
    QSpinBox *width{}, *height{}, *samples{};
    QCheckBox *fallback{};
    QPushButton *probeButton{}, *renderButton{}, *cancelButton{};
    QLabel *setupStatus{}, *jobStatus{};
    QPlainTextEdit *logs{};
    QPointer<BlenderJob> probe, job;
    QTimer poll;
    QElapsedTimer elapsed;
    std::future<std::shared_ptr<const PreparedRender>> preparing;
    BlenderJob::Options workerOptions;
    bool preparingCanceled{}, verified{}, handled{};
    QString status{"Ready"}, details;
    Document::SaveStamp sourceSession;
    struct Result {
        QPointer<QWidget> page;
        QPointer<QLabel> provenance;
        Document::SaveStamp session;
        std::shared_ptr<const BlenderResult> result;
    };
    std::vector<Result> results;
    Impl(RenderPanel &owner, Document &document, Viewport &view, QTabWidget &tabs,
         QPushButton &chip, QWidget &window)
        : owner(owner), document(document), view(view), tabs(tabs), chip(chip), window(window),
          poll(&owner) {
        chip.hide();
        chip.setObjectName("renderStatusChip");
        QObject::connect(&chip, &QPushButton::clicked, &owner, [this] { show(); });
        QObject::connect(&tabs, &QTabWidget::tabCloseRequested, &owner, [this](int index) {
            if (index <= 0)
                return;
            auto *page = this->tabs.widget(index);
            this->tabs.removeTab(index);
            delete page;
            refresh();
        });
        poll.setInterval(25);
        QObject::connect(&poll, &QTimer::timeout, &owner, [this] { tick(); });
    }
    ~Impl() {
        if (probe)
            delete probe;
        if (job)
            delete job;
        if (dialog)
            delete dialog;
    }
    void checkOwner() const {
        if (QThread::currentThread() != owner.thread())
            throw std::runtime_error("Render panel requires owner thread");
    }
    bool active() const { return preparing.valid() || (job && !job->done()); }
    void publish() {
        chip.setText(status);
        chip.setVisible(elapsed.isValid());
        chip.setAccessibleName("Render job: " + status);
        if (dialog) {
            const auto seconds = elapsed.isValid() ? elapsed.elapsed() / 1000 : 0;
            jobStatus->setText(status + (active() ? " · " + QString::number(seconds) + " s" : "") +
                               (details.isEmpty() ? QString{} : "\n" + details));
            const bool busy = active() || (probe && !probe->done());
            for (QWidget *control : std::initializer_list<QWidget *>{
                     path, camera, width, height, samples, backend, device, fallback, probeButton})
                control->setEnabled(!busy);
            renderButton->setEnabled(!busy && verified && device->currentIndex() >= 0);
            cancelButton->setEnabled(busy);
        }
        emit owner.changed();
    }
    void invalidate() {
        verified = false;
        device->clear();
        if (setupStatus)
            setupStatus->setText("Check Blender and the selected device before rendering.");
        publish();
    }
    void show() {
        checkOwner();
        if (dialog) {
            dialog->show();
            dialog->raise();
            dialog->activateWindow();
            return;
        }
        dialog = new QDialog(&window);
        dialog->setObjectName("renderSetup");
        dialog->setWindowTitle("Render with Blender");
        dialog->resize(460, 640);
        auto *outer = new QVBoxLayout(dialog);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *contents = new QWidget;
        auto *layout = new QVBoxLayout(contents);
        scroll->setWidget(contents);
        outer->addWidget(scroll, 1);
        auto *intro = new QLabel("Blender is optional and runs outside the editor. The studio "
                                 "preset renders a captured model while you keep working.");
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        QSettings preferences("SketchyUp", "SketchyUp");
        path = new QLineEdit(preferences.value("render/blenderPath").toString());
        path->setObjectName("blenderPath");
        path->setPlaceholderText("Discover blender on PATH");
        form->addRow("Blender executable", path);
        auto *browse = new QPushButton("Choose executable…");
        browse->setObjectName("chooseBlender");
        form->addRow({}, browse);
        QObject::connect(browse, &QPushButton::clicked, &owner, [this] {
            if (active() || (probe && !probe->done()))
                return;
            const auto selected = QFileDialog::getOpenFileName(dialog, "Choose Blender 5.2 LTS");
            if (!selected.isEmpty())
                path->setText(selected);
        });
        camera = new QComboBox;
        camera->setObjectName("renderCamera");
        camera->addItems({"Current view", "Fit visible model"});
        form->addRow("Camera", camera);
        auto spin = [&](QString name, int low, int high, int value) {
            auto *control = new QSpinBox;
            control->setObjectName(name);
            control->setRange(low, high);
            control->setValue(value);
            return control;
        };
        width = spin("renderWidth", 64, 4096, preferences.value("render/width", 1024).toInt());
        form->addRow("Width (pixels)", width);
        height = spin("renderHeight", 64, 4096, preferences.value("render/height", 768).toInt());
        form->addRow("Height (pixels)", height);
        samples = spin("renderSamples", 1, 1024, preferences.value("render/samples", 32).toInt());
        form->addRow("Samples", samples);
        backend = new QComboBox;
        backend->setObjectName("renderBackend");
        backend->addItems({"CPU", "CUDA", "OPTIX", "HIP", "ONEAPI", "METAL"});
        form->addRow("Device backend", backend);
        device = new QComboBox;
        device->setObjectName("renderDevice");
        form->addRow("Device", device);
        fallback = new QCheckBox("Retry once on CPU if the selected GPU fails");
        fallback->setChecked(true);
        form->addRow(fallback);
        layout->addLayout(form);
        auto *framing =
            new QLabel("Current view preserves vertical framing; image width follows the chosen "
                       "aspect ratio. Materials use the documented GLB subset.");
        framing->setWordWrap(true);
        layout->addWidget(framing);
        probeButton = new QPushButton("Check Blender and devices");
        probeButton->setObjectName("probeBlender");
        layout->addWidget(probeButton);
        setupStatus = new QLabel("Blender 5.2 LTS with Cycles is required only for rendering. On "
                                 "Arch, install the blender package, then check again.");
        setupStatus->setObjectName("renderSetupStatus");
        setupStatus->setWordWrap(true);
        setupStatus->setTextFormat(Qt::PlainText);
        layout->addWidget(setupStatus);
        jobStatus = new QLabel;
        jobStatus->setObjectName("renderJobStatus");
        jobStatus->setWordWrap(true);
        jobStatus->setTextFormat(Qt::PlainText);
        layout->addWidget(jobStatus);
        logs = new QPlainTextEdit;
        logs->setObjectName("renderLogs");
        logs->setReadOnly(true);
        logs->setMaximumBlockCount(2000);
        logs->setMaximumHeight(130);
        logs->setPlaceholderText("Worker diagnostics appear here after an attempt finishes.");
        layout->addWidget(logs);
        auto *buttons = new QHBoxLayout;
        renderButton = new QPushButton("Render");
        renderButton->setObjectName("startRender");
        renderButton->setDefault(true);
        buttons->addWidget(renderButton);
        cancelButton = new QPushButton("Cancel job");
        cancelButton->setObjectName("cancelRender");
        buttons->addWidget(cancelButton);
        auto *close = new QPushButton("Close");
        buttons->addWidget(close);
        outer->addLayout(buttons);
        QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::hide);
        QObject::connect(path, &QLineEdit::textChanged, &owner, [this] { invalidate(); });
        QObject::connect(backend, &QComboBox::currentIndexChanged, &owner,
                         [this] { invalidate(); });
        QObject::connect(probeButton, &QPushButton::clicked, &owner, [this] { checkBlender(); });
        QObject::connect(cancelButton, &QPushButton::clicked, &owner, [this] { cancel(); });
        QObject::connect(renderButton, &QPushButton::clicked, &owner, [this] {
            try {
                RenderOptions settings;
                settings.settings = {width->value(), height->value(), samples->value(), 0};
                BlenderJob::Options worker;
                worker.executable = path->text().trimmed();
                worker.backend = backend->currentText();
                worker.deviceId = device->currentData().toString();
                worker.allowCpuFallback = fallback->isChecked();
                QSettings preferences("SketchyUp", "SketchyUp");
                preferences.setValue("render/blenderPath", worker.executable);
                preferences.setValue("render/width", width->value());
                preferences.setValue("render/height", height->value());
                preferences.setValue("render/samples", samples->value());
                start(settings, worker, camera->currentIndex() == 0);
                dialog->hide();
                view.setFocus();
            } catch (const std::exception &error) {
                status = "Render could not start";
                details = QString::fromUtf8(error.what());
                publish();
            }
        });
        publish();
        dialog->show();
        if (!active())
            checkBlender();
    }
    void checkBlender() {
        if (active() || (probe && !probe->done()))
            return;
        if (probe)
            delete probe;
        verified = false;
        device->clear();
        probe = new BlenderJob(&owner);
        QObject::connect(probe, &BlenderJob::changed, &owner, [this] {
            if (!probe)
                return;
            if (probe->done()) {
                const auto report = probe->report();
                if (probe->phase() == BlenderJob::Phase::Succeeded) {
                    const auto worker = report.value("worker").toObject();
                    for (auto value : worker.value("devices").toArray()) {
                        const auto item = value.toObject();
                        device->addItem(item.value("name").toString(), item.value("id"));
                    }
                    verified = device->count() > 0;
                    setupStatus->setText(
                        verified ? "Blender " + worker.value("blenderVersionString").toString() +
                                       " · device ready"
                                 : "No device is available for this backend. Choose CPU or another "
                                   "backend.");
                } else
                    setupStatus->setText("Blender check: " + phaseText(probe->phase()) + ". " +
                                         report.value("message").toString() +
                                         " Choose Blender 5.2 LTS with Cycles and check again.");
            } else
                setupStatus->setText("Checking Blender and the selected backend…");
            publish();
        });
        BlenderJob::Options options;
        options.executable = path->text().trimmed();
        options.backend = backend->currentText();
        options.deviceId = "probe-only";
        try {
            probe->probe(options);
        } catch (const std::exception &error) {
            setupStatus->setText(QString::fromUtf8(error.what()));
            publish();
        }
    }
    void start(RenderOptions settings, BlenderJob::Options worker, bool currentView) {
        checkOwner();
        if (active() || (probe && !probe->done()))
            throw std::runtime_error("A render or Blender check is already running");
        if (view.inspectionBusy())
            throw std::runtime_error(
                "Finish or cancel the active modeling gesture before capturing a render");
        if (view.inspectionRenderOverrides())
            throw std::runtime_error(
                "Disable temporary clipping, opacity or benchmark overrides before rendering");
        if (currentView)
            settings.camera = view.renderCamera();
        auto snapshot =
            RenderSnapshot::capture(document, settings, view.selectionState().hiddenEntities());
        sourceSession = document.saveStamp();
        workerOptions = std::move(worker);
        if (job)
            delete job;
        preparingCanceled = false;
        handled = false;
        details.clear();
        status = "Preparing render";
        elapsed.restart();
        preparing = std::async(std::launch::async, [snapshot = std::move(snapshot)] {
            return PreparedRender::prepare(snapshot);
        });
        poll.start();
        publish();
    }
    void tick() {
        if (preparing.valid() &&
            preparing.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto input = preparing.get();
                if (preparingCanceled) {
                    status = "Render canceled";
                    poll.stop();
                    publish();
                    return;
                }
                job = new BlenderJob(&owner);
                QObject::connect(job, &BlenderJob::changed, &owner, [this] { updateJob(); });
                job->start(std::move(input), workerOptions);
            } catch (const std::exception &error) {
                status = "Render failed";
                details = QString::fromUtf8(error.what());
                poll.stop();
            }
        }
        publish();
    }
    void updateJob() {
        if (!job)
            return;
        status = phaseText(job->phase());
        const auto report = job->report();
        details = job->done() ? report.value("message").toString() : job->progress();
        if (logs) {
            QString text;
            for (auto value : report.value("attempts").toArray()) {
                const auto attempt = value.toObject();
                text += attempt.value("backend").toString() + " attempt\n" +
                        attempt.value("logTail").toString() + "\n";
            }
            logs->setPlainText(text.right(128 * 1024));
        }
        if (job->done() && !handled) {
            handled = true;
            poll.stop();
            if (job->result())
                addResult(job->result());
            job->deleteLater();
            job = nullptr;
        }
        publish();
    }
    void cancel() {
        checkOwner();
        if (probe && !probe->done())
            probe->cancel();
        if (preparing.valid()) {
            preparingCanceled = true;
            status = "Canceling preparation";
            publish();
        }
        if (job && !job->done())
            job->cancel();
    }
    void addResult(std::shared_ptr<const BlenderResult> result) {
        refresh();
        if (results.size() >= 2) {
            auto page = results.front().page;
            tabs.removeTab(tabs.indexOf(page));
            delete page;
            results.erase(results.begin());
        }
        auto *page = new QWidget;
        page->setObjectName("renderResult");
        auto *layout = new QVBoxLayout(page);
        auto *provenance = new QLabel;
        provenance->setObjectName("renderProvenance");
        provenance->setWordWrap(true);
        provenance->setTextFormat(Qt::PlainText);
        layout->addWidget(provenance);
        layout->addWidget(new ImageView(result->image), 1);
        auto *buttons = new QHBoxLayout;
        auto *save = new QPushButton("Save image as…");
        save->setObjectName("saveRender");
        buttons->addWidget(save);
        save->setToolTip("Save this image; only the two latest renders are kept in memory.");
        auto *again = new QPushButton("Render again…");
        again->setObjectName("renderAgain");
        buttons->addWidget(again);
        buttons->addStretch();
        layout->addLayout(buttons);
        QObject::connect(save, &QPushButton::clicked, &owner, [this, result] {
            const auto path = QFileDialog::getSaveFileName(&window, "Save render image",
                                                           "render.png", "PNG images (*.png)");
            if (path.isEmpty())
                return;
            try {
                saveImage(*result, path);
                status = "Render image saved";
                details.clear();
            } catch (const std::exception &error) {
                status = "Image save failed";
                details = QString::fromUtf8(error.what());
            }
            publish();
        });
        QObject::connect(again, &QPushButton::clicked, &owner, [this] { show(); });
        results.push_back({page, provenance, sourceSession, std::move(result)});
        const auto revision = results.back().result->manifest.value("revision").toString();
        tabs.setCurrentIndex(tabs.addTab(page, "Render · r" + revision));
        refresh();
    }
    void refresh() {
        std::erase_if(results, [](const Result &entry) { return entry.page.isNull(); });
        for (const auto &entry : results) {
            const auto &manifest = entry.result->manifest;
            const bool same =
                document.owns(entry.session) && manifest.value("documentId").toString() ==
                                                    QString::fromStdString(document.identity());
            QString text = "From revision " + manifest.value("revision").toString();
            if (!same)
                text += " · from another model session";
            else if (manifest.value("revision").toString() != QString::number(document.revision()))
                text += " · model has changed since";
            const auto device = manifest.value("device").toObject();
            text += "\nBlender " + manifest.value("blenderVersionString").toString() +
                    " · Studio · " + device.value("backend").toString();
            if (device.value("name") != device.value("backend"))
                text += " · " + device.value("name").toString();
            if (manifest.value("cpuFallbackUsed") == true)
                text += " · CPU fallback";
            const auto losses = manifest.value("losses").toObject();
            QStringList limitations;
            for (const auto &[key, label] :
                 {std::pair{"wiresOmitted", "standalone edges omitted"},
                  {"sectionCutEdgesOmitted", "section cut edges omitted"},
                  {"annotationsOmitted", "dimensions and labels omitted"},
                  {"editableTextSourcesOmitted", "editable text sources omitted; geometry retained"},
                  {"solarLightingOmitted", "sun lighting omitted; study settings retained"},
                  {"referenceImagesOmitted", "reference images omitted"},
                  {"guidesOmitted", "guides omitted"},
                  {"analyticCurvesTessellatedOrOmitted", "curves approximated or omitted"},
                  {"differentBackAppearancesUseFront", "back faces use front appearance"},
                  {"textureAssetsPreservedWithoutUVMapping",
                   "some material images could not be rendered"},
                  {"missingAssets", "some material assets are missing"}})
                if (losses.value(key).toDouble() > 0)
                    limitations.append(QString::fromLatin1(label));
            if (!limitations.isEmpty())
                text += "\nTransfer limitations: " + limitations.join("; ");
            entry.provenance->setText(text);
        }
    }
};
RenderPanel::RenderPanel(Document &document, Viewport &view, QTabWidget &tabs, QPushButton &chip,
                         QWidget &window)
    : QObject(&window), impl_(std::make_unique<Impl>(*this, document, view, tabs, chip, window)) {}
RenderPanel::~RenderPanel() = default;
void RenderPanel::showSetup() { impl_->show(); }
void RenderPanel::refreshProvenance() {
    impl_->checkOwner();
    impl_->refresh();
}
void RenderPanel::start(RenderOptions settings, BlenderJob::Options worker, bool currentView) {
    impl_->start(settings, std::move(worker), currentView);
}
void RenderPanel::cancel() { impl_->cancel(); }
bool RenderPanel::active() const {
    impl_->checkOwner();
    return impl_->active();
}
QString RenderPanel::status() const {
    impl_->checkOwner();
    return impl_->status;
}
std::shared_ptr<const BlenderResult> RenderPanel::latest() const {
    impl_->checkOwner();
    return impl_->results.empty() ? nullptr : impl_->results.back().result;
}
void RenderPanel::saveLatest(const QString &path) {
    const auto result = latest();
    if (!result)
        throw std::runtime_error("No verified render to save");
    saveImage(*result, path);
}
} // namespace sketchy
