#include "core/hosted_components.hpp"
#include "core/component_glue.hpp"
#include "core/model.hpp"
#include <algorithm>
namespace sketchy {
namespace {
constexpr size_t hostLimit = 16, attachmentLimit = 64;
[[noreturn]] void fail(const char *code, const char *message) { throw OpeningError(code, message); }
template <class T>
bool equal(const std::shared_ptr<const T> &a, const std::shared_ptr<const T> &b) {
    return a == b || (a && b && *a == *b);
}
template <class Records> bool equalRecords(const Records &a, const Records &b) {
    if (a.size() != b.size())
        return false;
    for (const auto &[id, value] : a)
        if (!b.contains(id) || !equal(value, b.at(id)))
            return false;
    return true;
}
void bounded(const HostedComponents &records) {
    if (records.hosts.size() > hostLimit || records.attachments.size() > attachmentLimit)
        fail("HOST_LIMIT", "A document accepts at most 16 hosts and 64 component attachments");
    size_t vertices = 0, faces = 0, wires = 0, corners = 0, openingCorners = 0;
    for (const auto &[id, host] : records.hosts) {
        if (!id || !host || host->uncut.vertices.size() > 10000 ||
            host->uncut.faces.size() > 1000 || host->uncut.wires.size() > 10000 ||
            host->openings.size() > 16)
            fail("HOST_LIMIT", "Invalid or oversized hosted surface record");
        vertices += host->uncut.vertices.size();
        faces += host->uncut.faces.size();
        wires += host->uncut.wires.size();
        for (const auto &[face, record] : host->uncut.faces) {
            if (record.loops.size() > 64)
                fail("HOST_LIMIT", "Hosted face exceeds the boundary loop budget");
            for (const auto &loop : record.loops) {
                if (loop.size() > 4096)
                    fail("HOST_LIMIT", "Hosted face exceeds the loop corner budget");
                corners += loop.size();
            }
        }
        for (const auto &[owner, opening] : host->openings) {
            if (!owner || opening.profile.corners.size() > 256 || opening.vertices.size() > 256 ||
                opening.jambs.size() > 256)
                fail("HOST_LIMIT", "Invalid or oversized hosted opening record");
            openingCorners += opening.profile.corners.size();
        }
    }
    if (vertices > 20000 || faces > 2000 || wires > 20000 || corners > 64000 ||
        openingCorners > 4096)
        fail("HOST_LIMIT", "Hosted geometry exceeds its aggregate document budget");
    for (const auto &[root, attachment] : records.attachments)
        if (!root || !attachment || !attachment->host || !attachment->face ||
            !std::isfinite(attachment->inset) || std::abs(attachment->inset) > coordinateLimit)
            fail("HOST_RECORDS", "Invalid component attachment identity or inset");
}
Transform matrix(const DrawingPlane &frame) {
    Transform result;
    result.m = {frame.xAxis.x,  frame.xAxis.y,  frame.xAxis.z,  0,
                frame.yAxis.x,  frame.yAxis.y,  frame.yAxis.z,  0,
                frame.normal.x, frame.normal.y, frame.normal.z, 0,
                frame.origin.x, frame.origin.y, frame.origin.z, 1};
    result.validate();
    return result;
}
Transform world(const std::map<Id, BodyPtr> &bodies, Id id) {
    Transform result;
    size_t depth = 0;
    while (id) {
        if (!bodies.contains(id) || !bodies.at(id) || ++depth > 128)
            fail("HOST_SCOPE", "Attachment has a missing or cyclic scene parent");
        result = bodies.at(id)->transform * result;
        id = bodies.at(id)->parent;
    }
    return result;
}
bool near(const Transform &a, const Transform &b) {
    for (size_t i = 0; i < a.m.size(); ++i) {
        const auto epsilon =
            i >= 12 ? 4 * tolerance
                    : std::max(1e-10, std::max(std::abs(a.m[i]), std::abs(b.m[i])) * 1e-12);
        if (std::abs(a.m[i] - b.m[i]) > epsilon)
            return false;
    }
    return true;
}
std::set<Id> ownedMembers(const ComponentInstances &instances) {
    std::set<Id> result;
    for (const auto &[root, instance] : instances) {
        if (!instance)
            fail("HOST_SCOPE", "Invalid component instance binding");
        for (const auto &[member, body] : instance->members)
            if (body != root)
                result.insert(body);
    }
    return result;
}
const ComponentDefinition &definitionFor(Id root, const ComponentAttachment &attachment,
                                         const std::map<Id, BodyPtr> &bodies,
                                         const ComponentDefinitions &definitions,
                                         const ComponentInstances &instances,
                                         const std::set<Id> &owned) {
    if (!bodies.contains(root) || !bodies.at(root) || !instances.contains(root) ||
        !instances.at(root) || owned.contains(root) || !bodies.contains(attachment.host) ||
        !bodies.at(attachment.host) || bodies.at(attachment.host)->kind != BodyKind::Geometry ||
        owned.contains(attachment.host) || instances.contains(attachment.host))
        fail("HOST_SCOPE",
             "Choose an independent component placement and an ordinary geometry host");
    const auto id = instances.at(root)->definition;
    if (!definitions.contains(id) || !definitions.at(id) || !definitions.at(id)->glue)
        fail("HOST_SCOPE", "Hosted component requires an explicit canonical glue face");
    return *definitions.at(id);
}
OpeningProfile cutProfile(const Surface &uncut, const ComponentAttachment &attachment,
                          const ComponentDefinition &definition) {
    attachment.frame.validate();
    if (!uncut.faces.contains(attachment.face))
        fail("HOST_RECORDS", "Attachment references a missing original host face");
    const auto source = resolveComponentGlue(definition);
    const auto normal = uncut.normal(attachment.face);
    const auto anchor = attachment.frame.point({}) - normal * attachment.inset;
    const auto x = attachment.frame.vector({1, 0, 0}), y = attachment.frame.vector({0, 1, 0});
    if (std::abs(dot(normalized(x), normal)) > 1e-9 || std::abs(dot(normalized(y), normal)) > 1e-9)
        fail("HOST_ALIGNMENT", "Component glue plane must remain parallel to its host face");
    // Validates the baseline face, anchor and explicit tangent, including holes.
    (void)componentPlacementOnFace(uncut, attachment.face, {}, {}, {anchor, x});
    OpeningProfile result{attachment.face, {}};
    if (!definition.glue->cutsOpening)
        return result;
    const auto local = attachment.frame * matrix(source.frame).inverse();
    const auto &keys = definition.members.at(definition.glue->member)
                           ->surface.faces.at(definition.glue->face)
                           .loops.front();
    for (size_t i = 0; i < source.profile.size(); ++i) {
        const auto point = local.point(source.profile[i]) - normal * attachment.inset;
        checkPoint(point);
        result.corners.push_back({keys.at(i), point});
    }
    return result;
}
} // namespace
bool HostedComponents::operator==(const HostedComponents &other) const {
    return equalRecords(hosts, other.hosts) && equalRecords(attachments, other.attachments);
}
HostedPtr freezeHostedComponents(const HostedPtr &source) {
    if (!source)
        fail("HOST_RECORDS", "Missing hosted component records");
    bounded(*source);
    auto result = std::make_shared<HostedComponents>();
    for (const auto &[id, host] : source->hosts)
        result->hosts[id] = std::make_shared<const HostedSurface>(*host);
    for (const auto &[id, attachment] : source->attachments)
        result->attachments[id] = std::make_shared<const ComponentAttachment>(*attachment);
    return result;
}
size_t hostedComponentBytes(const HostedPtr &records) {
    if (!records)
        return 0;
    size_t bytes = sizeof(HostedComponents) +
                   records->attachments.size() * (sizeof(ComponentAttachment) + 128);
    for (const auto &[id, host] : records->hosts) {
        bytes += sizeof(HostedSurface) + 128 + host->uncut.vertices.size() * (sizeof(Vec3) + 96) +
                 host->uncut.wires.size() * sizeof(std::array<Id, 2>);
        for (const auto &[face, record] : host->uncut.faces) {
            bytes += sizeof(Face) + 96;
            for (const auto &loop : record.loops)
                bytes += sizeof(std::vector<Id>) + loop.size() * sizeof(Id);
        }
        for (const auto &[owner, opening] : host->openings)
            bytes += sizeof(HostOpening) + 96 +
                     opening.profile.corners.size() * sizeof(OpeningCorner) +
                     opening.vertices.size() * 128 + opening.jambs.size() * 128;
    }
    return bytes;
}
void validateHostedComponents(const HostedComponents &records, const std::map<Id, BodyPtr> &bodies,
                              const ComponentDefinitions &definitions,
                              const ComponentInstances &instances) {
    bounded(records);
    if (records.hosts.empty() && records.attachments.empty())
        return;
    const auto owned = ownedMembers(instances);
    std::map<Id, std::map<Id, OpeningProfile>> requested;
    std::set<Id> usedHosts;
    for (const auto &[root, attachment] : records.attachments) {
        if (!records.hosts.contains(attachment->host))
            fail("HOST_RECORDS", "Attachment has no original host surface");
        const auto &definition =
            definitionFor(root, *attachment, bodies, definitions, instances, owned);
        const auto &host = *records.hosts.at(attachment->host);
        const auto profile = cutProfile(host.uncut, *attachment, definition);
        const auto expected = world(bodies, attachment->host) * attachment->frame *
                              matrix(resolveComponentGlue(definition).frame).inverse();
        if (!near(expected, world(bodies, root)))
            fail("HOST_ALIGNMENT", "Component pose disagrees with its stored host attachment");
        usedHosts.insert(attachment->host);
        if (definition.glue->cutsOpening)
            requested[attachment->host][root] = profile;
    }
    if (usedHosts.size() != records.hosts.size())
        fail("HOST_RECORDS", "An original host surface requires a live component attachment");
    for (const auto &[id, host] : records.hosts) {
        const auto result =
            regenerateHost(host->uncut, *bodies.at(id), host->openings, requested[id]);
        if (result.body != *bodies.at(id) || result.openings != host->openings)
            fail("HOST_RECORDS",
                 "Stored openings disagree with their component profiles or host geometry");
    }
}
namespace {
template <class Records> std::set<Id> changedKeys(const Records &a, const Records &b) {
    std::set<Id> ids;
    for (const auto &[id, value] : a)
        if (!b.contains(id) || !equal(value, b.at(id)))
            ids.insert(id);
    for (const auto &[id, value] : b)
        if (!a.contains(id))
            ids.insert(id);
    return ids;
}
bool sameGeometry(Surface a, const Surface &b) {
    a.nextId = b.nextId;
    return a == b;
}
bool ancestryMoved(const std::map<Id, BodyPtr> &before, const std::map<Id, BodyPtr> &after,
                   Id root) {
    size_t depth = 0;
    for (Id id = root; id; id = after.at(id)->parent) {
        if (++depth > 128 || !after.contains(id) || !after.at(id))
            fail("HOST_SCOPE", "Invalid attachment ancestry");
        if (!before.contains(id) || before.at(id)->parent != after.at(id)->parent ||
            before.at(id)->transform != after.at(id)->transform)
            return true;
    }
    return false;
}
void bodyChange(const Document &before, Edit &edit, std::map<Id, BodyPtr> &bodies, BodyPtr body,
                const std::map<Id, std::vector<Id>> &lineage = {}) {
    if (bodies.contains(body->id) && *bodies.at(body->id) == *body)
        return;
    const auto id = body->id;
    auto change = std::find_if(edit.changes.begin(), edit.changes.end(),
                               [=](const auto &c) { return c.id == id; });
    if (change == edit.changes.end()) {
        edit.changes.push_back(
            {id, before.bodies().contains(id) ? before.bodies().at(id) : nullptr, body});
        change = std::prev(edit.changes.end());
    } else
        change->after = body;
    if (change->before)
        for (const auto &[face, descendants] : lineage)
            if (change->before->surface.faces.contains(face))
                change->faceDescendants[face] = descendants;
    change->edgeAppearancesResolved = true;
    bodies[id] = std::move(body);
}
bool stillGlue(Id root, const ComponentAttachment &attachment, const std::map<Id, BodyPtr> &bodies,
               const ComponentDefinitions &definitions, const ComponentInstances &instances) {
    if (!bodies.contains(root) || !bodies.contains(attachment.host) || !instances.contains(root) ||
        !instances.at(root))
        return false;
    const auto def = definitions.find(instances.at(root)->definition);
    return def != definitions.end() && def->second && def->second->glue;
}
} // namespace
void expandHostedEdit(const Document &before, Edit &edit) {
    if (edit.hostedResolved)
        return;
    const auto &old = before.hostedComponents();
    if (!edit.hosted && old.attachments.empty()) {
        edit.hostedResolved = true;
        return;
    }
    const auto &requestedRecords = edit.hosted ? *edit.hosted->after : old;
    bounded(requestedRecords);
    auto records = std::make_shared<HostedComponents>(requestedRecords);
    if (!equalRecords(old.hosts, records->hosts))
        fail("HOST_RECORDS", "Original host surfaces are maintained by document edits");
    const auto explicitAttachments = changedKeys(old.attachments, records->attachments);
    auto bodies = before.bodies();
    auto definitions = before.definitions();
    auto instances = before.instances();
    // Reject duplicate/stale patches before using caller-owned candidates. The
    // ordinary document validator still checks identities and allocator floors.
    auto patch = [](auto &records, const auto &changes, auto identity) {
        std::set<Id> seen;
        for (const auto &change : changes) {
            const auto id = identity(change);
            if (!id || !seen.insert(id).second || (!change.before && !change.after) ||
                (records.contains(id) ? records.at(id) : nullptr) != change.before)
                fail("HOST_RECORDS", "Invalid or stale scene change for hosted components");
            if (change.after)
                records[id] = change.after;
            else
                records.erase(id);
        }
    };
    patch(bodies, edit.changes, [](const auto &change) { return change.id; });
    patch(definitions, edit.definitions, [](const auto &change) { return change.id; });
    patch(instances, edit.instances, [](const auto &change) { return change.root; });
    const auto owned = ownedMembers(instances);
    for (auto it = records->attachments.begin(); it != records->attachments.end();) {
        const auto root = it->first;
        if (!stillGlue(root, *it->second, bodies, definitions, instances)) {
            if (explicitAttachments.contains(root))
                fail("HOST_SCOPE", "A new attachment requires live host and gluing component");
            it = records->attachments.erase(it);
            continue;
        }
        const auto &definition =
            definitionFor(root, *it->second, bodies, definitions, instances, owned);
        const auto source = matrix(resolveComponentGlue(definition).frame);
        if (!explicitAttachments.contains(root) && ancestryMoved(before.bodies(), bodies, root)) {
            auto attachment = std::make_shared<ComponentAttachment>(*it->second);
            attachment->frame =
                world(bodies, attachment->host).inverse() * world(bodies, root) * source;
            if (!near(attachment->frame, it->second->frame))
                it->second = std::move(attachment);
        }
        if (!records->hosts.contains(it->second->host)) {
            const auto host = it->second->host;
            if (before.bodies().contains(host) &&
                !sameGeometry(before.bodies().at(host)->surface, bodies.at(host)->surface))
                fail("HOST_CHANGED", "Attach after committing independent host geometry edits");
            records->hosts[host] =
                std::make_shared<const HostedSurface>(HostedSurface{bodies.at(host)->surface, {}});
        }
        ++it;
    }
    bounded(*records);
    std::map<Id, std::map<Id, OpeningProfile>> requested;
    std::set<Id> used;
    size_t requestedCorners = 0;
    for (const auto &[root, attachment] : records->attachments) {
        const auto &definition =
            definitionFor(root, *attachment, bodies, definitions, instances, owned);
        const auto profile =
            cutProfile(records->hosts.at(attachment->host)->uncut, *attachment, definition);
        if (profile.corners.size() > 4096 - requestedCorners)
            fail("HOST_LIMIT", "Requested openings exceed the aggregate profile budget");
        requestedCorners += profile.corners.size();
        used.insert(attachment->host);
        if (definition.glue->cutsOpening)
            requested[attachment->host][root] = profile;
        const auto desired = world(bodies, attachment->host) * attachment->frame *
                             matrix(resolveComponentGlue(definition).frame).inverse();
        auto body = std::make_shared<Body>(*bodies.at(root));
        const auto local = world(bodies, body->parent).inverse() * desired;
        if (!near(local, body->transform)) {
            body->transform = local;
            bodyChange(before, edit, bodies, std::move(body));
        }
    }
    for (auto it = records->hosts.begin(); it != records->hosts.end();) {
        const auto id = it->first;
        if (!bodies.contains(id)) {
            it = records->hosts.erase(it);
            continue;
        }
        const auto result =
            regenerateHost(it->second->uncut, *bodies.at(id), it->second->openings, requested[id]);
        bodyChange(before, edit, bodies, std::make_shared<const Body>(result.body),
                   result.faceDescendants);
        if (!used.contains(id))
            it = records->hosts.erase(it);
        else {
            if (result.openings != it->second->openings)
                it->second = std::make_shared<const HostedSurface>(
                    HostedSurface{it->second->uncut, result.openings});
            ++it;
        }
    }
    if (*records == old)
        edit.hosted.reset();
    else
        edit.hosted = HostedChange{before.hostedRecords(), std::move(records)};
    edit.hostedResolved = true;
}
std::set<Id> hostedEditingContexts(const Edit &edit) {
    std::set<Id> result;
    if (!edit.hosted)
        return result;
    const auto &a = *edit.hosted->before, &b = *edit.hosted->after;
    for (auto root : changedKeys(a.attachments, b.attachments)) {
        result.insert(root);
        for (const auto *records : {&a, &b})
            if (records->attachments.contains(root))
                result.insert(records->attachments.at(root)->host);
    }
    for (auto host : changedKeys(a.hosts, b.hosts))
        result.insert(host);
    return result;
}
void validateHostedAmendment(const Edit &original, const HostedComponents &baseline,
                             const HostedComponents &replacement) {
    auto validate = [](const auto &old, const auto &next, const auto &allowed, size_t created) {
        size_t additions = 0;
        for (auto id : changedKeys(old, next)) {
            if (!allowed.contains(id))
                fail("HOST_SCOPE", "Amendment cannot change unrelated hosted component records");
            additions += !old.contains(id) && next.contains(id);
        }
        if (additions != created)
            fail("HOST_SCOPE", "Amendment must retain attachment and host creation counts");
    };
    const auto &previous = original.hosted ? *original.hosted->before : baseline;
    const auto &after = original.hosted ? *original.hosted->after : baseline;
    auto added = [](const auto &a, const auto &b) {
        size_t result = 0;
        for (const auto &[id, value] : b)
            result += !a.contains(id);
        return result;
    };
    validate(baseline.hosts, replacement.hosts, changedKeys(previous.hosts, after.hosts),
             added(previous.hosts, after.hosts));
    validate(baseline.attachments, replacement.attachments,
             changedKeys(previous.attachments, after.attachments),
             added(previous.attachments, after.attachments));
}
namespace {
HostedChangeReport setAttachment(Document &doc, Id root, ComponentAttachment attachment,
                                 const char *label) {
    auto records = std::make_shared<HostedComponents>(doc.hostedComponents());
    if (records->attachments.contains(root) && *records->attachments.at(root) == attachment)
        return {};
    records->attachments[root] = std::make_shared<const ComponentAttachment>(attachment);
    Edit edit{label, {}};
    edit.hosted = HostedChange{doc.hostedRecords(), records};
    return doc.apply(std::move(edit), doc.revision());
}
const Surface &originalHost(const Document &doc, Id host) {
    if (doc.hostedComponents().hosts.contains(host))
        return doc.hostedComponents().hosts.at(host)->uncut;
    if (!doc.bodies().contains(host))
        fail("HOST_SCOPE", "Choose an existing host");
    return doc.bodies().at(host)->surface;
}
} // namespace
HostedChangeReport attachComponent(Document &doc, Id root, Id host, Id face,
                                   const FacePlacementOptions &placement, double inset) {
    ComponentAttachment attachment{host, face, {}, inset};
    const auto owned = ownedMembers(doc.instances());
    const auto &definition =
        definitionFor(root, attachment, doc.bodies(), doc.definitions(), doc.instances(), owned);
    const auto source = matrix(resolveComponentGlue(definition).frame);
    const auto &surface = originalHost(doc, host);
    const auto placed = componentPlacementOnFace(surface, face, doc.worldTransform(host),
                                                 resolveComponentGlue(definition).frame, placement);
    attachment.frame = doc.worldTransform(host).inverse() * placed.world * source;
    attachment.frame = Transform::translation(surface.normal(face) * inset) * attachment.frame;
    return setAttachment(doc, root, attachment, "Attach component to face");
}
HostedChangeReport bindComponentAtCurrentPose(Document &doc, Id root, Id host, Id face,
                                              double inset) {
    ComponentAttachment attachment{host, face, {}, inset};
    const auto owned = ownedMembers(doc.instances());
    const auto &definition =
        definitionFor(root, attachment, doc.bodies(), doc.definitions(), doc.instances(), owned);
    attachment.frame = doc.worldTransform(host).inverse() * doc.worldTransform(root) *
                       matrix(resolveComponentGlue(definition).frame);
    return setAttachment(doc, root, attachment, "Bind component to face");
}
HostedChangeReport detachComponent(Document &doc, Id root) {
    if (!doc.hostedComponents().attachments.contains(root))
        return {};
    auto records = std::make_shared<HostedComponents>(doc.hostedComponents());
    records->attachments.erase(root);
    Edit edit{"Detach component", {}};
    edit.hosted = HostedChange{doc.hostedRecords(), records};
    return doc.apply(std::move(edit), doc.revision());
}
HostedChangeReport bakeHostedComponents(Document &doc, Id host) {
    if (!doc.hostedComponents().hosts.contains(host))
        return {};
    auto records = std::make_shared<HostedComponents>(doc.hostedComponents());
    records->hosts.erase(host);
    std::erase_if(records->attachments,
                  [=](const auto &entry) { return entry.second->host == host; });
    Edit edit{"Bake hosted components", {}};
    edit.hosted = HostedChange{doc.hostedRecords(), records};
    edit.hostedResolved = true;
    return doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
