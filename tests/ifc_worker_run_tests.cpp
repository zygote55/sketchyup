// R097.b: running the helper (ADR 0171 section 1.6 and checks I1 to I3): the cheap checks of the
// application itself, the resource limits, the deadline, cancel, and the restart that leaves the element at
// fault out. The helper is made to stop with its test faults (crash, abort, hang, alloc on a named product).
// Needs sketchyup-ifc-worker (IFC_WORKER_PATH) and python3; exits 77 (skipped) when the helper is not built.
#include "io/ifc_worker.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <set>
using namespace sketchy;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString fixture(const QString &name) { return QStringLiteral(SOURCE_DIR "/tests/fixtures/ifc/") + name; }
IfcWorkerOptions options(const QString &scratch) {
    IfcWorkerOptions worker;
    worker.workerPath = QStringLiteral(IFC_WORKER_PATH);
    worker.threads = 1;
    worker.deadlineSeconds = 60;
    worker.scratchRoot = scratch;
#ifdef IFC_NO_ADDRESS_LIMIT
    worker.addressSpaceBytes = 0;
#endif
    return worker;
}
// The STEP id of a named product, from the raw-text scan.
int stepOf(const QString &file, const QString &name) {
    QProcess process;
    process.start("python3", {"-I", QStringLiteral(SOURCE_DIR "/tests/ifc_text_scan.py"), fixture(file)});
    check(process.waitForFinished(60000) && process.exitCode() == 0, "raw-text scan");
    const auto objects = QJsonDocument::fromJson(process.readAllStandardOutput()).object()["objects"].toObject();
    for (auto it = objects.begin(); it != objects.end(); ++it)
        if (it.value().toObject()["name"] == name)
            return it.value().toObject()["step"].toInt();
    throw std::runtime_error("no product named " + name.toStdString());
}
template <class F> IfcImportError refusedBy(F operation) {
    try {
        operation();
    } catch (const IfcImportError &error) {
        return error;
    }
    throw std::runtime_error("must be refused");
}
std::string firstReason(const QJsonObject &report) {
    const auto excluded = report["excluded"].toArray();
    if (excluded.isEmpty())
        return "<none: " + QJsonDocument(report["engine"].toObject()).toJson(QJsonDocument::Compact).toStdString() + ">";
    return excluded.at(0).toObject()["reason"].toString().toStdString();
}
std::size_t bodiesNamed(const Document &document, const std::string &name) {
    std::size_t n = 0;
    for (const auto &[id, body] : document.bodies())
        n += body->name == name;
    return n;
}
std::size_t leftovers(const QString &scratch) { return QDir(scratch).entryList({"sketchyup-ifc-*"}, QDir::Dirs | QDir::NoDotAndDotDot).size(); }
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (!QFileInfo(QStringLiteral(IFC_WORKER_PATH)).isExecutable()) {
        std::cerr << "sketchyup-ifc-worker is not built: skipped\n";
        return 77;
    }
    try {
        QTemporaryDir scratch;
        check(scratch.isValid(), "scratch");
        const QString profiles = "extrusion-profiles.ifc";
        const int circle = stepOf(profiles, "circle");

        // ---- A plain import leaves nothing behind -----------------------------------------------
        {
            const auto imported = importIfcFile(fixture(profiles), {}, options(scratch.path()));
            check(bodiesNamed(imported.document, "circle") == 1, "the circle is imported");
            check(imported.report["engine"].toObject()["restarts"].toInt() == 0, "no restarts");
            check(leftovers(scratch.path()) == 0, "the private directory is removed");
        }

        // ---- I3: a crash on one element: the element is left out and the rest imported -----------------
        for (const char *kind : {"crash", "abort"}) {
            auto worker = options(scratch.path());
            worker.testFault = QString("%1:%2").arg(kind).arg(circle);
            const auto imported = importIfcFile(fixture(profiles), {}, worker);
            check(bodiesNamed(imported.document, "circle") == 0 && bodiesNamed(imported.document, "rectangle") == 1 &&
                      bodiesNamed(imported.document, "l-shape") == 1 && bodiesNamed(imported.document, "with-voids") == 1,
                  std::string(kind) + ": the element at fault is left out, the others are imported");
            const auto report = imported.report;
            check(report["engine"].toObject()["restarts"].toInt() == 1, std::string(kind) + ": one restart");
            const auto excluded = report["excluded"].toArray();
            check(excluded.size() == 1 && excluded[0].toObject()["step"].toInt() == circle && excluded[0].toObject()["reason"] == "crash",
                  std::string(kind) + ": the excluded element is named");
            bool located = false;
            for (const auto &entry : report["locations"].toArray())
                located = located || (entry.toObject()["kind"] == "excluded" && entry.toObject()["step"].toInt() == circle);
            check(located, std::string(kind) + ": and located");
            check(leftovers(scratch.path()) == 0, "nothing left behind after a crash");
        }
        // With four threads the element at fault is not known after the crash: one run on a single thread finds it.
        {
            auto worker = options(scratch.path());
            worker.threads = 4;
            worker.testFault = QString("crash:%1").arg(circle);
            const auto imported = importIfcFile(fixture(profiles), {}, worker);
            check(imported.report["engine"].toObject()["restarts"].toInt() == 2 && bodiesNamed(imported.document, "circle") == 0 &&
                      bodiesNamed(imported.document, "rectangle") == 1,
                  "four threads: a run on one thread finds the element, then it is left out");
        }
        // ---- walls that yield nothing before the wall that stops the library --------------------------------
        // A restart that left out "the next task" would blame the zero-depth wall and then the empty one and use up its
        // restarts; the helper tries the tasks one at a time and leaves out the wall at fault only.
        {
            const int culprit = stepOf("restart-silent.ifc", "X1");
            for (const int threads : {1, 4}) {
                auto worker = options(scratch.path());
                worker.threads = threads;
                const auto imported = importIfcFile(fixture("restart-silent.ifc"), {}, worker);
                const auto excluded = imported.report["excluded"].toArray();
                check(excluded.size() == 1 && excluded[0].toObject()["step"].toInt() == culprit,
                      "silent tasks: only X1 is left out (" + std::to_string(threads) + " thread(s)): " +
                          QJsonDocument(imported.report["excluded"].toArray()).toJson(QJsonDocument::Compact).toStdString());
                check(bodiesNamed(imported.document, "G1") == 1 && bodiesNamed(imported.document, "G2") == 1 && bodiesNamed(imported.document, "X1") == 0,
                      "silent tasks: the good walls before and after are imported");
                check(imported.report["engine"].toObject()["restarts"].toInt() == (threads == 1 ? 1 : 2), "silent tasks: restarts used");
            }
        }
        // ---- the deadline ------------------------------------------------------------------------------------
        {
            auto worker = options(scratch.path());
            worker.deadlineSeconds = 2;
            worker.testFault = QString("hang:%1").arg(circle);
            QElapsedTimer clock;
            clock.start();
            const auto imported = importIfcFile(fixture(profiles), {}, worker);
            check(bodiesNamed(imported.document, "circle") == 0 && bodiesNamed(imported.document, "rectangle") == 1, "a hang: the element is left out after the deadline");
            check(firstReason(imported.report) == "timeout", "and the reason is the timeout: " + firstReason(imported.report));
            check(clock.elapsed() < 20000, "the deadline is enforced");
        }
        // ---- the address-space limit --------------------------------------------------------------------------
#ifndef IFC_NO_ADDRESS_LIMIT
        {
            auto worker = options(scratch.path());
            worker.addressSpaceBytes = qint64(2) << 30;
            worker.testFault = QString("alloc:%1").arg(circle);
            const auto imported = importIfcFile(fixture(profiles), {}, worker);
            check(bodiesNamed(imported.document, "circle") == 0 && bodiesNamed(imported.document, "rectangle") == 1, "memory: the element that exhausts the address space is left out");
            const auto reason = firstReason(imported.report);
            check(reason == "memory" || reason == "crash" || reason == "error", "memory: reported (" + reason + ")");
        }
#endif
        // ---- failure after the restarts: nothing is created ------------------------------------------------------
        {
            auto worker = options(scratch.path());
            worker.maxRestarts = 0;
            worker.testFault = QString("crash:%1").arg(circle);
            const auto error = refusedBy([&] { importIfcFile(fixture(profiles), {}, worker); });
            check(error.code() == "workerFailed", "no restarts allowed: the import fails");
            check(error.report()["engine"].toObject()["completed"] == false, "and the report says it did not complete");
            check(leftovers(scratch.path()) == 0, "and leaves nothing behind");
        }
        // ---- cancel ---------------------------------------------------------------------------------------------------
        {
            auto worker = options(scratch.path());
            worker.testFault = QString("hang:%1").arg(circle);
            QElapsedTimer clock;
            bool started = false;
            worker.progress = [&](const QString &phase, qint64) {
                if (phase == "geometry" && !started) {
                    started = true;
                    clock.start();
                }
            };
            QElapsedTimer sinceCancel;
            bool asked = false;
            worker.cancelled = [&] {
                if (started && clock.elapsed() > 300 && !asked) {
                    asked = true;
                    sinceCancel.start();
                }
                return asked;
            };
            QElapsedTimer total;
            total.start();
            const auto error = refusedBy([&] { importIfcFile(fixture(profiles), {}, worker); });
            check(error.code() == "cancelled", "cancel ends the import");
            std::cerr << "cancel returned control after " << sinceCancel.elapsed() << " ms\n";
            check(sinceCancel.elapsed() < 1000, "and returns control (and ends the helper) within a second of the request");
            check(total.elapsed() < 5000, "and does not wait for the deadline");
            check(leftovers(scratch.path()) == 0, "and leaves nothing behind");
        }

        // ---- a helper that misbehaves: it is data, never trusted -------------------------------------------------
        {
            const auto script = [&](const QString &name, const QByteArray &body) {
                const auto path = QDir(scratch.path()).filePath(name);
                QFile file(path);
                check(file.open(QIODevice::WriteOnly) && file.write("#!/bin/sh\n" + body) > 0, "write a fake helper");
                file.close();
                QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
                auto worker = options(scratch.path());
                worker.workerPath = path;
                worker.deadlineSeconds = 2;
                return worker;
            };
            const auto outputOf = QByteArray("out=; while [ $# -gt 0 ]; do [ \"$1\" = --output ] && out=$2; shift; done\n");
            // A flood without a newline is stopped, not buffered.
            {
                auto worker = script("flood.sh", "echo 'P phase parse'; head -c 4000000 /dev/zero | tr '\\000' x; sleep 30\n");
                QElapsedTimer clock;
                clock.start();
                const auto error = refusedBy([&] { importIfcFile(fixture(profiles), {}, worker); });
                check(error.code() == "workerFailed" && clock.elapsed() < 10000, "a flood of output without a line end stops the helper");
            }
            // A helper that ignores SIGTERM is killed.
            {
                auto worker = script("stubborn.sh", "trap '' TERM; echo 'P phase geometry'; while :; do :; done\n");
                QElapsedTimer clock;
                clock.start();
                const auto error = refusedBy([&] { importIfcFile(fixture(profiles), {}, worker); });
                check(error.code() == "workerFailed" && clock.elapsed() < 12000, "a helper that ignores SIGTERM is killed after the deadline");
                check(leftovers(scratch.path()) == 0, "and nothing is left behind");
            }
            // A package that is not one, and a package that is missing.
            {
                auto worker = script("garbage.sh", outputOf + "echo garbage > \"$out\"; echo 'R {\"status\":\"ok\"}'\n");
                check(refusedBy([&] { importIfcFile(fixture(profiles), {}, worker); }).code() == "package", "a package that is garbage is refused by the decoder");
                worker = script("missing.sh", "echo 'R {\"status\":\"ok\"}'\n");
                check(refusedBy([&] { importIfcFile(fixture(profiles), {}, worker); }).code() == "package", "a missing package is an error, not a crash");
            }
            // Hostile words in a refusal.
            {
                auto worker = script("words.sh", "printf 'R {\"status\":\"refused\",\"code\":\"a b\\\\nc<script>\",\"message\":\"%s\"}\\n' \"$(head -c 30000 /dev/zero | tr '\\000' y)\"; exit 3\n");
                const auto error = refusedBy([&] { importIfcFile(fixture(profiles), {}, worker); });
                check(error.code() == "workerRefused", "an odd refusal code becomes workerRefused: " + error.code());
                check(std::string(error.what()).size() <= 240, "and its message is bounded");
            }
            check(leftovers(scratch.path()) == 0, "no private directory is left after the fake helpers");
        }

        // ---- the helper is missing -------------------------------------------------------------------------------------
        {
            auto worker = options(scratch.path());
            worker.workerPath = "/nonexistent/sketchyup-ifc-worker";
            check(refusedBy([&] { importIfcFile(fixture(profiles), {}, worker); }).code() == "workerMissing", "a missing helper is reported, nothing else is affected");
        }

        // ---- I1: the cheap checks ----------------------------------------------------------------------------------------
        QByteArray valid;
        {
            QFile file(fixture("units-mm.ifc"));
            check(file.open(QIODevice::ReadOnly), "read a fixture");
            valid = file.readAll();
        }
        const auto write = [&](const QString &name, const QByteArray &bytes) {
            const auto path = QDir(scratch.path()).filePath(name);
            QFile file(path);
            check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write " + name.toStdString());
            return path;
        };
        const auto code = [&](const QString &path) {
            return refusedBy([&] { importIfcFile(path, {}, options(scratch.path())); }).code();
        };
        {
            QElapsedTimer clock;
            clock.start();
            const auto big = QDir(scratch.path()).filePath("big.ifc");
            QFile file(big);
            check(file.open(QIODevice::WriteOnly) && file.resize(qint64(128) * 1024 * 1024 + 1), "a sparse 128 MiB + 1 byte file");
            file.close();
            check(code(big) == "tooLarge", "I1: a file over 128 MiB is refused");
            check(clock.elapsed() < 1000, "I1: and quickly");
            QFile::remove(big);
        }
        check(code(write("a.ifczip", QByteArray("PK\x03\x04") + valid)) == "unsupportedFormat", "an .ifczip is refused with its format named");
        check(code(write("a.ifcxml", "<?xml version=\"1.0\"?><ifc/>")) == "unsupportedFormat", "an ifcXML file is refused");
        check(code(write("a.json", "{\"data\":[]}")) == "unsupportedFormat", "an ifcJSON file is refused");
        check(code(write("a.txt", "hello")) == "notIfc", "text is not IFC");
        check(code(write("trunc.ifc", valid.left(valid.size() - 40))) == "truncated", "a file without END-ISO-10303-21; is refused");
        {
            auto other = valid;
            other.replace("FILE_SCHEMA(('IFC4'))", "FILE_SCHEMA(('IFC4X1'))");
            const auto error = refusedBy([&] { importIfcFile(write("x1.ifc", other), {}, options(scratch.path())); });
            check(error.code() == "unsupportedSchema" && std::string(error.what()).find("IFC4X1") != std::string::npos, "IFC4X1 is refused with the schema named");
            auto multi = valid;
            multi.replace("FILE_SCHEMA(('IFC4'))", "FILE_SCHEMA(('IFC4','IFC2X3'))");
            check(code(write("multi.ifc", multi)) == "unsupportedSchema", "several schemas are refused");
            auto none = valid;
            none.replace("FILE_SCHEMA(('IFC4'))", "FILE_NAME_X(())");
            check(code(write("none.ifc", none)) == "noSchema", "a file without FILE_SCHEMA is refused");
        }
        {
            auto worker = options(scratch.path());
            worker.maxEntities = 5;
            const auto error = refusedBy([&] { importIfcFile(fixture(profiles), {}, worker); });
            check(error.code() == "entityLimit" && std::string(error.what()).find("limit is 5") != std::string::npos, "I1: too many instances: refused with the count and the limit named");
        }
        check(code(QDir(scratch.path()).absolutePath()) == "notAFile", "a directory is not a file");
        check(leftovers(scratch.path()) == 0, "no private directory is left after the refusals");
        std::cout << "ifc_worker_run ok\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
