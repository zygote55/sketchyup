#pragma once
#include "geometry/surface.hpp"
#include <string>
namespace sketchy {
class SweepError : public std::runtime_error {
  public:
    SweepError(std::string code, const std::string &message)
        : std::runtime_error(message), code_(std::move(code)) {}
    const std::string &code() const { return code_; }

  private:
    std::string code_;
};
struct SweepResult {
    Surface surface;
    std::vector<Id> caps;
    // Source boundary vertex pair -> generated side faces, in path order.
    std::map<std::array<Id, 2>, std::vector<Id>> sides;
    std::vector<std::vector<Id>> segments;
    double volume{};
};
// Immutable profile sweep along a bounded polyline. The path starts in the
// profile plane; shortest rotation aligns its normal with the first segment.
// Minimal-rotation transport and bisector miters (limit 4x) define corners.
// Closed paths must return the frame without twist; closed holed profiles are
// explicitly unsupported by this initial single-shell validation adapter.
SweepResult sweepProfile(const Surface &source, Id face, const std::vector<Vec3> &path,
                         bool closed = false);
} // namespace sketchy
