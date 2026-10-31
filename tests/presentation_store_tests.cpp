#include "integrations/presentation_store.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action, const char *message) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write store fixture");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read store fixture");
    return file.readAll();
}
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
PresentationPlan plan(const QString &name = "Tour") {
    PresentationPlan result;
    result.path = name;
    result.keyframes = {3, 5, 9};
    result.timing = {24, 24, 12};
    RenderSettings draft;
    draft.width = 320;
    draft.height = 240;
    draft.samples = 4;
    RenderSettings final;
    final.engine = RenderEngine::Eevee;
    result.presets = {{"Draft", draft}, {"Final", final}};
    return result;
}
QByteArray capture(const char *tag = "frozen document") {
    return QByteArray(tag) + QByteArray(256, 'x');
}
const PresentationJobStore::Limits limits{2, 4, 64LL * 1024 * 1024};
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        check(files.isValid(), "Presentation store fixture directory");
        const auto path = files.filePath("jobs");
        const auto output = files.filePath("out");
        QString first, second, third;
        {
            PresentationJobStore store(path, limits);
            const auto others = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                QFileDevice::WriteOther | QFileDevice::ExeOther;
            check(QFileInfo(path).isDir() && !(QFileInfo(path).permissions() & others),
                  "Store directory is private to its owner");
            rejects([&] { PresentationJobStore duplicate(path, limits); },
                    "A second writer must not open the store");
            first = store.enqueue(capture("first"), "doc-1", "7", plan(), output + "/a");
            check(store.jobs().at(first).state == PresentationJobState::Queued &&
                      store.jobs().at(first).captureSha256 == digest(capture("first")),
                  "Created job records its state and capture hash");
            check(store.capture(first) == capture("first"), "Stored capture is returned unchanged");
            second = store.enqueue(capture("second"), "doc-1", "8", plan("Orbit"), output + "/b");
            rejects([&] { store.enqueue(capture(), "doc-1", "7", plan(), output + "/c"); },
                    "A third unfinished batch exceeds the pending limit");
            rejects([&] { store.enqueue(capture(), "doc-1", "7", plan(), "relative/out"); },
                    "Batch output must be absolute");
            rejects([&] { store.remove(first); }, "A queued batch cannot be removed");
            store.transition(first, PresentationJobState::Running);
            store.transition(first, PresentationJobState::Canceling);
            store.transition(first, PresentationJobState::Canceled, "Canceled by test");
            check(store.jobs().at(first).attempts == 1 &&
                      store.jobs().at(first).message == "Canceled by test",
                  "Terminal transition records attempts and message");
            rejects([&] { store.transition(second, PresentationJobState::Completed); },
                    "A queued batch cannot complete without running");
            store.transition(second, PresentationJobState::Running);
            store.transition(second, PresentationJobState::Canceling);
            // A run's worker scratch lives in the job's own directory and is cleaned on recovery.
            check(QDir().mkpath(store.scratchDirectory(second) + "/scratch-abcdef/attempt-1"),
                  "Scratch directory is inside the job directory");
            write(store.scratchDirectory(second) + "/scratch-abcdef/attempt-1/worker.log",
                  "Retained worker log before the process stopped\n");
        }
        {
            // Restart: the stopped batch is interrupted, keeps its plan and log, loses its scratch.
            PresentationJobStore store(path, limits);
            const auto &recovered = store.jobs().at(second);
            check(recovered.state == PresentationJobState::Interrupted &&
                      recovered.plan == plan("Orbit") && recovered.plan.presets.size() == 2,
                  "Restart reopens a stopped batch as interrupted with its plan intact");
            check(recovered.message == presentationInterruptedMessage() &&
                      recovered.message.contains("Resume") && !recovered.message.contains("Retry"),
                  "The interrupted message names Resume, in the interface's words");
            check(recovered.report.value("interruptedWorkerLogs")
                      .toObject()
                      .contains("scratch-abcdef/1"),
                  "Interrupted batch retains its bounded worker log");
            check(!QDir(store.scratchDirectory(second) + "/scratch-abcdef").exists(),
                  "Interrupted batch removes its scratch");
            check(store.jobs().at(first).state == PresentationJobState::Canceled,
                  "Terminal batches survive a restart unchanged");
            store.retry(second);
            check(store.jobs().at(second).state == PresentationJobState::Queued,
                  "Interrupted batch is retried into the queue");
            rejects([&] { store.retry(second); }, "A queued batch cannot be retried again");
            check(store.capture(second) == capture("second"), "Retry reuses the stored capture");
            third = store.enqueue(capture("third"), "doc-1", "9", plan(), output + "/c");
            rejects([&] { store.enqueue(capture(), "doc-1", "9", plan(), output + "/d"); },
                    "The pending limit counts queued and retried batches");
            store.transition(second, PresentationJobState::Running);
            rejects([&] { store.transition(third, PresentationJobState::Running); },
                    "Only one presentation batch may run at a time");
            store.transition(second, PresentationJobState::Failed, "Frame failed");
            check(store.jobs().at(second).state == PresentationJobState::Failed,
                  "Failure is recorded for the batch");
            store.transition(third, PresentationJobState::Running);
            store.transition(third, PresentationJobState::Canceled);
        }
        {
            // Retention: four records are kept, and a full history refuses the next batch.
            PresentationJobStore store(path, limits);
            const auto fourth =
                store.enqueue(capture("fourth"), "doc-1", "9", plan(), output + "/e");
            store.transition(fourth, PresentationJobState::Canceled);
            check(store.jobs().size() == 4, "Four batches are retained");
            rejects([&] { store.enqueue(capture(), "doc-1", "9", plan(), output + "/f"); },
                    "A full history refuses another batch");
            store.clearFinished();
            check(store.jobs().empty() && !QDir(path + "/" + fourth).exists(),
                  "Cleanup removes only finished batch records");
        }
        {
            // Storage budget: the capture and its record must fit the store's bytes.
            PresentationJobStore small(files.filePath("small"), {8, 16, 1024LL * 1024});
            rejects(
                [&] { small.enqueue(QByteArray(900 * 1024, 'y'), "doc-1", "9", plan(), output); },
                "A capture over the store budget is refused");
            check(small.jobs().empty(), "Refused capture leaves no job");
        }
        {
            // Plan refusals: the R099.a record rules, the four-preset limit and the frame cap.
            PresentationJobStore store(files.filePath("plans"), {8, 16, 64LL * 1024 * 1024});
            auto five = plan();
            five.presets = {{"A", {}}, {"B", {}}, {"C", {}}, {"D", {}}, {"E", {}}};
            rejects([&] { store.enqueue(capture(), "d", "1", five, output); },
                    "Five presets are refused");
            auto none = plan();
            none.presets.clear();
            rejects([&] { store.enqueue(capture(), "d", "1", none, output); },
                    "A batch needs a preset");
            auto duplicate = plan();
            duplicate.presets[1].name = "Draft";
            rejects([&] { store.enqueue(capture(), "d", "1", duplicate, output); },
                    "Preset names must be unique");
            auto one = plan();
            one.keyframes = {3};
            rejects([&] { store.enqueue(capture(), "d", "1", one, output); },
                    "A path needs two keyframes");
            auto long_ = plan();
            long_.timing = {24, 24, 80}; // 1 + 3 x 80 + 2 x 24 = 289 planned frames.
            rejects([&] { store.enqueue(capture(), "d", "1", long_, output); },
                    "A plan over 240 frames is refused");
            rejects([&] { store.enqueue(capture(), "d", "1", plan(""), output); },
                    "A path needs a name");
            check(store.jobs().empty(), "Refused plans leave no job");
        }
        {
            // A damaged job record reopens as failed, with its reason, and never resumes.
            const auto damaged = files.filePath("damaged");
            QString id;
            {
                PresentationJobStore store(damaged, limits);
                id = store.enqueue(capture("damaged"), "doc-2", "3", plan(), output + "/x");
            }
            write(damaged + "/" + id + "/job.json", "{ not a record");
            PresentationJobStore store(damaged, limits);
            check(store.jobs().at(id).state == PresentationJobState::Failed &&
                      !store.jobs().at(id).message.isEmpty(),
                  "Corrupt job record reopens as failed with its reason");
            rejects([&] { store.retry(id); }, "A damaged batch cannot be retried");
            store.remove(id);
            check(store.jobs().empty(), "Damaged evidence can be removed explicitly");
        }
        {
            // A changed capture is refused on every use and at reopen, never rendered.
            const auto tampered = files.filePath("tampered");
            QString id;
            {
                PresentationJobStore store(tampered, limits);
                id = store.enqueue(capture("tamper"), "doc-3", "4", plan(), output + "/y");
            }
            write(tampered + "/" + id + "/capture.sketchyup", capture("TAMPER!"));
            PresentationJobStore store(tampered, limits);
            check(store.jobs().at(id).state == PresentationJobState::Failed,
                  "A capture of the wrong size is refused at reopen");
            rejects([&] { store.capture(id); }, "A changed capture is refused");
            write(tampered + "/" + id + "/capture.sketchyup", capture("TAMPER"));
            rejects([&] { store.capture(id); }, "A same-size changed capture is refused");
        }
        {
            // The video codec is stored with the plan. A job stored before the codec choice existed
            // has no codec field, and reads as H.264, the only codec that existed then.
            const auto codecs = files.filePath("codecs");
            QString id;
            {
                PresentationJobStore store(codecs, limits);
                auto mpeg4 = plan("Mpeg");
                mpeg4.video = true;
                mpeg4.videoCodec = PresentationVideoCodec::MPEG4;
                id = store.enqueue(capture("codec"), "doc-4", "1", mpeg4, output + "/codec");
            }
            {
                PresentationJobStore store(codecs, limits);
                check(store.jobs().at(id).plan.videoCodec == PresentationVideoCodec::MPEG4 &&
                          store.jobs().at(id).plan.video,
                      "The store keeps the video codec with the plan");
            }
            const auto record = codecs + "/" + id + "/job.json";
            auto stored = QJsonDocument::fromJson(read(record)).object();
            auto legacy = stored.value("plan").toObject();
            legacy.remove("videoCodec");
            stored["plan"] = legacy;
            write(record, QJsonDocument(stored).toJson());
            PresentationJobStore reopened(codecs, limits);
            check(reopened.jobs().at(id).plan.videoCodec == PresentationVideoCodec::H264,
                  "A job stored without a codec field reads as H.264");
            check(presentationVideoCodecFromName("mpeg4") == PresentationVideoCodec::MPEG4 &&
                      presentationVideoCodecName(PresentationVideoCodec::MPEG4) == "mpeg4" &&
                      presentationVideoCodecId(PresentationVideoCodec::MPEG4) == "MPEG4",
                  "The codec names and identifiers agree");
            rejects([&] { presentationVideoCodecFromName("av1"); },
                    "An unknown codec name is refused");
        }
        {
            // R107.c3 (UX_DESIGN 17.13): the frame credit choice is stored with the plan. A job
            // stored before the choice existed has no field, and reads on: 1.5.0 always burned the
            // notice.
            const auto credits = files.filePath("credits");
            QString id;
            {
                PresentationJobStore store(credits, limits);
                auto unburned = plan("Unburned");
                unburned.burnCredits = false;
                id = store.enqueue(capture("credits"), "doc-5", "1", unburned, output + "/credits");
            }
            {
                PresentationJobStore store(credits, limits);
                check(!store.jobs().at(id).plan.burnCredits,
                      "The store keeps a batch that does not burn its credits");
            }
            const auto record = credits + "/" + id + "/job.json";
            auto stored = QJsonDocument::fromJson(read(record)).object();
            auto legacy = stored.value("plan").toObject();
            legacy.remove("burnCredits");
            stored["plan"] = legacy;
            write(record, QJsonDocument(stored).toJson());
            PresentationJobStore reopened(credits, limits);
            check(reopened.jobs().at(id).plan.burnCredits,
                  "A job stored without the credit field burns its credits, as 1.5.0 did");
        }
        {
            // A symbolic link in place of the store directory is refused.
            const auto target = files.filePath("target");
            check(QDir().mkpath(target) && QFile::link(target, files.filePath("link")),
                  "Create store link fixture");
            rejects([&] { PresentationJobStore linked(files.filePath("link"), limits); },
                    "A linked store directory is refused");
        }
        std::cout << "Presentation job store create, list, retention, corrupt-record and "
                     "changed-capture refusals passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
