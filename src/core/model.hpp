#pragma once
#include "core/transform.hpp"
#include "geometry/topology.hpp"
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <variant>
namespace sketchy {
struct Body {
    Id id{};
    std::string name{"Face"};
    std::array<float, 3> color{0.73f, 0.79f, 0.73f};
    Surface surface;
    Topology topology;
    Transform transform;
    Id parent{};
    std::map<std::string, std::variant<bool, double, std::string>> properties;
    bool operator==(const Body &) const = default;
};
using BodyPtr = std::shared_ptr<const Body>;
struct Change {
    Id id;
    BodyPtr before, after;
    std::map<Id, std::vector<Id>> faceDescendants{};
};
struct Edit {
    std::string label;
    std::vector<Change> changes;
    size_t bytes{};
    Id nextIdFloor{};
};
using ChangeReport = std::map<Id, TopologyChanges>;
class Document {
    struct State {};
    using StatePtr = std::shared_ptr<const State>;

  public:
    class SaveStamp {
        friend class Document;
        StatePtr session, state;
    };
    const std::map<Id, BodyPtr> &bodies() const { return bodies_; }
    std::uint64_t revision() const { return revision_; }
    Id nextId() const { return nextId_; }
    const std::string &identity() const { return identity_; }
    bool dirty() const { return state_ != savedState_; }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t historyBytes() const { return historyBytes_; }
    static constexpr size_t historyLimit = 64 * 1024 * 1024;
    Document();
    Id addFace(const std::vector<std::vector<Vec3>> &loops, std::string name = "Face");
    void extrude(Id body, Id face, double distance);
    void move(Id body, Vec3 delta);
    void erase(Id body);
    void paint(Id body, std::array<float, 3> color);
    Transform worldTransform(Id body) const;
    std::vector<Triangle> worldTriangles(Id body) const;
    double worldArea(Id body, Id face) const;
    void transform(Id body, Transform local, Id parent = 0);
    ChangeReport apply(Edit edit, std::uint64_t expectedRevision);
    Id addWire(Id context, Vec3 a, Vec3 b);
    void splitEdge(Id context, Id edge, double fraction);
    void undo();
    void redo();
    SaveStamp saveStamp() const;
    bool owns(const SaveStamp &stamp) const { return stamp.session == session_; }
    bool markSaved(const SaveStamp &stamp);
    void markSaved() { savedState_ = state_; }
    void restore(std::string identity, Id next, std::map<Id, BodyPtr> bodies,
                 std::uint64_t revision = 0);

  private:
    std::string identity_;
    std::map<Id, BodyPtr> bodies_;
    Id nextId_{1};
    std::map<Id, Id> surfaceFloors_, edgeFloors_;
    std::uint64_t revision_{0};
    StatePtr session_{std::make_shared<State>()};
    StatePtr state_{std::make_shared<State>()}, savedState_{state_};
    struct History {
        Edit edit;
        StatePtr before, after;
    };
    std::deque<History> undo_, redo_;
    size_t historyBytes_{};
    void update(Edit edit, bool forward);
};
} // namespace sketchy
