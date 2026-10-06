#include "automation/model_recipes.hpp"
#include "automation/component_scope.hpp"
#include "automation/inspection.hpp"
#include "automation/inspection_validation.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "core/hosted_components.hpp"
#include <QJsonDocument>
#include <algorithm>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *message) { throw InspectionError("INVALID_RECIPE", message); }
QString id(Id value) { return QString::number(value); }
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
QJsonArray matrix(Transform transform) {
    QJsonArray result;
    for (auto n : transform.m)
        result.append(n);
    return result;
}
QJsonArray rectangle(double x0, double x1, double y, double z0, double z1) {
    return {point({x0, y, z0}), point({x1, y, z0}), point({x1, y, z1}), point({x0, y, z1})};
}
struct Builder {
    Document &doc;
    ModelRecipeResult result;
    QJsonArray commands;
    explicit Builder(Document &doc) : doc(doc) {}
    QJsonObject run(QJsonObject command) {
        if (command.value("command").toString().startsWith("assembly."))
            fail("Recipes cannot expand recursively");
        commands.append(command);
        if (commands.size() > 100 ||
            QJsonDocument(commands).toJson(QJsonDocument::Compact).size() > 64 * 1024)
            fail("Recipe expansion exceeds bounded command budget");
        auto receipt = executeBatch(doc,
                                    {{"apiVersion", 1},
                                     {"documentId", QString::fromStdString(doc.identity())},
                                     {"expectedRevision", id(doc.revision())},
                                     {"commands", QJsonArray{command}}},
                                    BatchResponse::Changes);
        result.steps.append(receipt);
        doc = doc.readSnapshot(); // No accumulated scratch undo history between expansion steps.
        return receipt;
    }
    Id created(QJsonObject command) {
        const auto reply = run(command);
        const auto ids = reply["created"].toArray();
        if (ids.size() != 1)
            fail("Expected one created recipe body");
        return ids[0].toString().toULongLong();
    }
    void properties(Id body, QJsonObject values) {
        run({{"command", "entity.properties"}, {"body", id(body)}, {"values", values}});
    }
    Id material(QString name, QJsonArray color, double opacity = 1) {
        return run({{"command", "material.create"},
                    {"name", name},
                    {"color", color},
                    {"opacity", opacity}})["createdMaterials"]
            .toArray()[0]
            .toString()
            .toULongLong();
    }
    void paint(Id body, Id material) {
        run({{"command", "material.assign"},
             {"body", id(body)},
             {"material", id(material)},
             {"side", "both"}});
    }
    ModelRecipeResult finish(QJsonObject report) {
        report["expandedCommands"] = commands;
        result.report = report;
        if (QJsonDocument(result.report).toJson(QJsonDocument::Compact).size() > 128 * 1024)
            fail("Recipe report exceeds bound");
        return std::move(result);
    }
};
double number(const QJsonObject &request, const char *key, double fallback) {
    return request.value(key).toDouble(fallback);
}
Id rectangularRegion(const Document &doc, Id body, double cx, double y, double bottom, double width,
                     double height) {
    Id found{};
    const auto &surface = doc.bodies().at(body)->surface;
    for (const auto &[face, record] : surface.faces) {
        if (record.loops.size() != 1 || record.loops[0].size() != 4)
            continue;
        bool match = true;
        for (auto vertex : record.loops[0]) {
            const auto p = surface.vertices.at(vertex);
            match &=
                std::abs(p.y - y) < tolerance &&
                std::abs(std::abs(p.x - cx) - width / 2) < tolerance &&
                (std::abs(p.z - bottom) < tolerance || std::abs(p.z - bottom - height) < tolerance);
        }
        if (match) {
            if (found)
                fail("Opening region is ambiguous");
            found = face;
        }
    }
    if (!found)
        fail("Opening region was not formed by planar drawing");
    return found;
}
struct Opening {
    double cx, width, height, sill;
};
Id createWalls(Builder &build, double w, double d, double t, double h,
               const std::vector<Opening> &openings) {
    auto &doc = build.doc;
    const auto walls = build.created(
        {{"command", "geometry.face"},
         {"name", "Room walls"},
         {"loops", QJsonArray{QJsonArray{point({0, 0, 0}), point({w, 0, 0}), point({w, d, 0}),
                                         point({0, d, 0})},
                              QJsonArray{point({t, t, 0}), point({t, d - t, 0}),
                                         point({w - t, d - t, 0}), point({w - t, t, 0})}}}});
    build.run({{"command", "geometry.extrude_isolated"},
               {"body", id(walls)},
               {"face", id(doc.bodies().at(walls)->surface.faces.begin()->first)},
               {"distance", h}});
    for (const auto &opening : openings) {
        const double cx = opening.cx, ww = opening.width, wh = opening.height, sill = opening.sill;
        build.run({{"command", "geometry.rectangle"},
                   {"body", id(walls)},
                   {"origin", point({cx - ww / 2, 0, sill})},
                   {"normal", point({0, -1, 0})},
                   {"xAxis", point({1, 0, 0})},
                   {"width", ww},
                   {"height", wh}});
        const auto region = rectangularRegion(doc, walls, cx, 0, sill, ww, wh);
        build.run({{"command", "geometry.push_pull"},
                   {"body", id(walls)},
                   {"face", id(region)},
                   {"distance", -t}});
    }
    return walls;
}
ModelRecipeResult room(Document &doc, const QJsonObject &command) {
    if (!doc.bodies().empty() || !doc.definitions().empty())
        fail("Room creation requires an empty geometry document");
    const double w = number(command, "width", 6), d = number(command, "depth", 4),
                 t = number(command, "wallThickness", .2), h = number(command, "height", 2.7),
                 ww = number(command, "windowWidth", 1.2), wh = number(command, "windowHeight", 1),
                 sill = number(command, "sill", .9),
                 member = number(command, "memberThickness", .08),
                 fd = number(command, "frameDepth", .1);
    if (w <= 2 * t || d <= 2 * t || ww <= 2 * member || wh <= 2 * member || sill <= 0 ||
        sill + wh >= h || fd > t || w / 4 - ww / 2 <= t || ww >= w / 2)
        fail("Room, openings and frame dimensions conflict");
    Builder build(doc);
    const auto walls =
        createWalls(build, w, d, t, h, {{w / 4, ww, wh, sill}, {3 * w / 4, ww, wh, sill}});
    const auto floor = build.created(
        {{"command", "geometry.face"},
         {"name", "Floor"},
         {"loops", QJsonArray{QJsonArray{point({t, t, 0}), point({w - t, t, 0}),
                                         point({w - t, d - t, 0}), point({t, d - t, 0})}}}});
    const auto root = build.created({{"command", "group.create"},
                                     {"members", QJsonArray{id(walls), id(floor)}},
                                     {"name", "Recipe room"}});
    const auto plaster = build.material("Warm plaster", {.84, .8, .71}),
               frameMaterial = build.material("Window frame", {.25, .31, .32}),
               glassMaterial = build.material("Glass", {.2, .55, .8}, .35);
    build.paint(walls, plaster);
    build.paint(floor, plaster);
    const double y = (t + fd) / 2;
    auto inner = rectangle(-ww / 2 + member, ww / 2 - member, y, member, wh - member);
    QJsonArray reversed;
    for (auto i = inner.size(); i > 0; --i)
        reversed.append(inner[i - 1]);
    const auto frame =
        build.created({{"command", "geometry.face"},
                       {"name", "Frame"},
                       {"loops", QJsonArray{rectangle(-ww / 2, ww / 2, y, 0, wh), reversed}}});
    build.run({{"command", "geometry.extrude_isolated"},
               {"body", id(frame)},
               {"face", id(doc.bodies().at(frame)->surface.faces.begin()->first)},
               {"distance", fd}});
    build.properties(frame, {{"recipe.role", "frame"}});
    build.paint(frame, frameMaterial);
    const auto glass = build.created(
        {{"command", "geometry.face"},
         {"name", "Glass panel"},
         {"loops",
          QJsonArray{rectangle(-ww / 2 + member, ww / 2 - member, t / 2, member, wh - member)}}});
    build.properties(glass, {{"recipe.role", "glass"}});
    build.paint(glass, glassMaterial);
    const auto first = build.created({{"command", "group.create"},
                                      {"members", QJsonArray{id(frame), id(glass)}},
                                      {"name", "Window A"}});
    const auto component = build
                               .run({{"command", "component.create"},
                                     {"body", id(first)},
                                     {"name", "Recipe window"}})["componentOperations"]
                               .toArray()[0]
                               .toObject();
    const auto definition = component["definition"].toString();
    Transform firstPlacement;
    firstPlacement.m[12] = w / 4;
    firstPlacement.m[14] = sill;
    build.run({{"command", "scene.transform"},
               {"body", id(first)},
               {"matrix", matrix(firstPlacement)},
               {"parent", id(root)}});
    Transform secondPlacement;
    secondPlacement.m[12] = 3 * w / 4;
    secondPlacement.m[14] = sill;
    const Id second = build
                          .run({{"command", "component.instance"},
                                {"definition", definition},
                                {"matrix", matrix(secondPlacement)},
                                {"parent", id(root)},
                                {"name", "Window B"}})["componentOperations"]
                          .toArray()[0]
                          .toObject()["instance"]
                          .toString()
                          .toULongLong();
    QJsonArray windows;
    int slot = 0;
    for (auto window : {first, second}) {
        const double cx = slot ? 3 * w / 4 : w / 4;
        build.properties(window, {{"recipe.kind", "window"},
                                  {"recipe.version", 1},
                                  {"recipe.dimensionConvention", "outer-frame"},
                                  {"recipe.room", id(root)},
                                  {"recipe.wall", id(walls)},
                                  {"recipe.slot", slot},
                                  {"recipe.centerX", cx},
                                  {"recipe.width", ww},
                                  {"recipe.height", wh},
                                  {"recipe.sill", sill},
                                  {"recipe.memberThickness", member},
                                  {"recipe.frameDepth", fd}});
        windows.append(QJsonObject{{"body", id(window)},
                                   {"outerWidth", ww},
                                   {"clearWidth", ww - 2 * member},
                                   {"centerX", cx}});
        ++slot;
    }
    build.properties(root, {{"recipe.kind", "room"},
                            {"recipe.version", 1},
                            {"recipe.width", w},
                            {"recipe.depth", d},
                            {"recipe.height", h},
                            {"recipe.wallThickness", t},
                            {"recipe.wall", id(walls)},
                            {"recipe.floor", id(floor)}});
    build.properties(walls, {{"recipe.role", "host-wall"},
                             {"recipe.room", id(root)},
                             {"recipe.window.0", id(first)},
                             {"recipe.window.1", id(second)}});
    const auto measured = measureEntity(doc, {walls, SelectionKind::Body, 0});
    const double expected = (w * d - (w - 2 * t) * (d - 2 * t)) * h - 2 * ww * wh * t;
    if (!measured.world.volume || std::abs(*measured.world.volume - expected) > 1e-6)
        fail("Room wall volume or through-openings failed verification");
    const auto bounds = measureEntity(doc, {root, SelectionKind::Body, 0}).local.bounds;
    if (!bounds || length(bounds->dimensions() - Vec3{w, d, h}) > tolerance)
        fail("Room dimensions failed verification");
    return build.finish({{"recipe", "room-v1"},
                         {"dimensionConvention", "outer-frame"},
                         {"room", id(root)},
                         {"wall", id(walls)},
                         {"floor", id(floor)},
                         {"windows", windows},
                         {"wallVolume", *measured.world.volume}});
}
// Compare oriented face loops through a one-to-one coordinate mapping. IDs, loop
// start vertices and face ordering may differ; holes and winding must still agree.
void sameShape(const Surface &actual, const Surface &expected) {
    if (actual.vertices.size() != expected.vertices.size() ||
        actual.faces.size() != expected.faces.size() || !actual.wires.empty())
        fail("Authored recipe geometry has changed; restore it before resizing");
    std::map<Id, Id> mapped;
    std::set<Id> used;
    for (const auto &[source, p] : actual.vertices) {
        Id match{};
        for (const auto &[target, q] : expected.vertices)
            if (length(p - q) < tolerance) {
                if (match)
                    fail("Recipe vertex correspondence is ambiguous");
                match = target;
            }
        if (!match || !used.insert(match).second)
            fail("Recipe dimensions no longer match the authored geometry");
        mapped[source] = match;
    }
    using Loops = std::vector<std::vector<Id>>;
    auto faces = [](const Surface &surface, const std::map<Id, Id> *mapping) {
        std::vector<Loops> result;
        for (const auto &[_, face] : surface.faces) {
            Loops loops;
            for (const auto &loop : face.loops) {
                std::vector<Id> normalized;
                for (auto vertex : loop)
                    normalized.push_back(mapping ? mapping->at(vertex) : vertex);
                if (normalized.empty())
                    fail("Recipe face loop is empty");
                std::rotate(normalized.begin(),
                            std::min_element(normalized.begin(), normalized.end()),
                            normalized.end());
                loops.push_back(std::move(normalized));
            }
            if (loops.empty())
                fail("Recipe face is empty");
            std::sort(loops.begin() + 1, loops.end());
            result.push_back(std::move(loops));
        }
        std::sort(result.begin(), result.end());
        return result;
    };
    if (faces(actual, &mapped) != faces(expected, nullptr))
        fail("Recipe face topology or winding has changed");
}
const Body &body(const Document &doc, Id value) {
    if (!doc.bodies().contains(value))
        fail("Recipe references a missing body");
    return *doc.bodies().at(value);
}
const std::variant<bool, double, std::string> &property(const Body &body, const std::string &key) {
    auto found = body.properties.find("recipe." + key);
    if (found == body.properties.end())
        fail("Body is missing authored recipe metadata");
    return found->second;
}
std::string textProperty(const Body &body, const std::string &key) {
    const auto *value = std::get_if<std::string>(&property(body, key));
    if (!value)
        fail("Recipe metadata has an invalid text value");
    return *value;
}
double numericProperty(const Body &body, const std::string &key, double low, double high) {
    const auto *value = std::get_if<double>(&property(body, key));
    if (!value || !std::isfinite(*value) || *value < low || *value > high)
        fail("Recipe metadata has an invalid dimension");
    return *value;
}
Id idProperty(const Body &body, const std::string &key) {
    const auto text = QString::fromStdString(textProperty(body, key));
    bool ok{};
    const Id value = text.toULongLong(&ok);
    if (!ok || !value || id(value) != text)
        fail("Recipe metadata has an invalid body ID");
    return value;
}
void kind(const Body &body, const char *expected) {
    if (textProperty(body, "kind") != expected || numericProperty(body, "version", 1, 1) != 1)
        fail("Unsupported authored recipe version or kind");
}
void sameTransform(const Transform &actual, const Transform &expected) {
    for (size_t i = 0; i < 16; ++i)
        if (std::abs(actual.m[i] - expected.m[i]) > tolerance)
            fail("Recipe placement changed independently of its host");
}
Opening opening(const Body &window) {
    kind(window, "window");
    if (textProperty(window, "dimensionConvention") != "outer-frame")
        fail("Unsupported window dimension convention");
    return {numericProperty(window, "centerX", 0, 100), numericProperty(window, "width", .3, 10),
            numericProperty(window, "height", .3, 5), numericProperty(window, "sill", .05, 5)};
}
Surface frameShape(double width, double height, double member, double depth, double wall) {
    Surface surface;
    const double a = width / 2, y = (wall + depth) / 2;
    const auto face = surface.addFace({{{-a, y, 0}, {a, y, 0}, {a, y, height}, {-a, y, height}},
                                       {{-a + member, y, height - member},
                                        {a - member, y, height - member},
                                        {a - member, y, member},
                                        {-a + member, y, member}}});
    surface.extrude(face, depth);
    return surface;
}
Surface glassShape(double width, double height, double member, double wall) {
    Surface surface;
    const double a = width / 2 - member, y = wall / 2;
    surface.addFace(
        {{{-a, y, member}, {a, y, member}, {a, y, height - member}, {-a, y, height - member}}});
    return surface;
}
QJsonObject vertexShift(const QJsonArray &entities, double x) {
    return {{"command", "geometry.transform_selection"},
            {"entities", entities},
            {"matrix", matrix(Transform::translation({x, 0, 0}))},
            {"space", "local"}};
}
QJsonObject target(Id owner, Id vertex) {
    return {{"body", id(owner)}, {"kind", "vertex"}, {"entity", id(vertex)}};
}
QJsonObject allProperties(const Body &record) {
    QJsonObject result;
    for (const auto &[key, value] : record.properties)
        std::visit(
            [&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>)
                    result[QString::fromStdString(key)] = QString::fromStdString(v);
                else
                    result[QString::fromStdString(key)] = v;
            },
            value);
    return result;
}
using FaceLoops = std::vector<std::vector<Id>>;
std::vector<Id> normalizedLoop(std::vector<Id> loop) {
    if (loop.empty())
        fail("Recipe face loop is empty");
    std::rotate(loop.begin(), std::min_element(loop.begin(), loop.end()), loop.end());
    return loop;
}
FaceLoops normalizedLoops(FaceLoops loops) {
    for (auto &loop : loops)
        loop = normalizedLoop(std::move(loop));
    std::sort(loops.begin() + 1, loops.end());
    return loops;
}
std::map<Id, Id> matchingVertices(const Surface &source, const Surface &target) {
    std::map<Id, Id> result;
    std::set<Id> used;
    for (const auto &[vertex, point] : source.vertices) {
        Id match{};
        for (const auto &[candidate, position] : target.vertices)
            if (length(point - position) < tolerance) {
                if (match)
                    fail("Recipe vertex correspondence is ambiguous");
                match = candidate;
            }
        if (!match || !used.insert(match).second)
            fail("Recipe geometry no longer matches its authored shape");
        result[vertex] = match;
    }
    return result;
}
Surface remappedSurface(const Surface &source, const std::map<Id, Id> &vertices,
                        const std::map<Id, Id> &faces, Id next) {
    Surface result;
    result.nextId = next;
    for (const auto &[id, point] : source.vertices)
        result.vertices[vertices.at(id)] = point;
    for (const auto &[id, face] : source.faces) {
        auto loops = face.loops;
        for (auto &loop : loops)
            for (auto &vertex : loop)
                vertex = vertices.at(vertex);
        result.faces[faces.at(id)] = {faces.at(id), std::move(loops)};
    }
    if (!source.wires.empty())
        fail("Recipe adoption does not accept host wires");
    result.validate();
    return result;
}
ModelRecipeResult adoptRoom(Document &doc, const QJsonObject &command) {
    const Id roomId = command["body"].toString().toULongLong();
    const auto &room = body(doc, roomId);
    kind(room, "room");
    if (room.kind != BodyKind::Group)
        fail("Choose an authored room group");
    for (auto ancestor = roomId; ancestor; ancestor = body(doc, ancestor).parent)
        if (doc.instances().contains(ancestor))
            fail("A shared component room needs an independent scope before adoption");
    const Id wallId = idProperty(room, "wall");
    const auto originalWall = doc.bodies().at(wallId);
    const auto &wall = *originalWall;
    if (wall.kind != BodyKind::Geometry || wall.parent != roomId ||
        idProperty(wall, "room") != roomId || textProperty(wall, "role") != "host-wall" ||
        !wall.curves.empty() || !wall.guides.empty() || persistentlyLocked(doc, wallId) ||
        doc.hostedComponents().hosts.contains(wallId))
        fail("Choose an unlocked, unbound authored room wall");
    sameTransform(wall.transform, {});
    const double w = numericProperty(room, "width", 2, 100),
                 d = numericProperty(room, "depth", 2, 100),
                 h = numericProperty(room, "height", 1, 10),
                 t = numericProperty(room, "wallThickness", .05, 1);
    if (w <= 2 * t || d <= 2 * t)
        fail("Authored room dimensions conflict");
    struct Placement {
        Id root{}, definition{};
        double inset{};
        QJsonObject glue;
    };
    std::vector<Placement> placements;
    std::vector<Opening> openings;
    std::set<Id> roots, definitions;
    for (int slot = 0; slot < 2; ++slot) {
        const Id root = idProperty(wall, "window." + std::to_string(slot));
        const auto &record = body(doc, root);
        const auto region = opening(record);
        if (!roots.insert(root).second || !doc.instances().contains(root) ||
            record.parent != roomId || idProperty(record, "room") != roomId ||
            idProperty(record, "wall") != wallId || numericProperty(record, "slot", 0, 1) != slot ||
            persistentlyLocked(doc, root) || doc.hostedComponents().attachments.contains(root))
            fail("Authored window bindings are missing, locked or already adopted");
        sameTransform(record.transform, Transform::translation({region.cx, 0, region.sill}));
        const double member = numericProperty(record, "memberThickness", .01, .5),
                     depth = numericProperty(record, "frameDepth", .01, 1);
        if (depth > t || region.width <= 2 * member || region.height <= 2 * member ||
            region.cx - region.width / 2 <= t || region.cx + region.width / 2 >= w - t ||
            region.sill + region.height >= h)
            fail("Authored window dimensions conflict with its wall");
        Id frame{}, glass{};
        for (const auto &[part, value] : doc.bodies())
            if (value->parent == root) {
                if (value->kind != BodyKind::Geometry || !value->curves.empty() ||
                    !value->guides.empty() || persistentlyLocked(doc, part))
                    fail("Window contains unsupported or locked members");
                sameTransform(value->transform, {});
                const auto role = textProperty(*value, "role");
                if (role == "frame" && !frame)
                    frame = part;
                else if (role == "glass" && !glass)
                    glass = part;
                else
                    fail("Window contains additional or ambiguous members");
            }
        const auto &instance = *doc.instances().at(root);
        if (!frame || !glass || instance.members.size() != 3 ||
            doc.definitions().at(instance.definition)->glue)
            fail("Adoption requires the unmodified authored frame and glass without existing glue");
        sameShape(body(doc, frame).surface,
                  frameShape(region.width, region.height, member, depth, t));
        sameShape(body(doc, glass).surface, glassShape(region.width, region.height, member, t));
        const auto y = (t + depth) / 2;
        Id face{}, canonical{};
        for (const auto &[candidate, value] : body(doc, frame).surface.faces)
            if (value.loops.size() == 2 &&
                std::all_of(value.loops.front().begin(), value.loops.front().end(), [&](Id v) {
                    return std::abs(body(doc, frame).surface.vertices.at(v).y - y) < tolerance;
                })) {
                if (face)
                    fail("Authored frame alignment face is ambiguous");
                face = candidate;
            }
        for (const auto &[memberId, placed] : instance.members)
            if (placed == frame)
                canonical = memberId;
        if (!face || !canonical)
            fail("Authored frame lacks a canonical alignment face");
        placements.push_back({root,
                              instance.definition,
                              -y,
                              {{"member", id(canonical)},
                               {"face", id(face)},
                               {"anchor", point({0, y, region.height / 2})},
                               {"tangent", point({1, 0, 0})},
                               {"cutsOpening", true}}});
        definitions.insert(instance.definition);
        openings.push_back(region);
    }
    if (openings[0].cx + openings[0].width / 2 >= openings[1].cx - openings[1].width / 2)
        fail("Authored host openings overlap");
    for (const auto &[root, instance] : doc.instances())
        if (definitions.contains(instance->definition) && !roots.contains(root))
            fail(
                "Make the room windows unique before adopting a definition used outside this room");
    Document expected;
    Builder reference(expected);
    const auto expectedWall = createWalls(reference, w, d, t, h, openings);
    sameShape(wall.surface, body(expected, expectedWall).surface);

    // Reconstruct only a validated recipe baseline, retaining the live wall's
    // original corner and face IDs. No caller-supplied replacement surface exists.
    Document uncutDocument;
    Builder uncutBuilder(uncutDocument);
    const auto uncutId = createWalls(uncutBuilder, w, d, t, h, {});
    const auto &referenceSurface = body(uncutDocument, uncutId).surface;
    const auto baselineVertices = matchingVertices(referenceSurface, wall.surface);
    std::map<std::vector<Id>, Id> actualBoundaries;
    for (const auto &[face, value] : wall.surface.faces)
        if (!actualBoundaries.emplace(normalizedLoop(value.loops.front()), face).second)
            fail("Authored wall boundaries are ambiguous");
    std::map<Id, Id> baselineFaces;
    for (const auto &[face, value] : referenceSurface.faces) {
        auto loop = value.loops.front();
        for (auto &vertex : loop)
            vertex = baselineVertices.at(vertex);
        const auto key = normalizedLoop(std::move(loop));
        if (!actualBoundaries.contains(key))
            fail("Authored wall lacks an original face boundary");
        baselineFaces[face] = actualBoundaries.at(key);
    }
    const auto uncut =
        remappedSurface(referenceSurface, baselineVertices, baselineFaces, wall.surface.nextId);
    Id entry{};
    for (const auto &[face, value] : uncut.faces)
        if (uncut.normal(face).y < -.99 &&
            std::abs(uncut.vertices.at(value.loops.front().front()).y) < tolerance)
            entry = face;
    if (!entry)
        fail("Authored room lacks its exterior front wall face");
    Builder build(doc);
    std::set<Id> configured;
    for (const auto &placement : placements)
        if (configured.insert(placement.definition).second)
            build.run({{"command", "component.glue"},
                       {"definition", id(placement.definition)},
                       {"glue", placement.glue}});
    auto scratch = doc.readSnapshot();
    auto baseline = std::make_shared<Body>();
    baseline->id = wallId;
    baseline->parent = wall.parent;
    baseline->transform = wall.transform;
    baseline->surface = uncut;
    baseline->topology = Topology::rebuild(uncut, wall.topology);
    scratch.apply({"Validate recipe baseline", {{wallId, scratch.bodies().at(wallId), baseline}}},
                  scratch.revision());
    for (const auto &placement : placements)
        bindComponentAtCurrentPose(scratch, placement.root, wallId, entry, placement.inset);
    const auto &generated = scratch.bodies().at(wallId)->surface;
    sameShape(generated, wall.surface);
    const auto vertices = matchingVertices(generated, wall.surface);
    std::map<FaceLoops, Id> actualFaces;
    for (const auto &[face, value] : wall.surface.faces)
        if (!actualFaces.emplace(normalizedLoops(value.loops), face).second)
            fail("Authored wall faces are ambiguous");
    std::map<Id, Id> faces;
    for (const auto &[face, value] : generated.faces) {
        auto loops = value.loops;
        for (auto &loop : loops)
            for (auto &vertex : loop)
                vertex = vertices.at(vertex);
        const auto key = normalizedLoops(std::move(loops));
        if (!actualFaces.contains(key))
            fail("Adoption changed an authored wall face");
        faces[face] = actualFaces.at(key);
    }
    auto records = std::make_shared<HostedComponents>(doc.hostedComponents());
    auto host = std::make_shared<HostedSurface>(*scratch.hostedComponents().hosts.at(wallId));
    for (auto &[owner, opening] : host->openings) {
        opening.profile.face = faces.at(opening.profile.face);
        opening.exit = faces.at(opening.exit);
        for (auto &[key, pair] : opening.vertices)
            for (auto &vertex : pair)
                vertex = vertices.at(vertex);
        for (auto &[key, face] : opening.jambs)
            face = faces.at(face);
    }
    records->hosts[wallId] = host;
    QJsonArray adopted;
    for (const auto &placement : placements) {
        records->attachments[placement.root] =
            scratch.hostedComponents().attachments.at(placement.root);
        adopted.append(id(placement.root));
    }
    auto canonicalWall = std::make_shared<Body>(wall);
    canonicalWall->surface = remappedSurface(generated, vertices, faces, wall.surface.nextId);
    canonicalWall->topology = Topology::rebuild(canonicalWall->surface, wall.topology);
    Edit edit{"Adopt authored room attachments",
              {{wallId, doc.bodies().at(wallId), canonicalWall}}};
    edit.hosted = HostedChange{doc.hostedRecords(), records};
    edit.hostedResolved = true;
    doc.apply(std::move(edit), doc.revision());
    return build.finish({{"recipe", "room-hosted-adoption-v1"},
                         {"room", id(roomId)},
                         {"wall", id(wallId)},
                         {"adoptedAttachments", adopted},
                         {"preservedVertices", int(wall.surface.vertices.size())},
                         {"preservedFaces", int(wall.surface.faces.size())}});
}
ModelRecipeResult resizeWindow(Document &doc, const QJsonObject &command) {
    const Id selected = command.value("body").toString().toULongLong();
    const auto before = doc.readSnapshot();
    const auto &window = body(before, selected);
    const auto old = opening(window);
    const double width = command.value("width").toDouble();
    const double delta = width - old.width;
    if (std::abs(delta) < tolerance)
        fail("Window already has the requested outer width");
    if (!doc.instances().contains(selected))
        fail("Window must be a component instance");
    const Id roomId = idProperty(window, "room"), wallId = idProperty(window, "wall");
    const bool adopted = doc.hostedComponents().hosts.contains(wallId);
    const auto &room = body(before, roomId), &wall = body(before, wallId);
    kind(room, "room");
    if (room.kind != BodyKind::Group || window.parent != roomId || wall.parent != roomId ||
        idProperty(room, "wall") != wallId || idProperty(wall, "room") != roomId ||
        textProperty(wall, "role") != "host-wall")
        fail("Window and host bindings no longer agree");
    for (Id ancestor = roomId; ancestor; ancestor = body(before, ancestor).parent)
        if (doc.instances().contains(ancestor))
            fail("A shared component host room needs an explicit independent scope");
    if (persistentlyLocked(doc, selected) || persistentlyLocked(doc, wallId))
        fail("Window or host wall is locked");
    sameTransform(wall.transform, {});
    const auto world = doc.worldTransform(roomId);
    const Vec3 x = world.vector({1, 0, 0}), y = world.vector({0, 1, 0}),
               z = world.vector({0, 0, 1});
    if (std::abs(length(x) - 1) > tolerance || std::abs(length(y) - 1) > tolerance ||
        std::abs(length(z) - 1) > tolerance || std::abs(dot(x, y)) > tolerance ||
        std::abs(dot(x, z)) > tolerance || std::abs(dot(y, z)) > tolerance)
        fail("Resize requires an unscaled rigid host frame; rotation and reflection are supported");
    const double w = numericProperty(room, "width", 2, 100),
                 d = numericProperty(room, "depth", 2, 100),
                 h = numericProperty(room, "height", 1, 10),
                 t = numericProperty(room, "wallThickness", .05, 1),
                 member = numericProperty(window, "memberThickness", .01, .5),
                 depth = numericProperty(window, "frameDepth", .01, 1);
    if (w <= 2 * t || d <= 2 * t || depth > t || old.height <= 2 * member ||
        old.width <= 2 * member || width <= 2 * member)
        fail("Window members or host dimensions conflict");
    std::vector<Opening> openings;
    int selectedSlot = -1;
    for (int slot = 0; slot < 2; ++slot) {
        const Id instance = idProperty(wall, "window." + std::to_string(slot));
        const auto &record = body(before, instance);
        const auto region = opening(record);
        if (!doc.instances().contains(instance) || record.parent != roomId ||
            idProperty(record, "room") != roomId || idProperty(record, "wall") != wallId ||
            numericProperty(record, "slot", 0, 1) != slot)
            fail("Host opening bindings no longer agree");
        const auto attached = doc.hostedComponents().attachments.find(instance);
        if ((attached != doc.hostedComponents().attachments.end()) != adopted ||
            (adopted && (attached->second->host != wallId ||
                         !doc.hostedComponents().hosts.at(wallId)->openings.contains(instance))))
            fail("Authored window and general host relationships no longer agree");
        sameTransform(record.transform, Transform::translation({region.cx, 0, region.sill}));
        if (region.cx - region.width / 2 <= t || region.cx + region.width / 2 >= w - t ||
            region.sill + region.height >= h)
            fail("Authored opening is outside the host wall");
        if (instance == selected)
            selectedSlot = slot;
        openings.push_back(region);
    }
    if (selectedSlot < 0)
        fail("Selected window is not bound to a host opening");
    if (openings[0].cx + openings[0].width / 2 >= openings[1].cx - openings[1].width / 2)
        fail("Host openings overlap");
    auto widened = openings;
    widened[selectedSlot].width = width;
    if (old.cx - width / 2 <= t || old.cx + width / 2 >= w - t ||
        widened[0].cx + widened[0].width / 2 >= widened[1].cx - widened[1].width / 2)
        fail("Requested width reaches another opening or a wall corner");
    Id frame{}, glass{};
    for (const auto &[key, record] : doc.bodies())
        if (record->parent == selected) {
            if (record->kind != BodyKind::Geometry || !record->curves.empty() ||
                !record->guides.empty() || persistentlyLocked(doc, key))
                fail("Window contains unsupported or locked members");
            sameTransform(record->transform, {});
            const auto role = textProperty(*record, "role");
            if (role == "frame" && !frame)
                frame = key;
            else if (role == "glass" && !glass)
                glass = key;
            else
                fail("Window contains additional or ambiguous members");
        }
    if (!frame || !glass || doc.instances().at(selected)->members.size() != 3)
        fail("Window must contain its authored frame and glass members");
    sameShape(body(doc, frame).surface, frameShape(old.width, old.height, member, depth, t));
    sameShape(body(doc, glass).surface, glassShape(old.width, old.height, member, t));
    if (!wall.curves.empty() || !wall.guides.empty())
        fail("Host wall has unsupported authored additions");
    Document expected;
    Builder reference(expected);
    const Id expectedWall = createWalls(reference, w, d, t, h, openings);
    sameShape(wall.surface, body(expected, expectedWall).surface);
    if (adopted) {
        Document uncut;
        Builder referenceUncut(uncut);
        const auto referenceWall = createWalls(referenceUncut, w, d, t, h, {});
        sameShape(doc.hostedComponents().hosts.at(wallId)->uncut,
                  body(uncut, referenceWall).surface);
        if (doc.hostedComponents().hosts.at(wallId)->openings.size() != openings.size())
            fail("Authored wall has additional hosted openings");
    }
    const auto volumeBefore = measureEntity(doc, {wallId, SelectionKind::Body, 0}).local.volume;
    if (!volumeBefore)
        fail("Host wall must remain a closed solid");
    QJsonArray hostLeft, hostRight, memberLeft, memberRight;
    for (const auto &[vertex, p] : wall.surface.vertices) {
        if ((std::abs(p.y) < tolerance || std::abs(p.y - t) < tolerance) &&
            (std::abs(p.z - old.sill) < tolerance ||
             std::abs(p.z - old.sill - old.height) < tolerance)) {
            if (std::abs(p.x - old.cx + old.width / 2) < tolerance)
                hostLeft.append(target(wallId, vertex));
            if (std::abs(p.x - old.cx - old.width / 2) < tolerance)
                hostRight.append(target(wallId, vertex));
        }
    }
    if (hostLeft.size() != 4 || hostRight.size() != 4)
        fail("Host opening corners are ambiguous");
    for (auto part : {frame, glass})
        for (const auto &[vertex, p] : body(doc, part).surface.vertices)
            (p.x < 0 ? memberLeft : memberRight).append(target(part, vertex));
    Builder build(doc);
    const Id definitionBefore = doc.instances().at(selected)->definition;
    const bool shared =
        std::count_if(doc.instances().begin(), doc.instances().end(), [&](const auto &entry) {
            return entry.second->definition == definitionBefore;
        }) > 1;
    if (shared)
        build.run({{"command", "component.make_unique"}, {"body", id(selected)}});
    build.run(componentScopeCommand(
        doc, selected, {vertexShift(memberLeft, -delta / 2), vertexShift(memberRight, delta / 2)}));
    if (!adopted) {
        build.run(vertexShift(hostLeft, -delta / 2));
        build.run(vertexShift(hostRight, delta / 2));
    }
    auto properties = allProperties(body(doc, selected));
    properties["recipe.width"] = width;
    build.properties(selected, properties);
    sameShape(body(doc, frame).surface, frameShape(width, old.height, member, depth, t));
    sameShape(body(doc, glass).surface, glassShape(width, old.height, member, t));
    Document expectedAfter;
    Builder referenceAfter(expectedAfter);
    const auto newWall = createWalls(referenceAfter, w, d, t, h, widened);
    sameShape(body(doc, wallId).surface, body(expectedAfter, newWall).surface);
    const auto volume = measureEntity(doc, {wallId, SelectionKind::Body, 0}).local.volume;
    if (!volume || std::abs(*volume - (*volumeBefore - delta * old.height * t)) > 1e-6)
        fail("Resized opening failed solid volume verification");
    const std::set<Id> changed{selected, frame, glass, wallId};
    if (before.bodies().size() != doc.bodies().size())
        fail("Resize changed unrelated body membership");
    for (const auto &[key, record] : before.bodies())
        if (!changed.contains(key) && *record != body(doc, key))
            fail("Resize changed an unrelated body");
    for (const auto &[key, record] : before.definitions())
        if ((shared || key != definitionBefore) && record != doc.definitions().at(key))
            fail("Resize changed an unrelated shared definition");
    if (before.materials() != doc.materials() || before.assets() != doc.assets() ||
        before.tags() != doc.tags())
        fail("Resize changed unrelated appearance or organization records");
    return build.finish({{"recipe", "window-resize-v1"},
                         {"scope", "instance"},
                         {"dimensionConvention", "outer-frame"},
                         {"body", id(selected)},
                         {"wall", id(wallId)},
                         {"madeUnique", shared},
                         {"definitionBefore", id(definitionBefore)},
                         {"definitionAfter", id(doc.instances().at(selected)->definition)},
                         {"previousOuterWidth", old.width},
                         {"outerWidth", width},
                         {"clearWidth", width - 2 * member},
                         {"height", old.height},
                         {"centerX", old.cx},
                         {"sill", old.sill},
                         {"memberThickness", member},
                         {"wallVolume", *volume}});
}
} // namespace
ModelRecipeResult executeModelRecipe(Document &doc, const QJsonObject &command) {
    inspection_detail::validateParameters(
        command, commandDescription(command.value("command").toString())["parameters"].toObject());
    if (command.value("command") == "assembly.room")
        return room(doc, command);
    if (command.value("command") == "assembly.room.adopt_hosted")
        return adoptRoom(doc, command);
    if (command.value("command") == "assembly.window.resize")
        return resizeWindow(doc, command);
    fail("Unsupported modeling recipe");
}
} // namespace sketchy
