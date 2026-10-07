#include "app/render_panel.hpp"
#include "app/render_jobs.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
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
    QLineEdit *path{}, *environmentPath{};
    QDoubleSpinBox *environmentStrength{}, *environmentRotation{};
    QComboBox *camera{}, *engine{}, *backend{}, *device{};
    QSpinBox *width{}, *height{}, *samples{};
    QCheckBox *fallback{};
    QPushButton *probeButton{}, *renderButton{}, *cancelButton{};
    QLabel *setupStatus{}, *jobStatus{};
    QPlainTextEdit *logs{};
    QPointer<BlenderJob> probe;
    std::unique_ptr<RenderQueue> queue;
    QPointer<RenderJobsDialog> jobsDialog;
    std::map<QString, Document::SaveStamp> sessions;
    QString recentJob;
    QTimer poll;
    QElapsedTimer elapsed;
    std::future<std::shared_ptr<const PreparedRender>> preparing;
    BlenderJob::Options workerOptions;
    bool preparingCanceled{}, verified{};
    QString status{"Ready"}, details;
    Document::SaveStamp sourceSession;
    struct Result {
        QPointer<QWidget> page;
        QPointer<QLabel> provenance;
        Document::SaveStamp session;
        QString jobId;
        std::shared_ptr<const BlenderResult> result;
    };
    std::vector<Result> results;
    Impl(RenderPanel &owner, Document &document, Viewport &view, QTabWidget &tabs,
         QPushButton &chip, QWidget &window)
        : owner(owner), document(document), view(view), tabs(tabs), chip(chip), window(window),
          poll(&owner) {
        chip.hide();
        chip.setObjectName("renderStatusChip");
        QObject::connect(&chip, &QPushButton::clicked, &owner, [this] { showJobs(); });
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
        QTimer::singleShot(0, &owner, [this] {
            if (!QFileInfo::exists(
                    QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                    "/render-jobs"))
                return;
            try {
                ensureQueue();
            } catch (const std::exception &error) {
                status = "Render jobs unavailable";
                details = QString::fromUtf8(error.what());
                elapsed.start();
                publish();
            }
        });
    }
    ~Impl() {
        if (probe)
            delete probe;
        if (jobsDialog)
            delete jobsDialog;
        queue.reset();
        if (dialog)
            delete dialog;
    }
    void checkOwner() const {
        if (QThread::currentThread() != owner.thread())
            throw std::runtime_error("Render panel requires owner thread");
    }
    bool active() const {
        if (preparing.valid())
            return true;
        if (queue)
            for (const auto &record : queue->jobs())
                if (record.state == RenderJobState::Queued ||
                    record.state == RenderJobState::Running ||
                    record.state == RenderJobState::Canceling)
                    return true;
        return false;
    }
    void ensureQueue() {
        if (queue)
            return;
        queue = std::make_unique<RenderQueue>(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
            "/render-jobs");
        QObject::connect(queue.get(), &RenderQueue::changed, &owner, [this] { updateQueue(); });
        QObject::connect(queue.get(), &RenderQueue::resultReady, &owner, [this](const QString &id) {
            try {
                openResult(id);
            } catch (const std::exception &error) {
                status = "Render could not open";
                details = QString::fromUtf8(error.what());
                publish();
            }
        });
        QObject::connect(queue.get(), &RenderQueue::error, &owner, [this](const QString &message) {
            details = message;
            publish();
        });
        if (!queue->jobs().empty()) {
            recentJob = queue->jobs().back().id;
            elapsed.start();
            updateQueue();
        }
    }
    QString provenance(const StoredRenderJob &record) const {
        QString text = "Revision " + record.revision;
        const auto session = sessions.find(record.id);
        if (session == sessions.end())
            text += " · restored capture";
        else if (!document.owns(session->second) ||
                 record.documentId != QString::fromStdString(document.identity()))
            text += " · another model session";
        else if (record.revision != QString::number(document.revision()))
            text += " · model has changed since";
        return text;
    }
    void showJobs() {
        checkOwner();
        try {
            ensureQueue();
            if (!jobsDialog)
                jobsDialog = new RenderJobsDialog(
                    *queue, [this](const StoredRenderJob &job) { return provenance(job); },
                    [this](const QString &id) { openResult(id); }, &window);
            jobsDialog->refresh();
            jobsDialog->show();
            jobsDialog->raise();
            jobsDialog->activateWindow();
        } catch (const std::exception &error) {
            status = "Render jobs unavailable";
            details = QString::fromUtf8(error.what());
            elapsed.start();
            publish();
            show();
        }
    }
    void openResult(const QString &id) {
        for (const auto &result : results)
            if (result.jobId == id && result.page) {
                tabs.setCurrentWidget(result.page);
                return;
            }
        addResult(queue->result(id), id);
    }
    void publish() {
        QString chipText = status;
        if (queue) {
            int running{}, waiting{};
            for (const auto &record : queue->jobs()) {
                running += record.state == RenderJobState::Running ||
                           record.state == RenderJobState::Canceling;
                waiting += record.state == RenderJobState::Queued;
            }
            if (running || waiting)
                chipText = QString::number(running) + " rendering · " + QString::number(waiting) +
                           " queued";
        }
        chip.setText(chipText);
        chip.setVisible(elapsed.isValid());
        chip.setAccessibleName("Render jobs: " + chipText);
        if (dialog) {
            const auto seconds = elapsed.isValid() ? elapsed.elapsed() / 1000 : 0;
            jobStatus->setText(status + (active() ? " · " + QString::number(seconds) + " s" : "") +
                               (details.isEmpty() ? QString{} : "\n" + details));
            const bool busy = preparing.valid() || (probe && !probe->done());
            for (QWidget *control : std::initializer_list<QWidget *>{
                     path, camera, engine, width, height, samples, environmentPath,
                     environmentStrength, environmentRotation, backend, device, fallback,
                     probeButton})
                control->setEnabled(!busy);
            fallback->setEnabled(!busy && engine->currentIndex() == 0);
            renderButton->setEnabled(!busy && verified && device->currentIndex() >= 0);
            probeButton->setEnabled(!busy && !active());
            cancelButton->setEnabled(busy || active());
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
        auto *intro = new QLabel("Blender renders a captured model while you keep working. "
                                 "Enabled sun studies carry into renders. An optional HDR panorama "
                                 "replaces the ambient world and studio lights.");
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
            if (preparing.valid() || (probe && !probe->done()))
                return;
            const auto selected = QFileDialog::getOpenFileName(dialog, "Choose Blender 5.2 LTS");
            if (!selected.isEmpty())
                path->setText(selected);
        });
        engine = new QComboBox;
        engine->setObjectName("renderEngine");
        engine->addItems({"Cycles", "Eevee preview"});
        form->addRow("Render engine", engine);
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
        environmentPath = new QLineEdit;
        environmentPath->setObjectName("renderEnvironmentPath");
        environmentPath->setPlaceholderText("Optional 2:1 Radiance HDR panorama");
        form->addRow("HDR environment", environmentPath);
        auto *chooseEnvironment = new QPushButton("Choose HDR panorama…");
        chooseEnvironment->setObjectName("chooseRenderEnvironment");
        form->addRow({}, chooseEnvironment);
        QObject::connect(chooseEnvironment, &QPushButton::clicked, &owner, [this] {
            if (preparing.valid())
                return;
            const auto selected = QFileDialog::getOpenFileName(dialog, "Choose HDR environment", {},
                                                               "Radiance panoramas (*.hdr)");
            if (!selected.isEmpty())
                environmentPath->setText(selected);
        });
        environmentStrength = new QDoubleSpinBox;
        environmentStrength->setObjectName("renderEnvironmentStrength");
        environmentStrength->setRange(0, 100);
        environmentStrength->setDecimals(3);
        environmentStrength->setValue(1);
        form->addRow("Environment strength", environmentStrength);
        environmentRotation = new QDoubleSpinBox;
        environmentRotation->setObjectName("renderEnvironmentRotation");
        environmentRotation->setRange(-360, 360);
        environmentRotation->setDecimals(2);
        environmentRotation->setSuffix("°");
        form->addRow("Environment rotation", environmentRotation);
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
        QObject::connect(engine, &QComboBox::currentIndexChanged, &owner, [this] {
            backend->clear();
            if (engine->currentIndex() == 1)
                backend->addItem("OPENGL");
            else
                backend->addItems({"CPU", "CUDA", "OPTIX", "HIP", "ONEAPI", "METAL"});
            fallback->setChecked(engine->currentIndex() == 0);
            invalidate();
        });
        layout->addLayout(form);
        auto *framing = new QLabel(
            "Current view preserves vertical framing; image width follows the chosen "
            "aspect ratio. Eevee preview approximates indirect lighting and uses the "
            "active OpenGL renderer. Cycles supports explicit compute-device selection.");
        framing->setWordWrap(true);
        layout->addWidget(framing);
        probeButton = new QPushButton("Check Blender and devices");
        probeButton->setObjectName("probeBlender");
        layout->addWidget(probeButton);
        setupStatus = new QLabel("Blender 5.2 LTS is required only for rendering. On "
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
        auto *jobs = new QPushButton("Jobs…");
        jobs->setObjectName("showRenderJobs");
        buttons->addWidget(jobs);
        QObject::connect(jobs, &QPushButton::clicked, &owner, [this] { showJobs(); });
        auto *close = new QPushButton("Close");
        buttons->addWidget(close);
        outer->addLayout(buttons);
        QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::hide);
        QObject::connect(path, &QLineEdit::textChanged, &owner, [this] { invalidate(); });
        QObject::connect(backend, &QComboBox::currentIndexChanged, &owner,
                         [this] { invalidate(); });
        QObject::connect(probeButton, &QPushButton::clicked, &owner, [this] { checkBlender(); });
        QObject::connect(cancelButton, &QPushButton::clicked, &owner, [this] {
            try {
                cancel();
            } catch (const std::exception &error) {
                status = "Cancellation failed";
                details = QString::fromUtf8(error.what());
                publish();
            }
        });
        QObject::connect(renderButton, &QPushButton::clicked, &owner, [this] {
            try {
                RenderOptions settings;
                settings.settings = {width->value(), height->value(), samples->value(), 0};
                settings.settings.engine =
                    engine->currentIndex() == 1 ? RenderEngine::Eevee : RenderEngine::Cycles;
                if (!environmentPath->text().trimmed().isEmpty())
                    settings.environment = readRenderEnvironment(environmentPath->text().trimmed(),
                                                                 environmentStrength->value(),
                                                                 environmentRotation->value());
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
                                         " Choose Blender 5.2 LTS and check again.");
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
        if (preparing.valid() || (probe && !probe->done()))
            throw std::runtime_error("A render capture or Blender check is already running");
        ensureQueue();
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
        preparingCanceled = false;
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
                recentJob = queue->enqueue(std::move(input), workerOptions);
                sessions[recentJob] = sourceSession;
                updateQueue();
            } catch (const std::exception &error) {
                status = "Render failed";
                details = QString::fromUtf8(error.what());
                poll.stop();
            }
        }
        publish();
    }
    void updateQueue() {
        if (!queue)
            return;
        const auto records = queue->jobs();
        if (records.empty() && !preparing.valid()) {
            status = "Ready";
            details.clear();
        }
        for (auto &result : results) {
            if (!result.page)
                continue;
            if (std::none_of(records.begin(), records.end(),
                             [&](const auto &record) { return record.id == result.jobId; })) {
                tabs.removeTab(tabs.indexOf(result.page));
                delete result.page;
            }
        }
        refresh();
        if (!preparing.valid())
            for (const auto &record : records) {
                if (record.id != recentJob)
                    continue;
                switch (record.state) {
                case RenderJobState::Queued:
                    status = "Render queued";
                    break;
                case RenderJobState::Running:
                    status = "Rendering";
                    break;
                case RenderJobState::Canceling:
                    status = "Canceling";
                    break;
                case RenderJobState::Completed:
                    status = "Render ready";
                    break;
                case RenderJobState::Failed:
                    status = "Render failed";
                    break;
                case RenderJobState::Canceled:
                    status = "Render canceled";
                    break;
                case RenderJobState::Interrupted:
                    status = "Render interrupted";
                    break;
                }
                details = record.message;
                if (logs)
                    logs->setPlainText(
                        QString::fromUtf8(QJsonDocument(queue->diagnostics(record.id)).toJson())
                            .right(128 * 1024));
            }
        if (!active())
            poll.stop();
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
        if (queue && !preparing.valid())
            for (const auto &record : queue->jobs())
                if (record.id == recentJob && (record.state == RenderJobState::Queued ||
                                               record.state == RenderJobState::Running ||
                                               record.state == RenderJobState::Canceling))
                    queue->cancel(record.id);
    }
    void addResult(std::shared_ptr<const BlenderResult> result, const QString &id) {
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
        save->setToolTip("Save this image. Reopen retained images from Jobs after closing a tab.");
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
        results.push_back({page, provenance,
                           sessions.contains(id) ? sessions.at(id) : Document::SaveStamp{}, id,
                           std::move(result)});
        const auto revision = results.back().result->manifest.value("revision").toString();
        tabs.setCurrentIndex(tabs.addTab(page, "Render · r" + revision));
        refresh();
    }
    void refresh() {
        std::erase_if(results, [](const Result &entry) { return entry.page.isNull(); });
        if (jobsDialog)
            jobsDialog->refresh();
        for (const auto &entry : results) {
            const auto &manifest = entry.result->manifest;
            const bool same =
                document.owns(entry.session) && manifest.value("documentId").toString() ==
                                                    QString::fromStdString(document.identity());
            QString text = "From revision " + manifest.value("revision").toString();
            if (!sessions.contains(entry.jobId))
                text += " · restored capture; current session not verified";
            else if (!same)
                text += " · from another model session";
            else if (manifest.value("revision").toString() != QString::number(document.revision()))
                text += " · model has changed since";
            const auto device = manifest.value("device").toObject();
            const auto lighting = manifest.value("lighting").toObject();
            const bool sun = lighting.contains("settings"), hdri = lighting.contains("environment");
            const auto lightName = sun ? (hdri ? "Sun study + HDR" : "Sun study")
                                       : (hdri ? "HDR environment" : "Studio");
            text += "\nBlender " + manifest.value("blenderVersionString").toString() + " · " +
                    lightName + " · " + device.value("backend").toString();
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
                  {"editableTextSourcesOmitted",
                   "editable text sources omitted; geometry retained"},
                  {"solarLightingOmitted", "sun lighting omitted; study settings retained"},
                  {"environmentLightingOmitted", "HDR environment lighting omitted"},
                  {"indirectLightingApproximated", "indirect lighting approximated by Eevee"},
                  {"samplingSeedNotApplied", "sampling seed applies only to Cycles"},
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
void RenderPanel::showJobs() { impl_->showJobs(); }
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
