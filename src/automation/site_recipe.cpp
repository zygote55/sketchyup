#include "automation/site_recipe.hpp"
#include "automation/measurement_assertions.hpp"
#include "automation/recipe_builder.hpp"
#include <algorithm>
namespace sketchy {
namespace {
QString id(Id value) { return QString::number(value); }
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
QJsonArray matrix(Transform transform) {
    QJsonArray result;
    for (double value : transform.m)
        result.append(value);
    return result;
}
[[noreturn]] void fail(const char *message) { throw InspectionError("INVALID_RECIPE", message); }
struct Bounds {
    std::optional<Vec3> low, high;
    void include(Vec3 p) {
        if (!low) {
            low = high = p;
            return;
        }
        *low = {std::min(low->x, p.x), std::min(low->y, p.y), std::min(low->z, p.z)};
        *high = {std::max(high->x, p.x), std::max(high->y, p.y), std::max(high->z, p.z)};
    }
};
QJsonObject assertion(Id body, QString frame, QString metric, Vec3 expected) {
    return {
        {"command", "assert.measurement"}, {"body", id(body)}, {"space", frame}, {"metric", metric},
        {"expected", point(expected)},     {"tolerance", 1e-6}};
}
std::set<Id> subtree(const Document &doc, Id root) {
    std::map<Id, std::vector<Id>> children;
    for (const auto &[bodyId, body] : doc.bodies())
        children[body->parent].push_back(bodyId);
    std::vector<Id> pending{root};
    std::set<Id> result;
    size_t vertices{}, edges{}, faces{}, corners{};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!result.insert(current).second)
            continue;
        const auto &body = *doc.bodies().at(current);
        vertices += body.surface.vertices.size();
        edges += body.topology.edges.size();
        faces += body.surface.faces.size();
        for (const auto &[face, record] : body.surface.faces)
            for (const auto &loop : record.loops)
                corners += loop.size();
        if (result.size() > maxAssertionRecords || vertices > maxAssertionVertices ||
            edges > maxAssertionEdges || faces > maxAssertionFaces || corners > maxAssertionCorners)
            fail("Site target exceeds bounded measurement geometry limits");
        const auto &next = children[current];
        pending.insert(pending.end(), next.begin(), next.end());
    }
    for (const auto &[instance, attachment] : doc.hostedComponents().attachments)
        if (result.contains(instance) != result.contains(attachment->host))
            fail("Place a host and all of its attachments through a common group");
    return result;
}
void checkPreserved(const Document &before, const Document &after, Id target) {
    if (before.bodies().size() != after.bodies().size())
        fail("Site placement changed body identities");
    for (const auto &[bodyId, body] : before.bodies()) {
        auto expected = *body;
        if (bodyId == target)
            expected.transform = after.bodies().at(target)->transform;
        if (*after.bodies().at(bodyId) != expected)
            fail("Site placement changed local geometry or unrelated records");
    }
    if (before.definitions() != after.definitions() || before.instances() != after.instances() ||
        before.hostedComponents() != after.hostedComponents() ||
        before.materials() != after.materials() || before.tags() != after.tags() ||
        before.assets() != after.assets() || before.displayUnits() != after.displayUnits() ||
        before.style() != after.style())
        fail("Site placement changed model resources or component relationships");
}
} // namespace
ModelRecipeResult executeSiteRecipe(Document &doc, const QJsonObject &command) {
    bool valid{};
    const auto target = command.value("body").toString().toULongLong(&valid);
    if (!valid || !doc.bodies().contains(target))
        fail("Site target must be an existing scene context");
    const auto scope = subtree(doc, target);
    const auto unit = command.value("positionUnit").toString().toStdString();
    const auto input = command.value("position").toArray();
    const Vec3 position{meters(input[0].toDouble(), unit), meters(input[1].toDouble(), unit),
                        meters(input[2].toDouble(), unit)};
    const auto frame = command.value("frame").toString();
    const auto yaw = command.value("yawDeltaRadians").toDouble();
    const auto parent = doc.bodies().at(target)->parent;
    const auto parentFrame = parent ? doc.worldTransform(parent) : Transform{};
    const auto beforeWorld = doc.worldTransform(target),
               beforeLocal = doc.bodies().at(target)->transform;
    auto requested =
        Transform::rotation({0, 0, 1}, yaw) * (frame == "world" ? beforeWorld : beforeLocal);
    requested.m[12] = position.x;
    requested.m[13] = position.y;
    requested.m[14] = position.z;
    requested.validate();
    const auto afterLocal = frame == "world" ? parentFrame.inverse() * requested : requested;
    const auto afterWorld = parentFrame * afterLocal;
    checkPoint(afterWorld.point({}));
    Bounds localBounds, worldBounds;
    const auto inverse = beforeWorld.inverse();
    for (const auto bodyId : scope) {
        const auto relative = inverse * doc.worldTransform(bodyId),
                   proposed = afterWorld * relative;
        for (const auto &[vertex, p] : doc.bodies().at(bodyId)->surface.vertices) {
            const auto world = proposed.point(p);
            checkPoint(world);
            worldBounds.include(world);
            localBounds.include(relative.point(p));
        }
    }
    if (!worldBounds.low)
        fail("Site target must contain finite vertex geometry");
    const auto before = doc.readSnapshot();
    RecipeBuilder build(doc);
    const auto verified = build.runBatch(
        {QJsonObject{{"command", "scene.transform"},
                     {"body", id(target)},
                     {"parent", id(parent)},
                     {"matrix", matrix(afterLocal)}},
         assertion(target, "world", "minimum", *worldBounds.low),
         assertion(target, "world", "maximum", *worldBounds.high),
         assertion(target, "local", "dimensions", *localBounds.high - *localBounds.low)});
    checkPreserved(before, doc, target);
    const auto actual =
        frame == "world" ? doc.worldTransform(target) : doc.bodies().at(target)->transform;
    if (length(actual.point({}) - position) > 1e-6)
        fail("Site origin failed frame verification");
    for (int axis = 0; axis < 3; ++axis)
        for (int row = 0; row < 3; ++row) {
            const int i = axis * 4 + row;
            if (std::abs(actual.m[i] - requested.m[i]) >
                1e-10 * std::max(1., std::abs(requested.m[i])))
                fail("Site rotation, scale or shear failed frame verification");
        }
    QJsonArray assertions;
    for (const auto &value : verified["assertions"].toArray()) {
        auto record = value.toObject();
        record["evaluation"] = "recipe_completion";
        assertions.append(record);
    }
    return build.finish({{"recipe", "site-placement-v1"},
                         {"body", id(target)},
                         {"parent", id(parent)},
                         {"frame", frame},
                         {"inputPosition", input},
                         {"positionUnit", QString::fromStdString(unit)},
                         {"positionMeters", point(position)},
                         {"yawDeltaRadians", yaw},
                         {"records", int(scope.size())},
                         {"worldOrigin", point(doc.worldTransform(target).point({}))},
                         {"parentOrigin", point(doc.bodies().at(target)->transform.point({}))},
                         {"beforeParentMatrix", matrix(beforeLocal)},
                         {"afterParentMatrix", matrix(doc.bodies().at(target)->transform)},
                         {"preservedLocalGeometry", true},
                         {"preservedRelationships", true},
                         {"assertions", assertions}});
}
} // namespace sketchy
