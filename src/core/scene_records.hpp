#pragma once
#include "core/model_style.hpp"
#include "solar/solar.hpp"
#include "core/section_records.hpp"
#include "core/transform.hpp"
#include <compare>
#include <map>
#include <memory>
#include <optional>
#include <set>
namespace sketchy {
// Native Z-up orbit pose. Angles are degrees, coordinates and distance are metres.
struct SceneCamera {
    Vec3 target{};
    double yaw{-45}, pitch{35}, distance{14}, fieldOfView{45};
    bool orthographic{};
    bool operator==(const SceneCamera &) const = default;
    void validate() const;
};
enum class SceneEntityKind { Body, Face, Edge, Guide };
struct SceneEntity {
    Id body{};
    SceneEntityKind kind{SceneEntityKind::Body};
    Id entity{};
    auto operator<=>(const SceneEntity &) const = default;
    void validate() const;
};
struct SceneVisibility {
    // Intrinsic flags, not inherited visibility. Unrecorded bodies/tags stay unchanged.
    std::map<Id, bool> bodyVisible, tagVisible;
    std::set<SceneEntity> hiddenEntities;
    bool showHidden{};
    bool operator==(const SceneVisibility &) const = default;
    void validate() const;
};
struct SceneSection {
    // An absent plane captures clipping OFF. An absent SceneSnapshot::section
    // means that the scene does not control section state at all.
    std::optional<std::array<double, 4>> plane;
    ActiveSections active; // Complete named activation snapshot; empty turns named cuts off.
    bool operator==(const SceneSection &) const = default;
    void validate() const;
};
struct SceneSnapshot {
    std::optional<SceneCamera> camera;
    std::optional<SceneVisibility> visibility;
    std::optional<ModelStyle> style;
    std::optional<SceneSection> section;
    std::optional<SolarSettings> solar;
    bool operator==(const SceneSnapshot &) const = default;
    void validate() const;
};
struct SceneRecord {
    Id id{};
    std::string name;
    std::uint32_t position{};
    SceneSnapshot snapshot;
    bool operator==(const SceneRecord &) const = default;
};
using ScenePtr = std::shared_ptr<const SceneRecord>;
using SceneRecords = std::map<Id, ScenePtr>;
inline constexpr size_t sceneCountLimit = 256, sceneBytesLimit = 8 * 1024 * 1024;
size_t sceneBytes(const ScenePtr &scene);
void validateSceneRecords(const SceneRecords &scenes, Id next);
class Document;
struct MissingSceneReferences {
    std::set<Id> bodies, tags, sections;
    std::set<SceneEntity> entities;
    bool empty() const { return bodies.empty() && tags.empty() && entities.empty() && sections.empty(); }
    size_t size() const { return bodies.size() + tags.size() + entities.size() + sections.size(); }
};
MissingSceneReferences missingSceneReferences(const Document &doc, const SceneSnapshot &snapshot);
// Creation/update require current references; later model edits may leave diagnosed
// missing references in the immutable saved snapshot.
void validateSceneCapture(const Document &doc, const SceneSnapshot &snapshot);
} // namespace sketchy
