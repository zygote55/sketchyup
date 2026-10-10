#pragma once
#include "core/asset_records.hpp"
#include "core/annotation_records.hpp"
#include "core/component_records.hpp"
#include "core/hosted_components.hpp"
#include "core/material_records.hpp"
#include "core/model_style.hpp"
#include "core/scene_records.hpp"
#include "core/section_records.hpp"
#include "core/units.hpp"
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
namespace sketchy {
struct Change {
    Id id;
    BodyPtr before, after;
    std::map<Id, std::vector<Id>> faceDescendants{}, vertexDescendants{}, edgeDescendants{};
    // True for validated snapshots/composed edits. Do not re-inherit metadata
    // that a later operation in the same transaction intentionally changed.
    bool edgeAppearancesResolved{};
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
struct AssetChange {
    Id id{};
    AssetPtr before, after;
};
struct SceneChange {
    Id id{};
    ScenePtr before, after;
};
struct SectionChange {
    Id id{};
    SectionPtr before, after;
};
struct AnnotationChange {
    Id id{};
    AnnotationPtr before, after;
};
struct HistoryMetadata {
    std::string taskId, request;
    bool assistant{};
    bool operator==(const HistoryMetadata &) const = default;
};
struct HistoryEntry {
    size_t position{};
    std::string label;
    HistoryMetadata metadata;
    bool applied{}, saved{};
};
struct HistoryPage {
    size_t total{}, position{}, offset{}, bytes{};
    bool pruned{}, baseSaved{};
    std::vector<HistoryEntry> entries;
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
    std::vector<AssetChange> assets{};
    Id nextAssetFloor{};
    HistoryMetadata metadata{};
    std::optional<std::pair<DisplayUnit, DisplayUnit>> displayUnits{};
    std::optional<std::pair<int, int>> displayPrecision{};
    std::optional<HostedChange> hosted{};
    bool hostedResolved{};
    std::optional<std::pair<ModelStyle, ModelStyle>> style{};
    std::vector<SceneChange> scenes{};
    Id nextSceneFloor{};
    std::vector<SectionChange> sections{};
    Id nextSectionFloor{};
    std::optional<std::pair<ActiveSections, ActiveSections>> activeSections{};
    std::vector<AnnotationChange> annotations{};
    Id nextAnnotationFloor{};
    bool annotationsResolved{};
    std::optional<std::pair<SolarSettings, SolarSettings>> solar{};
};
using ChangeReport = std::map<Id, TopologyChanges>;
// Running materialized-record totals compared with DocumentLimits.
struct DocumentTotals {
    size_t records{}, vertices{}, faces{}, wires{}, edges{}, curves{}, guides{};
    bool operator==(const DocumentTotals &) const = default;
};
// Raised only by the test/debug full-validation oracle; never by ordinary validation.
struct ValidationOracleMismatch : std::logic_error {
    using std::logic_error::logic_error;
};
class Document {
    struct State {};
    using StatePtr = std::shared_ptr<const State>;

  public:
    class SaveStamp {
        friend class Document;
        StatePtr session, state;
    };
    class PreparedEdit {
        friend class Document;
        PreparedEdit() = default;
        SaveStamp base_;
        std::uint64_t revision_{};
        Edit edit_;
        std::shared_ptr<const Document> snapshot_;

