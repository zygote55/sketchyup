#include "automation/measurement_assertions.hpp"
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/inspection_validation.hpp"
#include "core/entity_measure.hpp"
#include <QJsonDocument>
#include <cmath>
#include <set>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const QString &message) {
    throw InspectionError(code, message.toStdString());
}
QJsonArray point(Vec3 value) { return {value.x, value.y, value.z}; }
QString encoded(QJsonValue value) {
    const auto json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}
} // namespace
QJsonObject measurementAssertionCapabilities() {
    return {
        {"evaluation", "final private batch candidate before publication"},
        {"scope", "whole body with descendants, including hidden and locked geometry"},
        {"volume", "one validated material solid, subtracting inward cavities; no inferred sums"},
        {"comparison", "absolute tolerance in SI units; bounds compare each axis"},
        {"maximumAssertions", int(maxMeasurementAssertions)},
        {"maximumTargets", int(maxAssertionTargets)},
        {"aggregateRecords", int(maxAssertionRecords)},
        {"aggregateVertices", int(maxAssertionVertices)},
        {"aggregateEdges", int(maxAssertionEdges)},
        {"aggregateFaces", int(maxAssertionFaces)},
        {"aggregateLoopCorners", int(maxAssertionCorners)}};
}
QJsonArray evaluateMeasurementAssertions(const Document &candidate, const QJsonArray &assertions) {
    if (assertions.isEmpty())
        return {};
    if (size_t(assertions.size()) > maxMeasurementAssertions)
        fail("ASSERTION_LIMIT", "A batch admits at most 32 measurement assertions");
    const auto schema = commandDescription("assert.measurement")["parameters"].toObject();
    std::set<Id> targets;
    for (const auto &value : assertions) {
        const auto request = value.toObject();
        inspection_detail::validateParameters(request, schema);
        bool valid{};
        const auto target = request["body"].toString().toULongLong(&valid);
        if (!valid || !target || !candidate.bodies().contains(target))
            fail("ASSERTION_TARGET", "Assertion target must exist in the final batch candidate");
        const auto metric = request["metric"].toString();
        const bool vector = metric == "dimensions" || metric == "minimum" || metric == "maximum";
        if (request["expected"].isArray() != vector)
            fail("INVALID_REQUEST",
                 "Bounds assertions require three numbers; other metrics require one");
        targets.insert(target);
    }
    if (targets.size() > maxAssertionTargets)
        fail("ASSERTION_LIMIT", "A batch admits at most 16 distinct assertion bodies");
    // Charge overlapping target hierarchies once before doing any measurement.
    // The per-solid manifold/intersection analysis retains its own stricter work budget.
    std::map<Id, std::vector<Id>> children;
    for (const auto &[id, body] : candidate.bodies())
        children[body->parent].push_back(id);
    std::set<Id> charged;
    std::vector<Id> pending(targets.begin(), targets.end());
    size_t vertices{}, edges{}, faces{}, corners{};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!charged.insert(id).second)
            continue;
        const auto &body = *candidate.bodies().at(id);
        vertices += body.surface.vertices.size();
        edges += body.topology.edges.size();
        faces += body.surface.faces.size();
        for (const auto &[face, record] : body.surface.faces)
            for (const auto &loop : record.loops)
                corners += loop.size();
        if (charged.size() > maxAssertionRecords || vertices > maxAssertionVertices ||
            edges > maxAssertionEdges || faces > maxAssertionFaces || corners > maxAssertionCorners)
            fail("ASSERTION_LIMIT", "Assertion targets exceed the aggregate geometry work budget");
        const auto &descendants = children[id];
        pending.insert(pending.end(), descendants.begin(), descendants.end());
    }
    std::map<Id, EntityMeasures> measured;
    for (const auto target : targets)
        measured.emplace(target, measureEntity(candidate, {target, SelectionKind::Body, 0}));
    QJsonArray result;
    for (const auto &value : assertions) {
        const auto request = value.toObject();
        const auto target = request["body"].toString().toULongLong();
        const auto &entity = measured.at(target);
        const auto &frame = request["space"] == "world" ? entity.world : entity.local;
        const auto metric = request["metric"].toString();
        QJsonValue actual;
        QString unit = "m";
        if (metric == "volume") {
            if (!frame.volume)
                fail("MEASUREMENT_UNAVAILABLE",
                     "Volume assertion requires a validated single material "
                     "solid; status: " +
                         QString::fromStdString(entity.solid.status));
            actual = *frame.volume;
            unit = "m3";
        } else if (metric == "area") {
            actual = frame.area;
            unit = "m2";
        } else if (metric == "length") {
            if (frame.infiniteLength)
                fail("MEASUREMENT_UNAVAILABLE", "Length assertion requires a finite measurement");
            actual = frame.length;
        } else {
            if (!frame.bounds)
                fail("MEASUREMENT_UNAVAILABLE", "Bounds assertion requires finite geometry");
            actual = point(metric == "dimensions" ? frame.bounds->dimensions()
                           : metric == "minimum"  ? frame.bounds->low
                                                  : frame.bounds->high);
        }
        const auto expected = request["expected"];
        const auto tolerance = request["tolerance"].toDouble();
        auto agrees = [&](QJsonValue a, QJsonValue b) {
            return a.isDouble() && b.isDouble() && std::isfinite(a.toDouble()) &&
                   std::abs(a.toDouble() - b.toDouble()) <= tolerance;
        };
        bool passed = true;
        if (actual.isArray()) {
            const auto a = actual.toArray(), b = expected.toArray();
            for (int axis = 0; axis < 3; ++axis)
                passed &= agrees(a[axis], b[axis]);
        } else
            passed = agrees(actual, expected);
        if (!passed)
            fail("ASSERTION_FAILED", "Body " + request["body"].toString() + " " + metric + " in " +
                                         request["space"].toString() + " coordinates: measured " +
                                         encoded(actual) + ", expected " + encoded(expected) +
                                         " within " + QString::number(tolerance, 'g', 17) + " " +
                                         unit);
        auto report = request;
        report.remove("command");
        report["actual"] = actual;
        report["unit"] = unit;
        report["passed"] = true;
        report["evaluation"] = "final_batch";
        if (metric == "volume")
            report["solidStatus"] = QString::fromStdString(entity.solid.status);
        result.append(report);
    }
    return result;
}
} // namespace sketchy
