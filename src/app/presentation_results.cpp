#include "app/presentation_results.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace sketchy {
namespace {

QString frameText(int index) { return QString("frame %1").arg(index + 1, 4, 10, QChar('0')); }

QString bold(const QString &text) { return QString("<b>%1</b>").arg(text.toHtmlEscaped()); }

} // namespace

PresentationResultsDialog::PresentationResultsDialog(PresentationBatches &batches,
                                                     const QString &id, QWidget *parent)
    : QDialog(parent), batches_(batches), id_(id) {
    setObjectName("presentationResults");
    setWindowTitle("Presentation results");
    resize(1040, 760);
    auto *outer = new QVBoxLayout(this);

    StoredPresentationJob job;
    bool found = false;
    for (const auto &candidate : batches_.jobs())
        if (candidate.id == id) {
            job = candidate;
            found = true;
        }
    if (!found) {
        outer->addWidget(new QLabel("This batch was removed, so there are no results to show."));
        auto *close = new QPushButton("Close");
        close->setObjectName("presentationResultsClose");
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
        outer->addWidget(close, 0, Qt::AlignRight);
        return;
    }
    const auto manifest = presentationManifest(job.output);
    const auto presets = manifest.value("presets").toArray();
    const bool completed = job.state == PresentationJobState::Completed;
    const bool running =
        job.state == PresentationJobState::Running || job.state == PresentationJobState::Canceling;
    const bool unchanged = batches_.modelUnchanged(job);
    int videos = 0;
    for (const auto &value : presets)
        videos += value.toObject().contains("video");
    const int frames = manifest.value("completedFrames").toInt();
    const int totalFrames = manifest.value("totalFrames").toInt();

    // The title, with its state as a word (UX_DESIGN 17.10: never colour alone).
    auto *title = new QLabel(
        QString("%1 · presentation results %2")
            .arg(bold(job.plan.path))
            .arg(completed ? "<span>✓ Completed</span>" : "<span>⚠ Not finished</span>"));
    title->setObjectName("resultsTitle");
    title->setTextFormat(Qt::RichText);
    outer->addWidget(title);
    auto *location = new QLabel(QString("%1 · %2 presets · %3 frames")
                                    .arg(job.output)
                                    .arg(job.plan.presets.size())
                                    .arg(totalFrames));
    location->setObjectName("resultsLocation");
    location->setWordWrap(true);
    outer->addWidget(location);

    // The banner (UX_DESIGN 17.7). A finished batch says the model is unchanged only when it is.
    banner_ = new QLabel;
    banner_->setObjectName("resultsBanner");
    banner_->setWordWrap(true);
    banner_->setTextFormat(Qt::PlainText);
    if (completed)
        banner_->setText(
            QString("✓ Completed. %1 presets, %2 frames, %3 %4. %5")
                .arg(job.plan.presets.size())
                .arg(frames)
                .arg(videos)
                .arg(videos == 1 ? "video" : "videos")
                .arg(unchanged ? "The model is unchanged." : "The model has changed since."));
    else
        banner_->setText(QString("⚠ Not finished. %1 Its frames are kept. Resume continues the "
                                 "batch at its first unfinished frame.")
                             .arg(job.message));
    outer->addWidget(banner_);

    auto *body = new QHBoxLayout;
    outer->addLayout(body, 1);

    // The left column: what it was made from, each preset with its video, and the folder layout.
    auto *left = new QVBoxLayout;
    auto *leftPane = new QWidget;
    leftPane->setLayout(left);
    body->addWidget(leftPane, 1);
    auto *madeHeading = new QLabel(bold("Made from"));
    left->addWidget(madeHeading);
    auto *made = new QFormLayout;
    const auto device =
        presets.isEmpty() ? QJsonObject{} : presets.first().toObject().value("device").toObject();
    const QString modelState =
        unchanged ? QString("model unchanged") : QString("model has changed since");
    made->addRow("Model", new QLabel(QString("Revision %1 · %2").arg(job.revision, modelState)));
    made->addRow(
        "Render",
        new QLabel(QString("Device %1").arg(device.value("backend").toString("not recorded"))));
    made->addRow("Frames", new QLabel(QString("%1 kept of %2").arg(frames).arg(totalFrames)));
    left->addLayout(made);
    left->addWidget(new QLabel(bold("Presets")));

    const int fps = job.plan.timing.framesPerSecond;
    for (int p = 0; p < int(job.plan.presets.size()); ++p) {
        const auto &preset = job.plan.presets[p];
        const auto entry = presets.size() > p ? presets.at(p).toObject() : QJsonObject{};
        auto *card = new QFrame;
        card->setObjectName("resultPreset");
        card->setFrameShape(QFrame::StyledPanel);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->addWidget(new QLabel(bold(preset.name)));
        cardLayout->addWidget(new QLabel(QString("%1 × %2 · %3 samples · %4 frames")
                                             .arg(preset.settings.width)
                                             .arg(preset.settings.height)
                                             .arg(preset.settings.samples)
                                             .arg(entry.value("frames").toArray().size())));
        const auto directory = entry.value("directory").toString();
        const auto videoPath = job.output + "/" + directory + "/video.mp4";
        auto *buttons = new QHBoxLayout;
        if (entry.contains("video")) {
            const auto frameCount = entry.value("frames").toArray().size();
            const auto seconds = fps > 0 ? double(frameCount) / fps : 0.0;
            const auto megabytes = double(QFileInfo(videoPath).size()) / 1000000.0;
            cardLayout->addWidget(
                new QLabel(QString("✓ Video %1 s · %2 MP4 · %3 MB")
                               .arg(seconds, 0, 'f', 1)
                               .arg(presentationVideoCodecName(job.plan.videoCodec) == "mpeg4"
                                        ? QString("MPEG-4 Part 2")
                                        : QString("H.264"))
                               .arg(megabytes, 0, 'f', 1)));
            auto *play = new QPushButton("Play video");
            play->setObjectName("resultPlayVideo");
            play->setAccessibleName("Play video, " + preset.name);
            connect(play, &QPushButton::clicked, this,
                    [videoPath] { QDesktopServices::openUrl(QUrl::fromLocalFile(videoPath)); });
            buttons->addWidget(play);
        } else if (job.plan.video) {
            cardLayout->addWidget(new QLabel("✕ Video not written"));
            if (!running) {
                auto *resume = new QPushButton("Resume: encode video");
                resume->setObjectName("resultResumeVideo");
                resume->setAccessibleName("Resume: encode video, " + preset.name);
                connect(resume, &QPushButton::clicked, this, [this, id = job.id] {
                    try {
                        batches_.resume(id);
                        accept();
                    } catch (const std::exception &error) {
                        banner_->setText(QString::fromUtf8(error.what()));
                    }
                });
                buttons->addWidget(resume);
            }
        }
        auto *show = new QPushButton("Show in folder");
        show->setObjectName("resultShowFolder");
        show->setAccessibleName("Show in folder, " + preset.name);
        const auto presetFolder = job.output + "/" + directory;
        connect(show, &QPushButton::clicked, this,
                [presetFolder] { QDesktopServices::openUrl(QUrl::fromLocalFile(presetFolder)); });
        buttons->addWidget(show);
        buttons->addStretch();
        cardLayout->addLayout(buttons);
        left->addWidget(card);
    }
    left->addWidget(new QLabel(bold("Folder layout")));
    QString layout = QFileInfo(job.output).fileName() + "/\n    manifest.json\n";
    for (const auto &value : presets) {
        const auto directory = value.toObject().value("directory").toString();
        layout += "    " + directory + "/frames/frame-0001.png …\n";
        if (value.toObject().contains("video"))
            layout += "    " + directory + "/video.mp4\n";
    }
    auto *tree = new QLabel(layout);
    tree->setObjectName("resultsLayout");
    tree->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tree->setStyleSheet("font-family: monospace;");
    left->addWidget(tree);
    left->addStretch();

    // The right column: the stills grid. Rows are the keyframes in path order, and columns the
    // presets, so each still is a frame flagged as a keyframe in its own preset's sequence (ADR
    // 0173 §4).
    auto *right = new QVBoxLayout;
    auto *rightPane = new QWidget;
    rightPane->setLayout(right);
    body->addWidget(rightPane, 1);
    right->addWidget(new QLabel(bold("Keyframe stills") + " (scene × preset)"));
    auto *grid = new QGridLayout;
    auto *gridPane = new QWidget;
    gridPane->setLayout(grid);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("resultsStills");
    scroll->setAccessibleName("Keyframe stills, scene by preset");
    scroll->setWidgetResizable(true);
    scroll->setWidget(gridPane);
    right->addWidget(scroll, 1);
    for (int p = 0; p < int(job.plan.presets.size()); ++p)
        grid->addWidget(new QLabel(bold(job.plan.presets[p].name)), 0, p + 1);
    std::vector<std::vector<QJsonObject>> keyframes(job.plan.presets.size());
    for (int p = 0; p < presets.size() && p < int(keyframes.size()); ++p)
        for (const auto &value : presets.at(p).toObject().value("frames").toArray()) {
            const auto frame = value.toObject();
            if (frame.value("keyframe").toBool())
                keyframes[size_t(p)].push_back(frame);
        }
    for (int k = 0; k < int(job.plan.keyframes.size()); ++k) {
        grid->addWidget(new QLabel(QString("%1. %2").arg(k + 1).arg(
                            batches_.sceneName(job.plan.keyframes[size_t(k)]))),
                        k + 1, 0);
        for (int p = 0; p < int(keyframes.size()); ++p) {
            if (size_t(k) >= keyframes[size_t(p)].size()) {
                grid->addWidget(new QLabel("not finished"), k + 1, p + 1);
                continue;
            }
            const auto frame = keyframes[size_t(p)][size_t(k)];
            const auto text = frameText(frame.value("index").toInt());
            const auto path = job.output + "/" +
                              presets.at(p).toObject().value("directory").toString() + "/frames/" +
                              frame.value("file").toString();
            auto *still = new QPushButton(text);
            still->setObjectName("resultStill");
            still->setAutoDefault(true);
            still->setAccessibleName(QString("%1, %2, %3")
                                         .arg(batches_.sceneName(job.plan.keyframes[size_t(k)]),
                                              job.plan.presets[p].name, text));
            connect(still, &QPushButton::clicked, this,
                    [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
            grid->addWidget(still, k + 1, p + 1);
        }
    }
    auto *note =
        new QLabel("Each still is the frame flagged as a keyframe in its preset's sequence. "
                   "Activating a still opens its PNG in the system viewer.");
    note->setWordWrap(true);
    right->addWidget(note);

    // The footer. Close is the default, so Enter in the results never runs an edit.
    auto *footer = new QHBoxLayout;
    auto *copy = new QPushButton("Copy report");
    copy->setObjectName("resultsCopyReport");
    connect(copy, &QPushButton::clicked, this, [manifest] {
        QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(manifest).toJson()));
    });
    footer->addWidget(copy);
    auto *openFolder = new QPushButton("Show output folder");
    openFolder->setObjectName("resultsShowFolder");
    connect(openFolder, &QPushButton::clicked, this,
            [folder = job.output] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
    footer->addWidget(openFolder);
    footer->addStretch();
    auto *close = new QPushButton("Close");
    close->setObjectName("resultsClose");
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(close);
    outer->addLayout(footer);
}

} // namespace sketchy
