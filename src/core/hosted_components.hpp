#pragma once
#include "core/component_placement.hpp"
#include "core/component_records.hpp"
#include "core/host_regeneration.hpp"
#include <set>
namespace sketchy {
struct ComponentAttachment {
    Id host{}, face{};
    Transform frame; // Glue coordinates -> host coordinates, including explicit inset.
    double inset{};  // Signed distance from host plane in host-local units.
    bool operator==(const ComponentAttachment &) const = default;
};
struct HostedSurface {
    Surface uncut;
    HostOpenings openings;
    bool operator==(const HostedSurface &) const = default;
};
struct HostedComponents {
    std::map<Id, std::shared_ptr<const HostedSurface>> hosts;
    std::map<Id, std::shared_ptr<const ComponentAttachment>> attachments;
    bool operator==(const HostedComponents &other) const;
};
using HostedPtr = std::shared_ptr<const HostedComponents>;
struct HostedChange {
    HostedPtr before, after;
};
HostedPtr freezeHostedComponents(const HostedPtr &records);
size_t hostedComponentBytes(const HostedPtr &records);
void validateHostedComponents(const HostedComponents &records, const std::map<Id, BodyPtr> &bodies,
                              const ComponentDefinitions &definitions,
                              const ComponentInstances &instances);
// Authoritative edit expansion; resolved composed edits still undergo final validation.
void expandHostedEdit(const Document &before, Edit &edit);
std::set<Id> hostedEditingContexts(const Edit &edit);
void validateHostedAmendment(const Edit &original, const HostedComponents &baseline,
                             const HostedComponents &replacement);
using HostedChangeReport = std::map<Id, TopologyChanges>;
HostedChangeReport attachComponent(Document &doc, Id instance, Id host, Id face,
                                   const FacePlacementOptions &placement, double inset = 0);
HostedChangeReport bindComponentAtCurrentPose(Document &doc, Id instance, Id host, Id face,
                                              double inset);
HostedChangeReport detachComponent(Document &doc, Id instance);
// Keep current host geometry and release all of its attachments in one edit.
HostedChangeReport bakeHostedComponents(Document &doc, Id host);
} // namespace sketchy
