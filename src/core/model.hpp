#pragma once
#include "core/annotation_records.hpp"
#include "core/asset_records.hpp"
#include "core/component_records.hpp"
#include "core/drawing_item_records.hpp"
#include "core/drawing_sheet_records.hpp"
#include "core/hosted_components.hpp"
#include "core/material_records.hpp"
#include "core/model_style.hpp"
#include "core/presentation_records.hpp"
#include "core/scene_records.hpp"
#include "core/section_records.hpp"
#include "core/site_origin.hpp"
#include "core/units.hpp"
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
namespace sketchy {
struct ComponentVariant;
using ComponentVariantsPtr = std::shared_ptr<const ComponentVariantIndex>;
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
struct DrawingSheetChange {
    Id id{};
    DrawingSheetPtr before, after;
};
struct DrawingViewChange {
    Id id{};
    DrawingViewPtr before, after;
};
struct DrawingItemChange {
    Id id{};
    DrawingItemPtr before, after;
};
struct SectionChange {
    Id id{};
    SectionPtr before, after;
};
struct CameraPathChange {
    Id id{};
    CameraPathPtr before, after;
};
struct RenderPresetChange {
    Id id{};
    RenderPresetPtr before, after;
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
    // The body changes of a terrain operation (D-043): the record of each terrain whose
    // record the edit creates, changes or drops, and its tiles. Charged against
    // Document::terrainHistoryLimit instead of `bytes` and historyLimit.
    size_t terrainBytes{};
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
    // Document site origin (ADR 0168 §4): established once, then changed only by a
    // re-anchor that moves geometry in the same edit.
    std::optional<std::pair<std::optional<SiteOrigin>, std::optional<SiteOrigin>>> site{};
    // Drawing-sheet edits (ADR 0163) change only these tables. An edit that changes
    // them, or that is tagged with the drawing-sheet scope, must satisfy
    // isDrawingSheetOnlyEdit and must not raise a model allocator floor.
    std::vector<DrawingSheetChange> drawingSheets{};
    Id nextDrawingSheetFloor{};
    std::vector<DrawingViewChange> drawingViews{};
    Id nextDrawingViewFloor{};
    // Page items (ADR 0164) follow the same rule.
    std::vector<DrawingItemChange> drawingItems{};
    Id nextDrawingItemFloor{};
    bool drawingSheetScope{};
    // Paper anchor lineage (ADR 0164 §3, addendum). A model edit rewrites the anchors
    // of bound dimensions and labels inside the same edit, from its topology lineage
    // and these geometry transfers, and may carry those anchor-only item changes.
    // Source context -> contexts that received some of its geometry with the
    // vertex, edge and face IDs unchanged (make group or component from a selection,
    // or a geometry body moved under a new component frame). Not persisted.
    std::map<Id, std::set<Id>> transfers{};
    bool drawingAnchorsResolved{};
    // Component parameter records ride on definitions and overrides on instances
    // (R093.b); driver IDs come from this document-global allocator.
    Id nextComponentDriverFloor{};
    // Camera paths and render presets (ADR 0173 §2, §3): ordinary model edits in schema 30.
    std::vector<CameraPathChange> cameraPaths{};
    Id nextCameraPathFloor{};
    std::vector<RenderPresetChange> renderPresets{};
    Id nextRenderPresetFloor{};
};
using ChangeReport = std::map<Id, TopologyChanges>;
// Running materialized-record totals compared with DocumentLimits::materialized.
// Terrain tiles (ADR 0168 §2) count as records but their triangles, vertices and edges
// count only in terrainTriangles, compared with TerrainLimits::triangles.
struct DocumentTotals {
    size_t records{}, vertices{}, faces{}, wires{}, edges{}, curves{}, guides{};
    size_t terrainTriangles{};
    bool operator==(const DocumentTotals &) const = default;
};
// Unique-geometry counts (ADR 0005, ADR 0165 §3): `totals` less every record a placement
// owns (a variant placement owns its variant's records), plus each definition's members
// and each distinct variant's own records once. `placementCounts` counts placements by
// definition. Each edit and load compares this with DocumentLimits::unique.
LimitCounts uniqueGeometryCounts(const DocumentTotals &totals,
                                 const std::map<Id, size_t> &placementCounts,
                                 const ComponentDefinitions &definitions,
                                 const ComponentVariantIndex *variants);
// Test hook (D-039; default none): called with the inputs and the result of every
// unique-geometry check an edit or load makes, accepted or refused, before the limit
// is applied. Not thread-safe to change while documents are validated.
using UniqueGeometryObserver =
    std::function<void(const DocumentTotals &, const std::map<Id, size_t> &,
                       const ComponentDefinitions &, const ComponentVariantIndex *,
                       const LimitCounts &)>;
void setUniqueGeometryObserver(UniqueGeometryObserver observer);
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
        // The proposal's charge against the live document it is held beside: its own edit
        // plus the records it does not share with that document (D-082).
        size_t retainedBytes(const Document &live) const;
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
    const CameraPathRecords &cameraPaths() const { return cameraPaths_; }
    Id nextCameraPathId() const { return nextCameraPathId_; }
    const RenderPresetRecords &renderPresets() const { return renderPresets_; }
    Id nextRenderPresetId() const { return nextRenderPresetId_; }
    const SectionRecords &sections() const { return sections_; }
    Id nextSectionId() const { return nextSectionId_; }
    const ActiveSections &activeSections() const { return activeSections_; }
    const AnnotationRecords &annotations() const { return annotations_; }
    Id nextAnnotationId() const { return nextAnnotationId_; }
    const DrawingSheetRecords &drawingSheets() const { return drawingSheets_; }
    Id nextDrawingSheetId() const { return nextDrawingSheetId_; }
    const DrawingViewRecords &drawingViews() const { return drawingViews_; }
    Id nextDrawingViewId() const { return nextDrawingViewId_; }
    const DrawingItemRecords &drawingItems() const { return drawingItems_; }
    Id nextDrawingItemId() const { return nextDrawingItemId_; }
    // R093.b (ADR 0165): the driver allocator and the derived variant index, which
    // every apply, undo, redo and restore recomputes from the definitions and bindings.
    Id nextComponentDriverId() const { return nextComponentDriverId_; }
    const ComponentVariantIndex &componentVariants() const;
    const ComponentVariantsPtr &componentVariantRecords() const;
    // The variant a placement shows; null when it shows its definition's template.
    std::shared_ptr<const ComponentVariant> componentVariant(Id placement) const;
    std::uint64_t revision() const { return revision_; }
    // A component edit draft (D-075, editComponentDefinition): its unique-geometry check
    // counts its plain bodies, not the stored definition records. The real document's check
    // counts those records when the edit publishes. A draft also removes no definition.
    bool componentDraft() const { return componentDraft_; }
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
    // Terrain's own history limit class (ADR 0168 decision 3, D-043). A terrain operation
    // replaces whole tile sets in one edit, so the record and tile changes of terrain
    // edits are charged here (Edit::terrainBytes), not against historyLimit. Sized for a
    // 250,000-triangle terrain plus one regeneration's before and after: the worst tile
    // set any 250,000-triangle mesh can make plus a full record is about 191 MiB, so every
    // terrain edit up to the document limit fits. The oldest entries are dropped while
    // either budget is exceeded; an edit over this one refuses (LIMIT_EXCEEDED).
    static constexpr size_t terrainHistoryLimit = 512 * 1024 * 1024;
    size_t terrainHistoryBytes() const { return terrainHistoryBytes_; }
    explicit Document(DisplayUnit units = DisplayUnit::Meters,
                      int displayPrecision = fullDisplayPrecision);
    DisplayUnit displayUnits() const { return displayUnits_; }
    int displayPrecision() const { return displayPrecision_; }
    const ModelStyle &style() const { return style_; }
    void setStyle(const ModelStyle &style);
    const SolarSettings &solar() const { return solar_; }
    const std::optional<SiteOrigin> &site() const { return site_; }
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
    // R096.f1: TerrainTiles revises a terrain edit (sculpt, smooth; later stamps): the
    // replacement may replace a different set of that terrain's tiles and create another number
    // of them, since a new value moves other vertices. Everything else is held as for
    // FixedContextCount, and the original edit must have changed exactly one terrain record.
    enum class AmendPolicy { FixedContextCount, CopyArray, TerrainTiles };
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
    // The bytes this copy holds beyond `base`: every record whose pointer differs from base's
    // record of the same ID, or that base lacks, at full size, plus the bookkeeping every copy
    // owns. A readSnapshot() of base holds only the bookkeeping until base replaces records.
    size_t bytesNotSharedWith(const Document &base) const;
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
                 int displayPrecision = fullDisplayPrecision,
                 DrawingSheetRecords drawingSheets = {}, Id nextDrawingSheetId = 1,
                 DrawingViewRecords drawingViews = {}, Id nextDrawingViewId = 1,
                 DrawingItemRecords drawingItems = {}, Id nextDrawingItemId = 1,
                 Id nextComponentDriverId = 1, std::optional<SiteOrigin> site = std::nullopt,
                 CameraPathRecords cameraPaths = {}, Id nextCameraPathId = 1,
                 RenderPresetRecords renderPresets = {}, Id nextRenderPresetId = 1,
                 bool componentDraft = false);

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
                        int displayPrecision, DrawingSheetRecords drawingSheets = {},
                        Id nextDrawingSheetId = 1, DrawingViewRecords drawingViews = {},
                        Id nextDrawingViewId = 1, DrawingItemRecords drawingItems = {},
                        Id nextDrawingItemId = 1, Id nextComponentDriverId = 1,
                        std::optional<SiteOrigin> site = std::nullopt,
                        CameraPathRecords cameraPaths = {}, Id nextCameraPathId = 1,
                        RenderPresetRecords renderPresets = {}, Id nextRenderPresetId = 1,
                        bool componentDraft = false);

    std::string identity_;
    bool componentDraft_{};
    DisplayUnit displayUnits_{DisplayUnit::Meters};
    int displayPrecision_{fullDisplayPrecision};
    ModelStyle style_;
    SolarSettings solar_;
    // Persisted from schema 29 (R095.a); absent until established.
    std::optional<SiteOrigin> site_;
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
    // Persisted from schema 27 (R090.b); older schemas restore empty tables.
    DrawingSheetRecords drawingSheets_;
    Id nextDrawingSheetId_{1};
    DrawingViewRecords drawingViews_;
    Id nextDrawingViewId_{1};
    // Not persisted until R091.b; restore starts with an empty item table.
    DrawingItemRecords drawingItems_;
    Id nextDrawingItemId_{1};
    // Persisted from schema 28 (R093.c); parameter-free saves do not store it.
    Id nextComponentDriverId_{1};
    // Persisted from schema 30 (ADR 0173); older schemas restore empty tables.
    CameraPathRecords cameraPaths_;
    Id nextCameraPathId_{1};
    RenderPresetRecords renderPresets_;
    Id nextRenderPresetId_{1};
    ComponentVariantsPtr variants_;
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
    size_t historyBytes_{}, terrainHistoryBytes_{};
    bool historyPruned_{};
    // Incremental bookkeeping. Invariants (checked by verifyIncrementalState):
    // totals_ sums bodies_; lockedBodies_ is the set of locked body IDs;
    // detachedFloors_ is the set of allocator-floor keys without a live body;
    // historyRefs_ counts undo_ entries whose body changes name each ID;
    // placementCounts_ counts instances_ by definition (unique-geometry limits).
    DocumentTotals totals_;
    std::map<Id, size_t> placementCounts_;
    std::set<Id> lockedBodies_, detachedFloors_;
    std::map<Id, size_t> historyRefs_;
    void addHistoryRefs(const Edit &edit);
    void removeHistoryRefs(const Edit &edit) noexcept;
    std::string stateMismatch(const Document &other) const;
    ChangeReport applyEdit(Edit edit, std::uint64_t expectedRevision, bool fullValidation);
    void update(Edit edit, bool forward);
    // readSnapshotBytes() and bytesNotSharedWith(): a null base charges every record.
    size_t chargedBytes(const Document *base) const;
};
} // namespace sketchy
