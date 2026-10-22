#pragma once
// Where an assembly is placed, apart from the library operations (io/component_library.hpp)
// so that app/viewport.hpp needs only this (build fan-out, ADR 0170).
#include "core/transform.hpp"
#include <optional>
namespace sketchy {
// A gluing assembly placed on a host face, with component.attach's parameters: the anchor
// and tangent are host-local, the inset a signed distance from the host plane.
struct AssemblyHostFace {
    Id host{}, face{};
    Vec3 anchor{}, tangent{1, 0, 0};
    double inset{};
};
struct AssemblyPlacement {
    Transform transform{};               // free placement (ignored on a host face)
    std::optional<AssemblyHostFace> host; // a gluing assembly attached to a face
    Id parent{};
};
} // namespace sketchy
