#pragma once
#include "core/model.hpp"
#include <array>
namespace sketchy {
struct ScreenPoint {
    double x{}, y{}, depth{}, w{};
};
struct InferenceCamera {
    std::array<double, 16> clipFromWorld{}, worldFromClip{};
    double width{}, height{}; // Logical pixels, independent of device pixel ratio.
    std::optional<ScreenPoint> project(Vec3 point) const;
    std::pair<Vec3, Vec3> ray(double x, double y) const;
};
enum class InferenceKind {
    Endpoint,
    GuidePoint,
    Intersection,
    Midpoint,
    Center,
    OnEdge,
    OnGuide,
    OnFace
};
const char *inferenceLabel(InferenceKind kind);
enum class InferenceEntity { Vertex, Edge, Face, Curve, Guide };
const char *inferenceEntityLabel(InferenceEntity entity);
struct InferenceCandidate {
    InferenceKind kind{};
    Vec3 point;
    Id body{}, entity{}, otherBody{}, otherEntity{};
    double pixels{}, depth{};
    InferenceEntity entityType{InferenceEntity::Vertex};
    InferenceEntity otherEntityType{InferenceEntity::Vertex};
};
struct InferenceQuery {
    InferenceCamera camera;
    double x{}, y{}, radius{8};
    std::optional<DrawingPlane> plane;
    Id context{}; // Zero searches all visible editing contexts.
    bool includeGuides{true};
    // Optional editor view policy. Visible but ineligible faces still occlude.
    std::function<bool(Id, InferenceEntity, Id)> visible{}, eligible{};
    // View clipping affects both candidate points and occluding ray intersections.
    std::function<bool(Id, Vec3)> pointVisible{};
    // Derived surfaces (for example section caps) may occlude native inference.
    std::function<bool(Vec3, Vec3, double)> extraOcclusion{};
};
struct InferenceResult {
    std::vector<InferenceCandidate> candidates;
    size_t visitedPrimitives{}, intersectionPairs{};
    bool truncated{};
};
namespace inference_detail {
struct Box {
    Vec3 low{}, high{};
};
// World-space primitive, as presented to the query core.
struct Primitive {
    InferenceKind kind{};
    Id entity{};
    Vec3 a{}, b{}, c{};
    Box box;
};
} // namespace inference_detail
// Two-level index. Each distinct body geometry (one per component definition
// member, shared by every instance) is indexed once in its local space. Every
// scene body is a placement: its composed world transform plus that shared local
// index. Queries transform candidate primitives to world space on the fly with the
// placement's own transform, so snapped points equal a fully expanded world index.
class InferenceIndex {
  public:
    void sync(const Document &document, const std::function<bool()> &canceled = {});
    InferenceResult query(const InferenceQuery &query) const;
    // Placement entries (re)built: one per new, edited or moved scene body.
    size_t bodyBuilds() const { return bodyBuilds_; }
    // Local geometry indexes built; a moved placement or an instance of an
    // already indexed definition member builds none.
    size_t localBuilds() const { return localBuilds_; }
    // Primitives visible to queries, as if every placement were expanded.
    size_t primitiveCount() const;
    // Primitives actually stored: shared local primitives once, plus guides.
    size_t indexedPrimitiveCount() const;
    // Approximate retained bytes of index-owned structures (shared parts once).
    size_t indexBytes() const;

    struct LocalPrimitive {
        InferenceKind kind{};
        Id entity{};
        Vec3 a{}, b{}, c{}; // Midpoint keeps its edge endpoints in a and b.
    };
    struct Node {
        inference_detail::Box box;
        std::uint32_t first{}, count{}, left{}, right{};
    };
    struct Local;
    struct Placement;

  private:
    std::map<Id, std::shared_ptr<const Placement>> placements_;
    std::vector<std::pair<Id, const Placement *>> entries_;
    std::vector<std::uint32_t> entryOrder_;
    std::vector<Node> nodes_;
    std::string identity_;
    Document::SaveStamp snapshot_;
    std::uint64_t revision_{UINT64_MAX};
    size_t bodyBuilds_{}, localBuilds_{}, primitiveCount_{};
    void visit(const std::array<std::array<double, 4>, 6> &planes, Id context,
               const std::function<void(Id, const inference_detail::Primitive &)> &fn) const;
};
} // namespace sketchy