      public:
        const Document &snapshot() const { return *snapshot_; }
        std::uint64_t baseRevision() const { return revision_; }
        size_t retainedBytes() const;
    };
    // The callback runs only on a private history-free copy and must produce one
    // composed edit. The returned immutable proposal cannot publish itself.
    PreparedEdit prepareEdit(const std::function<void(Document &)> &operation) const;
    bool canApply(const PreparedEdit &prepared) const;
    ChangeReport applyPrepared(const PreparedEdit &prepared);
    class AmendStamp {
        friend class Document;
        StatePtr session, state;
        std::uint64_t revision{};
    };
    const std::map<Id, BodyPtr> &bodies() const { return bodies_; }
    const ComponentDefinitions &definitions() const { return definitions_; }
    const ComponentInstances &instances() const { return instances_; }
    const HostedComponents &hostedComponents() const { return *hosted_; }
    const HostedPtr &hostedRecords() const { return hosted_; }
    Id nextDefinitionId() const { return nextDefinitionId_; }
    const TagRecords &tags() const { return tags_; }
    Id nextTagId() const { return nextTagId_; }
    const MaterialRecords &materials() const { return materials_; }
    Id nextMaterialId() const { return nextMaterialId_; }
    const AssetRecords &assets() const { return assets_; }
    Id nextAssetId() const { return nextAssetId_; }
    const SceneRecords &scenes() const { return scenes_; }
    Id nextSceneId() const { return nextSceneId_; }
    const SectionRecords &sections() const { return sections_; }
    Id nextSectionId() const { return nextSectionId_; }
    const ActiveSections &activeSections() const { return activeSections_; }
    const AnnotationRecords &annotations() const { return annotations_; }
    Id nextAnnotationId() const { return nextAnnotationId_; }
    std::uint64_t revision() const { return revision_; }
    // Maintained incrementally by apply, undo/redo, amendment and restore.
    const DocumentTotals &materializedTotals() const { return totals_; }
    // Test/debug switch (default off, or SKETCHYUP_FULL_VALIDATION_ORACLE=1). When on,
    // every apply also runs the full-document validator on a private copy, and apply,
    // undo and redo recount all incremental bookkeeping; any disagreement in the
    // accept/reject decision, error text, report or resulting state throws
    // ValidationOracleMismatch.
    static void setFullValidationOracle(bool enabled);
    static bool fullValidationOracle();
    // Process-wide count of edits that chose the incremental scheme (diagnostics/tests).
    static size_t incrementalValidationCount();
    // Recount totals and every incremental index; throws ValidationOracleMismatch.
    void verifyIncrementalState() const;
    Id nextId() const { return nextId_; }
    const std::string &identity() const { return identity_; }
    bool dirty() const { return state_ != savedState_; }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t historyBytes() const { return historyBytes_; }
    static constexpr size_t historyLimit = 64 * 1024 * 1024;
    explicit Document(DisplayUnit units = DisplayUnit::Meters,
                      int displayPrecision = fullDisplayPrecision);
    DisplayUnit displayUnits() const { return displayUnits_; }
    int displayPrecision() const { return displayPrecision_; }
    const ModelStyle &style() const { return style_; }
    void setStyle(const ModelStyle &style);
    const SolarSettings &solar() const { return solar_; }
    void setSolar(const SolarSettings &solar);
    // A unit change resets precision to Full unless a precision is given.
    void setDisplayUnits(DisplayUnit units, std::optional<int> precision = std::nullopt);
    void setDisplayPrecision(int precision);
    Id addFace(const std::vector<std::vector<Vec3>> &loops, std::string name = "Face");
    void extrude(Id body, Id face, double distance);
    ChangeReport pushPull(Id body, Id face, double distance, bool newFace = false);
    ChangeReport offsetFace(Id body, Id face, double distance, bool worldSpace = false);
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
    HistoryPage history(size_t offset = 0, size_t limit = 200) const;
    void navigateHistory(size_t position, std::uint64_t expectedRevision);
    static constexpr size_t historyEntryLimit = 10000;
    SaveStamp saveStamp() const;
    // Share immutable scene records and preserve identity, allocator floors,
    // session and saved-state markers, without retaining undo/redo history.
    Document readSnapshot() const;
    // Conservative admission charge for all retained records, including assets
    // and allocator bookkeeping; shared records are charged at full size.
    size_t readSnapshotBytes() const;
    bool owns(const SaveStamp &stamp) const { return stamp.session == session_; }
    bool isCurrentSnapshot(const SaveStamp &stamp) const {
        return owns(stamp) && stamp.state == state_;
    }
    bool markSaved(const SaveStamp &stamp);
    void markSaved() { savedState_ = state_; }
    // Recovered bytes are not an explicit save, even when there is no undo history.
    void markRecovered() { savedState_.reset(); }
    void restore(std::string identity, Id next, std::map<Id, BodyPtr> bodies,
                 std::uint64_t revision = 0, ComponentDefinitions definitions = {},
                 ComponentInstances instances = {}, Id nextDefinitionId = 1, TagRecords tags = {},
                 Id nextTagId = 1, MaterialRecords materials = {}, Id nextMaterialId = 1,
                 AssetRecords assets = {}, Id nextAssetId = 1,
                 DisplayUnit units = DisplayUnit::Meters,
                 HostedPtr hosted = std::make_shared<const HostedComponents>(),
                 ModelStyle style = {}, SceneRecords scenes = {}, Id nextSceneId = 1,
                 SectionRecords sections = {}, Id nextSectionId = 1, ActiveSections activeSections = {},
                 AnnotationRecords annotations = {}, Id nextAnnotationId = 1, SolarSettings solar = {},
                 int displayPrecision = fullDisplayPrecision);

