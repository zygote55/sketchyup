#pragma once
#include "app/inference_worker.hpp"
#include "app/terrain_tool.hpp"
#include "app/texture_cache.hpp"
#include "app/theme.hpp"
#include "app/tool_session.hpp"
#include "core/model.hpp"
#include "core/selection.hpp"
#include "core/terrain_display.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "geometry/constraints.hpp"
#include "geometry/drawing.hpp"
#include "geometry/inference.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/component_library.hpp"
#include "io/drawing_view.hpp"
#include "io/measured_export.hpp"
#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointF>
#include <QPolygonF>
#include <QPointer>
#include <QRect>
#include <functional>
#include <optional>
#include <tuple>
class QTabBar;
class QTimer;
class QVariantAnimation;
namespace sketchy {
class Viewport : public QOpenGLWidget {
    Q_OBJECT
  public:
    explicit Viewport(Document &doc, QWidget *parent = nullptr);
    ~Viewport() override;
    const Document &document() const { return doc_; }
    bool inspectionBusy() const {
        return session_.active() || terrainSession_.active() || dragging_ || toolPressed_ ||
               selectionPressed_ || selectingBox_ || !walkKeys_.empty();
    }
    bool inspectionRenderOverrides() const {
        return benchmarkTriangles_ > 0 || !opacity_.empty() || clipPlane_.has_value();
    }
    enum class Tool {
        Select = 0,
        Rectangle = 1,
        Circle = 2,
        Extrude = 3,
        Orbit = 4,
        Pan = 5,
        Line = 6,
        Polygon = 7,
        Freehand = 8,
        RotatedRectangle = 9,
        CenterArc = 10,
        TwoPointArc = 11,
        ThreePointArc = 12,
        Pie = 13,
        Tape = 14,
        Protractor = 15,
        Zoom = 16,
        Move = 17,
        Rotate = 18,
        Scale = 19,
        Paint = 20,
        Offset = 21,
        Sweep = 22,
        Intersect = 23,
        Boolean = 24,
        Orientation = 25,
        HostedPlacement = 26,
        LookAround = 27,
        Walk = 28,
        // R096.f1 (UX_DESIGN §15): the terrain tools. f2 adds Contours, Pad and Drape.
        Sculpt = 29,
        Smooth = 30
    };
    void setTool(Tool tool);
    void setSceneTransitionDuration(int milliseconds);
    int sceneTransitionDuration() const { return sceneTransitionMs_; }
    void setDrawingPlane(std::optional<DrawingPlane> plane, Id context = 0);
    void useSelectedFacePlane();
    DrawingPlane drawingPlane() const { return plane_; }
    unsigned curveSegments() const { return curveSegments_; }
    unsigned polygonSides() const { return polygonSides_; }
    Tool tool() const { return tool_; }
    ToolSession::Phase interactionPhase() const { return session_.phase(); }
    std::optional<Vec3> operationAnchor() const { return anchor_; }
    const InferenceResult &inference() const { return inference_; }
    std::optional<InferenceCandidate> acquiredInference() const;
    InferenceCamera inferenceCamera() const;
    std::optional<DirectionCandidate> acquiredDirection() const;
    std::optional<DirectionConstraint> lockedDirection() const { return directionLocks_.current(); }
    std::optional<InferenceCandidate> armedReference() const { return reference_; }
    bool planeHeld() const { return bool(heldPlane_); }
    bool inferenceReady() const { return bool(inferenceWorker_.ready(doc_)); }
    bool previewValid() const { return previewValid_; }
    void setOrientationMode(bool orient);
    QString orientationSummary() const;
    void setBooleanOperation(const QString &operation);
    void setBooleanKeepOperands(bool keep);
    void swapBooleanOperands();
    QString booleanSummary() const;
    void setIntersectionMode(const QString &mode);
    QString intersectionMode() const { return intersectionMode_; }
    void setGuideCreation(bool enabled);
    bool guideCreation() const { return createGuides_; }
    void clearGuides();
    void setGuidesVisible(bool visible);
    bool guidesVisible() const { return guidesVisible_; }
    void setSelection(Id body, Id face = 0);
    void selectEntities(const SelectionSet &entities, SelectionMode mode = SelectionMode::Replace);
    const Selection &selectionState() const { return selection_; }
    std::optional<SelectedEntity> hoveredEntity() const { return hover_; }
    std::optional<SelectedEntity> selectionAt(QPointF point);
    SelectionSet windowSelection(QRectF bounds, bool crossing);
    QString selectionSummary() const;
    void enterContext(Id context);
    void leaveContext();
    // R094.e (ADR 0166 §4.1): double-click, Enter, Edit selected context and Edit definition…
    // open a context through openContext, which first offers it to the opener (the window's
    // "has its own values" dialog); the opener returns true when it handled the request.
    void setContextOpener(std::function<bool(Id)> opener) { contextOpener_ = std::move(opener); }
    void openContext(Id context);
    void makeGroup();
    void explodeGroups();
    void mergeContextGeometry();
    Id componentScope() const;
    void makeComponent(const QString &name);
    void makeComponentUnique(bool activeScope = false);
    void replaceComponent(Id definition);
    void placeComponent(Id definition, Vec3 position);
    void changeComponentAxes(Transform axes);
    struct HostedPlacementOptions {
        double angle{}, inset{};
        Vec3 scale{1, 1, 1};
    };
    HostedPlacementOptions hostedPlacementOptions() const { return hostedOptions_; }
    void setHostedPlacementOptions(HostedPlacementOptions options);
    void startHostedPlacement(bool retainPose = false);
    QString hostedPlacementSummary() const;
    Id selectedComponentDefinition() const;
    ComponentGlue selectedGlueFace() const;
    void configureComponentGlue(std::optional<ComponentGlue> glue);
    void detachSelectedComponent();
    void bakeSelectedHost();
    void adoptSelectedRecipeRoom();
    void paintSelection(std::array<float, 3> color);
    void organize(const QJsonArray &commands);
    Id paintMaterial() const;
    int paintSide() const { return paintSide_; }
    void setPaintMaterial(Id material, int side);
    void editMaterials(const QJsonArray &commands);
    void applyMaterialToSelection();
    SelectionSet selectedTextureFaces();
    void mapSelectedTextures(const QJsonValue &projection, const QString &space, int side);
    void setSelectedEdgeAppearance(const QString &flag, bool value);
    void setPersistentState(bool hide, bool lock);
    void revealPersistentEntities();
    void unlockPersistentEntities();
    void showHiddenGeometry(bool show);
    void hideSelection();
    void revealHiddenGeometry();
    void lockSelection();
    void unlockContexts();
    void deleteSelection();
    void selectAll();
    // R095.e1: commits one terrain command (terrain.explode, terrain.set_parameters,
    // terrain.regenerate, terrain.create_from_edges) as one batch outside any component scope,
    // then refreshes and reports the change. Throws the batch's refusal.
    QJsonObject commitTerrainCommand(const QJsonObject &command);
    // R095.e1 (ADR 0168 "Tools on terrain"): the terrain whose tile the current tool is hovering
    // and refuses to edit, or 0. The cursor is then the not-allowed cursor.
    Id refusedTerrain() const { return terrainRefused_; }
    // The tool's name in the refusal message ("Push/pull does not edit terrain…"), or empty
    // for a tool that may act on terrain (Select, Paint, camera tools, Intersect with it).
    static QString terrainRefusingToolName(Tool tool);
    // R095.e2 (ADR 0168 "Decisions > Interaction design", Site): the site origin marker, a
    // labelled crosshair at model (0, 0, 0) = the site origin. View > Site origin shows it
    // always; otherwise it shows while a terrain is selected or Site settings is open. Never
    // without a document site origin.
    void setSiteOriginVisible(bool visible);
    bool siteOriginVisible() const { return siteOriginVisible_; }
    void setSiteSettingsOpen(bool open);
    bool siteOriginMarkerShown() const;
    // The marker's label as painted ("Site origin · E … · N … · Z …"), empty when not shown.
    QString siteOriginMarkerLabel() const;
    // R095.e2: the contour lines currently drawn for a terrain (null when off or none).
    std::shared_ptr<const TerrainContours> terrainContoursShown(Id terrain) const;
    // The contours of a terrain at its display interval from the viewport's cache (the same
    // lines the viewport draws, computed once), whether or not they are switched on.
    std::shared_ptr<const TerrainContours> terrainContours(Id terrain);
    // Whether an edge of a terrain tile is drawn: only edges on the terrain's outer boundary;
    // tile seams and interior edges are not (nor picked or snapped while not drawn).
    bool terrainEdgeDrawn(Id tile, Id edge) const;
    // ---- R096.f1: the terrain tools (viewport_sculpt.cpp; UX_DESIGN §15.1–15.2, §15.7) ----
    static bool terrainTool(Tool tool) { return tool == Tool::Sculpt || tool == Tool::Smooth; }
    const TerrainToolOptions &terrainOptions() const { return terrainOptions_; }
    // Options from the strip: clamped to their bounds; a change refreshes a shown preview.
    void setTerrainOptions(const TerrainToolOptions &options);
    // `[` and `]`: the radius times or divided by 1.1.
    void stepTerrainRadius(bool larger);
    // The Measurements box's step: "Radius" (or "Falloff width" for a region) before a stroke,
    // "Amount" (Sculpt) or "Strength" (Smooth) while a spot or region waits for its value and
    // after a commit, when a typed value amends it; "Point" while a region is being drawn.
    QString terrainMeasurementLabel() const;
    // What the brush shows, for the label, the status bar and tests.
    struct TerrainBrushView {
        Id terrain{};                 // the terrain under the brush (0: none)
        std::optional<Vec3> point;    // the brush centre on the surface (model)
        bool stroking{}, anchored{};  // a press-drag stroke; a one-spot brush waiting for its amount
        bool smoothing{};             // Smooth, or Sculpt with Shift held
        double amount{};              // the signed amount a commit would use (Sculpt)
        std::size_t vertices{}, tiles{}; // that would move, and the tiles they touch
        bool empty{};                 // no vertex under the brush (amber ring)
        bool previewShown{};          // the candidate is drawn in place of the tiles
        std::vector<Vec3> region;     // region scope: the polygon so far (model)
        bool regionClosed{};
        QString label;                // the label at the brush ("+1.80 m · radius 9.00 m" …)
        QString refusal;              // why the brush cannot act here (empty when it can)
        // The overlay draped on the surface: ring points, and points of the changed region's
        // outline (while a candidate shows).
        std::size_t ringPoints{}, outlinePoints{};
    };
    TerrainBrushView terrainBrush() const;
    // The candidate drawn in place of the tiles (null when none is shown), and the tile bodies it
    // hides (ADR 0169 §5).
    std::shared_ptr<const TerrainSculptPreview> terrainPreviewShown() const;
    std::set<Id> terrainPreviewHiddenTiles() const;
    // The amendable terrain command (after a commit, while a typed value revises it), or empty.
    QJsonObject terrainAmendable() const;
    Id selectedBody() const { return selected_; }
    Id selectedFace() const { return selectedFace_; }
    void refresh();
    SceneSnapshot captureSceneSnapshot(bool camera, bool visibility, bool style, bool section, bool solar = false) const;
    QJsonObject editSavedScenes(const QJsonArray &commands);
    QJsonObject editSections(const QJsonArray &commands);
    QJsonObject editAnnotations(const QJsonArray &commands);
    void importReferenceImage(const QByteArray &data, const QString &mediaType, const QString &name,
                              double width, double height);
    void editReferenceImages(const QJsonArray &commands);
    void applyPreparedEdit(const Document::PreparedEdit &edit);
    void applyTextEdit(const Document::PreparedEdit &edit) { applyPreparedEdit(edit); }
    void recallSavedScene(Id scene, bool animate = true);
    // Applies a saved scene's camera only, never its style, visibility or sections (ADR 0164
    // decision 9: "Show scene in model"). A camera change is view state, not an edit.
    void showSceneCamera(Id scene);
    void insertLibrary(const struct ComponentBundle &bundle, Vec3 worldPosition);
    void setReducedMotion(bool enabled);
    void setAssistantPreview(std::shared_ptr<const Document::PreparedEdit> edit);
    bool hasAssistantPreview() const;
    void setAssistantPreviewFocus(Id body);
    void fit();
    void frameBounds(Vec3 low, Vec3 high);
    void standardView(int view);
    void setOrthographic(bool enabled);
    bool orthographic() const { return ortho_; }
    RenderCamera renderCamera() const;
    void setFieldOfView(double degrees);
    double fieldOfView() const { return fov_; }
    void setTrackpadNavigation(bool enabled);
    bool trackpadNavigation() const { return trackpad_; }
    void setPushPullNewFace(bool enabled);
    bool pushPullNewFace() const { return pushNewFace_; }
    void repeatPushPull();
    bool canRepeatPushPull() const {
        return lastPushDistance_ && doc_.owns(repeatSession_) && selectedFace_ &&
               selectable({selected_, SelectionKind::Face, selectedFace_});
    }
    void setTransformCopy(bool enabled);
    bool transformCopy() const { return transformCopy_; }
    void setTransformLocal(bool enabled);
    bool transformLocal() const { return transformLocal_; }
    void flipSelection(int axis);
    void cancel();
    bool measurements(const QString &value);
    void setTheme(const ThemeColors &colors);
    void applyModelStyle(const ModelStyle &style);
    void applyModelSolar(const SolarSettings &solar);
    QPointF project(Vec3 p) const;
    std::pair<Id, Id> pick(QPointF point) const;
    std::pair<Id, Id> pickEdge(QPointF point, double radius = 6) const;
    struct Bounds {
        Vec3 minimum{}, maximum{};
        bool valid{};
    };
    Bounds bodyBounds(Id body) const;
    bool rendererReady() const { return ready_; }
    QString graphicsDescription() const { return graphics_; }
    // Feasibility controls are view-only; they do not alter saved materials or topology.
    void setBodyOpacity(Id body, float opacity);
    void setClipPlane(std::optional<std::array<double, 4>> plane);
    // Diagnostic control: when false, every body draws its own world-space mesh.
    void setPlacementMeshSharing(bool enabled);
    bool placementMeshSharing() const { return placementSharing_; }
    void benchmark(int triangles, bool instanced = false);
    struct RenderStats {
        std::uint64_t frames{}, geometryUploads{}, transparencyUploads{}, uploadedBytes{};
        // Body counters count each body whose mesh, world placement or GPU mesh
        // changed; tessellations and mesh uploads count the work, which bodies
        // drawing one shared placement mesh perform once.
        std::uint64_t bodyMeshBuilds{}, bodyWorldUpdates{}, bodyUploads{};
        std::uint64_t tessellations{}, meshUploads{};
        size_t cachedBodies{}, placementMeshes{}, sharedPlacements{};
        unsigned contextGeneration{}, glError{};
        size_t meshTriangles{}, profileEdges{};
        // R095.e2: contour segments uploaded in the last rebuild, and terrain surfaces built.
        size_t contourSegments{}, terrainSurfaces{};
        // R096.f1 (benchmark instrumentation): the last rebuild of the scene's caches and the last
        // terrain preview's display build and upload, in milliseconds.
        double rebuildMs{}, terrainPreviewMs{};
        // R096.f1: triangles in the last translucent pass upload, and in the last sun shadow map
        // (with the terrain preview's candidate triangles among them).
        size_t transparentTriangles{}, shadowTriangles{}, shadowPreviewTriangles{};
        // R096.f1b (benchmark instrumentation): the last terrain commit's command and refresh,
        // and the last rebuild's terrain display update (surfaces, contours, their uploads).
        double terrainCommandMs{}, terrainRefreshMs{}, terrainViewsMs{};
        // R096.f1b: seam-ring tiles whose normals were patched in place instead of rebuilt.
        std::uint64_t terrainNormalPatches{};
    };
    RenderStats renderStats() const { return stats_; }
    struct GeometryCacheMemory {
        size_t bodyVectorCapacityBytes{}, bodyGpuPayloadBytes{};
    };
    GeometryCacheMemory geometryCacheMemory() const;
    double lastFrameMs() const { return frameMs_; }
    // Benchmark instrumentation only: include GPU completion in frame timing.
    void setSynchronousFrameTiming(bool enabled) { synchronousFrameTiming_ = enabled; }
    bool texturesPending() const { return textureCache_.pending(); }
    QString textureSummary() const;
    // The projection/units label never overlaps the editing-context chips. The
    // owner registers them; geometry is read from them whenever the label is placed.
    void setContextOverlay(QWidget *breadcrumb, QWidget *banner);
    // Rect (viewport coordinates) the label was last painted at; empty before the first frame.
    QRect paintedProjectionLabelRect() const { return projectionLabelPainted_; }
    // Orthographic projection in the current view direction, at physical page scale.
    MeasuredExport renderMeasuredView(MeasuredPage page, MeasuredFormat format, bool raster,
                                      int dpi = 150);
    // hiddenAllowed: the Sheets workspace hides this widget but its context stays initialized.
    QImage renderRaster(QSize pixels, bool hiddenAllowed = false);
    // Scene-scoped raster capture of a drawing view (ADR 0164 §2, R091.e): the model drawn from
    // the view's scene camera into an offscreen image. Orthographic views use their paper
    // mapping (scale, offset and frame), perspective views the scene camera with the frame's
    // aspect. GUI thread only; works while the Sheets workspace hides the viewport. The input
    // must be captured at the document's current revision.
    QImage renderDrawingViewRaster(const DrawingViewEvaluationInput &view, QSize pixels);
    void exportRaster(const QString &path, QSize pixels);
    // ADR 0166 §3.4 (R094.d, viewport_parameters.cpp): the stretch plane of the driver form, a
    // dashed quad across a member's bounds at `at` (member-local metres) perpendicular to
    // `axis` (0 x, 1 y, 2 z). Dragging it moves it along the axis, snapping within 8 logical px
    // to the member's distinct vertex coordinates and the midpoints between neighbours; Alt
    // drags without snapping. Points past the plane, which the stretch moves, are marked.
    struct StretchPlane {
        Id body{}; // the scene record of the member in the open context
        int axis{};
        double at{};
        QString label;
    };
    void setStretchPlane(std::optional<StretchPlane> plane);
    const std::optional<StretchPlane> &stretchPlane() const { return stretchPlane_; }
    std::vector<double> stretchSnaps() const;
    // The member's distinct points past the plane and all of them.
    std::pair<std::size_t, std::size_t> stretchMoving() const;
    // The plane's outline in logical viewport pixels (empty without a plane).
    QPolygonF stretchPlaneOutline() const;
    // Where the plane would be at member-local `at`, in logical viewport pixels.
    QPointF stretchPlanePoint(double at) const;
    // ADR 0166 §2: the Preview ghost, a dry run (previewBatch) of
    // component_parameter.override {body: placement, values}. Draws the regenerated members
    // with dashed edges and hatch over the current geometry; never commits. Returns the
    // dry run's refusal ({code, message, details}) or an empty object. The ghost belongs to
    // the revision it was computed at: any committed edit clears it.
    QJsonObject previewParameters(Id placement, const QJsonObject &values);
    void clearParameterGhost();
    bool parameterGhostShown() const;
    std::size_t parameterGhostEdges() const { return ghostEdges_.size(); }
    // ADR 0166 §6.3 (R094.f, viewport_assembly.cpp): placing a configurable assembly from the
    // Components tab. The ghost is the assembly at the form's values (a dry run of
    // component_parameter.override on a private copy of the bundle's document, once per value
    // change) moved with the pointer: a gluing assembly onto the hovered face, with the
    // opening outline and, 150 ms after the pointer rests, the cut of a dry run of the full
    // placement; a free one onto the hovered face's plane or the ground. A click commits one
    // edit (placeLibraryAssembly); Enter places a gluing assembly on the selected face at its
    // centre; the Measurements box takes anchor coordinates or an inset (gluing) or [x,y,z]
    // (free), and re-entry amends the placement. Esc cancels. `values` are override literals;
    // a refusal of the values is returned ({code, message, details}) and leaves the ghost.
    QJsonObject beginAssemblyPlacement(std::shared_ptr<const ComponentBundle> bundle,
                                       const QJsonObject &values);
    QJsonObject setAssemblyValues(const QJsonObject &values);
    void endAssemblyPlacement();
    bool assemblyPlacing() const;
    // The Measurements box is the assembly's while placing and while re-entry can amend.
    bool assemblyMeasurementsActive() const;
    QString assemblyPlacementStatus() const;
    // Why the pointer has no valid placement (empty while it has one).
    QString assemblyPlacementError() const { return assembly_ ? assembly_->error : QString{}; }
    std::size_t assemblyGhostEdges() const;
    std::optional<Transform> assemblyGhostTransform() const;
    // The opening outline in logical viewport pixels (a gluing assembly's ghost).
    QPolygonF assemblyOpeningOutline() const;
  signals:
    // Placement mode ended: by placing (`placed`, the new placement) or by cancelling.
    void assemblyPlacementEnded(bool placed, qulonglong placement);
    void stretchPlaneMoved(double at, bool finished);
    void materialChanged();
    void selected(qulonglong body, qulonglong face);
    void toolChanged(int tool);
    void guideCreationChanged(bool enabled);
    void navigationChanged();
    void pushPullModeChanged(bool enabled);
    void transformOptionsChanged();
    void measurementsRequested(const QString &text);
    void measurementPreview(const QString &text);
    void changed();
    void sceneRecalled(qulonglong scene);
    void message(const QString &text);
    // R095.e1: the hovered tile's terrain that the current tool refuses (0 when the pointer left
    // it or the tool changed) and the status-bar text naming the tool.
    void terrainToolRefused(qulonglong terrain, const QString &text);
    // R096.f1: a terrain tool's options, phase or Measurements step changed.
    void terrainToolChanged();

