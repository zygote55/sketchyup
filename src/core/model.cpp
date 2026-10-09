#include "core/model.hpp"
#include "core/appearance.hpp"
#include "core/component_validation.hpp"
#include "core/edge_appearance.hpp"
#include "core/face_textures.hpp"
#include "core/reference_images.hpp"
#include "geometry/offset.hpp"
#include <algorithm>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
namespace sketchy {
Document Document::readSnapshot() const {
    Document result(displayUnits_);
    result.style_ = style_;
    result.solar_ = solar_;
    result.identity_ = identity_;
    result.bodies_ = bodies_;
    result.nextId_ = nextId_;
    result.definitions_ = definitions_;
    result.instances_ = instances_;
    result.hosted_ = hosted_;
    result.nextDefinitionId_ = nextDefinitionId_;
    result.tags_ = tags_;
    result.nextTagId_ = nextTagId_;
    result.materials_ = materials_;
    result.nextMaterialId_ = nextMaterialId_;
    result.assets_ = assets_;
    result.nextAssetId_ = nextAssetId_;
    result.scenes_ = scenes_;
    result.nextSceneId_ = nextSceneId_;
    result.sections_ = sections_;
    result.nextSectionId_ = nextSectionId_;
    result.activeSections_ = activeSections_;
    result.annotations_ = annotations_;
    result.nextAnnotationId_ = nextAnnotationId_;
    result.definitionFloors_ = definitionFloors_;
    result.surfaceFloors_ = surfaceFloors_;
    result.edgeFloors_ = edgeFloors_;
    result.revision_ = revision_;
    result.session_ = session_;
    result.state_ = state_;
    result.savedState_ = savedState_;
    return result;
}
namespace {
size_t bytes(const BodyPtr &body);
size_t componentBytes(const DefinitionPtr &definition) {
    if (!definition)
        return 0;
    size_t result =
        sizeof(ComponentDefinition) + definition->name.size() + definition->references.size() * 96;
    for (const auto &[id, body] : definition->members)
        result += bytes(body) + 64;
    return result;
}
size_t componentBytes(const InstancePtr &instance) {
    return instance ? sizeof(ComponentInstance) + instance->members.size() * 96 : 0;
}
template <class Floor>
DefinitionPtr freezeDefinition(const DefinitionPtr &source, const Floor &floor) {
    auto definition = std::make_shared<ComponentDefinition>(*source);
    definition->nextMemberId = std::max(definition->nextMemberId, floor.nextMemberId);
    for (auto &[id, record] : definition->members) {
        if (!record)
            throw std::runtime_error("Null component definition member");
        auto body = std::make_shared<Body>(*record);
        if (floor.geometry.contains(id)) {
            body->surface.nextId = std::max(body->surface.nextId, floor.geometry.at(id).first);
            body->topology.nextId = std::max(body->topology.nextId, floor.geometry.at(id).second);
        }
        record = std::move(body);
    }
    return definition;
}
size_t bytes(const BodyPtr &b) {
    if (!b)
        return 0;
    size_t n = sizeof(Body) + b->name.size() + b->surface.vertices.size() * (sizeof(Vec3) + 64) +
               b->surface.wires.size() * sizeof(std::array<Id, 2>) +
               b->topology.edges.size() * (sizeof(EdgeRecord) + 64);
    if (b->textSource) n += textSourceBytes(*b->textSource);
    n += b->edgeAppearances.size() * (sizeof(Id) + sizeof(EdgeAppearance) + 64);
    n += b->faceColors.size() * (sizeof(Id) + sizeof(std::array<float, 3>) + 64);
    n += b->faceMaterials.size() * (sizeof(Id) + sizeof(MaterialSides) + 64);
    n += b->faceTextureMappings.size() * (sizeof(Id) + sizeof(TextureMappingSides) + 64);
    n += b->guides.size() * (sizeof(Guide) + 64);
    for (const auto &[id, curve] : b->curves)
        n += sizeof(Curve) + 64 + curve.edges.size() * sizeof(OrientedEdge);
    for (const auto &[id, f] : b->surface.faces) {
        n += sizeof(Face) + 64;
        for (const auto &l : f.loops)
            n += sizeof(l) + l.size() * sizeof(Id);
    }
    for (const auto &[key, value] : b->properties) {
        n += key.size() + sizeof(value) + 96;
        if (auto text = std::get_if<std::string>(&value))
            n += text->size();
    }
    return n;
}
Transform worldTransformIn(const std::map<Id, BodyPtr> &bodies, Id id) {
    Transform result;
    std::set<Id> visited;
    while (id) {
        if (!visited.insert(id).second || visited.size() > 128)
            throw std::runtime_error("Cyclic or excessively deep hierarchy");
        auto found = bodies.find(id);
        if (found == bodies.end())
            throw std::runtime_error("Missing parent entity");
        result = found->second->transform * result;
        id = found->second->parent;
    }
    return result;
}
void validateDocumentSize(const std::map<Id, BodyPtr> &bodies,
                          const std::map<Id, BodyPtr> *baseline = nullptr) {
    size_t vertices = 0, faces = 0, wires = 0, edges = 0, curves = 0, guides = 0;
    for (const auto &[id, b] : bodies) {
        vertices += b->surface.vertices.size();
        faces += b->surface.faces.size();
        wires += b->surface.wires.size();
        edges += b->topology.edges.size();
        curves += b->curves.size();
        guides += b->guides.size();
        if (b->parent && bodies.contains(b->parent) && bodies.at(b->parent)->referenceImage)
            throw std::runtime_error("Reference images cannot own child entities");
        const auto world = worldTransformIn(bodies, id);
        // worldTransformIn still validates every candidate parent chain. World
        // bounds need another scan only when this record or an ancestor changed.
        bool unchanged = baseline != nullptr;
        for (Id ancestor = id; unchanged && ancestor; ancestor = bodies.at(ancestor)->parent)
            unchanged =
                baseline->contains(ancestor) && baseline->at(ancestor) == bodies.at(ancestor);
        if (unchanged)
            continue;
        if (b->referenceImage)
            referenceImageCorners(*b->referenceImage, world);
        for (const auto &[vertex, point] : b->surface.vertices)
            checkPoint(world.point(point));
        for (const auto &[guideId, guide] : b->guides) {
            checkPoint(world.point(guide.origin));
            if (guide.kind == GuideKind::Line) {
                const auto n = length(world.vector(guide.direction));
                if (!std::isfinite(n) || n == 0)
                    throw std::runtime_error("Guide direction is singular in world space");
            }
        }
    }
    if (bodies.size() > 10000 || vertices > 100000 || faces > 100000 || wires > 100000 ||
        edges > Topology::edgeLimit || curves > 10000 || guides > 10000)
        throw std::runtime_error("Document complexity exceeds editing limits");
}
void validate(const Body &b) {
    if (b.textSource) {
        validateTextSource(*b.textSource);
    }
    if (!b.id || b.name.size() > 1024)
        throw std::runtime_error("Invalid body identity or name");
    for (float c : b.color)
        if (!std::isfinite(c) || c < 0 || c > 1)
            throw std::runtime_error("Invalid material color");
    if (b.referenceImage.has_value() != (b.kind == BodyKind::ReferenceImage))
        throw std::runtime_error("Reference image kind requires exactly one image record");
    if (b.referenceImage) {
        b.referenceImage->validate();
        if (!b.surface.vertices.empty() || !b.surface.faces.empty() || !b.surface.wires.empty() ||
            !b.topology.edges.empty() || !b.curves.empty() || !b.guides.empty() || b.textSource ||
            b.materials != MaterialSides{} || !b.faceColors.empty() || !b.faceMaterials.empty() ||
            !b.faceTextureMappings.empty() || !b.edgeAppearances.empty())
            throw std::runtime_error("Reference images cannot own modeled geometry or materials");
    }
    if (b.kind != BodyKind::Geometry && b.kind != BodyKind::Group &&
        b.kind != BodyKind::ReferenceImage)
        throw std::runtime_error("Unknown entity kind");
    for (const auto &[id, color] : b.faceColors) {
        if (!b.surface.faces.contains(id))
            throw std::runtime_error("Face color references a missing face");
        for (auto component : color)
            if (!std::isfinite(component) || component < 0 || component > 1)
                throw std::runtime_error("Invalid face color");
    }
    if (b.edgeAppearances.size() > Topology::edgeLimit)
        throw std::runtime_error("Too many edge appearance records");
    validateFaceTextureMappings(b);
    b.transform.validate();
    if (b.properties.size() > 128)
        throw std::runtime_error("Too many entity properties");
    for (const auto &[key, value] : b.properties) {
        if (key.empty() || key.size() > 128)
            throw std::runtime_error("Invalid property key");
        std::visit(
            [](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, double>) {
                    if (!std::isfinite(v))
                        throw std::runtime_error("Property number must be finite");
                } else if constexpr (std::is_same_v<T, std::string>) {
                    if (v.size() > 2048)
                        throw std::runtime_error("Property string exceeds limit");
                }
            },
            value);
    }
    b.surface.validate();
    validateGuides(b.guides, b.surface);
    for (const auto &[id, guide] : b.guides)
        if (b.curves.contains(id))
            throw std::runtime_error("Guide identity collides with a curve");
}
} // namespace
size_t Document::readSnapshotBytes() const {
    size_t result = sizeof(Document) + identity_.size() + 256 + hostedComponentBytes(hosted_);
    for (const auto &[id, body] : bodies_)
        result += bytes(body) + 64;
    for (const auto &[id, definition] : definitions_)
        result += componentBytes(definition) + 64;
    for (const auto &[id, instance] : instances_)
        result += componentBytes(instance) + 64;
    for (const auto &[id, tag] : tags_)
        result += sizeof(TagRecord) + tag->name.size() + 64;
    for (const auto &[id, material] : materials_)
        result += sizeof(MaterialRecord) + material->name.size() + 64;
    for (const auto &[id, asset] : assets_)
        result += assetBytes(asset) + 64;
    for (const auto &[id, scene] : scenes_)
        result += sceneBytes(scene) + 64;
    for (const auto &[id, section] : sections_)
        result += sectionBytes(section) + 64;
    result += activeSections_.size() * 64;
    for (const auto &[id, annotation] : annotations_)
        result += annotationBytes(annotation) + 64;
    result += (surfaceFloors_.size() + edgeFloors_.size()) * 96;
    for (const auto &[id, floor] : definitionFloors_)
        result += sizeof(DefinitionFloor) + 64 + floor.geometry.size() * 96;
    return result;
}
Document::Document(DisplayUnit units) : displayUnits_(units) {
    unitCode(units);
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i)
        out << std::setw(8) << random();
    identity_ = out.str();
}
void Document::setDisplayUnits(DisplayUnit units) {
    unitCode(units);
    if (units == displayUnits_)
        return;
    Edit edit{"Change document units", {}};
    edit.displayUnits = std::pair{displayUnits_, units};
    apply(std::move(edit), revision_);
}
void Document::setSolar(const SolarSettings &solar) {
    solar.validate();
    if (solar == solar_) return;
    Edit edit{"Change sun study", {}};
    edit.solar = std::pair{solar_, solar};
    apply(std::move(edit), revision_);
}
void Document::setStyle(const ModelStyle &style) {
    style.validate();
    if (style == style_)
        return;
    Edit edit{"Change model style", {}};
    edit.style = std::pair{style_, style};
    apply(std::move(edit), revision_);
}
Id Document::addFace(const std::vector<std::vector<Vec3>> &loops, std::string name) {
    auto b = std::make_shared<Body>();
    b->id = nextId_;
    b->name = std::move(name);
    b->surface.addFace(loops);
    apply({"Draw face", {{b->id, nullptr, b}}}, revision_);
    return b->id;
}
Id Document::addWire(Id context, Vec3 a, Vec3 b) {
    BodyPtr old = context ? bodies_.at(context) : nullptr;
    auto body = old ? std::make_shared<Body>(*old) : std::make_shared<Body>();
    if (!old) {
        body->id = nextId_;
        body->name = "Edges";
    }
    const auto first = body->surface.vertex(a), second = body->surface.vertex(b);
    if (first == second)
        throw std::runtime_error("Edge endpoints coincide");
    body->surface.wires.push_back({first, second});
    apply({"Draw edge", {{body->id, old, body}}}, revision_);
    return body->id;
}
ChangeReport Document::insertEdges(Id context, Vec3 origin, Vec3 normal,
                                   const std::vector<std::array<Vec3, 2>> &edges,
                                   std::string name) {
    BodyPtr old = context ? bodies_.at(context) : nullptr;
    auto body = old ? std::make_shared<Body>(*old) : std::make_shared<Body>();
    if (!old) {
        body->id = nextId_;
        body->name = std::move(name);
    }
    auto result = insertPlanarEdges(body->surface, origin, normal, edges);
    if (old && result.surface == old->surface)
        return {};
    body->surface = std::move(result.surface);
    if (old)
        inheritFaceAppearance(*old, *body, result.faces);
    return apply({"Insert planar edges", {{body->id, old, body, std::move(result.faces)}}},
                 revision_);
}
ChangeReport Document::addCurve(Id context, Curve curve) {
    const auto chords = curve.chords();
    BodyPtr old = context ? bodies_.at(context) : nullptr;
    auto body = old ? std::make_shared<Body>(*old) : std::make_shared<Body>();
    if (!old) {
        body->id = nextId_;
        body->name = curve.kind == CurveKind::Circle ? "Circle"
                     : curve.kind == CurveKind::Pie  ? "Pie"
                                                     : "Arc";
    }
    const auto normal =
        DrawingPlane::make(curve.center, cross(curve.xAxis, curve.yAxis), curve.xAxis).normal;
    auto result = insertPlanarEdges(body->surface, curve.center, normal, chords);
    body->surface = std::move(result.surface);
    if (old)
        inheritFaceAppearance(*old, *body, result.faces);
    body->topology = Topology::rebuild(body->surface, body->topology);
    size_t budget = 1000000;
    if (!bindCurve(curve, body->surface, body->topology, budget))
        throw std::runtime_error("Curve outline could not be associated with editable edges");
    if (body->surface.nextId == UINT64_MAX)
        throw std::runtime_error("Curve identity space exhausted");
    body->curves.emplace(body->surface.nextId++, std::move(curve));
    return apply({"Draw curve", {{body->id, old, body, std::move(result.faces)}}}, revision_);
}
ChangeReport Document::addGuide(Id context, Guide guide) {
    guide.validate();
    const BodyPtr old = context ? bodies_.at(context) : nullptr;
    auto body = old ? std::make_shared<Body>(*old) : std::make_shared<Body>();
    if (!old) {
        body->id = nextId_;
        body->name = "Guides";
    }
    if (body->surface.nextId == UINT64_MAX)
        throw std::runtime_error("Guide identity space exhausted");
    body->guides.emplace(body->surface.nextId++, guide);
    return apply({"Create guide", {{body->id, old, body}}}, revision_);
}
ChangeReport Document::eraseGuide(Id context, Id id) {
    const auto old = bodies_.at(context);
    if (!old->guides.contains(id))
        throw std::runtime_error("Guide does not exist");
    auto body = std::make_shared<Body>(*old);
    body->guides.erase(id);
    return apply({"Delete guide", {{context, old, body}}}, revision_);
}
ChangeReport Document::clearGuides(Id context) {
    if (context && !bodies_.contains(context))
        throw std::runtime_error("Guide context does not exist");
    Edit edit{"Delete guides", {}};
    for (const auto &[id, old] : bodies_) {
        if ((context && context != id) || old->guides.empty())
            continue;
        auto body = std::make_shared<Body>(*old);
        body->guides.clear();
        edit.changes.push_back({id, old, body});
    }
    std::set<Id> placedDefinitions;
    for (const auto &[root, instance] : instances_)
        placedDefinitions.insert(instance->definition);
    if (!context)
        for (const auto &[id, old] : definitions_) {
            if (!placedDefinitions.contains(id))
                continue;
            auto definition = std::make_shared<ComponentDefinition>(*old);
            bool changed = false;
            for (auto &[member, body] : definition->members)
                if (!body->guides.empty()) {
                    auto cleared = std::make_shared<Body>(*body);
                    cleared->guides.clear();
                    body = cleared;
                    changed = true;
                }
            if (changed)
                edit.definitions.push_back({id, old, definition});
        }
    return edit.changes.empty() && edit.definitions.empty() ? ChangeReport{}
                                                            : apply(std::move(edit), revision_);
}
ChangeReport Document::splitEdge(Id context, Id edge, double fraction) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    sketchy::splitEdge(body->surface, old->topology.edges.at(edge), fraction);
    return apply({"Split edge", {{context, old, body}}}, revision_);
}
ChangeReport Document::eraseFace(Id context, Id face) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    auto result = sketchy::eraseFace(old->surface, face);
    body->surface = std::move(result.surface);
    if (old)
        inheritFaceAppearance(*old, *body, result.faces);
    return apply({"Erase face", {{context, old, body, std::move(result.faces)}}}, revision_);
}
ChangeReport Document::eraseEdge(Id context, Id edge) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    auto result = sketchy::eraseEdge(old->surface, old->topology, edge);
    body->surface = std::move(result.surface);
    if (old)
        inheritFaceAppearance(*old, *body, result.faces);
    return apply({"Erase edge", {{context, old, body, std::move(result.faces)}}}, revision_);
}
ChangeReport Document::healFace(Id context, Id edge, Vec3 origin, Vec3 normal) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    if (!old->topology.edges.contains(edge))
        throw std::runtime_error("Healing boundary edge does not exist");
    const auto boundary = old->topology.edges.at(edge);
    auto result = insertPlanarEdges(
        old->surface, origin, normal,
        {{old->surface.vertices.at(boundary.a), old->surface.vertices.at(boundary.b)}}, true);
    if (result.surface.faces.size() <= old->surface.faces.size())
        throw PlanarError("NO_CLOSED_REGION",
                          "This edge does not bound a missing closed planar face");
    body->surface = std::move(result.surface);
    if (old)
        inheritFaceAppearance(*old, *body, result.faces);
    return apply({"Heal face", {{context, old, body, std::move(result.faces)}}}, revision_);
}
ChangeReport Document::cleanup(Id context) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    auto result = cleanupCoincident(old->surface, old->topology);
    if (result.surface == old->surface)
        return {};
    body->surface = std::move(result.surface);
    if (old)
        inheritFaceAppearance(*old, *body, result.faces);
    return apply({"Merge coincident topology",
                  {{context, old, body, std::move(result.faces), std::move(result.vertices),
                    std::move(result.edges)}}},
                 revision_);
}
ChangeReport Document::offsetFace(Id context, Id face, double distance, bool worldSpace) {
    const auto old = bodies_.at(context);
    // Only the selected face enters the offset kernel; unrelated geometry is
    // retained by the subsequent context-scoped planar arrangement.
    Surface input;
    input.faces.emplace(face, old->surface.faces.at(face));
    const auto transform = worldSpace ? worldTransform(context) : Transform{};
    const auto inverse = transform.inverse();
    for (const auto &loop : input.faces.at(face).loops)
        for (auto vertex : loop)
            input.vertices[vertex] = transform.point(old->surface.vertices.at(vertex));
    const auto offset = offsetFaceRegion(input, face, distance);
    if (distance == 0)
        return {};
    if (offset.collapsed())
        throw OffsetError("OFFSET_COLLAPSED",
                          "The offset removes every region; choose a smaller inset distance");
    std::vector<std::array<Vec3, 2>> edges;
    for (const auto &region : offset.regions)
        for (const auto &loop : region.loops)
            for (size_t i = 0; i < loop.size(); ++i)
                edges.push_back(
                    {inverse.point(loop[i]), inverse.point(loop[(i + 1) % loop.size()])});
    const auto origin = old->surface.vertices.at(old->surface.faces.at(face).loops[0][0]);
    auto result = insertPlanarEdges(old->surface, origin, old->surface.normal(face), edges);
    if (result.surface == old->surface)
        return {};
    auto body = std::make_shared<Body>(*old);
    body->surface = std::move(result.surface);
    inheritFaceAppearance(*old, *body, result.faces, faceColor(*old, face),
                          faceMaterials(*old, face), faceTextureMappings(*old, face));
    return apply({"Offset face boundaries", {{context, old, body, std::move(result.faces)}}},
                 revision_);
}
ChangeReport Document::pushPull(Id context, Id face, double distance, bool newFace) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    auto result = sketchy::pushPull(old->surface, face, distance, newFace);
    body->surface = std::move(result.surface);
    if (old)
        inheritFaceAppearance(*old, *body, result.faces, faceColor(*old, face),
                              faceMaterials(*old, face), faceTextureMappings(*old, face));
    return apply({"Push/pull face", {{context, old, body, std::move(result.faces)}}}, revision_);
}
void Document::extrude(Id id, Id face, double distance) {
    auto old = bodies_.at(id);
    auto b = std::make_shared<Body>(*old);
    b->surface.extrude(face, distance);
    inheritFaceAppearance(*old, *b, {}, faceColor(*old, face), faceMaterials(*old, face),
                          faceTextureMappings(*old, face));
    apply({"Extrude face", {{id, old, b}}}, revision_);
}
void Document::move(Id id, Vec3 delta) {
    auto old = bodies_.at(id);
    auto b = std::make_shared<Body>(*old);
    checkPoint(delta);
    const auto localDelta =
        old->parent ? worldTransform(old->parent).inverse().vector(delta) : delta;
    b->transform = Transform::translation(localDelta) * old->transform;
    apply({"Move", {{id, old, b}}}, revision_);
}
void Document::paint(Id id, std::array<float, 3> color) {
    auto old = bodies_.at(id);
    auto b = std::make_shared<Body>(*old);
    b->color = color;
    b->faceColors.clear();
    b->materials = {};
    b->faceMaterials.clear();
    b->faceTextureMappings.clear();
    apply({"Paint", {{id, old, b}}}, revision_);
}
Transform Document::worldTransform(Id id) const { return worldTransformIn(bodies_, id); }
std::vector<Triangle> Document::worldTriangles(Id id) const {
    auto triangles = bodies_.at(id)->surface.triangles();
    const auto world = worldTransform(id);
    for (auto &triangle : triangles) {
        triangle.a = world.point(triangle.a);
        triangle.b = world.point(triangle.b);
        triangle.c = world.point(triangle.c);
    }
    return triangles;
}
double Document::worldArea(Id id, Id face) const {
    const auto world = worldTransform(id);
    double area = 0;
    for (const auto &triangle : bodies_.at(id)->surface.triangulate(face)) {
        const auto a = world.point(triangle.a), b = world.point(triangle.b),
                   c = world.point(triangle.c);
        area += length(cross(b - a, c - a)) * .5;
    }
    return area;
}
void Document::transform(Id id, Transform local, Id parent) {
    auto old = bodies_.at(id);
    auto body = std::make_shared<Body>(*old);
    body->transform = local;
    body->parent = parent;
    apply({"Transform", {{id, old, body}}}, revision_);
}
void Document::erase(Id id) {
    Edit edit{"Delete", {{id, bodies_.at(id), nullptr}}};
    if (instances_.contains(id))
        edit.instances.push_back({id, instances_.at(id), nullptr});
    apply(std::move(edit), revision_);
}
void Document::update(Edit edit, bool forward) {
    // Allocate into a temporary map before replacing authoritative state.
    auto next = bodies_;
    auto definitions = definitions_;
    auto instances = instances_;
    auto scenes = scenes_;
    auto sections = sections_;
    auto annotations = annotations_;
    auto activeSections = activeSections_;
    auto assets = assets_;
    auto materials = materials_;
    auto tags = tags_;
    if (edit.activeSections)
        activeSections = forward ? edit.activeSections->second : edit.activeSections->first;
    for (const auto &change : edit.annotations) {
        const auto target = forward ? change.after : change.before;
        if (target) annotations[change.id] = target;
        else annotations.erase(change.id);
    }
    for (const auto &change : edit.sections) {
        const auto target = forward ? change.after : change.before;
        if (target)
            sections[change.id] = target;
        else
            sections.erase(change.id);
    }
    for (const auto &change : edit.scenes) {
        const auto target = forward ? change.after : change.before;
        if (target)
            scenes[change.id] = target;
        else
            scenes.erase(change.id);
    }
    for (const auto &change : edit.assets) {
        const auto target = forward ? change.after : change.before;
        if (target)
            assets[change.id] = target;
        else
            assets.erase(change.id);
    }
    for (const auto &change : edit.materials) {
        const auto target = forward ? change.after : change.before;
        if (target)
            materials[change.id] = target;
        else
            materials.erase(change.id);
    }
    for (const auto &change : edit.tags) {
        const auto target = forward ? change.after : change.before;
        if (target)
            tags[change.id] = target;
        else
            tags.erase(change.id);
    }
    for (const auto &change : edit.definitions) {
        const auto target = forward ? change.after : change.before;
        if (target)
            definitions[change.id] = freezeDefinition(target, definitionFloors_.at(change.id));
        else
            definitions.erase(change.id);
    }
    for (const auto &change : edit.instances) {
        const auto target = forward ? change.after : change.before;
        if (target)
            instances[change.root] = target;
        else
            instances.erase(change.root);
    }
    for (const auto &c : edit.changes) {
        auto p = forward ? c.after : c.before;
        if (p) {
            const auto floor =
                surfaceFloors_.contains(c.id) ? surfaceFloors_.at(c.id) : p->surface.nextId;
            const auto edgeFloor =
                edgeFloors_.contains(c.id) ? edgeFloors_.at(c.id) : p->topology.nextId;
            if (p->surface.nextId < floor || p->topology.nextId < edgeFloor) {
                auto restored = std::make_shared<Body>(*p);
                restored->surface.nextId = floor;
                restored->topology.nextId = edgeFloor;
                next[c.id] = std::move(restored);
            } else
                next[c.id] = p;
        } else
            next.erase(c.id);
    }
    bodies_.swap(next);
    definitions_.swap(definitions);
    instances_.swap(instances);
    tags_.swap(tags);
    materials_.swap(materials);
    assets_.swap(assets);
    scenes_.swap(scenes);
    sections_.swap(sections);
    annotations_.swap(annotations);
    activeSections_.swap(activeSections);
    if (edit.hosted)
        hosted_ = forward ? edit.hosted->after : edit.hosted->before;
    if (edit.displayUnits)
        displayUnits_ = forward ? edit.displayUnits->second : edit.displayUnits->first;
    if (edit.style)
        style_ = forward ? edit.style->second : edit.style->first;
    if (edit.solar)
        solar_ = forward ? edit.solar->second : edit.solar->first;
}
ChangeReport Document::apply(Edit edit, std::uint64_t expected) {
    auto boundedText = [](const std::string &text, size_t limit) {
        return text.size() <= limit && text.find('\0') == std::string::npos;
    };
    if (edit.label.empty() || !boundedText(edit.label, 512) ||
        !boundedText(edit.metadata.taskId, 128) || !boundedText(edit.metadata.request, 4096) ||
        (edit.metadata.assistant &&
         (edit.metadata.taskId.empty() || edit.metadata.request.empty())))
        throw std::runtime_error("Invalid history label or task metadata");
    if (revision_ == UINT64_MAX)
        throw std::runtime_error("Document revision space exhausted");
    if (expected != revision_)
        throw std::runtime_error("STALE_REVISION: inspect current document before retrying");
    if (edit.changes.empty() && edit.definitions.empty() && edit.instances.empty() &&
        edit.tags.empty() && edit.materials.empty() && edit.assets.empty() && !edit.displayUnits &&
        !edit.hosted && !edit.style && !edit.solar && edit.scenes.empty() && edit.sections.empty() && !edit.activeSections && edit.annotations.empty())
        throw std::runtime_error("Empty edit");
    if (edit.solar) {
        edit.solar->first.validate();
        edit.solar->second.validate();
        if (edit.solar->first != solar_ || edit.solar->first == edit.solar->second)
            throw std::runtime_error("Invalid or stale sun study change");
    }
    if (edit.style) {
        edit.style->first.validate();
        edit.style->second.validate();
        if (edit.style->first != style_ || edit.style->first == edit.style->second)
            throw std::runtime_error("Invalid or stale model style change");
    }
    if (edit.displayUnits) {
        unitCode(edit.displayUnits->first);
        unitCode(edit.displayUnits->second);
        if (edit.displayUnits->first != displayUnits_ ||
            edit.displayUnits->first == edit.displayUnits->second)
            throw std::runtime_error("Invalid or stale document unit change");
    }
    if (edit.hosted && (edit.hosted->before != hosted_ || !edit.hosted->after))
        throw std::runtime_error("Invalid or stale hosted component change");
    expandHostedEdit(*this, edit);
    auto hosted = edit.hosted ? freezeHostedComponents(edit.hosted->after) : hosted_;
    if (edit.hosted)
        edit.hosted->after = hosted;
    auto scenes = scenes_;
    auto sections = sections_;
    auto annotations = annotations_;
    auto activeSections = activeSections_;
    auto assets = assets_;
    auto materials = materials_;
    auto tags = tags_;
    if (edit.activeSections) {
        if (edit.activeSections->first != activeSections_ ||
            edit.activeSections->first == edit.activeSections->second)
            throw std::runtime_error("Invalid or stale active section change");
        activeSections = edit.activeSections->second;
    }
    Id nextAnnotation = std::max(nextAnnotationId_, edit.nextAnnotationFloor);
    std::set<Id> annotationIds;
    for (auto &change : edit.annotations) {
        if (!change.id || change.id == UINT64_MAX || !annotationIds.insert(change.id).second ||
            (!change.before && !change.after) ||
            (annotations_.contains(change.id) ? annotations_.at(change.id) : nullptr) != change.before ||
            (change.before && change.after && *change.before == *change.after))
            throw std::runtime_error("Invalid or stale annotation change");
        if (!change.before && change.id < nextAnnotationId_)
            throw std::runtime_error("Retired annotation ID cannot be reused");
        if (change.after) {
            change.after = std::make_shared<AnnotationRecord>(*change.after);
            annotations[change.id] = change.after;
            nextAnnotation = std::max(nextAnnotation, change.id + 1);
        } else
            annotations.erase(change.id);
    }
    validateAnnotationRecords(annotations, nextAnnotation);
    Id nextSection = std::max(nextSectionId_, edit.nextSectionFloor);
    std::set<Id> sectionIds;
    for (auto &change : edit.sections) {
        if (!change.id || change.id == UINT64_MAX || !sectionIds.insert(change.id).second ||
            (!change.before && !change.after) ||
            (sections_.contains(change.id) ? sections_.at(change.id) : nullptr) != change.before ||
            (change.before && change.after && *change.before == *change.after))
            throw std::runtime_error("Invalid or stale section change");
        if (!change.before && change.id < nextSectionId_)
            throw std::runtime_error("Retired section ID cannot be reused");
        if (change.after) {
            change.after = std::make_shared<SectionRecord>(*change.after);
            sections[change.id] = change.after;
            nextSection = std::max(nextSection, change.id + 1);
        } else
            sections.erase(change.id);
    }
    validateSectionRecords(sections, nextSection, activeSections);
    Id nextScene = std::max(nextSceneId_, edit.nextSceneFloor);
    std::set<Id> sceneIds;
    for (auto &change : edit.scenes) {
        if (!change.id || change.id == UINT64_MAX || !sceneIds.insert(change.id).second ||
            (!change.before && !change.after) ||
            (scenes_.contains(change.id) ? scenes_.at(change.id) : nullptr) != change.before ||
            (change.before && change.after && *change.before == *change.after))
            throw std::runtime_error("Invalid or stale scene change");
        if (!change.before && change.id < nextSceneId_)
            throw std::runtime_error("Retired scene ID cannot be reused");
        if (change.after) {
            change.after = std::make_shared<SceneRecord>(*change.after);
            scenes[change.id] = change.after;
            nextScene = std::max(nextScene, change.id + 1);
        } else
            scenes.erase(change.id);
    }
    validateSceneRecords(scenes, nextScene);
    Id nextAsset = std::max(nextAssetId_, edit.nextAssetFloor);
    std::set<Id> assetIds;
    for (auto &change : edit.assets) {
        if (!change.id || change.id == UINT64_MAX || !assetIds.insert(change.id).second ||
            (!change.before && !change.after) ||
            (assets_.contains(change.id) ? assets_.at(change.id) : nullptr) != change.before)
            throw std::runtime_error("Invalid or stale asset change");
        if (!change.before && change.id < nextAssetId_)
            throw std::runtime_error("Retired asset ID cannot be reused");
        if (change.after) {
            change.after = std::make_shared<AssetRecord>(*change.after);
            assets[change.id] = change.after;
            nextAsset = std::max(nextAsset, change.id + 1);
        } else
            assets.erase(change.id);
    }
    validateAssetRecords(assets, nextAsset);
    Id nextMaterial = std::max(nextMaterialId_, edit.nextMaterialFloor);
    std::set<Id> materialIds;
    for (auto &change : edit.materials) {
        if (!change.id || change.id == UINT64_MAX || !materialIds.insert(change.id).second ||
            (!change.before && !change.after) ||
            (materials_.contains(change.id) ? materials_.at(change.id) : nullptr) != change.before)
            throw std::runtime_error("Invalid or stale material change");
        if (!change.before && change.id < nextMaterialId_)
            throw std::runtime_error("Retired material ID cannot be reused");
        if (change.after) {
            change.after = std::make_shared<MaterialRecord>(*change.after);
            materials[change.id] = change.after;
            nextMaterial = std::max(nextMaterial, change.id + 1);
        } else
            materials.erase(change.id);
    }
    validateMaterialRecords(materials, nextMaterial);
    validateMaterialAssets(materials, assets);
    Id nextTag = std::max(nextTagId_, edit.nextTagFloor);
    std::set<Id> tagIds;
    for (auto &change : edit.tags) {
        if (!change.id || change.id == UINT64_MAX || !tagIds.insert(change.id).second ||
            (!change.before && !change.after) ||
            (tags_.contains(change.id) ? tags_.at(change.id) : nullptr) != change.before)
            throw std::runtime_error("Invalid or stale tag change");
        if (!change.before && change.id < nextTagId_)
            throw std::runtime_error("Retired tag ID cannot be reused");
        if (change.after) {
            if (change.before && change.before->folder != change.after->folder)
                throw std::runtime_error("Tag identities cannot change between tags and folders");
            change.after = std::make_shared<TagRecord>(*change.after);
            tags[change.id] = change.after;
            nextTag = std::max(nextTag, change.id + 1);
        } else
            tags.erase(change.id);
    }
    validateTagRecords(tags, nextTag);
    auto definitions = definitions_;
    auto instances = instances_;
    auto definitionFloors = definitionFloors_;
    Id nextDefinition = std::max(nextDefinitionId_, edit.nextDefinitionFloor);
    std::set<Id> definitionIds, instanceRoots;
    for (auto &change : edit.definitions) {
        if (!change.id || change.id == UINT64_MAX || !definitionIds.insert(change.id).second ||
            (!change.before && !change.after) ||
            (definitions_.contains(change.id) ? definitions_.at(change.id) : nullptr) !=
                change.before)
            throw std::runtime_error("Invalid or stale component definition change");
        if (!change.before && change.id < nextDefinitionId_)
            throw std::runtime_error("Retired component definition ID cannot be reused");
        if (!change.after) {
            definitions.erase(change.id);
            continue;
        }
        if (change.after->id != change.id ||
            (change.before && change.before->root != change.after->root))
            throw std::runtime_error("Component definition identity/root mismatch");
        auto &floor = definitionFloors[change.id];
        for (const auto &[member, body] : change.after->members) {
            if (!body)
                throw std::runtime_error("Null component member");
            const auto before = change.before && change.before->members.contains(member)
                                    ? change.before->members.at(member)
                                    : nullptr;
            if (!before && member < floor.nextMemberId)
                throw std::runtime_error("Retired component member ID cannot be reused");
            auto &geometryFloor = floor.geometry[member];
            const Body empty;
            const auto &previous = before ? *before : empty;
            auto checkIds = [](const auto &oldRecords, const auto &newRecords, Id floor) {
                for (const auto &[id, record] : newRecords)
                    if (id < floor && !oldRecords.contains(id))
                        throw std::runtime_error("Retired definition geometry ID cannot be reused");
            };
            checkIds(previous.surface.vertices, body->surface.vertices, geometryFloor.first);
            checkIds(previous.surface.faces, body->surface.faces, geometryFloor.first);
            checkIds(previous.curves, body->curves, geometryFloor.first);
            checkIds(previous.guides, body->guides, geometryFloor.first);
            checkIds(previous.topology.edges, body->topology.edges, geometryFloor.second);
            for (const auto &[edge, record] : body->topology.edges)
                if (previous.topology.edges.contains(edge) &&
                    (record.a != previous.topology.edges.at(edge).a ||
                     record.b != previous.topology.edges.at(edge).b))
                    throw std::runtime_error("Definition edge ID cannot be reassigned");
            geometryFloor.first = std::max(geometryFloor.first, body->surface.nextId);
            geometryFloor.second = std::max(geometryFloor.second, body->topology.nextId);
        }
        floor.nextMemberId = std::max(floor.nextMemberId, change.after->nextMemberId);
        change.after = freezeDefinition(change.after, floor);
        definitions[change.id] = change.after;
        nextDefinition = std::max(nextDefinition, change.id + 1);
    }
    for (auto &change : edit.instances) {
        if (!change.root || !instanceRoots.insert(change.root).second ||
            (!change.before && !change.after) ||
            (instances_.contains(change.root) ? instances_.at(change.root) : nullptr) !=
                change.before)
            throw std::runtime_error("Invalid or stale component instance change");
        if (change.after) {
            change.after = std::make_shared<ComponentInstance>(*change.after);
            instances[change.root] = change.after;
        } else
            instances.erase(change.root);
    }
    std::set<Id> ids;
    Id next = std::max(nextId_, edit.nextIdFloor);
    auto floors = surfaceFloors_;
    auto edgeFloors = edgeFloors_;
    edit.bytes = sizeof(Edit) + edit.label.size() + edit.metadata.taskId.size() +
                 edit.metadata.request.size();
    for (const auto &c : edit.changes) {
        if (!c.id || !ids.insert(c.id).second || (!c.before && !c.after))
            throw std::runtime_error("Invalid change set");
        auto it = bodies_.find(c.id);
        if ((it == bodies_.end() ? nullptr : it->second) != c.before)
            throw std::runtime_error("Change precondition failed");
        if (!c.before && c.id < nextId_)
            throw std::runtime_error("Retired ID cannot be reused");
        if (c.after) {
            if (c.after->id != c.id || c.id == UINT64_MAX)
                throw std::runtime_error("Identity mismatch");
            validate(*c.after);
            const auto floor = floors.contains(c.id) ? floors.at(c.id) : Id{1};
            for (const auto &[id, vertex] : c.after->surface.vertices)
                if (id < floor && (!c.before || !c.before->surface.vertices.contains(id)))
                    throw std::runtime_error("Retired vertex ID cannot be reused");
            for (const auto &[id, face] : c.after->surface.faces)
                if (id < floor && (!c.before || !c.before->surface.faces.contains(id)))
                    throw std::runtime_error("Retired face ID cannot be reused");
            for (const auto &[id, curve] : c.after->curves)
                if (id < floor && (!c.before || !c.before->curves.contains(id)))
                    throw std::runtime_error("Retired curve ID cannot be reused");
            for (const auto &[id, guide] : c.after->guides)
                if (id < floor && (!c.before || !c.before->guides.contains(id)))
                    throw std::runtime_error("Retired guide ID cannot be reused");
            floors[c.id] = std::max(floor, c.after->surface.nextId);
            next = std::max(next, c.id + 1);
        }
        edit.bytes += sizeof(Change) + bytes(c.before) + bytes(c.after);
    }
    // Freeze caller-owned mutable records before storing them as const pointers.
    for (auto &c : edit.changes)
        if (c.after) {
            auto frozen = std::make_shared<Body>(*c.after);
            frozen->surface.nextId = floors.at(c.id);
            const auto edgeFloor = edgeFloors.contains(c.id) ? edgeFloors.at(c.id) : Id{1};
            bool indexed = true;
            try {
                frozen->topology.validate(frozen->surface);
            } catch (const std::runtime_error &) {
                indexed = false;
            }
            if (!indexed)
                frozen->topology =
                    Topology::rebuild(frozen->surface, c.before ? c.before->topology : Topology{},
                                      std::max(edgeFloor, frozen->topology.nextId));
            for (const auto &[id, edge] : frozen->topology.edges) {
                if (c.before && c.before->topology.edges.contains(id)) {
                    const auto &previous = c.before->topology.edges.at(id);
                    if (edge.a != previous.a || edge.b != previous.b)
                        throw std::runtime_error(
                            "An edge ID cannot be reassigned to unrelated endpoints");
                } else if (id < edgeFloor)
                    throw std::runtime_error("Retired edge ID cannot be reused");
            }
            frozen->topology.nextId = std::max(frozen->topology.nextId, edgeFloor);
            frozen->topology.validate(frozen->surface);
            // Ordinary geometry edits retain the analytic record only while its
            // complete original outline still exists, including split chords.
            size_t curveBudget = 1000000;
            for (auto it = frozen->curves.begin(); it != frozen->curves.end();) {
                const bool existing = c.before && c.before->curves.contains(it->first) &&
                                      c.before->curves.at(it->first) == it->second;
                if (existing &&
                    !bindCurve(it->second, frozen->surface, frozen->topology, curveBudget))
                    it = frozen->curves.erase(it);
                else
                    ++it;
            }
            validateCurves(frozen->curves, frozen->surface, frozen->topology);
            edgeFloors[c.id] = frozen->topology.nextId;
            c.after = std::move(frozen);
        }
    edit.bytes = sizeof(Edit) + edit.label.size() + edit.metadata.taskId.size() +
                 edit.metadata.request.size();
    ChangeReport report;
    const Surface emptySurface;
    const Topology emptyTopology;
    for (auto &change : edit.changes) {
        for (const auto *mapping :
             {&change.faceDescendants, &change.vertexDescendants, &change.edgeDescendants})
            for (const auto &[id, targets] : *mapping)
                edit.bytes += 96 + targets.size() * sizeof(Id);
        auto changes = compareTopology(change.before ? change.before->surface : emptySurface,
                                       change.before ? change.before->topology : emptyTopology,
                                       change.after ? change.after->surface : emptySurface,
                                       change.after ? change.after->topology : emptyTopology,
                                       change.edgeDescendants.empty());
        auto mapEntities = [&](const auto &mapping, const auto &before, const auto &after,
                               EntityChanges &entities) {
            for (const auto &[old, descendants] : mapping) {
                if (!before.contains(old))
                    throw std::runtime_error("Topology lineage source does not exist");
                std::set<Id> unique;
                for (auto id : descendants)
                    if (!after.contains(id) || !unique.insert(id).second)
                        throw std::runtime_error("Invalid topology lineage target");
                entities.descendants[old] = descendants;
            }
        };
        mapEntities(change.faceDescendants,
                    change.before ? change.before->surface.faces : emptySurface.faces,
                    change.after ? change.after->surface.faces : emptySurface.faces, changes.faces);
        mapEntities(change.vertexDescendants,
                    change.before ? change.before->surface.vertices : emptySurface.vertices,
                    change.after ? change.after->surface.vertices : emptySurface.vertices,
                    changes.vertices);
        mapEntities(change.edgeDescendants,
                    change.before ? change.before->topology.edges : emptyTopology.edges,
                    change.after ? change.after->topology.edges : emptyTopology.edges,
                    changes.edges);
        const Body emptyBody;
        if (change.after) {
            const auto &before = change.before ? *change.before : emptyBody;
            const auto styles =
                change.edgeAppearancesResolved
                    ? change.after->edgeAppearances
                    : inheritedEdgeAppearances(before, *change.after, changes.edges.descendants);
            if (styles != change.after->edgeAppearances) {
                auto styled = std::make_shared<Body>(*change.after);
                styled->edgeAppearances = styles;
                change.after = std::move(styled);
            }
            validateEdgeAppearances(*change.after);
            reportEdgeAppearanceChanges(before, *change.after, changes.edges);
            reportFaceTextureMappingChanges(before, *change.after, changes.faces);
            change.edgeAppearancesResolved = true;
        }
        edit.bytes += sizeof(Change) + bytes(change.before) + bytes(change.after);
        const std::map<Id, Curve> noCurves;
        changes.curves = compareCurves(change.before ? change.before->curves : noCurves,
                                       change.after ? change.after->curves : noCurves);
        const std::map<Id, Guide> noGuides;
        changes.guides = compareGuides(change.before ? change.before->guides : noGuides,
                                       change.after ? change.after->guides : noGuides);
        report.emplace(change.id, std::move(changes));
    }
    auto updated = bodies_;
    for (const auto &c : edit.changes) {
        if (c.after)
            updated[c.id] = c.after;
        else
            updated.erase(c.id);
    }
    validateDocumentSize(updated, &bodies_);
    if (!annotations.empty()) {
        auto candidate = readSnapshot();
        candidate.bodies_ = updated;
        resolveAnnotationEdit(*this, candidate, report, edit, annotations);
        validateAnnotationRecords(annotations, nextAnnotation);
    }
    edit.annotationsResolved = true;
    for (const auto &change : edit.annotations)
        edit.bytes += sizeof(AnnotationChange) + annotationBytes(change.before) + annotationBytes(change.after);
    for (const auto &change : edit.definitions)
        edit.bytes +=
            sizeof(DefinitionChange) + componentBytes(change.before) + componentBytes(change.after);
    for (const auto &change : edit.instances)
        edit.bytes +=
            sizeof(InstanceChange) + componentBytes(change.before) + componentBytes(change.after);
    for (const auto &change : edit.tags)
        edit.bytes += sizeof(TagChange) +
                      (change.before ? sizeof(TagRecord) + change.before->name.size() + 64 : 0) +
                      (change.after ? sizeof(TagRecord) + change.after->name.size() + 64 : 0);
    for (const auto &change : edit.materials)
        edit.bytes +=
            sizeof(MaterialChange) +
            (change.before ? sizeof(MaterialRecord) + change.before->name.size() + 64 : 0) +
            (change.after ? sizeof(MaterialRecord) + change.after->name.size() + 64 : 0);
    for (const auto &change : edit.assets)
        edit.bytes += sizeof(AssetChange) + assetBytes(change.before) + assetBytes(change.after);
    for (const auto &change : edit.scenes)
        edit.bytes += sizeof(SceneChange) + sceneBytes(change.before) + sceneBytes(change.after);
    for (const auto &change : edit.sections)
        edit.bytes += sizeof(SectionChange) + sectionBytes(change.before) + sectionBytes(change.after);
    if (edit.activeSections)
        edit.bytes += sizeof(ActiveSections) * 2 +
                      (edit.activeSections->first.size() + edit.activeSections->second.size()) * 64;
    if (edit.hosted)
        edit.bytes += sizeof(HostedChange) + hostedComponentBytes(edit.hosted->before) +
                      hostedComponentBytes(edit.hosted->after);
    if (edit.bytes > historyLimit)
        throw std::runtime_error("Edit exceeds the 64 MiB history budget");
    if (!edit.sections.empty() || !activeSections.empty()) {
        auto sectionCandidate = readSnapshot();
        sectionCandidate.bodies_ = updated;
        sectionCandidate.sections_ = sections;
        sectionCandidate.nextSectionId_ = nextSection;
        sectionCandidate.activeSections_ = activeSections;
        for (const auto &change : edit.sections)
            if (change.after && (!change.before || change.before->context != change.after->context))
                validateSectionContext(sectionCandidate, *change.after);
        // Newly activated scopes need live contexts; existing missing contexts remain inert.
        for (const auto &[context, id] : activeSections)
            if (!activeSections_.contains(context) || activeSections_.at(context) != id)
                validateSectionContext(sectionCandidate, *sections.at(id));
        validateSectionDepth(sectionCandidate);
    }
    validateTagAssignments(tags, updated);
    if (!edit.scenes.empty()) {
        // Check newly captured references against the same candidate as the edit.
        // Renaming/reordering an old scene preserves its diagnosed missing refs.
        auto capture = readSnapshot();
        capture.bodies_ = updated;
        capture.tags_ = tags;
        for (const auto &change : edit.scenes)
            if (change.after && (!change.before ||
                                 change.before->snapshot != change.after->snapshot))
                validateSceneCapture(capture, change.after->snapshot);
    }
    validateMaterialAssignments(materials, updated);
    validateReferenceImageAssets(updated, assets);
    validateComponentDefinitions(definitions, nextDefinition, tags, nextTag, materials,
                                 nextMaterial, assets, nextAsset);
    validateComponentInstanceEdits(definitions, instances, updated, *this);
    validateHostedComponents(*hosted, updated, definitions, instances);
    // A lock is authoritative across every command path. Changing only visibility
    // or lock flags is allowed so a locked entity can always be revealed/unlocked.
    auto lockedIn = [](const auto &records, Id id) {
        for (; id && records.contains(id); id = records.at(id)->parent)
            if (records.at(id)->locked)
                return true;
        return false;
    };
    for (const auto &change : edit.changes) {
        bool stateOnly = false;
        if (change.before && change.after) {
            auto comparable = *change.after;
            comparable.hidden = change.before->hidden;
            comparable.locked = change.before->locked;
            stateOnly = comparable == *change.before;
        }
        if (!stateOnly && (lockedIn(bodies_, change.id) ||
                           (change.after && lockedIn(bodies_, change.after->parent))))
            throw std::runtime_error("Cannot edit a locked entity or its contents");
    }
    for (auto id : hostedEditingContexts(edit))
        if (lockedIn(bodies_, id))
            throw std::runtime_error("Cannot change attachments of a locked component or host");
    for (const auto &change : edit.instances)
        if (lockedIn(bodies_, change.root))
            throw std::runtime_error("Cannot change a locked component instance binding");
    for (const auto &change : edit.definitions)
        if (change.before && change.after && change.before->glue != change.after->glue)
            for (const auto &[root, instance] : instances_)
                if (instance->definition == change.id && lockedIn(bodies_, root))
                    throw std::runtime_error("Cannot change glue behavior of a locked component");
    for (const auto &[id, body] : bodies_) {
        if (!body->locked)
            continue;
        // Even an unchanged locked child cannot be moved, reparented or deleted
        // indirectly by an ancestor edit. Compare ancestry, not just world points.
        for (auto ancestor = id; ancestor; ancestor = bodies_.at(ancestor)->parent) {
            const auto &before = *bodies_.at(ancestor);
            if (instanceRoots.contains(ancestor) || !updated.contains(ancestor) ||
                updated.at(ancestor)->parent != before.parent ||
                updated.at(ancestor)->transform != before.transform ||
                updated.at(ancestor)->kind != before.kind)
                throw std::runtime_error("Cannot restructure a locked descendant");
        }
    }
    // Keep floors for live contexts and contexts reachable from retained history.
    // Redo is about to be discarded. Floors of permanently retired bodies are unnecessary.
    std::set<Id> reachable;
    for (const auto &[id, body] : updated)
        reachable.insert(id);
    for (const auto &entry : undo_)
        for (const auto &change : entry.edit.changes)
            reachable.insert(change.id);
    for (const auto &change : edit.changes)
        reachable.insert(change.id);
    std::erase_if(floors, [&](const auto &entry) { return !reachable.contains(entry.first); });
    std::erase_if(edgeFloors, [&](const auto &entry) { return !reachable.contains(entry.first); });
    std::map<Id, std::set<Id>> reachableDefinitions;
    auto retainDefinition = [&](Id id, const DefinitionPtr &definition) {
        if (!definition)
            return;
        auto &members = reachableDefinitions[id];
        for (const auto &[member, body] : definition->members)
            members.insert(member);
    };
    for (const auto &[id, definition] : definitions)
        retainDefinition(id, definition);
    for (const auto &entry : undo_)
        for (const auto &change : entry.edit.definitions) {
            retainDefinition(change.id, change.before);
            retainDefinition(change.id, change.after);
        }
    for (const auto &change : edit.definitions) {
        retainDefinition(change.id, change.before);
        retainDefinition(change.id, change.after);
    }
    std::erase_if(definitionFloors,
                  [&](const auto &entry) { return !reachableDefinitions.contains(entry.first); });
    for (auto &[id, floor] : definitionFloors)
        std::erase_if(floor.geometry, [&](const auto &entry) {
            return !reachableDefinitions.at(id).contains(entry.first);
        });
    History h{std::move(edit), state_, std::make_shared<State>()};
    undo_.push_back(h); // Allocation can still fail before any committed change.
    for (const auto &r : redo_)
        historyBytes_ -= r.edit.bytes;
    redo_.clear();
    historyBytes_ += h.edit.bytes;
    bodies_.swap(updated);
    definitions_.swap(definitions);
    instances_.swap(instances);
    definitionFloors_.swap(definitionFloors);
    nextDefinitionId_ = nextDefinition;
    tags_.swap(tags);
    materials_.swap(materials);
    assets_.swap(assets);
    scenes_.swap(scenes);
    sections_.swap(sections);
    annotations_.swap(annotations);
    activeSections_.swap(activeSections);
    hosted_ = std::move(hosted);
    nextTagId_ = nextTag;
    nextMaterialId_ = nextMaterial;
    nextAssetId_ = nextAsset;
    nextSceneId_ = nextScene;
    nextSectionId_ = nextSection;
    nextAnnotationId_ = nextAnnotation;
    nextId_ = next;
    surfaceFloors_.swap(floors);
    edgeFloors_.swap(edgeFloors);
    if (h.edit.displayUnits)
        displayUnits_ = h.edit.displayUnits->second;
    if (h.edit.style)
        style_ = h.edit.style->second;
    if (h.edit.solar)
        solar_ = h.edit.solar->second;
    state_ = h.after;
    ++revision_;
    while ((historyBytes_ > historyLimit || undo_.size() > historyEntryLimit) && undo_.size() > 1) {
        historyPruned_ = true;
        historyBytes_ -= undo_.front().edit.bytes;
        undo_.pop_front();
    }
    return report;
}
Document::AmendStamp Document::amendmentStamp() const {
    AmendStamp stamp;
    if (!undo_.empty() && undo_.back().after == state_) {
        stamp.session = session_;
        stamp.state = state_;
        stamp.revision = revision_;
    }
    return stamp;
}
bool Document::canAmend(const AmendStamp &stamp) const {
    return stamp.session == session_ && stamp.state == state_ && stamp.revision == revision_ &&
           !undo_.empty() && undo_.back().after == state_;
}
ChangeReport Document::amendLast(const AmendStamp &stamp,
                                 const std::function<void(Document &)> &replace,
                                 AmendPolicy policy) {
    if (policy != AmendPolicy::FixedContextCount && policy != AmendPolicy::CopyArray)
        throw std::runtime_error("Unknown amendment policy");
    if (!canAmend(stamp))
        throw std::runtime_error("The most recent operation can no longer be revised");
    auto contexts = hostedEditingContexts(undo_.back().edit);
    std::set<Id> definitionContexts;
    std::set<Id> tagContexts, materialContexts, assetContexts, sceneContexts, sectionContexts;
    size_t createdSections = 0;
    size_t createdAnnotations = 0;
    std::set<Id> annotationContexts;
    size_t createdScenes = 0;
    size_t createdAssets = 0;
    size_t createdMaterials = 0;
    size_t createdContexts = 0;
    size_t createdDefinitions = 0;
    size_t createdTags = 0;
    for (const auto &change : undo_.back().edit.changes) {
        contexts.insert(change.id);
        if (!change.before && change.after)
            ++createdContexts;
    }
    for (const auto &change : undo_.back().edit.instances)
        contexts.insert(change.root);
    for (const auto &change : undo_.back().edit.definitions) {
        definitionContexts.insert(change.id);
        if (!change.before && change.after)
            ++createdDefinitions;
    }
    for (const auto &change : undo_.back().edit.tags) {
        tagContexts.insert(change.id);
        if (!change.before && change.after)
            ++createdTags;
    }
    for (const auto &change : undo_.back().edit.materials) {
        materialContexts.insert(change.id);
        if (!change.before && change.after)
            ++createdMaterials;
    }
    for (const auto &change : undo_.back().edit.assets) {
        assetContexts.insert(change.id);
        if (!change.before && change.after)
            ++createdAssets;
    }
    for (const auto &change : undo_.back().edit.scenes) {
        sceneContexts.insert(change.id);
        if (!change.before && change.after)
            ++createdScenes;
    }
    for (const auto &change : undo_.back().edit.sections) {
        sectionContexts.insert(change.id);
        if (!change.before && change.after)
            ++createdSections;
    }
    for (const auto &change : undo_.back().edit.annotations) {
        annotationContexts.insert(change.id);
        if (!change.before && change.after) ++createdAnnotations;
    }
    Document staged = *this;
    staged.undo();
    const auto baseline = staged.bodies_;
    const auto baselineDefinitions = staged.definitions_;
    const auto baselineInstances = staged.instances_;
    const auto baselineTags = staged.tags_;
    const auto baselineMaterials = staged.materials_;
    const auto baselineAssets = staged.assets_;
    const auto baselineScenes = staged.scenes_;
    const auto baselineSections = staged.sections_;
    const auto baselineAnnotations = staged.annotations_;
    const auto baselineActiveSections = staged.activeSections_;
    const auto baselineUnits = staged.displayUnits_;
    const auto baselineStyle = staged.style_;
    const auto baselineSolar = staged.solar_;
    const auto baselineHosted = staged.hosted_;
    // Rewind only the private candidate. A replacement publishes one revision,
    // and retains the pre-operation history entry and monotonic allocator floors.
    staged.revision_ = revision_;
    replace(staged);
    if (staged.identity_ != identity_ || staged.session_ != session_ ||
        staged.revision_ != revision_ + 1 || staged.undo_.empty() || staged.state_ == state_)
        throw std::runtime_error("Replacement must commit exactly one atomic operation");
    if (!undo_.back().edit.displayUnits && staged.displayUnits_ != baselineUnits)
        throw std::runtime_error("Replacement cannot change document units outside its scope");
    if (!undo_.back().edit.solar && staged.solar_ != baselineSolar)
        throw std::runtime_error("Replacement cannot change sun study outside its scope");
    if (!undo_.back().edit.style && staged.style_ != baselineStyle)
        throw std::runtime_error("Replacement cannot change model style outside its scope");
    if (!undo_.back().edit.activeSections && staged.activeSections_ != baselineActiveSections)
        throw std::runtime_error("Replacement cannot activate sections outside its scope");
    if (undo_.back().edit.activeSections) {
        std::set<Id> allowed;
        const auto &original = *undo_.back().edit.activeSections;
        for (const auto &[context, id] : original.first)
            if (!original.second.contains(context) || original.second.at(context) != id)
                allowed.insert(context);
        for (const auto &[context, id] : original.second)
            if (!original.first.contains(context) || original.first.at(context) != id)
                allowed.insert(context);
        for (const auto &[context, id] : baselineActiveSections)
            if (!allowed.contains(context) &&
                (!staged.activeSections_.contains(context) || staged.activeSections_.at(context) != id))
                throw std::runtime_error("Replacement cannot change another active section context");
        for (const auto &[context, id] : staged.activeSections_)
            if (!allowed.contains(context) && !baselineActiveSections.contains(context))
                throw std::runtime_error("Replacement cannot add another active section context");
    }
    for (const auto &[id, section] : baselineSections)
        if (!sectionContexts.contains(id) &&
            (!staged.sections_.contains(id) || staged.sections_.at(id) != section))
            throw std::runtime_error("Replacement cannot change another section plane");
    for (const auto &[id, annotation] : baselineAnnotations)
        if (!annotationContexts.contains(id) &&
            (!staged.annotations_.contains(id) ||
             *staged.annotations_.at(id) != *annotation))
            throw std::runtime_error("Replacement cannot change another annotation");
    size_t newAnnotations{};
    for (const auto &[id, annotation] : staged.annotations_)
        newAnnotations += !baselineAnnotations.contains(id);
    if (newAnnotations != createdAnnotations)
        throw std::runtime_error("Replacement must preserve annotation creation count");
    size_t newSections{};
    for (const auto &[id, section] : staged.sections_)
        newSections += !baselineSections.contains(id);
    if (newSections != createdSections)
        throw std::runtime_error("Replacement must preserve section creation count");
    validateHostedAmendment(undo_.back().edit, staged.undo_.back().edit, *baselineHosted,
                            *staged.hosted_, policy == AmendPolicy::CopyArray);
    for (const auto &[id, body] : baseline)
        if (!contexts.contains(id) &&
            (!staged.bodies_.contains(id) || staged.bodies_.at(id) != body))
            throw std::runtime_error("Replacement cannot change another editing context");
    for (const auto &[id, definition] : baselineDefinitions)
        if (!definitionContexts.contains(id) &&
            (!staged.definitions_.contains(id) || staged.definitions_.at(id) != definition))
            throw std::runtime_error("Replacement cannot change another component definition");
    size_t newDefinitions = 0;
    for (const auto &[id, definition] : staged.definitions_)
        if (!baselineDefinitions.contains(id))
            ++newDefinitions;
    if (newDefinitions != createdDefinitions)
        throw std::runtime_error("Replacement must preserve definition creation count");
    for (const auto &[id, tag] : baselineTags)
        if (!tagContexts.contains(id) && (!staged.tags_.contains(id) || staged.tags_.at(id) != tag))
            throw std::runtime_error("Replacement cannot change another tag");
    size_t newTags = 0;
    for (const auto &[id, tag] : staged.tags_)
        newTags += !baselineTags.contains(id);
    if (newTags != createdTags)
        throw std::runtime_error("Replacement must preserve tag creation count");
    for (const auto &[id, material] : baselineMaterials)
        if (!materialContexts.contains(id) &&
            (!staged.materials_.contains(id) || staged.materials_.at(id) != material))
            throw std::runtime_error("Replacement cannot change another material");
    size_t newMaterials = 0;
    for (const auto &[id, material] : staged.materials_)
        newMaterials += !baselineMaterials.contains(id);
    if (newMaterials != createdMaterials)
        throw std::runtime_error("Replacement must preserve material creation count");
    for (const auto &[id, scene] : baselineScenes)
        if (!sceneContexts.contains(id) &&
            (!staged.scenes_.contains(id) || staged.scenes_.at(id) != scene))
            throw std::runtime_error("Replacement cannot change another scene");
    size_t newScenes = 0;
    for (const auto &[id, scene] : staged.scenes_)
        newScenes += !baselineScenes.contains(id);
    if (newScenes != createdScenes)
        throw std::runtime_error("Replacement must preserve scene creation count");
    for (const auto &[id, asset] : baselineAssets)
        if (!assetContexts.contains(id) &&
            (!staged.assets_.contains(id) || staged.assets_.at(id) != asset))
            throw std::runtime_error("Replacement cannot change another asset");
    size_t newAssets = 0;
    for (const auto &[id, asset] : staged.assets_)
        newAssets += !baselineAssets.contains(id);
    if (newAssets != createdAssets)
        throw std::runtime_error("Replacement must preserve asset creation count");
    for (const auto &[root, instance] : baselineInstances)
        if (!contexts.contains(root) &&
            (!staged.instances_.contains(root) || staged.instances_.at(root) != instance))
            throw std::runtime_error("Replacement cannot change another instance binding");
    for (const auto &[root, instance] : staged.instances_)
        if (!baselineInstances.contains(root) && baseline.contains(root) &&
            !contexts.contains(root))
            throw std::runtime_error("Replacement cannot bind another scene context");
    size_t newContexts = 0;
    for (const auto &[id, body] : staged.bodies_)
        if (!baseline.contains(id))
            ++newContexts;
    if (newContexts != createdContexts && policy != AmendPolicy::CopyArray)
        throw std::runtime_error(
            "Replacement must preserve the operation's context creation count");
    ChangeReport report;
    std::set<Id> ids;
    for (const auto &[id, body] : bodies_)
        ids.insert(id);
    for (const auto &[id, body] : staged.bodies_)
        ids.insert(id);
    const Body empty;
    for (auto id : ids) {
        auto before = bodies_.contains(id) ? bodies_.at(id) : nullptr;
        auto after = staged.bodies_.contains(id) ? staged.bodies_.at(id) : nullptr;
        if (before != after) {
            report.emplace(id, compareTopology(before ? before->surface : empty.surface,
                                               before ? before->topology : empty.topology,
                                               after ? after->surface : empty.surface,
                                               after ? after->topology : empty.topology));
            reportEdgeAppearanceChanges(before ? *before : empty, after ? *after : empty,
                                        report.at(id).edges);
            report.at(id).curves = compareCurves(before ? before->curves : empty.curves,
                                                 after ? after->curves : empty.curves);
            report.at(id).guides = compareGuides(before ? before->guides : empty.guides,
                                                 after ? after->guides : empty.guides);
        }
    }
    // Numeric re-entry revises the original user task; it must not silently relabel its origin.
    auto &revised = staged.undo_.back().edit;
    const auto &original = undo_.back().edit;
    const auto oldTextBytes =
        revised.label.size() + revised.metadata.taskId.size() + revised.metadata.request.size();
    const auto newTextBytes =
        original.label.size() + original.metadata.taskId.size() + original.metadata.request.size();
    revised.label = original.label;
    revised.metadata = original.metadata;
    revised.bytes = revised.bytes - oldTextBytes + newTextBytes;
    staged.historyBytes_ = staged.historyBytes_ - oldTextBytes + newTextBytes;
    if (revised.bytes > historyLimit)
        throw std::runtime_error("Amended edit exceeds history budget");
    while (staged.historyBytes_ > historyLimit && staged.undo_.size() > 1) {
        staged.historyPruned_ = true;
        staged.historyBytes_ -= staged.undo_.front().edit.bytes;
        staged.undo_.pop_front();
    }
    *this = std::move(staged);
    return report;
}
void Document::undo() {
    if (undo_.empty())
        return;
    if (revision_ == UINT64_MAX)
        throw std::runtime_error("Document revision space exhausted");
    auto h = undo_.back();
    redo_.push_back(h);
    try {
        update(h.edit, false);
    } catch (...) {
        redo_.pop_back();
        throw;
    }
    undo_.pop_back();
    state_ = h.before;
    ++revision_;
}
void Document::redo() {
    if (redo_.empty())
        return;
    if (revision_ == UINT64_MAX)
        throw std::runtime_error("Document revision space exhausted");
    auto h = redo_.back();
    undo_.push_back(h);
    try {
        update(h.edit, true);
    } catch (...) {
        undo_.pop_back();
        throw;
    }
    redo_.pop_back();
    state_ = h.after;
    ++revision_;
}
Document::SaveStamp Document::saveStamp() const {
    SaveStamp stamp;
    stamp.session = session_;
    stamp.state = state_;
    return stamp;
}
bool Document::markSaved(const SaveStamp &stamp) {
    if (!owns(stamp))
        return false;
    savedState_ = stamp.state;
    return true;
}
void Document::restore(std::string identity, Id next, std::map<Id, BodyPtr> bodies,
                       std::uint64_t revision, ComponentDefinitions definitions,
                       ComponentInstances instances, Id nextDefinitionId, TagRecords tags,
                       Id nextTagId, MaterialRecords materials, Id nextMaterialId,
                       AssetRecords assets, Id nextAssetId, DisplayUnit units, HostedPtr hosted,
                       ModelStyle style, SceneRecords scenes, Id nextSceneId, SectionRecords sections,
                       Id nextSectionId, ActiveSections activeSections,
                       AnnotationRecords annotations, Id nextAnnotationId, SolarSettings solar) {
    unitCode(units);
    style.validate();
    solar.validate();
    if (identity.size() != 32 ||
        !std::all_of(identity.begin(), identity.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        !next || bodies.size() > 10000)
        throw std::runtime_error("Invalid document metadata");
    std::map<Id, Id> floors, edgeFloors;
    for (auto &[id, b] : bodies) {
        if (!b || id != b->id || id >= next)
            throw std::runtime_error("Invalid body ID allocator");
        validate(*b);
        auto restored = std::make_shared<Body>(*b);
        if (restored->topology.edges.empty() && restored->topology.nextId == 1)
            restored->topology = Topology::rebuild(restored->surface, {});
        restored->topology.validate(restored->surface);
        validateEdgeAppearances(*restored);
        validateCurves(restored->curves, restored->surface, restored->topology);
        edgeFloors.emplace(id, restored->topology.nextId);
        b = std::move(restored);
        floors.emplace(id, b->surface.nextId);
    }
    validateDocumentSize(bodies);
    validateTagRecords(tags, nextTagId);
    validateTagAssignments(tags, bodies);
    validateAssetRecords(assets, nextAssetId);
    validateSceneRecords(scenes, nextSceneId);
    validateSectionRecords(sections, nextSectionId, activeSections);
    validateAnnotationRecords(annotations, nextAnnotationId);
    {
        Document candidate;
        candidate.bodies_ = bodies;
        candidate.activeSections_ = activeSections;
        validateSectionDepth(candidate);
    }
    validateMaterialRecords(materials, nextMaterialId);
    validateMaterialAssets(materials, assets);
    validateMaterialAssignments(materials, bodies);
    validateReferenceImageAssets(bodies, assets);
    validateComponentDefinitions(definitions, nextDefinitionId, tags, nextTagId, materials,
                                 nextMaterialId, assets, nextAssetId);
    validateComponentInstances(definitions, instances, bodies);
    hosted = freezeHostedComponents(hosted);
    validateHostedComponents(*hosted, bodies, definitions, instances);
    std::map<Id, DefinitionFloor> definitionFloors;
    for (auto &[id, definition] : definitions) {
        auto &floor = definitionFloors[id];
        floor.nextMemberId = definition->nextMemberId;
        for (const auto &[member, body] : definition->members)
            floor.geometry[member] = {body->surface.nextId, body->topology.nextId};
        definition = freezeDefinition(definition, floor);
    }
    for (auto &[root, instance] : instances)
        instance = std::make_shared<ComponentInstance>(*instance);
    for (auto &[id, tag] : tags)
        tag = std::make_shared<TagRecord>(*tag);
    for (auto &[id, material] : materials)
        material = std::make_shared<MaterialRecord>(*material);
    for (auto &[id, asset] : assets)
        asset = std::make_shared<AssetRecord>(*asset);
    for (auto &[id, scene] : scenes)
        scene = std::make_shared<SceneRecord>(*scene);
    for (auto &[id, section] : sections)
        section = std::make_shared<SectionRecord>(*section);
    for (auto &[id, annotation] : annotations)
        annotation = std::make_shared<AnnotationRecord>(*annotation);
    auto fresh = std::make_shared<State>();
    auto session = std::make_shared<State>();
    identity_ = std::move(identity);
    displayUnits_ = units;
    style_ = style;
    solar_ = solar;
    nextId_ = next;
    bodies_ = std::move(bodies);
    definitions_ = std::move(definitions);
    instances_ = std::move(instances);
    hosted_ = std::move(hosted);
    definitionFloors_ = std::move(definitionFloors);
    nextDefinitionId_ = nextDefinitionId;
    tags_ = std::move(tags);
    nextTagId_ = nextTagId;
    materials_ = std::move(materials);
    nextMaterialId_ = nextMaterialId;
    assets_ = std::move(assets);
    nextAssetId_ = nextAssetId;
    scenes_ = std::move(scenes);
    nextSceneId_ = nextSceneId;
    sections_ = std::move(sections);
    nextSectionId_ = nextSectionId;
    activeSections_ = std::move(activeSections);
    annotations_ = std::move(annotations);
    nextAnnotationId_ = nextAnnotationId;
    surfaceFloors_ = std::move(floors);
    edgeFloors_ = std::move(edgeFloors);
    undo_.clear();
    redo_.clear();
    historyBytes_ = 0;
    historyPruned_ = false;
    revision_ = revision;
    session_ = std::move(session);
    state_ = std::move(fresh);
    savedState_ = state_;
}
} // namespace sketchy
