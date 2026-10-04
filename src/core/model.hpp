#pragma once
#include "core/component_records.hpp"
#include "core/material_records.hpp"
#include <deque>
#include <functional>
#include <optional>
namespace sketchy {
struct Change {
    Id id;
    BodyPtr before, after;
    std::map<Id, std::vector<Id>> faceDescendants{}, vertexDescendants{}, edgeDescendants{};
};
struct DefinitionChange {
    Id id{};
    DefinitionPtr before, after;
};
struct InstanceChange {
    Id root{};
    InstancePtr before, after;
};
struct TagChange {
    Id id{};
    TagPtr before, after;
};
struct MaterialChange {
    Id id{};
    MaterialPtr before, after;
};
struct Edit {
    std::string label;
    std::vector<Change> changes;
    size_t bytes{};
    Id nextIdFloor{};
    std::vector<DefinitionChange> definitions{};
    std::vector<InstanceChange> instances{};
    Id nextDefinitionFloor{};
    std::vector<TagChange> tags{};
    Id nextTagFloor{};
    std::vector<MaterialChange> materials{};
    Id nextMaterialFloor{};
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
    class AmendStamp {
        friend class Document;
        StatePtr session, state;
        std::uint64_t revision{};
    };
    const std::map<Id, BodyPtr> &bodies() const { return bodies_; }
    const ComponentDefinitions &definitions() const { return definitions_; }
    const ComponentInstances &instances() const { return instances_; }
    Id nextDefinitionId() const { return nextDefinitionId_; }
    const TagRecords &tags() const { return tags_; }
    Id nextTagId() const { return nextTagId_; }
    const MaterialRecords &materials() const { return materials_; }
    Id nextMaterialId() const { return nextMaterialId_; }
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
    ChangeReport pushPull(Id body, Id face, double distance, bool newFace = false);
    void move(Id body, Vec3 delta);
    void erase(Id body);
    void paint(Id body, std::array<float, 3> color);
    Transform worldTransform(Id body) const;
    std::vector<Triangle> worldTriangles(Id body) const;
    double worldArea(Id body, Id face) const;
    void transform(Id body, Transform local, Id parent = 0);
    ChangeReport apply(Edit edit, std::uint64_t expectedRevision);
    Id addWire(Id context, Vec3 a, Vec3 b);
    ChangeReport insertEdges(Id context, Vec3 origin, Vec3 normal,
                             const std::vector<std::array<Vec3, 2>> &edges,
                             std::string name = "Planar geometry");
    ChangeReport addCurve(Id context, Curve curve);
    ChangeReport addGuide(Id context, Guide guide);
    ChangeReport eraseGuide(Id context, Id guide);
    ChangeReport clearGuides(Id context = 0);
    ChangeReport splitEdge(Id context, Id edge, double fraction);
    ChangeReport eraseFace(Id context, Id face);
    ChangeReport eraseEdge(Id context, Id edge);
    ChangeReport healFace(Id context, Id edge, Vec3 origin, Vec3 normal);
    ChangeReport cleanup(Id context);
    AmendStamp amendmentStamp() const;
    bool canAmend(const AmendStamp &stamp) const;
    enum class AmendPolicy { FixedContextCount, CopyArray };
    ChangeReport amendLast(const AmendStamp &stamp, const std::function<void(Document &)> &replace,
                           AmendPolicy policy = AmendPolicy::FixedContextCount);
    void undo();
    void redo();
    SaveStamp saveStamp() const;
    bool owns(const SaveStamp &stamp) const { return stamp.session == session_; }
    bool isCurrentSnapshot(const SaveStamp &stamp) const {
        return owns(stamp) && stamp.state == state_;
    }
    bool markSaved(const SaveStamp &stamp);
    void markSaved() { savedState_ = state_; }
    void restore(std::string identity, Id next, std::map<Id, BodyPtr> bodies,
                 std::uint64_t revision = 0, ComponentDefinitions definitions = {},
                 ComponentInstances instances = {}, Id nextDefinitionId = 1, TagRecords tags = {},
                 Id nextTagId = 1, MaterialRecords materials = {}, Id nextMaterialId = 1);

  private:
    std::string identity_;
    std::map<Id, BodyPtr> bodies_;
    Id nextId_{1};
    ComponentDefinitions definitions_;
    ComponentInstances instances_;
    Id nextDefinitionId_{1};
    TagRecords tags_;
    Id nextTagId_{1};
    MaterialRecords materials_;
    Id nextMaterialId_{1};
    struct DefinitionFloor {
        Id nextMemberId{1};
        std::map<Id, std::pair<Id, Id>> geometry;
    };
    std::map<Id, DefinitionFloor> definitionFloors_;
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
