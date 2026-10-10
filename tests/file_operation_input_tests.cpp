#include "app/file_operation.hpp"
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"
#include "core/assets.hpp"
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sys/mman.h>
#include <unistd.h>
using namespace sketchy;
namespace {
// Set by the modal guard below when a dialog nobody asked for appears. Every later
// check() then fails with the dialog's text instead of an unrelated symptom.
std::string unexpectedModal;
void check(bool value, const char *message) {
    if (!unexpectedModal.empty())
        throw std::runtime_error(unexpectedModal);
    if (!value)
        throw std::runtime_error(message);
}
// Fails fast and informatively when a modal dialog the test did not drive appears,
// e.g. the "Save failed" warning, which would otherwise block QDialog::exec() until an
// external timeout kills the process with no output. The progress popup is always
// expected; the unsaved-changes prompt only while expectUnsavedPrompt is set.
class UnexpectedModalGuard {
  public:
    bool expectUnsavedPrompt = false;
    UnexpectedModalGuard() {
        timer_.setInterval(50);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] { poll(); });
        timer_.start();
    }

  private:
    void poll() {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal || !unexpectedModal.empty() || modal->objectName() == "fileOperationDialog")
            return;
        auto *box = qobject_cast<QMessageBox *>(modal);
        if (expectUnsavedPrompt && box && box->windowTitle() == "Unsaved changes")
            return;
        QStringList text;
        if (box) {
            text << box->text() << box->informativeText();
        } else {
            for (auto *label : modal->findChildren<QLabel *>())
                text << label->text();
        }
        text.removeAll(QString());
        unexpectedModal = QString("Unexpected modal %1 titled \"%2\": %3")
                              .arg(modal->metaObject()->className(), modal->windowTitle(),
                                   text.join(" | "))
                              .toStdString();
        std::cerr << unexpectedModal << '\n';
        if (auto *dialog = qobject_cast<QDialog *>(modal))
            dialog->reject(); // Unwind the nested event loop so the test can fail.
        else
            modal->close();
    }
    QTimer timer_;
};
// Test-only fault injection (SKETCHYUP_TEST_INJECT_SAVE_FAILURE=1): make the save
// directory read-only so publication fails and the app raises its "Save failed" dialog.
struct ReadOnlyDirectory {
    explicit ReadOnlyDirectory(const QString &directory) : path_(directory.toStdString()) {
        namespace fs = std::filesystem;
        fs::permissions(path_, fs::perms::owner_write | fs::perms::group_write |
                                   fs::perms::others_write,
                        fs::perm_options::remove);
    }
    ~ReadOnlyDirectory() {
        namespace fs = std::filesystem;
        std::error_code ignored;
        fs::permissions(path_, fs::perms::owner_write, fs::perm_options::add, ignored);
    }
    std::string path_;
};
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read native fixture");
    return file.readAll();
}
// The cold policy operates only on this test's private copy. mincore establishes
// OS page-cache residency; it says nothing about storage-device/controller caches.
QJsonObject prepareCache(const QString &path, qsizetype size) {
    const auto policy = qEnvironmentVariable("SKETCHYUP_TEST_FILE_CACHE_POLICY", "warm");
    check(policy == "warm" || policy == "verified-cold", "Unknown fixture cache policy");
    QJsonObject result{{"policy", policy}};
    if (policy == "warm")
        return result; // read(path) immediately above populated the cache.
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    check(fd >= 0, "Open owned cache fixture");
    const bool evicted = ::fsync(fd) == 0 && ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    const auto pageSize = ::sysconf(_SC_PAGESIZE);
    void *mapping =
        evicted && pageSize > 0 ? ::mmap(nullptr, size, PROT_NONE, MAP_PRIVATE, fd, 0) : MAP_FAILED;
    ::close(fd);
    check(evicted && pageSize > 0 && mapping != MAP_FAILED, "Advise owned fixture cache eviction");
    std::vector<unsigned char> residency((size + pageSize - 1) / pageSize);
    const bool queried = ::mincore(mapping, size, residency.data()) == 0;
    ::munmap(mapping, size);
    check(queried, "Inspect fixture page-cache residency");
    const auto resident = std::count_if(residency.begin(), residency.end(),
                                        [](unsigned char value) { return value & 1; });
    result["pages"] = qint64(residency.size());
    result["residentPagesBeforeOpen"] = qint64(resident);
    check(resident == 0, "Cold sample requires all owned fixture pages absent from OS cache");
    return result;
}
template <class Operation>
QJsonObject measureOperation(const char *name, Window &window, Operation operation,
                             bool includeFirstFrame = false) {
    QElapsedTimer clock;
    QTimer heartbeat;
    heartbeat.setTimerType(Qt::PreciseTimer);
    heartbeat.setInterval(5);
    std::vector<double> gaps;
    qint64 last = 0;
    int callbacks = 0;
    QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] {
        const auto now = clock.nsecsElapsed();
        gaps.push_back(double(now - last) / 1e6);
        last = now;
        ++callbacks;
    });
    clock.start();
    heartbeat.start();
    std::cerr << "File worker fixture: " << name << " started\n";
    operation();
    const double operationMs = double(clock.nsecsElapsed()) / 1e6;
    if (includeFirstFrame) {
        check(!window.viewport()->grabFramebuffer().isNull(), "Read first loaded viewport frame");
        check(window.viewport()->renderStats().glError == 0, "Loaded viewport has no GL error");
    }
    const auto finished = clock.nsecsElapsed();
    heartbeat.stop();
    std::cerr << "File worker fixture: " << name << " finished in " << double(finished) / 1e6
              << " ms\n";
    // Include synchronous work after the last callback, even if no timer ran.
    gaps.push_back(double(finished - last) / 1e6);
    std::sort(gaps.begin(), gaps.end());
    return {{"operation", name},
            {"operationMs", operationMs},
            {"operationAndFirstFrameMs", double(finished) / 1e6},
            {"includesFirstFrameReadback", includeFirstFrame},
            {"heartbeatIntervalMs", 5},
            {"heartbeatCallbacks", callbacks},
            {"maximumHeartbeatGapMs", gaps.back()},
            {"p95HeartbeatGapMs", gaps[(gaps.size() * 95 - 1) / 100]}};
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", 0);
    UnexpectedModalGuard modalGuard;
    std::optional<ReadOnlyDirectory> readOnlySaveDirectory;
    try {
        check(argc == 1 || argc == 2, "Optional argument is the large native fixture path");
        const auto path = files.filePath("Model.sketchyup");
        if (argc == 2) {
            check(QFile::copy(QString::fromLocal8Bit(argv[1]), path), "Copy large native fixture");
        } else {
            Document original;
            original.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
            createAsset(original, "Local data", "application/octet-stream",
                        std::make_shared<const AssetPayload>(
                            std::vector<std::uint8_t>(2 * 1024 * 1024, 42)));
            saveDocument(original, path);
        }
        const auto originalBytes = read(path);
        std::cerr << "File worker fixture: creating window\n";
        Window window;
        window.resize(1280, 850);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "File-operation window exposed");
        const auto prior = window.document().identity();
        bool loadPulse = false, reentrantOpenRejected = false, closeRejected = false;
        QTimer::singleShot(0, &window, [&] {
            loadPulse = window.findChild<QDialog *>("fileOperationDialog") != nullptr;
            check(window.document().identity() == prior, "Old document remains active during load");
            try {
                window.openPath(path);
            } catch (const std::exception &) {
                reentrantOpenRejected = true;
            }
            window.close();
            closeRejected = window.isVisible();
        });
        const auto cache = prepareCache(path, originalBytes.size());
        QJsonArray timings;
        timings.append(measureOperation("open", window, [&] { window.openPath(path); }, true));
        check(loadPulse && reentrantOpenRejected && closeRejected,
              "Load delivers events and fences reentrant open/close");
        check(encodeContainer(window.document()) == originalBytes && read(path) == originalBytes,
              "Open validates exact native bytes without changing source");
        auto *progress = window.findChild<QDialog *>("fileOperationDialog");
        check(progress && !progress->isVisible() &&
                  !progress->property("fileOperationActive").toBool(),
              "Completed operation retains a hidden inactive progress window");
        check(progress->windowType() == Qt::ToolTip,
              "File progress uses a nonactivating tooltip surface");
        check(progress->testAttribute(Qt::WA_ShowWithoutActivating) &&
                  progress->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus),
              "File progress does not request keyboard activation");
        // Inspect an actually exposed popup before releasing this separate worker.
        // A fixed sleep can end before a busy compositor delivers the inspection.
        // This operation is outside every measured file sample.
        bool shownModal = false, cancellationRejected = false;
        bool qtProgressActive = false, nativeProgressFocused = false;
        auto releaseWorker = std::make_shared<std::promise<void>>();
        const auto workerReady = releaseWorker->get_future().share();
        QTimer inspectProgress;
        inspectProgress.setTimerType(Qt::PreciseTimer);
        inspectProgress.setInterval(5);
        QObject::connect(&inspectProgress, &QTimer::timeout, &window, [&] {
            if (!progress->isVisible() || !progress->windowHandle() ||
                !progress->windowHandle()->isExposed())
                return;
            inspectProgress.stop();
            shownModal = progress->isVisible() &&
                         progress->property("fileOperationActive").toBool() &&
                         progress->windowModality() == Qt::ApplicationModal &&
                         QApplication::activeModalWidget() == progress;
            qtProgressActive = QApplication::activeWindow() == progress;
            nativeProgressFocused = QGuiApplication::focusWindow() == progress->windowHandle();
            QTest::keyClick(progress, Qt::Key_Escape);
            progress->close();
            cancellationRejected =
                progress->isVisible() && progress->property("fileOperationActive").toBool();
            releaseWorker->set_value();
        });
        inspectProgress.start();
        runFileOperation(&window, "Progress lifecycle", [workerReady] {
            workerReady.wait_for(std::chrono::seconds(2)); // Bound a missing-popup fixture failure.
        });
        inspectProgress.stop();
        check(shownModal, "Shown file progress preserves application modality");
        // Activation hints are platform policy. Record both observations separately;
        // original responsiveness, modality, cancellation and completion checks remain.
        check(cancellationRejected, "Shown file progress rejects Escape and close");
        check(!progress->isVisible() && !progress->property("fileOperationActive").toBool(),
              "Shown file progress finishes hidden and inactive");
        std::cerr << "File worker fixture: missing-file check started\n";
        bool failurePulse = false, failed = false;
        QTimer::singleShot(0, &window, [&] { failurePulse = true; });
        try {
            window.openPath(files.filePath("missing.sketchyup"));
        } catch (const std::exception &) {
            failed = true;
        }
        check(failed && failurePulse && encodeContainer(window.document()) == originalBytes,
              "Failed asynchronous load preserves the active document");
        QTest::qWait(180);
        check(window.findChild<QDialog *>("fileOperationDialog") == progress &&
                  !progress->isVisible() && !progress->property("fileOperationActive").toBool(),
              "A failed short operation cannot leave a delayed progress popup");
        std::cerr << "File worker fixture: missing-file check finished\n";
        auto *save = window.findChild<QAction *>("file.save");
        auto *newFile = window.findChild<QAction *>("file.new");
        check(save && newFile, "Native file actions exist");
        if (qEnvironmentVariableIntValue("SKETCHYUP_TEST_INJECT_SAVE_FAILURE") == 1)
            readOnlySaveDirectory.emplace(files.path());
        window.document().move(1, {.5, 0, 0});
        const auto captured = encodeContainer(window.document());
        bool savePulse = false;
        QTimer::singleShot(0, &window, [&] {
            savePulse = window.findChild<QDialog *>("fileOperationDialog") == progress &&
                        progress->property("fileOperationActive").toBool();
            window.document().move(1, {.25, 0, 0});
            save->trigger(); // Reentrant save must not publish another snapshot.
        });
        timings.append(measureOperation("save-with-newer-edit", window, [&] { save->trigger(); }));
        check(savePulse && read(path) == captured && window.document().dirty() &&
                  encodeContainer(window.document()) != captured,
              "Save delivers events, writes the captured state and keeps later edits dirty");
        timings.append(measureOperation("save-current", window, [&] { save->trigger(); }));
        check(!window.document().dirty() && read(path) == encodeContainer(window.document()),
              "A subsequent save durably records the newer state");
        check(window.findChildren<QDialog *>("fileOperationDialog").size() == 1 &&
                  window.findChild<QDialog *>("fileOperationDialog") == progress &&
                  !progress->isVisible() && !progress->property("fileOperationActive").toBool(),
              "Repeated saves reuse progress and finish without leaving modal input blocked");
        window.document().move(1, {.125, 0, 0});
        const auto session = window.document().identity();
        bool choiceHandled = false, lateEdit = false;
        QTimer chooseSave;
        QObject::connect(&chooseSave, &QTimer::timeout, &window, [&] {
            if (auto *dialog = window.findChild<QMessageBox *>(); dialog && dialog->isVisible()) {
                chooseSave.stop();
                choiceHandled = true;
                QTimer::singleShot(0, &window, [&] {
                    lateEdit = true;
                    window.document().move(1, {.0625, 0, 0});
                });
                dialog->button(QMessageBox::Save)->click();
            }
        });
        chooseSave.start(10);
        std::cerr << "File worker fixture: save-before-replace started\n";
        modalGuard.expectUnsavedPrompt = true;
        newFile->trigger();
        modalGuard.expectUnsavedPrompt = false;
        std::cerr << "File worker fixture: save-before-replace finished\n";
        chooseSave.stop();
        check(choiceHandled && lateEdit && window.document().identity() == session &&
                  window.document().dirty(),
              "Save-before-replace never discards edits arriving during the worker save");
        std::cerr << "File worker fixture: final save started\n";
        save->trigger();
        std::cerr << "File worker fixture: final save finished\n";
        check(!window.document().dirty(), "Final save completes after replacement is declined");
        const QJsonObject metrics{
            {"passed", true},
            {"releaseAcceptance", false},
            {"fileBytes", qint64(originalBytes.size())},
            {"fixtureSha256",
             QString::fromLatin1(
                 QCryptographicHash::hash(originalBytes, QCryptographicHash::Sha256).toHex())},
            {"platform", QGuiApplication::platformName()},
            {"graphics", window.viewport()->graphicsDescription()},
            {"scale", window.devicePixelRatioF()},
            {"viewportLogicalWidth", window.viewport()->width()},
            {"viewportLogicalHeight", window.viewport()->height()},
            {"cache", cache},
            {"timings", timings},
                {"progressFocusObservation",
                 QJsonObject{{"sample", "separate-lifecycle-first-exposure"},
                             {"qtActiveWindowMatched", qtProgressActive},
                             {"nativeFocusWindowMatched", nativeProgressFocused},
                             {"applicationModalityPreserved", shownModal},
                             {"cancellationRejected", cancellationRejected}}}};
        std::cerr << "File worker fixture: close started\n";
        window.close();
        std::cerr << "File worker fixture: close finished\n";
        std::cout << QJsonDocument(metrics).toJson(QJsonDocument::Compact).constData() << '\n';
        std::cout << "Native file workers: responsive load/save, stale-save preservation, "
                     "reentrant action fences and failed-load retention passed; bytes="
                  << originalBytes.size() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
