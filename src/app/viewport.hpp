#pragma once
#include "app/inference_worker.hpp"
#include "app/theme.hpp"
#include "app/tool_session.hpp"
#include "core/model.hpp"
#include "core/selection.hpp"
#include "geometry/constraints.hpp"
#include "geometry/drawing.hpp"
#include "geometry/inference.hpp"
#include "integrations/render_snapshot.hpp"
#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointF>
#include <functional>
#include <optional>
namespace sketchy {
class Viewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
  public:
    explicit Viewport(Document &doc, QWidget *parent = nullptr);
    ~Viewport() override;
    const Document &document() const { return doc_; }
    bool inspectionBusy() const {
        return session_.active() || dragging_ || toolPressed_ || selectionPressed_ || selectingBox_;
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
        Orientation = 25
    };
    void setTool(Tool tool);
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
    void makeGroup();
    void explodeGroups();
    void mergeContextGeometry();
    Id componentScope() const;
    void makeComponent(const QString &name);
    void makeComponentUnique(bool activeScope = false);
    void replaceComponent(Id definition);
    void placeComponent(Id definition, Vec3 position);
    void changeComponentAxes(Transform axes);
    void paintSelection(std::array<float, 3> color);
    void organize(const QJsonArray &commands);
    Id paintMaterial() const;
    int paintSide() const { return paintSide_; }
    void setPaintMaterial(Id material, int side);
    void editMaterials(const QJsonArray &commands);
    void applyMaterialToSelection();
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
    Id selectedBody() const { return selected_; }
    Id selectedFace() const { return selectedFace_; }
    void refresh();
    void setAssistantPreview(std::shared_ptr<const Document::PreparedEdit> edit);
    bool hasAssistantPreview() const;
    void setAssistantPreviewFocus(Id body);
    void fit();
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
    void benchmark(int triangles, bool instanced = false);
    struct RenderStats {
        std::uint64_t frames{}, geometryUploads{}, transparencyUploads{}, uploadedBytes{};
        std::uint64_t bodyMeshBuilds{}, bodyWorldUpdates{}, bodyUploads{};
        size_t cachedBodies{};
        unsigned contextGeneration{}, glError{};
        size_t meshTriangles{};
    };
    RenderStats renderStats() const { return stats_; }
    double lastFrameMs() const { return frameMs_; }
  signals:
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
    void message(const QString &text);

  protected:
    bool event(QEvent *) override;
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;

  private:
    QJsonObject commitCommands(const QJsonArray &commands, bool shared = true);
    Id selectedComponent() const;
    void paintAt(QPointF point, bool sample);
    Id paintMaterial_{};
    int paintSide_{2};
    Document::SaveStamp paintSession_;
    struct Vertex {
        float x{}, y{}, z{}, r{}, g{}, b{}, a{1};
        float br{r}, bg{g}, bb{b}, ba{a};
    };
    Document &doc_;
    ToolSession session_;
    ThemeColors colors_{themeColors(false)};
    Tool tool_{Tool::Select};
    struct GpuBatch {
        QOpenGLBuffer buffer{QOpenGLBuffer::VertexBuffer};
        int count{};
    };
    std::unique_ptr<QOpenGLShaderProgram> shader_;
    GpuBatch gridGpu_, transparentGpu_, benchmarkGpu_;
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
    bool visible(SelectedEntity entity) const;
    bool selectable(SelectedEntity entity) const;
    void selectionChanged(bool policy = false);
    void updatePrimarySelection();
    bool selectionKey(QKeyEvent *event);
    SelectionMode selectionMode(Qt::KeyboardModifiers modifiers) const;
    void selectionRelease(QPointF point);
    std::optional<SelectedEntity> pickEntity(QRgb color) const;
    struct BodyCache {
        BodyPtr record;
        MaterialRecords materials;
        Transform world;
        std::vector<Triangle> localTriangles, worldTriangles;
        struct MeshEdge {
            Vec3 a, b;
            Id id;
        };
        std::vector<MeshEdge> localEdges, worldEdges;
        Bounds bounds;
        std::vector<Vertex> opaque, lines;
        std::vector<std::array<Vertex, 3>> transparent;
        GpuBatch opaqueGpu, linesGpu;
        float alpha{1};
        std::uint64_t presentationRevision{};
    };
    std::map<Id, std::unique_ptr<BodyCache>> bodyCaches_;
    bool gridDirty_{true};
    QMetaObject::Connection contextCleanup_;
    QOpenGLVertexArrayObject vao_;
    std::vector<Vertex> benchmarkVertices_;
    std::vector<std::array<Vertex, 3>> transparent_;
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
    Vec3 extrusionAxis_{};
    double extrusionScale_{1}, previewDistance_{};
    Id offsetBody_{}, offsetFace_{};
    Vec3 offsetNormal_{}, offsetAxis_{};
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
    QVector3D target_{0, 0, 0};
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
    std::optional<std::array<QPointF, 2>> guideSegment(const Guide &guide) const;
    void paintGuide(QPainter &painter, const Guide &guide) const;
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
    void paintScene();
    void cleanupGL();
    void upload(GpuBatch &batch, const std::vector<Vertex> &vertices, bool transparent = false);
    void draw(GpuBatch &batch, GLenum mode, int instances = 1);
    void sortTransparent(const QMatrix4x4 &matrix);
    bool clipped(Vec3 point) const;
    void finishShape(Vec3 end, std::optional<QJsonObject> command = std::nullopt);
    QJsonObject shapeCommand(Vec3 end) const;
    QJsonObject extrusionCommand(double distance) const;
    QJsonObject previewCommand(const QJsonObject &command);
    void updateToolPreview(QPointF point);
    void finishExtrusion(double distance);
    void clearPreview();
};
} // namespace sketchy
