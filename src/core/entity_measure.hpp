#pragma once
#include "core/selection.hpp"
#include "core/transform_selection.hpp"
#include "geometry/solid.hpp"
namespace sketchy {
struct EntityBounds {
    Vec3 low{}, high{};
    Vec3 dimensions() const { return high - low; }
};
struct FrameMeasures {
    std::optional<EntityBounds> bounds;
    double length{}, area{};
    std::optional<double> volume;
    bool infiniteLength{};
};
struct EntityMeasures {
    FrameMeasures world, parent, local;
    Vec3 worldOrigin{}, parentOrigin{};
    size_t records{}, vertices{}, edges{}, faces{}, guides{};
    SolidReport solid{"not_a_context", {}};
    Id solidBody{};
};
// Local bounds use the selected owner's axes; parentOrigin is separately in its
// parent's coordinates. Context measures include all descendants, even hidden ones.
EntityMeasures measureEntity(const Document &doc, SelectedEntity entity);
ChangeReport positionEntity(Document &doc, Id body, Vec3 position, bool world);
ChangeReport dimensionEntity(Document &doc, Id body, Vec3 dimensions, bool world);
using EntityProperties = std::map<std::string, std::variant<bool, double, std::string>>;
ChangeReport setEntityProperties(Document &doc, Id body, EntityProperties properties);
} // namespace sketchy
