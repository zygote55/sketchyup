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
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
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
        std::cerr << "File worker fixture: missing-file check finished\n";
        auto *save = window.findChild<QAction *>("file.save");
        auto *newFile = window.findChild<QAction *>("file.new");
        check(save && newFile, "Native file actions exist");
        window.document().move(1, {.5, 0, 0});
        const auto captured = encodeContainer(window.document());
        bool savePulse = false;
        QTimer::singleShot(0, &window, [&] {
            savePulse = window.findChild<QDialog *>("fileOperationDialog") != nullptr;
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
        newFile->trigger();
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
            {"timings", timings}};
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
