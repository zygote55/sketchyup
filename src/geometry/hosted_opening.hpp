#pragma once
#include "geometry/cleanup.hpp"
#include <string>
namespace sketchy {
class OpeningError : public std::runtime_error {
  public:
    OpeningError(std::string code, const std::string &message)
        : std::runtime_error(message), code_(std::move(code)) {}
    const std::string &code() const { return code_; }

  private:
    std::string code_;
};
struct HostedOpening {
    TopologyEdit edit;
    Id entry{}, exit{};
    double depth{}, removedVolume{};
    std::vector<Id> jambs;
};
// Cut a simple host-local profile through the first parallel opposing face that
// contains it. Requires an outward-wound closed host, strict boundary clearance
// and no intervening geometry. Preserves all existing face/vertex identities.
// Immutable: no binding, history, material assignment or document publication.
HostedOpening cutHostedOpening(const Surface &host, Id face, const std::vector<Vec3> &profile);
} // namespace sketchy
