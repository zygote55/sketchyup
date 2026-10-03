#pragma once
#include "geometry/surface.hpp"
namespace sketchy {
// Feasibility adapter: partition one isolated planar face by an infinite plane.
// The source is immutable. Every replacement face gets a fresh identity.
struct FacePartition {
    Surface surface;
    std::map<Id, std::vector<Id>> descendants;
};
FacePartition partitionFace(const Surface &source, Id face, Vec3 point, Vec3 planeNormal);

// Stable edge identities scoped to one surface, independent of face incidence.
// Reconcile only accepted topology. Removed identities are never reused.
class EdgeIdentityIndex {
  public:
    using Key = std::array<Id, 2>;
    std::map<Id, std::vector<Id>> reconcile(const Surface &surface);
    const std::map<Key, Id> &records() const { return records_; }
    Id nextId() const { return next_; }
  private:
    std::map<Key, Id> records_;
    Id next_{1};
    std::map<Id, std::array<Vec3, 2>> geometry_;
};
} // namespace sketchy