    // R095.e1: the viewport's context menu was asked for at `position` (a right click without
    // a drag, the Menu key or Shift+F10).
    void contextMenuRequested(const QPoint &position);
    // R094.e (ADR 0166 §5): a message's action (Show the driver, Select the locked placement,
    // close the definition), asked for at the control that showed it. `action` is a
    // ParameterMessageAction; `target` the driver or placement, `definition` the driver's.
    void parameterAction(int action, qulonglong target, qulonglong definition);

  protected:
    bool event(QEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void initializeGL() override;
    void paintGL() override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

  private:
    // `historyLabel` (at most 512 UTF-8 bytes) names the History entry when not empty.
    QJsonObject commitCommands(const QJsonArray &commands, bool shared = true,
                               const QString &historyLabel = {});
    Id selectedComponent() const;
    std::function<bool(Id)> contextOpener_;
    void paintAt(QPointF point, bool sample);
    Id paintMaterial_{};
    int paintSide_{2};
    Document::SaveStamp paintSession_;
    struct Vertex {
        double x{}, y{}, z{};
        float r{}, g{}, b{}, a{1};
        float br{r}, bg{g}, bb{b}, ba{a};
        float u{}, v{}, bu{}, bv{}, light{1}, dim{1};
        Id image{}, backImage{};
        float backLight{1}, reference{};
    };
    struct PackedVertex {
        float x, y, z, r, g, b, a, br, bg, bb, ba, u, v, bu, bv, light, dim, backLight, reference;
    };
    Document &doc_;
    ToolSession session_;
    ThemeColors colors_{themeColors(false)}; // Contrast colors derived from document background.
    std::optional<ModelStyle> displayedStyle_;
    std::optional<SolarSettings> displayedSolar_;
    SolarPosition displayedSun_;
    void syncSolar();
    float solarLight(Vec3 normal) const;
    GLuint solarShadowTexture_{}, solarShadowFramebuffer_{};
    void drawSolarShadowMap(const QMatrix4x4 &viewMatrix);
    void cleanupSolarShadowMap();
    void syncModelStyle();
    void drawStyleGround(const QMatrix4x4 &transform);
    void drawStyleProfiles(const QMatrix4x4 &transform);
    QMatrix4x4 profileMatrix_;
    bool profilesDirty_{true};
    QMatrix4x4 groundMatrix_;
    Tool tool_{Tool::Select};
    struct GpuBatch {
        struct Run {
            int first{}, count{};
            Id front{}, back{};
        };
        QOpenGLBuffer buffer{QOpenGLBuffer::VertexBuffer};
        std::vector<Run> runs;
        int count{};
        Vec3 origin{};
    };
    QOpenGLFunctions_3_3_Core *gl_{}; // Owned by the current context.
    std::unique_ptr<QOpenGLShaderProgram> shader_;
    TextureCache textureCache_;
    std::shared_ptr<const TextureCache::Snapshot> textureSnapshot_{textureCache_.snapshot()};
    AssetRecords textureAssets_;
    std::map<Id, std::shared_ptr<const TextureImage>> textureImages_;
    std::map<Id, GLuint> textureGpu_;
    size_t textureFallbacks_{}, textureMappingFallbacks_{};
    static std::vector<Triangle> displayTriangles(const Body &body);
    void referenceVertices(const Body &body, const Triangle &local,
                           std::array<Vertex, 3> &vertices) const;
    void syncTextures();
    struct TextureProjection {
        Id image{};
        std::array<std::array<float, 2>, 3> uv{};
    };
    TextureProjection textureProjection(const Body &body, const Triangle &local, bool back) const;
    void textureVertices(const Body &body, const Triangle &local, bool reflected,
                         std::array<Vertex, 3> &vertices) const;
    GpuBatch gridGpu_, groundGpu_, profilesGpu_, transparentGpu_, benchmarkGpu_;
    GpuBatch pickFacesGpu_, pickEdgesGpu_, selectedFacesGpu_, selectedEdgesGpu_, hoverFacesGpu_,
        hoverEdgesGpu_;
    std::shared_ptr<const Document::PreparedEdit> assistantPreview_;
    Id assistantFocus_{};
    QString assistantPreviewLabel_;
    Vec3 assistantPreviewLabelPoint_;
    bool assistantPreviewDirty_{true};
    std::uint64_t assistantPreviewPresentation_{};
    std::vector<Vertex> assistantTriangles_, assistantLines_;
    GpuBatch assistantTrianglesGpu_, assistantLinesGpu_;
    void rebuildAssistantPreview();
    void drawAssistantPreview();
    void paintAssistantPreview(QPainter &painter);
    Selection selection_;
    std::optional<SelectedEntity> hover_, lastClickEntity_;
    // R095.e1 tool hover over terrain (viewport_terrain.cpp).
    Id terrainRefused_{};
    QPointF rightPress_;
    void updateTerrainHover(QPointF point);
    void clearTerrainHover();
    // R096.f1 terrain tools (viewport_sculpt.cpp).
    TerrainToolOptions terrainOptions_;
    struct TerrainBrushState {
        Id terrain{};
        std::optional<Vec3> point;      // brush centre on the surface (model)
        QPointF pointer;                // where the pointer was
        std::vector<Vec3> path;         // the stroke so far (model)
        bool pressed{}, stroking{}, anchored{};
        QPointF pressPosition, anchorPosition;
        double spotAmount{};            // the one-spot amount set by moving up or down
        std::vector<Vec3> region;       // region scope polygon (model, on the surface)
        bool regionClosed{}, regionSelected{};
        Qt::KeyboardModifiers modifiers;
        QString refusal, error;
    } brush_;
    ToolSession terrainSession_{doc_};
    QJsonObject terrainCommitted_;   // the last committed terrain command, for amend
    std::shared_ptr<const TerrainSculptPreview> terrainCandidate_;
    QJsonObject terrainCandidateCommand_; // the command the shown candidate was built from
    bool terrainCandidateShown_{}, terrainPreviewDirty_{};
    std::string terrainCandidateDocument_;
    std::uint64_t terrainCandidateRevision_{};
    struct TerrainPreviewGpu {
        Id terrain{};
        std::set<Id> hidden;           // tile bodies drawn by the preview instead
        std::set<Id> hiddenContours;   // tile bodies whose contour segments are replaced
        GpuBatch surface, lines, contours, indexContours;
        std::vector<Vertex> surfaceVertices; // the candidate surface (world), for sun shadows
        bool active{};
    } terrainPreviewGpu_;
    // Contour vertex ranges per tile in a chunk's line batches (ordinary, index).
    struct ContourRange {
        int lineFirst{}, lineCount{}, indexFirst{}, indexCount{};
    };
    bool terrainToolActive() const { return terrainTool(tool_); }
    bool terrainSmoothing() const;
    std::optional<std::pair<Id, Vec3>> terrainHit(QPointF point) const;
    void terrainHover(QPointF point);
    bool terrainPress(QMouseEvent *event);
    bool terrainMove(QMouseEvent *event);
    bool terrainRelease(QMouseEvent *event);
    bool terrainKey(QKeyEvent *event);
    void terrainKeyRelease(QKeyEvent *event);
    bool terrainMeasurements(const QString &text);
    void beginTerrainTool();
    void cancelTerrainOperation(bool keepAmend = false);
    void updateTerrainCandidate(bool show);
    QJsonObject terrainCommand(std::optional<double> value = std::nullopt) const;
    void commitTerrain(std::optional<double> value = std::nullopt);
    void amendTerrain(double value);
    void prefetchTerrainDigest(Id terrain);
    double terrainAmount() const;
    std::optional<double> terrainElevation(Id terrain, double x, double y) const;
    void rebuildTerrainPreview();
    void uploadTerrainPreview();
    void drawTerrainPreview();
    void paintTerrainBrush(QPainter &painter);
    void buildTerrainOverlay();
    struct TerrainOverlay {
        std::vector<std::vector<Vec3>> outline, band; // the changed region; a region's falloff band
        std::vector<Vec3> ring, halfRing;
    } terrainOverlay_;
    void emitTerrainStatus();
    QString terrainStatusShown_;
    SelectionSet boxBase_;
    QPointF selectionStart_, selectionEnd_, lastClickPosition_;
    bool selectionPressed_{}, selectingBox_{}, pickDirty_{true}, overlayDirty_{true};
    SelectionMode selectionMode_{SelectionMode::Replace};
    QElapsedTimer clickTimer_;
    unsigned clickCount_{};
    std::uint64_t presentationRevision_{};
    std::vector<SelectedEntity> pickEntities_;
    Document::SaveStamp selectionStamp_, selectionGestureStamp_;
    TagRecords cachedTags_;
    void syncSelection();
    struct PickPixels {
        QImage image;
        QRect deviceRect;
    };
    PickPixels selectionPixels(QRectF region);
    void rebuildPickGeometry();
    void rebuildSelectionOverlay();
    void drawSelectionOverlay();
    void paintSelection(QPainter &painter);
    void paintAnnotations(QPainter &painter);
    Document::SaveStamp annotationStamp_;
    std::map<Id, AnnotationMeasurement> annotationMeasurements_;
    bool visible(SelectedEntity entity) const;
    bool selectable(SelectedEntity entity) const;
    void selectionChanged(bool policy = false);
    void updatePrimarySelection();
    bool selectionKey(QKeyEvent *event);
    SelectionMode selectionMode(Qt::KeyboardModifiers modifiers) const;
    void selectionRelease(QPointF point);
    std::optional<SelectedEntity> pickEntity(QRgb color) const;
    struct MeshEdge {
        Vec3 a, b;
        Id id;
    };
    // Body-local tessellation. Placements of one definition member share it.
    struct BodyGeometry {
        BodyPtr record;
        std::vector<Triangle> localTriangles;
        Adjacency adjacency;
        std::vector<MeshEdge> localEdges;
    };
    // Display vertices and their GPU buffers. A placement mesh omits the
    // placement translation, which each draw adds relative to the render origin;
    // otherwise vertices are world positions, as for any unshared body.
    struct BodyMesh {
        bool placement{};
        std::vector<Vertex> opaque, lines, hiddenLines;
        std::vector<std::array<Vertex, 3>> transparent;
        // Placement meshes record, per drawn triangle and edge, whether its
        // placement-space value was degenerate (2) or kept (1); 0 is not drawn.
        // Placements whose world values disagree use their own mesh instead.
        std::vector<std::uint8_t> triangleFlags, edgeFlags;
        // R096.f1b: for a terrain tile's unshared mesh, each opaque vertex's local triangle and
        // corner (triangle * 3 + corner; the top bit marks a corner whose normal is the tile's
        // own, its position not being in the surface), so new normals can be patched in place.
        std::vector<std::uint32_t> terrainCorners;
        GpuBatch opaqueGpu, linesGpu, hiddenLinesGpu;
        size_t mappingFallbacks{};
        MaterialRecords materials;
        std::map<Id, std::shared_ptr<const TextureImage>> images;
        std::uint64_t presentationRevision{}, pass{}, uploadPass{};
    };
    // Everything a placement mesh depends on besides document-wide inputs:
    // the canonical member, the placement's linear part and its display state.
    struct PlacementKey {
        const Body *source{};
        std::array<double, 9> linear{};
        bool hidden{}, inactive{}, locked{};
        bool operator<(const PlacementKey &other) const {
            return std::tie(source, linear, hidden, inactive, locked) <
                   std::tie(other.source, other.linear, other.hidden, other.inactive, other.locked);
        }
    };
    struct BodyCache {
        BodyPtr record;
        MaterialRecords materials;
        std::map<Id, std::shared_ptr<const TextureImage>> images;
        Transform world;
        std::shared_ptr<BodyGeometry> geometry;
        std::shared_ptr<BodyMesh> mesh;
        // Canonical member record this body projects, when it may share: the
        // definition member, or the variant's member when its placement shows a variant.
        BodyPtr source;
        Id owner{};
        InstancePtr instance;
        DefinitionPtr definition;
        std::shared_ptr<const ComponentVariant> variant;
        Bounds bounds;
        std::vector<SectionCut> sectionCuts;
        SectionRecords sections;
        SectionMesh sectionMesh;
        std::vector<std::pair<Id, Triangle>> sectionCaps;
        QString sectionError;
        float alpha{1};
        std::uint64_t presentationRevision{}, meshPass{};
        // R095.e2: a terrain tile's surface display and elevation-band shading when built.
        // R096.a: the tile's own surface (pointer-identical while no edit touches it or its
        // seam ring) and the terrain's elevation range, which band colours depend on.
        std::shared_ptr<const TerrainTileSurface> terrainSurface;
        std::array<double, 2> terrainRange{};
        bool terrainBands{};
    };
    std::map<Id, std::unique_ptr<BodyCache>> bodyCaches_;
    // R095.e2: per terrain, the surface display (normals across seams, outer edges, elevation
    // range) and the contour lines' GPU batches, rebuilt when the tiles or settings change.
    struct TerrainView {
        BodyPtr record;
        std::vector<BodyPtr> tiles;
        std::shared_ptr<const TerrainTileSurfaces> surface;
        std::shared_ptr<const TerrainContours> contours;
        Transform world;
        bool hidden{}, dark{};
        // R096.f1b: the contour lines in chunks, one per row of the tile grid (key order keeps a
        // row's tiles together), so an edit re-uploads only the rows whose tiles changed. A chunk
        // is kept while it holds the same tile records (their lines depend on nothing else, as
        // long as the interval, offset, world transform, colours and render origin are the same).
        struct ContourChunk {
            std::int64_t row{};
            std::vector<BodyPtr> tiles;
            GpuBatch lines, index;
            std::map<Id, ContourRange> ranges; // per tile, within this chunk's batches
        };
        std::vector<std::unique_ptr<ContourChunk>> chunks;
        double interval{}, offset{};
        Vec3 origin{};
        bool built{};
    };
    std::map<Id, std::unique_ptr<TerrainView>> terrainViews_;
    TerrainContourCache contourCache_;
    TerrainSurfaceCache surfaceCache_; // R096.a: per-tile surfaces, updated after edits
    void syncTerrainViews();
    void drawTerrainContours();
    void paintSiteOrigin(QPainter &p);
    const TerrainView *terrainViewOfTile(Id tile) const;
    bool siteOriginVisible_{}, siteSettingsOpen_{};
    // R096.a: the document whose terrain edit meshes this viewport's display uses; its entries
    // in terrainEditMeshCache() are forgotten when the document is replaced or the viewport goes.
    std::string terrainCacheIdentity_;
    // Registries hold their source records so raw keys are never reused.
    std::map<const Body *, std::pair<BodyPtr, std::weak_ptr<BodyGeometry>>> sharedGeometry_;
    std::map<PlacementKey, std::pair<BodyPtr, std::weak_ptr<BodyMesh>>> placementMeshes_;
    ComponentDefinitions placementDefinitions_;
    ComponentInstances placementInstances_;
    std::uint64_t rebuildPass_{};
    bool placementSharing_{true}, placementReset_{};
    // Document-wide mesh inputs at the end of the last complete rebuild.
    MaterialRecords rebuiltMaterials_;
    std::map<Id, std::shared_ptr<const TextureImage>> rebuiltImages_;
    bool rebuiltInputsValid_{};
    Triangle worldTriangle(const BodyCache &cache, size_t index) const;
    MeshEdge worldEdge(const BodyCache &cache, size_t index) const;
    QVector3D placementOffset(const BodyCache &cache) const;
    void prepareSections(Id body, BodyCache &cache);
    bool sectionOccludes(Vec3 origin, Vec3 direction, double target) const;
    static std::array<Vertex, 3> clippedVertices(const std::array<Vertex, 3> &source,
                                                const SectionTriangle &triangle);
    std::array<Vertex, 3> sectionCapVertices(Id body, const SectionTriangle &triangle,
                                           float alpha) const;
    bool gridDirty_{true};
    QMetaObject::Connection contextCleanup_;
    QOpenGLVertexArrayObject vao_;
    std::vector<Vertex> benchmarkVertices_;
    std::vector<std::array<Vertex, 3>> transparent_;
    // R096.f1: the body of each triangle in transparent_, so a terrain preview can leave out
    // the tiles it replaces; and whether the last sort left them out.
    std::vector<Id> transparentBodies_;
    bool transparentPreviewFiltered_{};
    std::map<Id, float> opacity_;
    std::string cachedDocument_, selectionDocument_;
    std::uint64_t cachedRevision_{};
    std::optional<std::array<double, 4>> clipPlane_;
    QMatrix4x4 sortedMatrix_;
    bool transparentDirty_{true}, benchmarkDirty_{true};
    RenderStats stats_;
    struct FaceHit {
        Id body{}, face{};
        double distance{INFINITY};
    };
    FaceHit nearestFace(QPointF point) const;
    Id selected_{}, selectedFace_{};
    bool ready_{false}, cacheDirty_{true}, dragging_{false};
    Qt::MouseButton dragButton_{Qt::NoButton};
    QPointF previous_;
    std::optional<Vec3> anchor_, cursor_, committedAnchor_, committedEnd_;
    Id committedBody_{}, committedFace_{};
    DrawingPlane plane_;
    std::optional<DrawingPlane> configuredPlane_;
    Id configuredContext_{}, drawingContext_{}, chainContext_{};
    bool chainPending_{false};
    QPointF committedPointer_;
    std::optional<Vec3> baseline_, committedBaseline_;
    std::vector<Vec3> samples_;
    unsigned polygonSides_{6}, curveSegments_{48};
    QJsonObject committedShape_;
    bool toolPressed_{false}, dragCommit_{false}, previewValid_{false};
    QPointF toolPressPosition_;
    bool transformTool() const {
        return tool_ == Tool::Move || tool_ == Tool::Rotate || tool_ == Tool::Scale;
    }
    bool transformCopy_{}, transformLocal_{}, transformControlPending_{};
    QJsonArray transformTargets_;
    SelectionSet transformSelection_;
    Transform transformFrame_;
    Vec3 transformPivot_{}, transformAxis_{0, 0, 1};
    std::optional<Vec3> transformBase_, transformEnd_;
    QJsonObject transformPreview_, transformArray_;
    double transformAngle_{};
    void captureTransformTargets();
    void beginTransform(Vec3 pivot);
    void transformClick(QPointF point);
    void updateTransformPreview(QPointF point);
    QJsonObject transformCommand(const Transform &operation) const;
    QJsonObject transformAt(Vec3 point);
    void finishTransform(const QJsonObject &command, bool arrayEligible = true);
    bool transformMeasurements(const QString &text);
    // R095.e2: "@E, N[, Z]" (without the @) as the model point "[x;y;z]" in metres. Throws
    // without a site origin or for a tool that takes no point.
    QString sitePositionMeasurement(const QString &text) const;
    Vec3 extrusionAxis_{};
    double extrusionScale_{1}, previewDistance_{};
    Id offsetBody_{}, offsetFace_{};
    Vec3 offsetNormal_{}, offsetAxis_{};
    HostedPlacementOptions hostedOptions_;
    bool hostedRetainPose_{};
    std::optional<QJsonObject> hostedCommand_;
    void beginHostedPlacement();
    void updateHostedPlacement(QPointF point);
    void finishHostedPlacement();
    bool hostedMeasurements(const QString &text);
    void validateHostedLocks(const QJsonObject &command) const;
    void validateHostedTransform(const QJsonObject &command) const;
    std::optional<QJsonObject> orientationCommand_;
    bool orientConnected_{};
    std::vector<std::array<Vec3, 2>> orientationNormals_;
    void beginOrientation();
    void finishOrientation();
    std::optional<QJsonObject> booleanCommand_;
    QString booleanOperation_{"union"};
    bool booleanKeepOperands_{true}, booleanSwap_{};
    void beginBoolean();
    void finishBoolean();
    std::optional<QJsonObject> sweepCommand_;
    std::optional<QJsonObject> intersectionCommand_;
    QString intersectionMode_{"selected"};
    void beginIntersection();
    void finishIntersection();
    void beginSweep();
    void finishSweep();
    void beginOffset(Id body, Id face, Vec3 anchor);
    QJsonObject offsetCommand(double distance) const;
    void updateOffsetPreview(QPointF point);
    void finishOffset(double distance);
    QString previewError_;
    std::vector<std::array<Vec3, 2>> previewEdges_;
    std::vector<Guide> previewGuides_;
    QTabBar *sceneTabs_{};
    QVariantAnimation *sceneAnimation_{};
    int sceneTransitionMs_{160};
    QTimer *walkTimer_{};
    QElapsedTimer walkElapsed_;
    std::set<int> walkKeys_;
    double walkSpeed_{3};
    void initializeWalkNavigation();
    void stopWalking();
    bool walkNavigation(QEvent *event);
    bool reducedMotion_{}, sceneCameraStep_{};
    SceneCamera sceneCameraFrom_, sceneCameraTo_;
    Document::SaveStamp sceneAnimationStamp_;
    void initializeSceneViews();
    void layoutSceneTabs();
    void syncSceneTabs();
    void stopSceneTransition();
    void applySceneCamera(const SceneCamera &camera);
    Vec3 target_{};
    Vec3 renderOrigin_{}; // Nearby, snapped origin for GPU floats; model stays in world doubles.
    float yaw_{-45}, pitch_{35}, distance_{14};
    bool ortho_{false}, trackpad_{};
    float fov_{45};
    bool pushNewFace_{}, pushControlPending_{};
    Vec3 extrusionLocalOrigin_{}, extrusionLocalNormal_{0, 0, 1};
    void beginExtrusion(Id body, Id face, Vec3 anchor);
    std::optional<double> lastPushDistance_;
    Document::SaveStamp repeatSession_;
    void cameraChanged();
    void panCamera(QPointF position, QPointF delta);
    void orbitCamera(QPointF delta);
    void zoomCamera(QPointF position, double factor);
    bool nativeNavigation(QEvent *event);
    int instances_{0}, benchmarkTriangles_{0};
    double frameMs_{};
    bool synchronousFrameTiming_{};
    QPointer<QWidget> contextBreadcrumb_;
    QPointer<QWidget> contextBanner_;
    QRect projectionLabelPainted_;
    QString graphics_;
    QMatrix4x4 matrix() const;
    std::pair<Vec3, Vec3> ray(QPointF p) const;
    std::optional<Vec3> ground(QPointF p);
    void acquireInference(QPointF point, bool constrainPlane);
    InferenceWorker inferenceWorker_;
    bool inferencePending_{false};
    InferenceResult inference_;
    size_t inferenceChoice_{};
    bool inferenceCycled_{};
    QPointF inferencePointer_;
    std::vector<DirectionCandidate> directions_;
    DirectionLocks directionLocks_;
    std::optional<InferenceCandidate> reference_, hoverReference_, heldPoint_;
    std::optional<Document::SaveStamp> referenceStamp_;
    std::vector<DirectionConstraint> referenceDirections_;
    std::optional<Vec3> referenceAnchor_;
    QElapsedTimer referenceTimer_;
    QPointF referencePointer_;
    std::optional<DrawingPlane> heldPlane_;
    Id heldContext_{};
    bool shiftHeld_{};
    bool guidesVisible_{true}, createGuides_{true}, measurementCompleted_{}, guideControlPending_{};
    std::optional<Guide> tapeReference_, previewGuide_;
    bool guideTool() const { return tool_ == Tool::Tape || tool_ == Tool::Protractor; }
    void captureTapeReference();
    Guide prospectiveGuide(Vec3 end) const;
    QJsonObject guideCommand(Vec3 end) const;
    double guideMeasurement(Vec3 end) const;
    QString guideMeasurementText(Vec3 end) const;
    void finishGuide(Vec3 end);
    std::optional<std::array<QPointF, 2>> guideSegment(const Guide &guide, Id body = 0) const;
    void paintGuide(QPainter &painter, const Guide &guide, Id body = 0) const;
    void paintGuides(QPainter &painter) const;
    size_t inferenceCount() const { return inference_.candidates.size() + directions_.size(); }
    void armReference();
    void releaseInferenceHold();
    void clearConstraints();
    bool constraintKey(QKeyEvent *event);
    void validateLockedPoint(Vec3 point) const;
    void choosePlane(QPointF point);
    void beginChain();
    bool drawingTool() const;
    bool arcTool() const;
    bool threePointTool() const;
    QString nextPointHint() const;
    void rebuild();
    QSize rasterSize_;
    std::optional<QMatrix4x4> measuredRasterMatrix_;
    bool drawingViewRaster_{}; // renderDrawingViewRaster is drawing a sheet view.
    double measuredAnnotationScale_{1};
    int renderWidth() const { return rasterSize_.isEmpty() ? width() : rasterSize_.width(); }
    int renderHeight() const { return rasterSize_.isEmpty() ? height() : rasterSize_.height(); }
    qreal renderPixelRatio() const { return rasterSize_.isEmpty() ? devicePixelRatioF() : 1.; }
    void paintScene(QPaintDevice *device = nullptr);
    void cleanupGL();
    void upload(GpuBatch &batch, const std::vector<Vertex> &vertices, bool transparent = false,
                std::optional<Vec3> origin = std::nullopt);
    void draw(GpuBatch &batch, GLenum mode, int instances = 1, QVector3D offset = {});
    void sortTransparent(const QMatrix4x4 &matrix);
    bool clipped(Vec3 point, Id body = 0) const;
    void finishShape(Vec3 end, std::optional<QJsonObject> command = std::nullopt);
    QJsonObject shapeCommand(Vec3 end) const;
    QJsonObject extrusionCommand(double distance) const;
    QJsonObject previewCommand(const QJsonObject &command);
    void updateToolPreview(QPointF point);
    void finishExtrusion(double distance);
    void clearPreview();
    std::optional<StretchPlane> stretchPlane_;
    bool stretchDragging_{};
    std::vector<std::array<Vec3, 2>> ghostEdges_;
    std::vector<std::vector<Vec3>> ghostFaces_;
    std::string ghostDocument_;
    std::uint64_t ghostRevision_{};
    // The plane's member frame: world transform, local bounds and distinct coordinates.
    bool stretchFrame(Transform &world, Vec3 &low, Vec3 &high) const;
    bool stretchPress(QMouseEvent *event);
    bool stretchMove(QMouseEvent *event);
    bool stretchRelease(QMouseEvent *event);
    double stretchAtPointer(QPointF point, bool snap) const;
    void paintParameterOverlay(QPainter &painter);
    // R094.f: assembly placement (viewport_assembly.cpp).
    struct AssemblyPlacementState {
        std::shared_ptr<const ComponentBundle> bundle;
        QJsonObject values;
        bool gluing{};
        DrawingPlane glue{};                           // bundle-local glue frame
        std::vector<std::array<Vec3, 2>> edges;        // bundle-local ghost
        std::vector<std::vector<Vec3>> faces;
        std::vector<Vec3> outline;                     // bundle-local glue outline
        std::optional<AssemblyPlacement> target;       // where a click places
        std::optional<Transform> world;                // where the ghost is drawn
        std::vector<Vec3> cut;                         // the dry run's opening, world
        QString error;
    };
    std::optional<AssemblyPlacementState> assembly_;
    struct AssemblyAmend {
        std::shared_ptr<const ComponentBundle> bundle;
        QJsonObject values;
        AssemblyPlacement placement;
        Document::AmendStamp stamp;
        Id placed{};
    };
    std::optional<AssemblyAmend> assemblyAmend_;
    QTimer *assemblyDryRun_{};
    std::optional<AssemblyPlacement> assemblyOnFace(Id host, Id face, std::optional<Vec3> anchor,
                                                    Transform &world) const;
    void updateAssemblyTarget(QPointF position);
    void assemblyFullDryRun();
    void placeAssembly(const AssemblyPlacement &placement);
    bool assemblyPress(QMouseEvent *event);
    void assemblyMove(QMouseEvent *event);
    bool assemblyKey(QKeyEvent *event);
    bool assemblyMeasurements(const QString &text);
    void paintAssemblyOverlay(QPainter &painter);
};
} // namespace sketchy
