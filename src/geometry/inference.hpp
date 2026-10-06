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
// Per-body immutable geometry caches and a two-level world-space BVH. Camera
// movement changes only the query frustum, never the indexed world geometry.
class InferenceIndex {
  public:
    void sync(const Document &document, const std::function<bool()> &canceled = {});
    InferenceResult query(const InferenceQuery &query) const;
    size_t bodyBuilds() const { return bodyBuilds_; }
    size_t primitiveCount() const;

  private:
    struct Box {
        Vec3 low{}, high{};
    };
    struct Node {
        Box box;
        size_t first{}, count{}, left{}, right{};
    };
    struct Primitive {
        InferenceKind kind;
        Id entity{};
        Vec3 a{}, b{}, c{};
        Box box;
    };
    struct Cache {
        BodyPtr record;
        Transform world;
        std::map<Id, std::vector<Id>> vertexEdges;
        std::vector<Primitive> primitives;
        std::vector<size_t> order;
        std::vector<Node> nodes;
        Box box;
    };
    std::map<Id, std::shared_ptr<const Cache>> bodies_;
    std::vector<Id> bodyIds_;
    std::vector<size_t> bodyOrder_;
    std::vector<Node> nodes_;
    std::string identity_;
    Document::SaveStamp snapshot_;
    std::uint64_t revision_{UINT64_MAX};
    size_t bodyBuilds_{};
    static Box bounds(Vec3 a, Vec3 b, Vec3 c);
    static Box join(Box a, Box b);
    static std::vector<Node> build(std::vector<size_t> &order, const std::vector<Box> &boxes);
    using Planes = std::array<std::array<double, 4>, 6>;
    static Planes frustum(const InferenceCamera &camera, double x, double y, double radius);
    static bool intersects(Box box, const Planes &planes);
    void visit(const Planes &planes, Id context,
               const std::function<void(Id, const Primitive &)> &fn) const;
};
} // namespace sketchy
