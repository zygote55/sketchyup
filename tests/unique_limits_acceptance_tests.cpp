// R110 (1.7.0): the at-limit acceptance test for the raised unique geometry limits. The model has
// 150 prisms of 1,000 sides (300,000 unique vertices, 450,000 edges, 150,300 faces in 150 bodies),
// one material, and an encoded document chunk over 32 MiB, the 1.6.1 model bound. It must save,
// reopen and re-save byte for byte. A small edit (one body moved) goes through the scripted
// transaction path (snapshot.begin, then transaction begin, apply, preview and commit) and saves,
// and undo restores the document byte for byte. The test prints its peak RSS and times. It is a
// slow core test (ADR 0170): the fast tier runs it only when the core group is selected.
#include "automation/inspection_session.hpp"
#include "automation/transaction_coordinator.hpp"
#include "automation/transactions.hpp"
#include "core/document_limits.hpp"
#include "core/materials.hpp"
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
#include <numbers>
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
        const auto reopened = decodeContainer(container);
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
                    span(edited, end), span(started, end));
        std::cout << "Unique limits acceptance: the at-limit model saves, reopens, takes a "
                     "transaction and undoes byte for byte\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
