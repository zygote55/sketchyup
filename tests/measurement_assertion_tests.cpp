#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/transactions.hpp"
#include "core/groups.hpp"
#include "core/solid_boolean.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QJsonObject request(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject assertion(Id body, QString metric, QJsonValue expected, QString space = "local") {
    return {{"command", "assert.measurement"},
            {"body", QString::number(body)},
            {"metric", metric},
            {"space", space},
            {"expected", expected},
            {"tolerance", 1e-7}};
}
QJsonObject paint(Id body = 1) {
    return {{"command", "material.color"},
            {"body", QString::number(body)},
            {"color", QJsonArray{.3, .6, .9}}};
}
QJsonObject extrude() {
    return {
        {"command", "geometry.extrude_isolated"}, {"body", "1"}, {"face", "5"}, {"distance", 1}};
}
Document rectangle() {
    Document doc;
    doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
    return doc;
}
template <class F> void rejects(Document &doc, const char *code, F operation) {
    const auto before = encodeContainer(doc);
    const auto depth = doc.history().total;
    bool failed{};
    try {
        operation();
    } catch (const InspectionError &error) {
        if (error.code() != code)
            throw std::runtime_error(std::string("Expected ") + code + ", got " + error.code() +
                                     ": " + error.what());
        failed = true;
    }
    check(failed && encodeContainer(doc) == before && doc.history().total == depth,
          "Rejected assertion preserves document bytes, allocator and history");
}
void finalCandidate() {
    auto doc = rectangle();
    const auto before = encodeContainer(doc);
    const auto depth = doc.history().total;
    bool noChange{};
    try {
        executeBatch(doc, request(doc, {assertion(1, "area", 6)}));
    } catch (const std::runtime_error &error) {
        noChange = std::string(error.what()) == "Batch has no committed changes";
    }
    check(noChange && encodeContainer(doc) == before && doc.history().total == depth,
          "Assertion-only batch creates no history; read-only callers use measure.entity");
    const QJsonArray commands{assertion(1, "volume", 6),
                              extrude(),
                              assertion(1, "area", 22),
                              assertion(1, "length", 24),
                              assertion(1, "dimensions", QJsonArray{2, 3, 1}),
                              assertion(1, "minimum", QJsonArray{0, 0, 0}),
                              assertion(1, "maximum", QJsonArray{2, 3, 1})};
    const auto preview = previewBatch(doc, request(doc, commands));
    check(encodeContainer(doc) == before, "Assertion preview is private");
    const auto result = executeBatch(doc, request(doc, commands));
    check(result["assertions"].toArray().size() == 6 &&
              result["assertions"] == preview["assertions"] && doc.history().total == depth + 1,
          "Assertions evaluate final geometry independent of position and share one Undo");
    const auto volume = result["assertions"].toArray()[0].toObject();
    check(volume["actual"] == 6 && volume["unit"] == "m3" && volume["passed"] == true &&
              volume["evaluation"] == "final_batch",
          "Assertion receipt records independently measured value, units and final scope");
    const auto saved = encodeContainer(doc);
    check(encodeContainer(decodeContainer(saved)) == saved, "Asserted geometry persists exactly");
    doc.undo();
    check(doc.bodies().at(1)->surface.faces.size() == 1, "One Undo restores original face");
    rejects(doc, "ASSERTION_FAILED",
            [&] { executeBatch(doc, request(doc, {assertion(1, "area", 6), extrude()})); });
}
void framesAndValidity() {
    auto doc = rectangle();
    executeBatch(doc, request(doc, {extrude()}));
    const auto group = createGroup(doc, {1});
    doc.transform(group, Transform::translation({10, 20, 30}) * Transform::scaling({-2, 3, .5}));
    const auto result = executeBatch(
        doc, request(doc, {paint(), assertion(group, "volume", 6),
                           assertion(group, "volume", 18, "world"),
                           assertion(group, "dimensions", QJsonArray{4, 9, .5}, "world"),
                           assertion(group, "minimum", QJsonArray{6, 20, 30}, "world"),
                           assertion(group, "area", 85, "world")}));
    check(result["assertions"].toArray().size() == 5,
          "Mirrored nonuniform hierarchy uses the declared coordinate frame");
    auto open = rectangle();
    rejects(open, "MEASUREMENT_UNAVAILABLE",
            [&] { executeBatch(open, request(open, {paint(), assertion(1, "volume", 0)})); });
    const auto other = open.addFace({{{5, 0, 0}, {6, 0, 0}, {6, 1, 0}, {5, 1, 0}}});
    const auto aggregate = createGroup(open, {1, other});
    rejects(open, "MEASUREMENT_UNAVAILABLE", [&] {
        executeBatch(open, request(open, {paint(), assertion(aggregate, "volume", 0)}));
    });
    rejects(open, "ASSERTION_TARGET",
            [&] { executeBatch(open, request(open, {paint(), assertion(9999, "area", 6)})); });
    rejects(open, "ASSERTION_TARGET", [&] {
        executeBatch(open, request(open, {assertion(other, "area", 1),
                                          QJsonObject{{"command", "geometry.delete"},
                                                      {"body", QString::number(other)}}}));
    });
    for (const auto &metric : {"volume", "area", "length"}) {
        auto bad = assertion(1, metric, QJsonArray{1, 2, 3});
        rejects(open, "INVALID_REQUEST",
                [&] { executeBatch(open, request(open, {paint(), bad})); });
    }
    for (const auto &metric : {"dimensions", "minimum", "maximum"}) {
        auto bad = assertion(1, metric, 6);
        rejects(open, "INVALID_REQUEST",
                [&] { executeBatch(open, request(open, {paint(), bad})); });
    }
    for (double bound : {-1., 1.01}) {
        auto bad = assertion(1, "area", 6);
        bad["tolerance"] = bound;
        rejects(open, "LIMIT_EXCEEDED", [&] { executeBatch(open, request(open, {paint(), bad})); });
    }
    QJsonArray tooMany{paint()};
    for (int i = 0; i < 33; ++i)
        tooMany.append(assertion(1, "area", 6));
    rejects(open, "ASSERTION_LIMIT", [&] { executeBatch(open, request(open, tooMany)); });
}
void cavitiesAndBounds() {
    Document doc;
    const auto outer = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    doc.extrude(outer, 5, 2);
    const auto inner = doc.addFace({{{.5, .5, .5}, {1.5, .5, .5}, {1.5, 1.5, .5}, {.5, 1.5, .5}}});
    doc.extrude(inner, 5, 1);
    const auto result = booleanBodies(doc, outer, inner, BooleanOperation::Subtract, 0, false);
    check(result.parts.size() == 1, "Enclosed subtraction retains one material component");
    const auto shell = result.parts.front().body;
    doc.transform(shell, Transform::translation({999990, 999990, 999990}));
    const auto report =
        executeBatch(doc, request(doc, {paint(shell), assertion(shell, "volume", 7, "world")}));
    check(report["assertions"].toArray()[0].toObject()["actual"] == 7,
          "Validated volume subtracts inward cavity and remains stable far from origin");
    auto many = rectangle();
    QJsonArray assertions{paint()};
    for (int i = 0; i < 17; ++i) {
        const auto id = i == 0 ? 1
                               : many.addFace({{{double(i * 4), 0, 0},
                                                {double(i * 4 + 2), 0, 0},
                                                {double(i * 4 + 2), 3, 0},
                                                {double(i * 4), 3, 0}}});
        assertions.append(assertion(id, "area", 6));
    }
    rejects(many, "ASSERTION_LIMIT", [&] { executeBatch(many, request(many, assertions)); });
    Document hierarchy;
    Edit edit{"Bounded hierarchy", {}};
    for (Id i = 1; i <= 257; ++i) {
        auto body = std::make_shared<Body>();
        body->id = i;
        body->kind = BodyKind::Group;
        body->parent = i == 1 ? 0 : 1;
        edit.changes.push_back({i, {}, body});
    }
    hierarchy.apply(std::move(edit), hierarchy.revision());
    rejects(hierarchy, "ASSERTION_LIMIT", [&] {
        executeBatch(
            hierarchy,
            request(hierarchy, {QJsonObject{{"command", "document.units"}, {"units", "mm"}},
                                assertion(1, "dimensions", QJsonArray{0, 0, 0})}));
    });
}
void transactionGuards() {
    auto initial = rectangle();
    QTemporaryDir storage;
    TransactionCoordinator actor(initial, storage.path());
    TransactionDispatcher api(actor);
    const auto &doc = actor.document();
    auto call = [&](QString operation, QJsonObject fields) {
        fields["apiVersion"] = 1;
        fields["documentId"] = QString::fromStdString(doc.identity());
        fields["operation"] = "transaction." + operation;
        return api.execute(fields);
    };
    const auto before = encodeContainer(doc);
    const auto begin = call("begin", {{"expectedRevision", QString::number(doc.revision())}});
    const auto transaction = begin["transactionId"];
    const auto first =
        call("apply", {{"transactionId", transaction},
                       {"expectedVersion", 0},
                       {"operationId", "extrude-and-check"},
                       {"commands", QJsonArray{extrude(), assertion(1, "volume", 6, "world")}}});
    check(first["createdIds"].toObject()["assertions"].toArray().size() == 1 &&
              encodeContainer(doc) == before,
          "Transaction exposes private assertion evidence");
    bool failed{};
    try {
        call("apply", {{"transactionId", transaction},
                       {"expectedVersion", 1},
                       {"operationId", "invalidate-volume"},
                       {"commands", QJsonArray{QJsonObject{{"command", "entity.dimensions"},
                                                           {"body", "1"},
                                                           {"dimensions", QJsonArray{4, 3, 1}},
                                                           {"frame", "world"}}}}});
    } catch (const InspectionError &error) {
        check(error.code() == "ASSERTION_FAILED", error.what());
        failed = true;
    }
    check(failed && encodeContainer(doc) == before,
          "Later transaction operations must preserve earlier final-state assertions");
    const auto sealed = call("preview", {{"transactionId", transaction}, {"expectedVersion", 1}});
    const auto committed = call(
        "commit", {{"requestId", sealed["requestId"]}, {"payloadHash", sealed["payloadHash"]}});
    check(committed["result"].toObject()["assertions"].toArray().size() == 1,
          "Failed append preserves previous valid stage and committed assertion evidence");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        finalCandidate();
        framesAndValidity();
        cavitiesAndBounds();
        transactionGuards();
        std::cout << "Final-state measurement assertions, validated volume, affine frames, atomic "
                     "failure and incremental transaction guards passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
