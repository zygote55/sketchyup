// R110 (1.7.0): the at-limit acceptance test for the raised unique geometry limits. The model has
// 150 prisms of 1,000 sides (300,000 unique vertices, 450,000 edges, 150,300 faces in 150 bodies),
// one material, and an encoded document chunk over 32 MiB, the 1.6.1 model bound. It must save,
// reopen and re-save byte for byte. A small edit (one body moved) goes through the scripted
// transaction path (snapshot.begin, then transaction begin, apply, preview and commit) and saves,
// and undo restores the document byte for byte. R110 (history budget 192 MiB, owner decision of
// 2026-11-01) adds: one edit that moves every loose body, accepted and undone; the same move
// through a transaction, which the 128 MiB staging budget refuses at 100 bodies and accepts at 20;
// and a component of two closed 100,000-face spheres, accepted, against three, refused. The test
// prints its peak RSS, the charged history bytes and its times. It is a slow core test (ADR 0170):
// the fast tier runs it only when the core group is selected.
#include "automation/inspection_session.hpp"
#include "automation/transaction_coordinator.hpp"
#include "automation/transactions.hpp"
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/transform_selection.hpp"
#include "geometry/topology.hpp"
#include "io/document_io.hpp"
#include "io/native_limits.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>
using namespace sketchy;
namespace {
using Clock = std::chrono::steady_clock;
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
// A prism of `sides` sides centred at (x, 400): 2 * sides vertices, 3 * sides edges, sides + 2
// faces.
Id prism(Document &doc, int sides, double x) {
    std::vector<Vec3> loop;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        loop.push_back({x + std::cos(angle), 400 + std::sin(angle), 0});
    }
    const auto body = doc.addFace({loop});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
    return body;
}
// The request shapes of transaction_dispatch_tests and inspection_session_tests.
QJsonObject transactionRequest(const Document &doc, const QString &operation,
                               QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["documentId"] = QString::fromStdString(doc.identity());
    fields["operation"] = "transaction." + operation;
    return fields;
}
QJsonObject inspectionRequest(const Document &doc, const QString &query, QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["documentId"] = QString::fromStdString(doc.identity());
    fields["expectedRevision"] = QString::number(doc.revision());
    fields["query"] = query;
    return fields;
}
// The densest geometry the limits admit. A body holds at most 100,000 vertices or faces (the
// per-body kernel limit, which R110 does not change), so the densest unique geometry is three
// closed triangulated spheres of 100,000 faces each, held by one component. A closed sphere of
// `sides` by `rings` vertices has 2 + sides * rings vertices, 2 * sides * rings faces and 3 edges
// per face pair: 50,002 vertices, 100,000 faces and 150,000 edges each. Vertices go in directly,
// because Surface::vertex scans every vertex it already holds.
std::shared_ptr<Body> sphere(Id id, Vec3 centre, int sides, int rings) {
    const double radius = 20;
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Sphere " + std::to_string(id);
    auto &surface = body->surface;
    auto add = [&](Vec3 point) {
        const Id vertex = surface.nextId++;
        surface.vertices.emplace(vertex, centre + point);
        return vertex;
    };
    const Id north = add({0, 0, radius});
    const size_t perRing = size_t(sides), rowCount = size_t(rings);
    std::vector<Id> row(perRing);
    std::vector<std::vector<Id>> ring(rowCount, row);
    for (int r = 0; r < rings; ++r) {
        const double theta = std::numbers::pi * (r + 1) / (rings + 1);
        for (int j = 0; j < sides; ++j) {
            const double phi = 2 * std::numbers::pi * j / sides;
            ring[size_t(r)][size_t(j)] =
                add({radius * std::sin(theta) * std::cos(phi),
                     radius * std::sin(theta) * std::sin(phi), radius * std::cos(theta)});
        }
    }
    const Id south = add({0, 0, -radius});
    for (int j = 0; j < sides; ++j) {
        const int next = (j + 1) % sides;
        surface.addFaceIds({{north, ring.front()[size_t(j)], ring.front()[size_t(next)]}});
    }
    for (int r = 0; r + 1 < rings; ++r)
        for (int j = 0; j < sides; ++j) {
            const int next = (j + 1) % sides;
            const auto &a = ring[size_t(r)], &b = ring[size_t(r + 1)];
            surface.addFaceIds({{a[size_t(j)], b[size_t(j)], b[size_t(next)]}});
            surface.addFaceIds({{a[size_t(j)], b[size_t(next)], a[size_t(next)]}});
        }
    for (int j = 0; j < sides; ++j) {
        const int next = (j + 1) % sides;
        surface.addFaceIds({{south, ring.back()[size_t(next)], ring.back()[size_t(j)]}});
    }
    body->topology = Topology::rebuild(body->surface, body->topology);
    return body;
}
double seconds(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double>(to - from).count();
}
// The first member of a definition that is not its root.
Id firstMember(const Document &doc, Id definition) {
    const auto &record = *doc.definitions().at(definition);
    for (const auto &[member, body] : record.members)
        if (member != record.root)
            return member;
    throw std::runtime_error("Definition has no member");
}
// One whole-document edit of the at-limit loose bodies: every body moves in one undoable edit.
// It must be accepted and undo exactly. Returns the edit's charged history bytes.
size_t wholeDocumentMove(Document &doc, const std::vector<Id> &bodies, const char *label) {
    TransformTargets everything;
    for (const auto body : bodies)
        everything.insert({body});
    const auto before = doc.historyBytes();
    const auto undoable = doc.history().position;
    const auto revision = doc.revision();
    std::vector<double> xs;
    for (const auto body : bodies)
        xs.push_back(doc.worldTransform(body).point({0, 0, 0}).x);
    (void)transformSelected(doc, everything, Transform::translation({0, 0, 1}));
    check(doc.revision() == revision + 1 && doc.history().position == undoable + 1,
          std::string(label) + ": the move is one revision and one undo entry (revision " +
              std::to_string(revision) + " to " + std::to_string(doc.revision()) + ", undo " +
              std::to_string(undoable) + " to " + std::to_string(doc.history().position) + ")");
    for (size_t i = 0; i < bodies.size(); ++i)
        check(std::abs(doc.worldTransform(bodies[i]).point({0, 0, 0}).z - 1) < 1e-9,
              std::string(label) + ": every body moved");
    const auto charged = doc.historyBytes() - before;
    doc.undo();
    for (size_t i = 0; i < bodies.size(); ++i)
        check(std::abs(doc.worldTransform(bodies[i]).point({0, 0, 0}).x - xs[i]) < 1e-9 &&
                  std::abs(doc.worldTransform(bodies[i]).point({0, 0, 0}).z) < 1e-9,
              std::string(label) + ": undo returns every body to its place");
    std::printf("%s: whole-document move of %zu bodies charged %.1f MiB of history (budget %.0f "
                "MiB)\n",
                label, bodies.size(), double(charged) / (1024 * 1024),
                double(Document::historyLimit) / (1024 * 1024));
    return charged;
}
// The same whole-document move through the scripted transaction path. A transaction accepts at
// most 100 cumulative commands, so this model has 100 prisms of 1,500 sides: the same 300,000
// unique vertices, in 100 bodies that one transaction can all move. The 128 MiB staging budget
// refuses that proposal (R110 finding: the staged proposal holds the edit's whole history charge
// plus its new records); staging is not changed. A 20-body proposal fits, commits and undoes.
// Returns the 20-body proposal's charged history bytes.
size_t transactionWholeDocument(const QString &outcomes) {
    Document built;
    std::vector<Id> bodies;
    for (int i = 0; i < 100; ++i)
        bodies.push_back(prism(built, 1500, 3.0 * i));
    // Reload the model, so that its history starts empty as a document opened from a file does.
    Document doc = decodeContainer(encodeContainer(built));
    const auto totals = doc.materializedTotals();
    check(totals.vertices == DocumentLimits::unique.vertices && totals.records == 100 &&
              exceededLimit(LimitCounts{0, totals.records, totals.vertices, totals.faces,
                                        totals.wires, totals.edges, totals.curves, totals.guides},
                            LimitClass::UniqueGeometry)
                  .empty(),
          "The transaction model holds 300,000 unique vertices in 100 bodies, within the limits");
    TransactionCoordinator actor(TransactionCoordinator::BorrowedDocument{doc}, outcomes);
    TransactionDispatcher api(actor);
    // The staging charge of the whole-document edit, as staging computes it: the prepared
    // edit's retained bytes (its history charge plus the records it adds).
    TransformTargets everything;
    for (const auto body : bodies)
        everything.insert({body});
    const auto prepared = doc.prepareEdit([&](Document &draft) {
        (void)transformSelected(draft, everything, Transform::translation({0, 0, 1}));
    });
    std::printf("transaction staging: the whole-document edit stages %.1f MiB against the 128 MiB "
                "budget\n",
                double(prepared.retainedBytes(doc)) / (1024 * 1024));
    // Begin, apply the first `count` moves, preview and commit. Returns the staged bytes.
    auto move = [&](size_t count, const char *task) {
        const auto begun = api.execute(transactionRequest(
            doc, "begin",
            {{"expectedRevision", QString::number(doc.revision())},
             {"history", QJsonObject{{"label", QString(task)},
                                     {"assistant", false},
                                     {"request", "Move bodies of the at-limit model"},
                                     {"taskId", QString("unique-limits-") + task}}}}));
        const auto id = begun["transactionId"].toString();
        QJsonArray commands;
        for (size_t i = 0; i < count; ++i)
            commands.append(QJsonObject{{"command", "geometry.translate"},
                                        {"body", QString::number(bodies[i])},
                                        {"delta", QJsonArray{0, 0, 1}}});
        const auto applied = api.execute(transactionRequest(
            doc, "apply",
            {{"transactionId", id}, {"expectedVersion", 0}, {"operationId", "move"},
             {"commands", commands}}));
        check(applied["stageVersion"] == 1 && applied["commandCount"] == int(count),
              "Apply stages every move in one draft");
        const auto sealed = api.execute(
            transactionRequest(doc, "preview", {{"transactionId", id}, {"expectedVersion", 1}}));
        check(sealed["status"] == "sealed", "Preview seals the draft");
        const auto committed = api.execute(transactionRequest(
            doc, "commit",
            {{"requestId", sealed["requestId"]}, {"payloadHash", sealed["payloadHash"]}}));
        check(committed["status"] == "committed", "Commit publishes the sealed draft");
    };
    // The staged charge of the first `count` moves, as staging computes it.
    auto stagedBytes = [&](size_t count) {
        TransformTargets targets;
        for (size_t i = 0; i < count; ++i)
            targets.insert({bodies[i]});
        return doc.prepareEdit([&](Document &draft) {
                      (void)transformSelected(draft, targets, Transform::translation({0, 0, 1}));
                  })
            .retainedBytes(doc);
    };
    // The staging budget refuses the whole-document proposal, and a refused proposal changes
    // nothing. The refusal is reported, not worked around: staging keeps its 128 MiB budget.
    const auto revision = doc.revision();
    std::string refusal;
    try {
        (void)move(bodies.size(), "whole-document");
    } catch (const std::exception &error) {
        refusal = error.what();
    }
    check(refusal == "Proposal exceeds the retained staging budget",
          "The whole-document proposal is refused by the staging budget: " + refusal);
    check(doc.revision() == revision && doc.historyBytes() == 0,
          "A refused proposal changes neither the revision nor the history");
    // A proposal that fits the budget goes through the same path: 20 bodies move and undo.
    const auto before = doc.historyBytes();
    const auto staged = stagedBytes(20);
    move(20, "subset");
    check(doc.revision() == revision + 1, "The 20-body transaction is one revision");
    for (size_t i = 0; i < bodies.size(); ++i)
        check(std::abs(doc.worldTransform(bodies[i]).point({0, 0, 0}).z - (i < 20 ? 1 : 0)) < 1e-9,
              "The committed transaction moved exactly its 20 bodies");
    const auto charged = doc.historyBytes() - before;
    doc.undo();
    for (const auto body : bodies)
        check(std::abs(doc.worldTransform(body).point({0, 0, 0}).z) < 1e-9,
              "Undo returns every body after the transaction");
    std::printf("transaction of 20 bodies: staged %.1f MiB, charged %.1f MiB of history\n",
                double(staged) / (1024 * 1024), double(charged) / (1024 * 1024));
    return charged;
}
// R110: a component of `count` closed 100,000-face spheres (50,002 vertices, 100,000 faces and
// 150,000 edges each). Moving one member is one undoable edit, and the edit charges the whole
// definition, before and after. Two spheres (200,000 faces) must be accepted by the 192 MiB
// budget, with undo and redo exact. Three (300,000 faces, the unique limit) are refused: the
// finding of R110, reported and not worked around. Returns the charge, or the refusal.
struct DenseEdit {
    size_t charged{};
    std::string refusal;
};
DenseEdit denseComponentEdit(int count) {
    const auto started = Clock::now();
    std::map<Id, BodyPtr> bodies;
    std::set<Id> parts;
    for (Id id = 1; id <= Id(count); ++id) {
        bodies.emplace(id, sphere(id, {60.0 * double(id - 1), 0, 0}, 200, 250));
        parts.insert(id);
    }
    Document doc;
    doc.restore("00000000000000000000000000000833", Id(count) + 1, std::move(bodies));
    const auto totals = doc.materializedTotals();
    check(totals.vertices == 50002 * size_t(count) && totals.faces == 100000 * size_t(count) &&
              totals.edges == 150000 * size_t(count) && totals.records == size_t(count),
          "The spheres hold the counts of the densest component");
    const auto group = createGroup(doc, parts, "Dense mesh");
    const auto component = createComponent(doc, group, "Dense mesh");
    // Reload, so the edit starts with empty history as a file does: the build's own history
    // (about 150 MB for two spheres) would otherwise be pruned to make room for the edit.
    doc = decodeContainer(encodeContainer(doc));
    const auto componentTotals = doc.materializedTotals();
    const auto definition = component.definition;
    const auto member = firstMember(doc, definition);
    const auto built = Clock::now();
    const auto x0 =
        doc.definitions().at(definition)->members.at(member)->transform.point({0, 0, 0}).x;
    const auto before = doc.historyBytes();
    const auto undoable = doc.history().position;
    const auto revision = doc.revision();
    DenseEdit result;
    try {
        editComponentDefinition(doc, definition, [&](Document &draft) {
            draft.move(member, {1, 0, 0});
            return ChangeReport{};
        });
    } catch (const std::exception &error) {
        result.refusal = error.what();
    }
    if (!result.refusal.empty()) {
        check(doc.revision() == revision && doc.history().position == undoable,
              "A refused component edit changes nothing");
        std::printf("densest component, %d spheres (%zu faces): refused: %s (built in %.1f s)\n",
                    count, size_t(totals.faces), result.refusal.c_str(),
                    seconds(started, built));
        return result;
    }
    check(doc.history().position == undoable + 1,
          "Moving one member of the component is one undo entry (undo " +
              std::to_string(undoable) + " to " + std::to_string(doc.history().position) + ")");
    result.charged = doc.historyBytes() - before;
    const auto moved = [&] {
        return doc.definitions().at(definition)->members.at(member)->transform.point({0, 0, 0}).x;
    };
    check(std::abs(moved() - (x0 + 1)) < 1e-9, "The member moved by one metre");
    check(doc.materializedTotals() == componentTotals, "The move keeps the materialized totals");
    doc.undo();
    check(std::abs(moved() - x0) < 1e-9, "Undo returns the member to its place");
    check(doc.materializedTotals() == componentTotals, "Undo restores the totals");
    doc.redo();
    check(std::abs(moved() - (x0 + 1)) < 1e-9, "Redo moves the member again");
    std::printf("component, %d spheres (%zu faces, %zu vertices): move of one member charged "
                "%.1f MiB of history; build %.1f s, edit, undo and redo %.1f s\n",
                count, size_t(totals.faces), size_t(totals.vertices),
                double(result.charged) / (1024 * 1024), seconds(started, built),
                seconds(built, Clock::now()));
    return result;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto started = Clock::now();
        Document doc;
        const auto oak = createMaterial(doc, "Oak", {0.6f, 0.4f, 0.2f});
        std::vector<Id> bodies;
        for (int i = 0; i < 150; ++i)
            bodies.push_back(prism(doc, 1000, 3.0 * i));
        for (const auto body : bodies)
            assignMaterial(doc, body, std::nullopt, oak);
        const auto totals = doc.materializedTotals();
        check(totals.vertices == DocumentLimits::unique.vertices && totals.edges == 450000 &&
                  totals.faces == 150300 && totals.records == 150,
              "The model holds 300,000 unique vertices, 450,000 edges and 150,300 faces in 150 "
              "bodies");
        check(exceededLimit(LimitCounts{0, totals.records, totals.vertices, totals.faces,
                                        totals.wires, totals.edges, totals.curves, totals.guides},
                            LimitClass::UniqueGeometry)
                  .empty(),
              "The model is within the unique geometry limits at the limit");
        const auto built = Clock::now();

        // Save, reopen, re-save: byte for byte. The document chunk is over the 1.6.1 bound.
        const auto document = encodeDocument(doc);
        check(document.size() > 32 * 1024 * 1024, "The document chunk is over 32 MiB");
        const auto container = encodeContainer(doc);
        check(container.size() <= NativeLimits::containerBytes,
              "The container is within its bound");
        auto reopened = decodeContainer(container);
        check(reopened.materializedTotals() == totals && reopened.bodies().size() == 150,
              "The model reopens unchanged");
        check(encodeContainer(reopened) == container, "The reopened model re-saves byte for byte");
        const auto saved = Clock::now();

        // A small edit through the scripted transaction path. The snapshot is charged only its
        // own bookkeeping, so it does not grow with the document.
        QTemporaryDir files;
        check(files.isValid(), "Temporary outcome storage");
        InspectionSession session;
        const auto captured =
            session.execute(doc, inspectionRequest(doc, "snapshot.begin", {{"ttlSeconds", 60}}));
        check(!captured["snapshotId"].toString().isEmpty() && session.retainedCount() == 1 &&
                  session.retainedBytes(doc) < 32 * 1024 * 1024,
              "The inspection snapshot of the at-limit model is accepted within its 32 MiB budget");
        // The coordinator borrows the live document, so the commit and undo act on `doc`.
        TransactionCoordinator actor(TransactionCoordinator::BorrowedDocument{doc},
                                     files.path() + "/outcomes");
        TransactionDispatcher api(actor);
        const auto revision = doc.revision();
        const auto begun = api.execute(transactionRequest(
            doc, "begin",
            {{"expectedRevision", QString::number(doc.revision())},
             {"history", QJsonObject{{"label", "Move one body"},
                                     {"assistant", false},
                                     {"request", "Move one face of the at-limit model"},
                                     {"taskId", "unique-limits-acceptance"}}}}));
        const auto id = begun["transactionId"].toString();
        check(begun["stageVersion"] == 0 && begun["status"] == "draft",
              "Begin creates an empty private draft at the limit");
        const auto moved = QString::number(bodies[75]);
        const auto applied = api.execute(transactionRequest(
            doc, "apply",
            {{"transactionId", id},
             {"expectedVersion", 0},
             {"operationId", "move"},
             {"commands", QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                 {"body", moved},
                                                 {"delta", QJsonArray{2, 0, 0}}}}}}));
        check(applied["stageVersion"] == 1 && applied["commandCount"] == 1,
              "Apply stages the move without a live edit");
        const auto sealed = api.execute(
            transactionRequest(doc, "preview", {{"transactionId", id}, {"expectedVersion", 1}}));
        check(sealed["status"] == "sealed", "Preview seals the draft");
        const auto committed = api.execute(transactionRequest(
            doc, "commit",
            {{"requestId", sealed["requestId"]}, {"payloadHash", sealed["payloadHash"]}}));
        check(committed["status"] == "committed" && doc.revision() == revision + 1 &&
                  doc.history().entries.back().metadata.taskId == "unique-limits-acceptance",
              "Commit creates one revision with its undo task");
        check(std::abs(doc.worldTransform(bodies[75]).point({0, 0, 0}).x - 2) < 1e-9,
              "The committed move places the body at x = 2");
        const auto afterCommit = encodeContainer(doc);
        check(encodeContainer(decodeContainer(afterCommit)) == afterCommit,
              "The edited at-limit model saves and re-saves byte for byte");
        const auto edited = Clock::now();

        // Undo returns the body to its place and the model to its saved totals.
        doc.undo();
        check(std::abs(doc.worldTransform(bodies[75]).point({0, 0, 0}).x) < 1e-9,
              "Undo returns the body to its place");
        check(doc.materializedTotals() == totals && doc.bodies().size() == 150,
              "Undo restores the saved totals");
        session.clear();
        const auto undone = Clock::now();

        // R110 (owner decision, 2026-11-01: history budget 192 MiB): one edit that moves every
        // loose body, and the same edit through the transaction path, and the densest component.
        // The reloaded model has no history, so the move's charge is the whole history: an edit
        // on `doc` would prune its construction history to fit the budget.
        const auto moveAll = wholeDocumentMove(reopened, bodies, "Loose bodies at the limit");
        const auto moveTransaction = transactionWholeDocument(files.path() + "/whole-document");
        const auto twoSpheres = denseComponentEdit(2);
        check(twoSpheres.refusal.empty(),
              "Two spheres (200,000 faces) are edited within the 192 MiB budget: " +
                  twoSpheres.refusal);
        const auto threeSpheres = denseComponentEdit(3);
        check(threeSpheres.refusal == "Edit exceeds the 192 MiB history budget",
              "Three spheres (300,000 faces) are refused by the 192 MiB budget: " +
                  threeSpheres.refusal);
        std::printf("history charges: whole-document move %.1f MiB, transaction of 20 bodies %.1f "
                    "MiB, two-sphere component member %.1f MiB; budget %.0f MiB\n",
                    double(moveAll) / (1024 * 1024), double(moveTransaction) / (1024 * 1024),
                    double(twoSpheres.charged) / (1024 * 1024),
                    double(Document::historyLimit) / (1024 * 1024));

        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        const auto end = Clock::now();
        auto span = [](Clock::time_point from, Clock::time_point to) {
            return std::chrono::duration<double>(to - from).count();
        };
        std::printf("at-limit acceptance: %zu unique vertices, document chunk %.1f MiB, container "
                    "%.1f MiB, peak RSS %.0f MiB; build %.1f s, save and reopen %.1f s, "
                    "transaction and save %.1f s, undo %.1f s, total %.1f s\n",
                    size_t(totals.vertices), double(document.size()) / (1024 * 1024),
                    double(container.size()) / (1024 * 1024), double(usage.ru_maxrss) / 1024,
                    span(started, built), span(built, saved), span(saved, edited),
                    span(edited, undone), span(started, end));
        std::printf("R110 whole-document and component checks: %.1f s\n", span(undone, end));
        std::cout << "Unique limits acceptance: the at-limit model saves, reopens, takes a "
                     "transaction and undoes byte for byte\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
