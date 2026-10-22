#include "app/terrain_replay_job.hpp"
#include "app/terrain_actions.hpp"
#include "app/viewport.hpp"
#include <QApplication>
#include <QDeadlineTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMetaObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QThread>
#include "core/document_limits.hpp"
namespace sketchy {
namespace {
TerrainReplayJob *runningJob = nullptr;
} // namespace

TerrainReplayChip::TerrainReplayChip(QWidget *parent) : QFrame(parent) {
    setObjectName("terrainReplayChip");
    setAccessibleName("Terrain replay");
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(8, 2, 4, 2);
    row->setSpacing(6);
    text_ = new QLabel;
    text_->setObjectName("terrainReplayText");
    row->addWidget(text_);
    cancel_ = new QPushButton("Cancel");
    cancel_->setObjectName("terrainReplayCancel");
    cancel_->setAccessibleName("Cancel the terrain replay");
    cancel_->setAutoDefault(false);
    row->addWidget(cancel_);
    connect(cancel_, &QPushButton::clicked, this, &TerrainReplayChip::cancelRequested);
    hide();
}
void TerrainReplayChip::show(const QString &text) {
    text_->setText(text);
    text_->setAccessibleName(text);
    QFrame::show();
}

QString terrainSkipReason(const QString &kind, QString detail) {
    detail = detail.trimmed();
    if (detail.size() >= 2 && (detail.front() == '\'' || detail.front() == '"'))
        detail = detail.mid(1, detail.size() - 2);
    const bool drape = kind.contains("drape");
    const QString what = drape ? "the lines now" : kind.contains("stamp") ? "the pad now" : "it now";
    if (detail.contains("inside-pad"))
        return what + " enter" + (drape ? "" : "s") + " a pad";
    if (detail.contains("outside-outline"))
        return what + (drape ? " leave" : " leaves") + " the terrain's outline";
    if (detail.contains("crossing-constraints"))
        return what + (drape ? " cross" : " crosses") + " another source";
    if (detail.contains("daylight-distance"))
        return "a slope ray now finds no ground within the maximum distance";
    const auto colon = detail.indexOf("nothing was changed: ");
    return colon >= 0 ? detail.mid(colon + 21) : detail;
}
std::map<std::uint32_t, QString> terrainReportSkips(const QJsonObject &report) {
    std::map<std::uint32_t, QString> skipped;
    static const QRegularExpression number("edit (\\d+)");
    for (const auto &notice : report["terrain"].toObject()["notices"].toArray()) {
        const auto object = notice.toObject();
        if (object["kind"] != "edit-skipped")
            continue;
        for (const auto &location : object["locations"].toArray()) {
            const auto reference = location.toObject()["reference"].toString();
            const auto match = number.match(reference);
            if (match.hasMatch())
                skipped[std::uint32_t(match.captured(1).toULongLong())] =
                    terrainSkipReason(reference, location.toObject()["detail"].toString());
        }
    }
    return skipped;
}
TerrainReplayJob *TerrainReplayJob::running() { return runningJob; }
TerrainReplayJob *TerrainReplayJob::start(QWidget *window, Document &doc, Viewport &view,
                                          const QJsonObject &command, const QString &title) {
    if (runningJob)
        throw std::runtime_error("A terrain replay is running; wait for it or cancel it");
    auto request = parseTerrainReplayRequest(doc, command);
    auto *job = new TerrainReplayJob(window, doc, view, command, title, std::move(request));
    runningJob = job;
    return job;
}
TerrainReplayJob::TerrainReplayJob(QWidget *window, Document &doc, Viewport &view,
                                   QJsonObject command, QString title,
                                   TerrainReplayRequest request)
    : QObject(qApp), window_(window), doc_(doc), view_(view), command_(std::move(command)),
      title_(std::move(title)), request_(std::move(request)),
      control_(std::make_shared<TerrainRunControl>()), stamp_(doc.saveStamp()) {
    pool_.setMaxThreadCount(1);
    // The changes the replay goes through (Reset edits: the ones it drops).
    changes_ = request_.record.edits.empty() ? request_.current.edits.size()
                                              : request_.record.edits.size();
    if (window_)
        chip_ = window_->findChild<TerrainReplayChip *>("terrainReplayChip");
    if (chip_)
        connect(chip_, &TerrainReplayChip::cancelRequested, this, &TerrainReplayJob::cancel);
    poll();
    connect(&poll_, &QTimer::timeout, this, &TerrainReplayJob::poll);
    poll_.start(50);
    QPointer<TerrainReplayJob> guard(this);
    // The worker reads the request (records it holds) and the control; never the document.
    pool_.start([this, guard, control = control_] {
        auto result = std::make_shared<TerrainImportResult>();
        try {
            auto progress = terrainRunProgress(control);
            *result = runTerrainReplay(request_, &progress);
        } catch (const std::exception &error) {
            result->ok = false;
            result->code = "INVALID_REQUEST";
            result->message = error.what();
        }
        QMetaObject::invokeMethod(
            qApp,
            [guard, result] {
                if (guard)
                    guard->done(result);
            },
            Qt::QueuedConnection);
    });
}
TerrainReplayJob::~TerrainReplayJob() {
    control_->cancel = true;
    pool_.waitForDone();
    if (runningJob == this)
        runningJob = nullptr;
}
void TerrainReplayJob::cancel() { control_->cancel = true; }
void TerrainReplayJob::poll() {
    if (!chip_)
        return;
    const auto permille = control_->permille.load();
    const auto text = QString("%1 %2 %3 · %4 %")
                          .arg(request_.record.edits.empty() ? "Resetting" : "Replaying")
                          .arg(QString::fromStdString(groupedCount(changes_)))
                          .arg(changes_ == 1 ? "change" : "changes")
                          .arg(permille / 10);
    if (text != chipText_ || !chip_->isVisible()) { // a screen reader hears each change once
        chipText_ = text;
        chip_->show(text);
    }
}
void TerrainReplayJob::done(std::shared_ptr<TerrainImportResult> result) {
    poll_.stop();
    if (chip_)
        chip_->hide();
    runningJob = nullptr;
    bool applied = false;
    const auto terrain = request_.terrain;
    if (!doc_.owns(stamp_)) {
        // The document was replaced (opened, reverted, new) while the replay ran.
        emit view_.message("The document changed; the replay was discarded");
    } else if (result->cancelled || control_->cancel.load()) {
        emit view_.message("Replay cancelled; nothing changed");
    } else if (!result->ok) {
        emit view_.message(QString("%1 refused; nothing changed: %2")
                               .arg(title_, QString::fromStdString(result->message)));
        if (window_ && !result->report.isEmpty()) {
            auto *dialog = new TerrainReportDialog(
                title_, title_ + " refused. Nothing was changed. " +
                            QString::fromStdString(result->message),
                true, result->report, window_);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->open();
        }
    } else {
        try {
            // Every skipped entry with its reason (the report lists a bounded number).
            std::map<std::uint32_t, QString> skipped;
            for (const auto &[edit, notice] : result->skipped) {
                QString kind;
                for (const auto &entry : request_.record.edits)
                    if (entry->id == edit)
                        kind = QString::fromStdString(entry->kind);
                skipped[edit] = terrainSkipReason(kind, QString::fromStdString(notice));
            }
            view_.cancel(); // a tool's preview of the old state goes
            const auto operation = applyTerrainReplay(doc_, request_, std::move(*result));
            applied = true;
            view_.refresh();
            emit view_.changed();
            const auto report = operation["terrainReport"].toObject();
            view_.setLastTerrainReport(terrain, report, title_);
            // Changes the replay skipped stay in the list, marked with their reasons.
            view_.setTerrainSkips(terrain, std::move(skipped));
            emit view_.message(title_ + " · done");
        } catch (const std::exception &error) {
            emit view_.message(QString("%1 refused; nothing changed: %2")
                                   .arg(title_, QString::fromUtf8(error.what())));
        }
    }
    emit finished(applied);
    deleteLater();
}
bool TerrainReplayJob::waitForIdle(int timeoutMs) {
    QDeadlineTimer deadline(timeoutMs);
    while (runningJob && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (runningJob)
            QThread::msleep(2);
    }
    QCoreApplication::processEvents();
    return !runningJob;
}
} // namespace sketchy
