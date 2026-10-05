#pragma once
#include "geometry/surface.hpp"
#include <string>
namespace sketchy {
class OffsetError : public std::runtime_error {
  public:
    OffsetError(std::string code, const std::string &message)
        : std::runtime_error(message), code_(std::move(code)) {}
    const std::string &code() const { return code_; }

  private:
    std::string code_;
};
struct OffsetRegion {
    // Outer boundary followed by its holes, in the source face's plane.
    std::vector<std::vector<Vec3>> loops;
};
struct OffsetResult {
    std::vector<OffsetRegion> regions;
    size_t inputHoles{}, outputHoles{};
    bool collapsed() const { return regions.empty(); }
};
// Immutable closed-region offset in local metres: positive expands material and
// contracts holes; negative erodes material. Retains every surviving island.
// Miter joins use a 4x limit with squared corners beyond that limit. Zero is exact.
// Empty regions report complete collapse at modeling precision, never deletion.
// Input: <=1024 vertices / 64 loops. Output: <=4096 vertices / 256 regions.
OffsetResult offsetFaceRegion(const Surface &source, Id face, double distance);
} // namespace sketchy
