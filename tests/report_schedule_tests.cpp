// R101.c (ADR 0175 §7, §8, §11 V11): saved report schedules, in process. The store (its bounds, the
// corrupt-file refusal, the atomic write, the changed-file refusal), save, describe and delete with
// the identity guard and the stale-ID refusal, the bit-identical document save, the run path, and
// the session's report.schedule. Every test keeps its store in a temporary user data folder: HOME
// and XDG_DATA_HOME point into it before any location is read, so no test touches the real user's
// data.
//
// R101.e1 adds the atomic rename and update of a schedule (gaps 2 and 3), the precision pinned at
// save (owner choice 4), and the stale schedule fixed by an update.
#include "automation/report_commands.hpp"
#include "automation/report_schedules.hpp"
#include "automation/session.hpp"
#include "core/units.hpp"
#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include "report_fixtures.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace sketchy;
using namespace sketchy::report_fixtures;

namespace {

void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

// The refusal an action makes: its code, its message and its details. Empty when it succeeds.
struct Refusal {
    std::string code, message;
    QJsonObject details;
};

template <class F> Refusal refusedBy(F action) {
    try {
        action();
    } catch (const InspectionError &error) {
        return {error.code(), error.what(), error.details()};
    }
    return {};
}

bool refusesWith(const Refusal &refusal, const std::string &code, const std::string &text = {}) {
    return refusal.code == code && refusal.message.find(text) != std::string::npos;
}

// A JSON string field equals the text (QJsonValue has no const char* comparison).
bool is(const QJsonValue &value, const char *text) {
    return value.toString() == QString::fromUtf8(text);
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Cannot read " + path.toStdString());
    return file.readAll();
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Cannot write " + path.toStdString());
}

// Sandboxes the process: the store lives under `home`, never under the real user's folders.
void isolate(const QString &home) {
    qputenv("HOME", home.toUtf8());
    qputenv("XDG_DATA_HOME", (home + "/data").toUtf8());
    QCoreApplication::setOrganizationName("SketchyUp");
    QCoreApplication::setApplicationName("SketchyUp");
}

// A request in the §1 vocabulary: quantities by kind, as the CLI and the session write it.
QJsonObject kindRequest() {
    return {{"metrics", QJsonArray{"counts"}},
            {"groupBy", "kind"},
            {"filters", QJsonObject{{"visibility", "all"}}}};
}

// The request with its precision set, as a save stores it (R101.e1): "full" or an integer.
QJsonObject withPrecision(QJsonObject request, const QJsonValue &precision) {
    request["precision"] = precision;
    return request;
}

QJsonObject tagRequest(const QString &tag) {
    return {{"metrics", QJsonArray{"counts", "volumes"}},
            {"groupBy", "tag"},
            {"filters", QJsonObject{{"visibility", "all"}, {"tags", QJsonArray{tag}}}}};
}

QString identity(const Document &doc) { return QString::fromStdString(doc.identity()); }

// A schedule record as the store reads it (the store checks the name and the timestamp itself).
ReportSchedule scheduleNamed(const QString &name, const QJsonObject &request) {
    return {name, "2026-10-26T12:00:00Z", request};
}

// V11 bounds: the store refuses each with a named reason, and the file it had is left unchanged.
void storeBounds(const QTemporaryDir &dir) {
    const auto path = dir.filePath("bounds/report-schedules.json");
    ReportScheduleStore store(path);
    const std::string doc = "0123456789abcdef0123456789abcdef";

    auto refusal =
        refusedBy([&] { store.add(doc, scheduleNamed(QString(65, 'a'), kindRequest())); });
    check(refusesWith(refusal, "INVALID_REQUEST", "1 to 64 characters"),
          "A name of 65 characters is refused by its bound");
    refusal = refusedBy([&] { store.add(doc, scheduleNamed("a,b", kindRequest())); });
    check(refusesWith(refusal, "INVALID_REQUEST", "starts and ends"),
          "A name with a comma is refused, because it would break the CSV metadata line");
    refusal = refusedBy([&] { store.add(doc, scheduleNamed(" leading", kindRequest())); });
    check(refusesWith(refusal, "INVALID_REQUEST", "starts and ends"),
          "A name that starts with a space is refused");

    for (int i = 0; i < 32; ++i)
        store.add(doc, scheduleNamed("Schedule " + QString::number(i), kindRequest()));
    refusal = refusedBy([&] { store.add(doc, scheduleNamed("Thirty three", kindRequest())); });
    check(refusesWith(refusal, "LIMIT_EXCEEDED", "at most 32"),
          "A document refuses a thirty-third schedule by name");

    QJsonObject large{{"metrics", "all"}, {"filters", QJsonObject{}}};
    large["note"] = QString(17 * 1024, 'x');
    refusal = refusedBy([&] { store.add(doc, scheduleNamed("Large", large)); });
    check(refusesWith(refusal, "LIMIT_EXCEEDED", "16 KiB"), "A request over 16 KiB is refused");

    // Sixty-four documents fill the store; a sixty-fifth is refused, naming the bound.
    const auto manyPath = dir.filePath("many/report-schedules.json");
    ReportScheduleStore many(manyPath);
    for (int i = 0; i < 64; ++i)
        many.add(QStringLiteral("doc%1").arg(i, 4, 10, QLatin1Char('0')).toStdString(),
                 scheduleNamed("One", kindRequest()));
    refusal = refusedBy([&] { many.add("doc9999", scheduleNamed("One", kindRequest())); });
    check(refusesWith(refusal, "LIMIT_EXCEEDED", "64 documents"),
          "A sixty-fifth document is refused by its bound");

    // A store over 1 MiB is refused on open, and never read further.
    const auto hugePath = dir.filePath("huge/report-schedules.json");
    QDir().mkpath(QFileInfo(hugePath).absolutePath());
    writeFile(hugePath, QByteArray(1024 * 1024 + 1, ' '));
    refusal = refusedBy([&] { ReportScheduleStore huge(hugePath); });
    check(refusesWith(refusal, "INPUT_ERROR", "exceeds 1 MiB"),
          "A store over 1 MiB is refused on open");
}

// The round trip: what a store writes, a fresh store reads back, in name order, with the request.
void storeRoundTrip(const QTemporaryDir &dir) {
    const auto path = dir.filePath("round/report-schedules.json");
    const std::string doc = "0123456789abcdef0123456789abcdef";
    {
        ReportScheduleStore store(path);
        check(store.schedules(doc).empty(), "A missing store is an empty store");
        store.add(doc, scheduleNamed("Tags", tagRequest("5")));
        store.add(doc, scheduleNamed("Kinds", kindRequest()));
    }
    const ReportScheduleStore reopened(path);
    const auto list = reopened.schedules(doc);
    check(list.size() == 2 && list[0].name == "Kinds" && list[1].name == "Tags",
          "The schedules come back in name order");
    check(list[1].createdAt == "2026-10-26T12:00:00Z" && list[1].request == tagRequest("5"),
          "A schedule comes back with its timestamp and request");
    check(reopened.lookup(doc, "Kinds").request == kindRequest(),
          "lookup returns the saved request");
#if !defined(_WIN32)
    const auto permissions = QFileInfo(path).permissions();
    check(permissions.testFlag(QFileDevice::ReadOwner) &&
              permissions.testFlag(QFileDevice::WriteOwner) &&
              !permissions.testFlag(QFileDevice::ReadGroup) &&
              !permissions.testFlag(QFileDevice::ReadOther) &&
              !permissions.testFlag(QFileDevice::WriteGroup) &&
              !permissions.testFlag(QFileDevice::WriteOther),
          "The store is readable and writable by its owner only");
#endif
    {
        ReportScheduleStore store(path);
        store.remove(doc, "Tags");
    }
    const auto after = ReportScheduleStore(path);
    check(after.schedules(doc).size() == 1, "remove drops one schedule");
    check(readFile(path).contains("\"documents\":[{") && !readFile(path).contains("Tags"),
          "The file holds the remaining schedule only");
    {
        ReportScheduleStore store(path);
        store.remove(doc, "Kinds");
    }
    check(readFile(path).contains("\"documents\":[]"),
          "A document with no schedules leaves the store");
}

// The text of a store file, built from its parts: documents, their schedules and each entry.
QByteArray scheduleEntry(const char *name, const char *createdAt = "2026-10-26T12:00:00Z") {
    return QByteArray(R"({"name":")") + name + R"(","createdAt":")" + createdAt +
           R"(","request":{}})";
}
QByteArray documentEntry(const std::string &id, const QByteArray &schedules) {
    return QByteArray(R"({"documentId":")") + id.c_str() + R"(","schedules":[)" + schedules + "]}";
}
QByteArray storeWith(const QByteArray &documents) {
    return R"({"storeVersion":1,"documents":[)" + documents + "]}";
}

// A corrupt or oversized store is refused with a located error, and never reset or rewritten.
void corruptStoreRefused(const QTemporaryDir &dir) {
    const std::string doc = "0123456789abcdef0123456789abcdef";
    const struct Case {
        const char *name;
        QByteArray bytes;
        const char *text;
    } cases[] = {
        {"garbage", "{\"storeVersion\":1,\"documents\":[", "not valid JSON"},
        {"version", R"({"storeVersion":2,"documents":[]})", "unsupported layout"},
        {"not an object", "[]", "not a JSON object"},
        {"duplicate document",
         storeWith(documentEntry(doc, scheduleEntry("A")) + "," +
                   documentEntry(doc, scheduleEntry("B"))),
         "repeats a document identity"},
        {"duplicate name",
         storeWith(documentEntry(doc, scheduleEntry("A") + "," + scheduleEntry("A"))),
         "repeats a schedule name"},
        {"bad timestamp", storeWith(documentEntry(doc, scheduleEntry("A", "yesterday"))),
         "invalid creation time"},
        {"extra field", R"({"storeVersion":1,"documents":[],"extra":1})", "unsupported layout"},
    };
    int index = 0;
    for (const auto &item : cases) {
        const auto path =
            dir.filePath("corrupt" + QString::number(index++) + "/report-schedules.json");
        QDir().mkpath(QFileInfo(path).absolutePath());
        writeFile(path, item.bytes);
        const auto refusal = refusedBy([&] { ReportScheduleStore store(path); });
        check(refusesWith(refusal, "INPUT_ERROR", item.text),
              std::string("A corrupt store is refused (") + item.name + "): " + refusal.message);
        check(refusal.message.find(path.toStdString()) != std::string::npos,
              "The refusal names the store's path");
        check(readFile(path) == item.bytes, "A refused store is left as it was");
    }

    // A valid file opens.
    const auto good = dir.filePath("good/report-schedules.json");
    QDir().mkpath(QFileInfo(good).absolutePath());
    writeFile(good, storeWith(documentEntry(doc, scheduleEntry("Kinds"))));
    check(ReportScheduleStore(good).schedules(doc).size() == 1, "A valid store opens");

    // A folder where the store should be is refused, not replaced.
    const auto folder = dir.filePath("folder/report-schedules.json");
    QDir().mkpath(folder);
    const auto refusal = refusedBy([&] { ReportScheduleStore store(folder); });
    check(refusesWith(refusal, "INPUT_ERROR", "regular file"), "A folder is refused as the store");
}

// The atomic write: a commit leaves exactly the store file behind, and a store folder that cannot
// be written leaves the earlier contents in place.
void atomicWrite(const QTemporaryDir &dir) {
    const auto folder = dir.filePath("atomic");
    const auto path = folder + "/report-schedules.json";
    const std::string doc = "0123456789abcdef0123456789abcdef";
    ReportScheduleStore store(path);
    store.add(doc, scheduleNamed("Kinds", kindRequest()));
    const auto before = readFile(path);
    const auto names = QDir(folder).entryList(QDir::Files | QDir::Hidden);
    check(names == QStringList{"report-schedules.json"},
          "A commit leaves only the store file behind (no temporary file, no lock file)");

    QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
    const bool readOnly = !QFileInfo(folder).isWritable();
    const auto refusal = refusedBy([&] { store.add(doc, scheduleNamed("Tags", tagRequest("5"))); });
    QFile::setPermissions(folder,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    if (readOnly) {
        check(!refusal.code.empty(), "A failed write is refused");
        check(readFile(path) == before, "A failed write leaves the previous contents");
    } else {
        // Running with rights to write a read-only folder (root): the write itself succeeds.
        std::cout << "  (write-failure check skipped: this process can write a read-only folder)\n";
        check(refusal.code.empty(), "A write in a writable folder succeeds");
    }
}

// A file that changed after this store read it is refused; the other writer's change stays.
void changedFileRefused(const QTemporaryDir &dir) {
    const auto path = dir.filePath("changed/report-schedules.json");
    const std::string doc = "0123456789abcdef0123456789abcdef";
    ReportScheduleStore first(path);
    {
        ReportScheduleStore second(path);
        second.add(doc, scheduleNamed("Kinds", kindRequest()));
    }
    const auto theirs = readFile(path);
    const auto refusal = refusedBy([&] { first.add(doc, scheduleNamed("Tags", tagRequest("5"))); });
    check(refusesWith(refusal, "FILE_CHANGED"), "A store changed on disk is refused, not merged");
    check(readFile(path) == theirs, "The other writer's store is left as it was");
}

// R101.e1 (gap 2, ADR 0175 §7): rename keeps the request and the creation time; each refusal leaves
// the store as it was, and the names are checked as a save checks them.
void renameKeepsRequest(const QTemporaryDir &dir) {
    const auto path = dir.filePath("rename/report-schedules.json");
    const std::string doc = "0123456789abcdef0123456789abcdef";
    ReportScheduleStore store(path);
    store.add(doc, scheduleNamed("Old", tagRequest("5")));
    store.add(doc, scheduleNamed("Other", kindRequest()));
    store.rename(doc, "Old", "New");
    const ReportScheduleStore reopened(path);
    const auto moved = reopened.lookup(doc, "New");
    check(moved.createdAt == "2026-10-26T12:00:00Z" && moved.request == tagRequest("5"),
          "A rename keeps the request and the creation time, and the file holds the new name");
    auto refusal = refusedBy([&] { reopened.lookup(doc, "Old"); });
    check(refusesWith(refusal, "NOT_FOUND"), "The old name is gone after a rename");

    refusal = refusedBy([&] { store.rename(doc, "New", "Other"); });
    check(refusesWith(refusal, "INVALID_REQUEST", "already exists") &&
              refusal.message.find("delete it first") == std::string::npos,
          "A rename onto a name in use is refused, and not with the delete-first text");
    refusal = refusedBy([&] { store.rename(doc, "New", "New"); });
    check(refusesWith(refusal, "INVALID_REQUEST", "already exists"),
          "A rename to the same name is refused as a name in use");
    refusal = refusedBy([&] { store.rename(doc, "New", "a,b"); });
    check(refusesWith(refusal, "INVALID_REQUEST", "starts and ends"),
          "A rename to an invalid name is refused by the name rules");
    refusal = refusedBy([&] { store.rename(doc, "Missing", "Fine"); });
    check(refusesWith(refusal, "NOT_FOUND"), "A rename of a name saved nowhere is not found");
    refusal = refusedBy([&] { store.rename("fedcba9876543210fedcba9876543210", "New", "Fine"); });
    check(refusesWith(refusal, "WRONG_DOCUMENT"),
          "A rename of another document's schedule is refused");
    const ReportScheduleStore after(path);
    check(after.schedules(doc).size() == 2 && after.lookup(doc, "New").request == tagRequest("5") &&
              after.lookup(doc, "Other").request == kindRequest(),
          "Every refused rename leaves both schedules as they were");
}

// R101.e1 (gap 2): a rename that cannot be written loses neither the name nor the request. The
// rename is one commit, so each failure is injected at the commit and checked against the file.
void renameIsAtomic(const QTemporaryDir &dir) {
    const auto path = dir.filePath("atomic-rename/report-schedules.json");
    const std::string doc = "0123456789abcdef0123456789abcdef";
    ReportScheduleStore first(path);
    first.add(doc, scheduleNamed("Old", kindRequest()));

    // Another process changes the store after this one read it: the rename is refused, the other
    // writer's store is kept, and this process's schedule is not renamed or lost.
    {
        ReportScheduleStore other(path);
        other.add(doc, scheduleNamed("Other", tagRequest("5")));
    }
    const auto theirs = readFile(path);
    auto refusal = refusedBy([&] { first.rename(doc, "Old", "New"); });
    check(refusesWith(refusal, "FILE_CHANGED"), "A rename over a changed store is refused");
    check(readFile(path) == theirs, "The other writer's store is left as it was");
    const ReportScheduleStore reopened(path);
    check(reopened.lookup(doc, "Old").request == kindRequest() &&
              reopened.lookup(doc, "Other").request == tagRequest("5"),
          "After the refused rename, the old name and its request are still there");

    // A store folder that cannot be written refuses the commit. Skipped where this process can
    // write a read-only folder (root).
    const auto folder = dir.filePath("atomic-locked");
    const auto locked = folder + "/report-schedules.json";
    ReportScheduleStore lockedStore(locked);
    lockedStore.add(doc, scheduleNamed("Old", kindRequest()));
    const auto before = readFile(locked);
    QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
    const bool readOnly = !QFileInfo(folder).isWritable();
    refusal = refusedBy([&] { lockedStore.rename(doc, "Old", "New"); });
    QFile::setPermissions(folder,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    if (readOnly) {
        check(!refusal.code.empty(), "A rename into a read-only folder is refused");
        check(readFile(locked) == before, "A refused rename leaves the previous contents");
        const ReportScheduleStore unchanged(locked);
        check(unchanged.lookup(doc, "Old").request == kindRequest(),
              "The name and the request survive a refused rename");
    } else {
        std::cout << "  (rename write-failure check skipped: this process can write a read-only "
                     "folder)\n";
    }
}

// R101.e1 (gap 3, owner choice 4): a schedule saved without a precision pins the document's
// precision at save; a request that names one keeps it; a later display change does not move a
// saved schedule (a run of it reports the pinned precision), and an update pins again.
void pinnedPrecision(const QTemporaryDir &dir) {
    Document doc;
    buildQuantitiesA(doc);
    const auto store = dir.filePath("pin/report-schedules.json");
    saveReportSchedule(doc, store, "Full", kindRequest());
    check(describeReportSchedules(doc, store, "Full")["schedule"]
                  .toObject()["request"]
                  .toObject()["precision"] == "full",
          "A schedule saved at full precision pins \"full\"");

    doc.setDisplayUnits(DisplayUnit::Meters, 3);
    saveReportSchedule(doc, store, "Three", kindRequest());
    check(describeReportSchedules(doc, store, "Three")["schedule"]
                  .toObject()["request"]
                  .toObject()["precision"] == 3,
          "A schedule saved at precision 3 pins 3");
    saveReportSchedule(doc, store, "Explicit", withPrecision(kindRequest(), 2));
    check(describeReportSchedules(doc, store, "Explicit")["schedule"]
                  .toObject()["request"]
                  .toObject()["precision"] == 2,
          "A precision the request names is kept as it was given");
    doc.setDisplayUnits(DisplayUnit::Meters, 1);
    const auto after = describeReportSchedules(doc, store, "Three")["schedule"].toObject();
    check(after["request"].toObject()["precision"] == 3,
          "A later display change leaves the saved precision where it was");
    updateReportSchedule(doc, store, "Three", kindRequest());
    check(describeReportSchedules(doc, store, "Three")["schedule"]
                  .toObject()["request"]
                  .toObject()["precision"] == 1,
          "An update of the request pins the precision in force at the update");

#if defined(Q_OS_LINUX)
    // The run reads the saved precision, not the document's: an ad-hoc run follows the document.
    isolate(dir.path());
    Document run;
    buildQuantitiesA(run);
    run.setDisplayUnits(DisplayUnit::Meters, 3);
    saveReportSchedule(run, reportScheduleStorePath(), "Pinned", kindRequest());
    run.setDisplayUnits(DisplayUnit::Meters, 1);
    ReportFileOptions options;
    options.schedule = "Pinned";
    options.path = dir.filePath("pinned.csv");
    writeReportFile(run, options);
    check(readFile(options.path).contains("#precision=3\n"),
          "A scheduled run reports the precision it was saved with");
    options.schedule.clear();
    options.path = dir.filePath("adhoc.csv");
    writeReportFile(run, options);
    check(readFile(options.path).contains("#precision=1\n"),
          "An ad-hoc run reports the document's current precision");
#endif
}

// R101.e1 (gap 2, UX_DESIGN §19.7): a stale schedule is fixed by an update. The name and the time
// it was saved stay; the request is the new one, which must resolve in the document, and a request
// that still does not is refused with nothing changed.
void staleFix(const QTemporaryDir &dir) {
    Document doc;
    buildQuantitiesA(doc);
    const auto store = dir.filePath("stale-fix/report-schedules.json");
    const auto id = identity(doc);
    ReportScheduleStore(store).add(id.toStdString(), scheduleNamed("Walls", tagRequest("987654")));
    const auto created = ReportScheduleStore(store).lookup(id.toStdString(), "Walls").createdAt;
    const auto tag = QString::number(doc.tags().begin()->first);

    auto refusal =
        refusedBy([&] { updateReportSchedule(doc, store, "Walls", tagRequest("987654")); });
    check(refusesWith(refusal, "NOT_FOUND", "987654"),
          "An update to a request that still names a missing tag is refused, naming the ID");
    check(ReportScheduleStore(store).lookup(id.toStdString(), "Walls").request ==
              tagRequest("987654"),
          "A refused update leaves the stale schedule unchanged");

    const auto fixed = updateReportSchedule(doc, store, "Walls", tagRequest(tag));
    const auto schedule = fixed["schedule"].toObject();
    check(is(fixed["status"], "updated") && is(schedule["name"], "Walls") &&
              schedule["createdAt"].toString() == created &&
              schedule["request"].toObject()["filters"].toObject()["tags"].toArray() ==
                  QJsonArray{tag},
          "An update fixes a stale schedule: its name and creation time stay, its request is new");
    refusal = refusedBy([&] {
        requireResolvedScheduleIds(
            doc, ReportScheduleStore(store).lookup(id.toStdString(), "Walls").request);
    });
    check(refusal.code.empty(), "The fixed schedule resolves in the document");

    refusal = refusedBy([&] { updateReportSchedule(doc, store, "Missing", kindRequest()); });
    check(refusesWith(refusal, "NOT_FOUND"), "An update of a name saved nowhere is not found");
    Document other;
    buildQuantitiesA(other);
    refusal = refusedBy([&] { updateReportSchedule(other, store, "Walls", kindRequest()); });
    check(refusesWith(refusal, "WRONG_DOCUMENT", "another document"),
          "An update of another document's schedule is refused by the identity guard");
    QJsonObject selected = kindRequest();
    selected["filters"] = QJsonObject{{"selection", true}};
    refusal = refusedBy([&] { updateReportSchedule(doc, store, "Walls", selected); });
    check(refusesWith(refusal, "INVALID_REQUEST", "session selection"),
          "An update cannot use the session selection");
}

// The identity guard (V11): a schedule saved for one document is never run or changed on another.
void identityGuard(const QTemporaryDir &dir) {
    const auto path = dir.filePath("guard/report-schedules.json");
    const std::string docA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const std::string docB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    ReportScheduleStore store(path);
    store.add(docA, scheduleNamed("Tags", tagRequest("5")));
    auto refusal = refusedBy([&] { store.lookup(docB, "Tags"); });
    check(refusesWith(refusal, "WRONG_DOCUMENT", "another document"),
          "A schedule saved for another document is refused with a located error");
    check(refusal.details["savedFor"].toString() == QString::fromStdString(docA) &&
              refusal.details["documentId"].toString() == QString::fromStdString(docB),
          "The refusal names both document identities");
    refusal = refusedBy([&] { store.lookup(docB, "Nope"); });
    check(refusesWith(refusal, "NOT_FOUND"), "A name saved nowhere is not found");
    refusal = refusedBy([&] { store.remove(docB, "Tags"); });
    check(refusesWith(refusal, "WRONG_DOCUMENT"), "Another document's schedule cannot be deleted");
    check(store.schedules(docA).size() == 1, "The refused delete leaves the schedule in place");
}

// Save, describe and delete on a real document, with each refusal the ADR names.
void saveDescribeDelete(const QTemporaryDir &dir) {
    Document doc;
    buildQuantitiesA(doc);
    const auto store = dir.filePath("sdd/report-schedules.json");
    const auto saved = saveReportSchedule(doc, store, "Kinds", kindRequest());
    check(is(saved["status"], "saved") && saved["documentId"] == identity(doc) &&
              saved["schedule"].toObject()["createdAt"].toString().endsWith('Z'),
          "A save reports the document and a UTC creation time");

    auto listed = describeReportSchedules(doc, store);
    check(listed["schedules"].toArray().size() == 1,
          "describe without a name lists the document's");
    check(describeReportSchedules(doc, store, "Kinds")["schedule"].toObject()["request"] ==
              withPrecision(kindRequest(), "full"),
          "describe with a name returns that schedule's request, with the precision it pinned");
    auto refusal = refusedBy([&] { describeReportSchedules(doc, store, "Nope"); });
    check(refusesWith(refusal, "NOT_FOUND"), "describe refuses a missing name");

    refusal = refusedBy([&] { saveReportSchedule(doc, store, "Kinds", kindRequest()); });
    check(refusesWith(refusal, "INVALID_REQUEST", "already exists") &&
              refusal.message.find("delete it first") == std::string::npos &&
              refusal.message.find("Choose another name") != std::string::npos,
          "Names are unique per document, and the refusal says to choose another name");

    QJsonObject withSelection = kindRequest();
    withSelection["filters"] = QJsonObject{{"selection", true}};
    refusal = refusedBy([&] { saveReportSchedule(doc, store, "Picked", withSelection); });
    check(refusesWith(refusal, "INVALID_REQUEST", "session selection"),
          "A schedule cannot use the session selection");

    QJsonObject unknown = kindRequest();
    unknown["surprise"] = 1;
    refusal = refusedBy([&] { saveReportSchedule(doc, store, "Odd", unknown); });
    check(refusesWith(refusal, "INVALID_REQUEST"), "An unknown request field is refused");

    // Stale IDs: a tag that is not in the document is refused at save, and named.
    refusal = refusedBy([&] { saveReportSchedule(doc, store, "Stale", tagRequest("987654")); });
    check(refusesWith(refusal, "NOT_FOUND", "987654") &&
              refusal.details["missing"].toObject()["tags"].toArray() == QJsonArray{"987654"},
          "A save with a missing tag ID is refused, naming the ID");
    check(describeReportSchedules(doc, store)["schedules"].toArray().size() == 1,
          "A refused save leaves the store unchanged");

    // A real tag of the document saves.
    const auto tag = QString::number(doc.tags().begin()->first);
    saveReportSchedule(doc, store, "Tagged", tagRequest(tag));

    const auto removed = deleteReportSchedule(doc, store, "Tagged");
    check(is(removed["status"], "deleted"), "delete reports the deletion");
    refusal = refusedBy([&] { deleteReportSchedule(doc, store, "Tagged"); });
    check(refusesWith(refusal, "NOT_FOUND"), "A deleted schedule is not found again");
}

// Names and identities are checked before anything is written.
void namesAndIdentity(const QTemporaryDir &dir) {
    Document doc;
    buildQuantitiesA(doc);
    const auto store = dir.filePath("names/report-schedules.json");
    auto refusal = refusedBy([&] { saveReportSchedule(doc, store, "", kindRequest()); });
    check(refusesWith(refusal, "INVALID_REQUEST"), "An empty name is refused");
    refusal = refusedBy([&] { deleteReportSchedule(doc, store, "bad\nname"); });
    check(refusesWith(refusal, "INVALID_REQUEST"), "A name with a line break is refused");
    check(!QFileInfo::exists(store), "A refused name writes no store");
}

// The document save is bit-identical with or without a schedule present, and its schema is the one
// the content needs (V11).
void bitIdenticalSave(const QTemporaryDir &dir) {
    Document doc;
    buildQuantitiesA(doc);
    const auto schema = nativeDocumentVersionFor(doc);
    const auto first = dir.filePath("save-before.sketchyup");
    const auto second = dir.filePath("save-again.sketchyup");
    saveDocument(doc, first);
    saveDocument(doc, second);
    check(readFile(first) == readFile(second),
          "Two saves without schedules are byte-identical (the premise of the check)");

    const auto store = dir.filePath("bit/report-schedules.json");
    saveReportSchedule(doc, store, "Kinds", kindRequest());
    saveReportSchedule(doc, store, "Tagged",
                       tagRequest(QString::number(doc.tags().begin()->first)));
    const auto with = dir.filePath("save-with.sketchyup");
    saveDocument(doc, with);
    check(readFile(with) == readFile(first), "A save with schedules present is bit-identical");
    check(nativeDocumentVersionFor(doc) == schema, "The document keeps its schema with schedules");
}

// The run path (the CLI's --schedule) reads the store, checks the IDs and the identity, and writes
// the same report pass as the flags.
void runPath(const QTemporaryDir &dir) {
    isolate(dir.path());
    const auto store = reportScheduleStorePath();
    check(store.startsWith(dir.path()), "The store is under the temporary folder");

    Document doc;
    buildQuantitiesA(doc);
    saveReportSchedule(doc, store, "Kinds", kindRequest());
    const auto out = dir.filePath("kinds.csv");
    ReportFileOptions options;
    options.path = out;
    options.schedule = "Kinds";
    const auto run = writeReportFile(doc, options);
    check(is(run["status"], "exported") && is(run["schedule"], "Kinds"),
          "A scheduled run reports the schedule it ran");
    check(readFile(out).contains("#schedule=Kinds\n") && !readFile(out).contains("#ad-hoc"),
          "A scheduled run names its schedule in the CSV metadata");

    // Stale IDs: the file never appears, and the refusal names the missing IDs.
    ReportScheduleStore(store).add(identity(doc).toStdString(),
                                   scheduleNamed("Stale", tagRequest("987654")));
    options.schedule = "Stale";
    options.path = dir.filePath("stale.csv");
    const auto refusal = refusedBy([&] { writeReportFile(doc, options); });
    check(refusesWith(refusal, "NOT_FOUND", "tags 987654"),
          "A schedule with a missing tag refuses at run time and names the ID");
    check(!QFileInfo::exists(options.path), "A refused run writes no report file");

    // The identity guard on the run: the same name on another document is refused.
    Document other;
    buildQuantitiesA(other);
    options.schedule = "Kinds";
    options.path = dir.filePath("other.csv");
    const auto wrong = refusedBy([&] { writeReportFile(other, options); });
    check(refusesWith(wrong, "WRONG_DOCUMENT", "another document"),
          "A run on another document refuses the schedule with the identity guard");
    check(!QFileInfo::exists(options.path), "A refused run writes no report file");

    // A run of a schedule whose request no longer validates refuses with the schedule named.
    ReportScheduleStore(store).add(
        identity(doc).toStdString(),
        scheduleNamed("Bad precision", QJsonObject{{"metrics", "all"}, {"precision", 99}}));
    options.schedule = "Bad precision";
    options.path = dir.filePath("bad.csv");
    const auto invalid = refusedBy([&] { writeReportFile(doc, options); });
    check(invalid.message.find("Bad precision") != std::string::npos &&
              invalid.message.find(store.toStdString()) != std::string::npos,
          "An invalid stored request is refused, located by schedule and store");
}

// The session's report.schedule (ADR 0175 §8): the same three actions, the identity guard on the
// session's document, and the operation advertised in the session capabilities.
void sessionSchedule(const QTemporaryDir &dir) {
    isolate(dir.path());
    Document doc;
    buildQuantitiesA(doc);
    const auto model = dir.filePath("session-model.sketchyup");
    saveDocument(doc, model);
    const auto id = identity(doc);
    const auto call = [](AutomationSession &session, const QJsonObject &request) {
        return session.respond({{"id", "schedule"}, {"request", request}});
    };
    const auto op = [&](QJsonObject fields) {
        fields["apiVersion"] = 1;
        fields["operation"] = "report.schedule";
        fields["documentId"] = id;
        return fields;
    };
    const auto rejectedWith = [](const QJsonObject &response, const char *code) {
        return response["ok"] == false && is(response["error"].toObject()["code"], code);
    };

    bool advertised = false;
    for (const auto &entry : sessionCapabilities()["operations"].toArray())
        advertised = advertised || is(entry.toObject()["name"], "report.schedule");
    check(advertised, "The session capabilities advertise report.schedule");

    AutomationSession session({model, {}, dir.filePath("session-outcomes")});
    auto response =
        call(session, op({{"action", "save"}, {"name", "Kinds"}, {"request", kindRequest()}}));
    check(response["ok"] == true && is(response["result"].toObject()["status"], "saved"),
          "The session saves a schedule for its document");
    response = call(session, op({{"action", "describe"}}));
    check(response["ok"] == true &&
              response["result"].toObject()["schedules"].toArray().size() == 1,
          "The session lists the document's schedules");
    response = call(session, op({{"action", "describe"}, {"name", "Kinds"}}));
    check(response["ok"] == true &&
              is(response["result"].toObject()["schedule"].toObject()["name"], "Kinds"),
          "The session describes a schedule by name");
    response = call(session, op({{"action", "delete"}, {"name", "Kinds"}}));
    check(response["ok"] == true && is(response["result"].toObject()["status"], "deleted"),
          "The session deletes a schedule");

    response = call(session, op({{"action", "save"}, {"name", "Kinds"}}));
    check(rejectedWith(response, "INVALID_REQUEST"), "A save without a request is refused");
    response = call(session, op({{"action", "describe"}, {"request", kindRequest()}}));
    check(rejectedWith(response, "INVALID_REQUEST"), "A describe with a request is refused");
    response = call(session, op({{"action", "bogus"}}));
    check(rejectedWith(response, "INVALID_REQUEST"), "An unknown action is refused by the schema");

    // R101.e1: rename and update on the session, and the refusals of their fields.
    response =
        call(session, op({{"action", "save"}, {"name", "Kinds"}, {"request", kindRequest()}}));
    response = call(session, op({{"action", "rename"}, {"name", "Kinds"}, {"newName", "Renamed"}}));
    check(response["ok"] == true && is(response["result"].toObject()["status"], "renamed") &&
              is(response["result"].toObject()["schedule"].toObject()["name"], "Renamed"),
          "The session renames a schedule");
    response =
        call(session, op({{"action", "update"},
                          {"name", "Renamed"},
                          {"request", tagRequest(QString::number(doc.tags().begin()->first))}}));
    check(response["ok"] == true && is(response["result"].toObject()["status"], "updated") &&
              is(response["result"].toObject()["schedule"].toObject()["name"], "Renamed"),
          "The session updates a schedule's request and keeps its name");
    response =
        call(session, op({{"action", "rename"}, {"name", "Renamed"}, {"newName", "Renamed"}}));
    check(rejectedWith(response, "INVALID_REQUEST"), "The session refuses a rename onto itself");
    response = call(session, op({{"action", "rename"}, {"name", "Renamed"}}));
    check(rejectedWith(response, "INVALID_REQUEST"), "A rename without a new name is refused");
    response = call(session, op({{"action", "rename"},
                                 {"name", "Renamed"},
                                 {"newName", "New"},
                                 {"request", kindRequest()}}));
    check(rejectedWith(response, "INVALID_REQUEST"), "A rename with a request is refused");
    response = call(session, op({{"action", "save"},
                                 {"name", "Other"},
                                 {"request", kindRequest()},
                                 {"newName", "Other2"}}));
    check(rejectedWith(response, "INVALID_REQUEST"), "A save with a new name is refused");
    response = call(session, op({{"action", "update"}, {"name", "Renamed"}}));
    check(rejectedWith(response, "INVALID_REQUEST"), "An update without a request is refused");
    response = call(session, op({{"action", "delete"}, {"name", "Renamed"}}));
    check(response["ok"] == true, "The renamed schedule can be deleted by its new name");

    response = call(session, op({{"action", "describe"}}));
    QJsonObject other = op({{"action", "describe"}});
    other["documentId"] = "00000000000000000000000000000000";
    response = call(session, other);
    check(rejectedWith(response, "WRONG_DOCUMENT"),
          "A request for another document's schedules is refused by the identity guard");
    session.close();
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir bounds, round, corrupt, atomic, changed, guard, sdd, names, bit, run, session,
            renaming, atomicRename, pin, stale;
        check(bounds.isValid() && round.isValid() && corrupt.isValid() && atomic.isValid() &&
                  changed.isValid() && guard.isValid() && sdd.isValid() && names.isValid() &&
                  bit.isValid() && run.isValid() && session.isValid() && renaming.isValid() &&
                  atomicRename.isValid() && pin.isValid() && stale.isValid(),
              "Temporary folders");
        storeBounds(bounds);
        storeRoundTrip(round);
        corruptStoreRefused(corrupt);
        atomicWrite(atomic);
        changedFileRefused(changed);
        identityGuard(guard);
        saveDescribeDelete(sdd);
        namesAndIdentity(names);
        bitIdenticalSave(bit);
        renameKeepsRequest(renaming);
        renameIsAtomic(atomicRename);
        pinnedPrecision(pin);
        staleFix(stale);
#if defined(Q_OS_LINUX)
        // These two use the application's store location, which XDG_DATA_HOME isolates on Linux
        // only.
        runPath(run);
        sessionSchedule(session);
#else
        std::cout << "run path and session checks skipped: the store location is isolated only on "
                     "Linux\n";
#endif
        std::cout << "report schedule tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << "report schedule tests failed: " << error.what() << '\n';
        return 1;
    }
}
