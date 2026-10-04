#pragma once
#include "core/transform.hpp"
#include "geometry/cleanup.hpp"
#include "geometry/curves.hpp"
#include "geometry/guides.hpp"
#include "geometry/planar.hpp"
#include "geometry/push_pull.hpp"
#include "geometry/topology.hpp"
#include <memory>
#include <string>
#include <variant>
namespace sketchy {
enum class BodyKind { Geometry, Group };
struct Body {
    Id id{};
    std::string name{"Face"};
    std::array<float, 3> color{0.73f, 0.79f, 0.73f};
    Surface surface;
    Topology topology;
    std::map<Id, std::array<float, 3>> faceColors;
    std::map<Id, Curve> curves;
    std::map<Id, Guide> guides;
    Transform transform;
    Id parent{};
    BodyKind kind{BodyKind::Geometry};
    bool hidden{}, locked{};
    std::map<std::string, std::variant<bool, double, std::string>> properties;
    bool operator==(const Body &) const = default;
};
using BodyPtr = std::shared_ptr<const Body>;
} // namespace sketchy