  private:
    // Only the native decoder may adopt fresh, unaliased mutable body records.
    friend struct DocumentDecodeAccess;
    void restoreRecords(bool decoderOwnsBodies, std::string identity, Id next,
                        std::map<Id, BodyPtr> bodies, std::uint64_t revision,
                        ComponentDefinitions definitions, ComponentInstances instances,
                        Id nextDefinitionId, TagRecords tags, Id nextTagId,
                        MaterialRecords materials, Id nextMaterialId, AssetRecords assets,
                        Id nextAssetId, DisplayUnit units, HostedPtr hosted, ModelStyle style,
                        SceneRecords scenes, Id nextSceneId, SectionRecords sections,
                        Id nextSectionId, ActiveSections activeSections,
                        AnnotationRecords annotations, Id nextAnnotationId, SolarSettings solar,
                        int displayPrecision);

    std::string identity_;
    DisplayUnit displayUnits_{DisplayUnit::Meters};
    int displayPrecision_{fullDisplayPrecision};
    ModelStyle style_;
    SolarSettings solar_;
    std::map<Id, BodyPtr> bodies_;
    Id nextId_{1};
    ComponentDefinitions definitions_;
    ComponentInstances instances_;
    HostedPtr hosted_{std::make_shared<const HostedComponents>()};
    Id nextDefinitionId_{1};
    TagRecords tags_;
    Id nextTagId_{1};
    MaterialRecords materials_;
    Id nextMaterialId_{1};
    AssetRecords assets_;
    Id nextAssetId_{1};
    SceneRecords scenes_;
    Id nextSceneId_{1};
    SectionRecords sections_;
    Id nextSectionId_{1};
    ActiveSections activeSections_;
    AnnotationRecords annotations_;
    Id nextAnnotationId_{1};
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
    bool historyPruned_{};
    // Incremental bookkeeping. Invariants (checked by verifyIncrementalState):
    // totals_ sums bodies_; lockedBodies_ is the set of locked body IDs;
    // detachedFloors_ is the set of allocator-floor keys without a live body;
    // historyRefs_ counts undo_ entries whose body changes name each ID.
    DocumentTotals totals_;
    std::set<Id> lockedBodies_, detachedFloors_;
    std::map<Id, size_t> historyRefs_;
    void addHistoryRefs(const Edit &edit);
    void removeHistoryRefs(const Edit &edit) noexcept;
    std::string stateMismatch(const Document &other) const;
    ChangeReport applyEdit(Edit edit, std::uint64_t expectedRevision, bool fullValidation);
    void update(Edit edit, bool forward);
};
} // namespace sketchy
