#pragma once
#include "geometry/topology.hpp"
#include <string>
namespace sketchy {
class PlanarError : public std::runtime_error {
  public:
    PlanarError(std::string code, const std::string &message)
        : std::runtime_error(message), code_(std::move(code)) {}
    const std::string &code() const { return code_; }

  private:
    std::string code_;
};
struct PlanarResult {
    Surface surface;
    std::map<Id, std::vector<Id>> faces;
};
// Rebuild finite coplanar arrangements atomically. Existing explicit holes remain
// void. Newly closed regions form faces; unrelated open networks remain wires.
PlanarResult insertPlanarEdges(const Surface &source, Vec3 origin, Vec3 normal,
                               const std::vector<std::array<Vec3, 2>> &edges, bool heal = false);
} // namespace sketchy
