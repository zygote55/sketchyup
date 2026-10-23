#include "ifc/placement.hpp"

#include <cmath>
#include <set>
#include <vector>

namespace sketchyup::ifc {
namespace {

using V3 = std::array<double, 3>;

V3 sub(V3 a, V3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double dot(V3 a, V3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 cross(V3 a, V3 b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double norm(V3 a) { return std::sqrt(dot(a, a)); }

std::optional<V3> direction(const Inst& d) {
  if (!d) return std::nullopt;
  const auto ratios = numbers(d, "DirectionRatios");
  if (ratios.size() < 2) return std::nullopt;
  V3 v{ratios[0], ratios[1], ratios.size() > 2 ? ratios[2] : 0.0};
  const double length = norm(v);
  if (!(length > 1e-12) || !std::isfinite(length)) return std::nullopt;
  return V3{v[0] / length, v[1] / length, v[2] / length};
}

std::optional<Mat> axisPlacement(const Inst& placement, double mpu) {
  const bool three = isA(placement, "IfcAxis2Placement3D");
  const bool two = isA(placement, "IfcAxis2Placement2D");
  if (!three && !two) return std::nullopt;
  const Inst point = ref(placement, "Location");
  const auto coordinates = numbers(point, "Coordinates");
  if (coordinates.size() < 2) return std::nullopt;
  V3 origin{coordinates[0] * mpu, coordinates[1] * mpu, coordinates.size() > 2 ? coordinates[2] * mpu : 0.0};
  V3 z{0, 0, 1}, x{1, 0, 0};
  if (three) {
    if (ref(placement, "Axis")) {
      const auto axis = direction(ref(placement, "Axis"));
      if (!axis) return std::nullopt;
      z = *axis;
    }
    if (ref(placement, "RefDirection")) {
      const auto reference = direction(ref(placement, "RefDirection"));
      if (!reference) return std::nullopt;
      x = *reference;
    } else if (!(std::abs(z[0]) < 1e-12 && std::abs(z[1]) < 1e-12 && z[2] > 0)) {
      // IfcFirstProjAxis: without a reference direction an axis other than +Z uses the world Z
      // direction projected onto the plane of the placement (+X when the axis is already +Z); an
      // axis along -Z makes that projection empty, and +X is used instead.
      x = {0, 0, 1};
      if (std::abs(z[2]) > 1 - 1e-12) x = {1, 0, 0};
    }
  } else if (ref(placement, "RefDirection")) {
    const auto reference = direction(ref(placement, "RefDirection"));
    if (!reference) return std::nullopt;
    x = *reference;
  }
  // x is made orthogonal to z; y completes a right-handed frame.
  const double along = dot(x, z);
  x = sub(x, {z[0] * along, z[1] * along, z[2] * along});
  const double length = norm(x);
  if (!(length > 1e-9)) return std::nullopt;
  x = {x[0] / length, x[1] / length, x[2] / length};
  const V3 y = cross(z, x);
  Mat m{x[0], y[0], z[0], origin[0], x[1], y[1], z[1], origin[1], x[2], y[2], z[2], origin[2], 0, 0, 0, 1};
  if (!finiteMatrix(m)) return std::nullopt;
  return m;
}

}  // namespace

std::optional<Mat> axisPlacementMatrix(const Inst& placement, double metresPerUnit) { return axisPlacement(placement, metresPerUnit); }

Mat identityMatrix() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

Mat multiply(const Mat& a, const Mat& b) {
  Mat r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      double sum = 0;
      for (int k = 0; k < 4; ++k) sum += a[static_cast<std::size_t>(i * 4 + k)] * b[static_cast<std::size_t>(k * 4 + j)];
      r[static_cast<std::size_t>(i * 4 + j)] = sum;
    }
  return r;
}

std::optional<Mat> inverseAffine(const Mat& m) {
  // Inverse of the 3x3 block by cofactors, then the translation.
  const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
  const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (!(std::abs(det) > 1e-14) || !std::isfinite(det)) return std::nullopt;
  const double s = 1.0 / det;
  Mat r{(e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s, 0,
        (f * g - d * i) * s, (a * i - c * g) * s, (c * d - a * f) * s, 0,
        (d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s, 0,
        0, 0, 0, 1};
  const double tx = m[3], ty = m[7], tz = m[11];
  r[3] = -(r[0] * tx + r[1] * ty + r[2] * tz);
  r[7] = -(r[4] * tx + r[5] * ty + r[6] * tz);
  r[11] = -(r[8] * tx + r[9] * ty + r[10] * tz);
  return r;
}

bool finiteMatrix(const Mat& m) {
  for (const double v : m)
    if (!std::isfinite(v)) return false;
  return true;
}

std::optional<Mat> placementWorld(const Inst& placement, double metresPerUnit, std::string& reason) {
  if (!placement) {
    reason.clear();
    return identityMatrix();
  }
  std::vector<Mat> chain;
  std::set<int> seen;
  Inst current = placement;
  while (current) {
    if (!seen.insert(stepOf(current)).second) {
      reason = "placementCycle";
      return std::nullopt;
    }
    if (seen.size() > 256) {
      reason = "badPlacement";
      return std::nullopt;
    }
    if (isA(current, "IfcGridPlacement")) {
      reason = "gridPlacement";
      return std::nullopt;
    }
    if (isA(current, "IfcLinearPlacement")) {
      reason = "linearPlacement";
      return std::nullopt;
    }
    if (!isA(current, "IfcLocalPlacement")) {
      reason = "unknownPlacement";
      return std::nullopt;
    }
    const auto local = axisPlacement(ref(current, "RelativePlacement"), metresPerUnit);
    if (!local) {
      reason = "badPlacement";
      return std::nullopt;
    }
    chain.push_back(*local);
    current = ref(current, "PlacementRelTo");
  }
  Mat world = identityMatrix();
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) world = multiply(world, *it);
  if (!finiteMatrix(world)) {
    reason = "badPlacement";
    return std::nullopt;
  }
  return world;
}

}  // namespace sketchyup::ifc
