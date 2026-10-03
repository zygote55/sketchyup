#pragma once
#include "app/theme.hpp"
#include "app/tool_session.hpp"
#include "core/model.hpp"
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
    enum class Tool {
        Select = 0,
        Rectangle = 1,
        Circle = 2,
        Extrude = 3,
        Orbit = 4,
        Pan = 5,
        Line = 6
    };
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    ToolSession::Phase interactionPhase() const { return session_.phase(); }
    std::optional<Vec3> operationAnchor() const { return anchor_; }
    bool previewValid() const { return previewValid_; }
    void setSelection(Id body, Id face = 0);
    Id selectedBody() const { return selected_; }
    Id selectedFace() const { return selectedFace_; }
    void refresh();
    void fit();
    void standardView(int view);
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
    void selected(qulonglong body, qulonglong face);
    void toolChanged(int tool);
    void changed();
    void message(const QString &text);

  protected:
    bool event(QEvent *) override;
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    struct Vertex {
        float x, y, z, r, g, b, a{1};
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
    struct BodyCache {
        BodyPtr record;
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
        bool selected{};
        Id selectedFace{};
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
    std::optional<Vec3> anchor_, cursor_;
    bool toolPressed_{false}, dragCommit_{false}, previewValid_{false};
    QPointF toolPressPosition_;
    Vec3 extrusionAxis_{};
    double extrusionScale_{1}, previewDistance_{};
    QString previewError_;
    std::vector<std::array<Vec3, 2>> previewEdges_;
    QVector3D target_{0, 0, 0};
    float yaw_{-45}, pitch_{35}, distance_{14};
    bool ortho_{false};
    int instances_{0}, benchmarkTriangles_{0};
    double frameMs_{};
    QString graphics_;
    QMatrix4x4 matrix() const;
    std::pair<Vec3, Vec3> ray(QPointF p) const;
    std::optional<Vec3> ground(QPointF p) const;
    void rebuild();
    void paintScene();
    void cleanupGL();
    void upload(GpuBatch &batch, const std::vector<Vertex> &vertices, bool transparent = false);
    void draw(GpuBatch &batch, GLenum mode, int instances = 1);
    void sortTransparent(const QMatrix4x4 &matrix);
    bool clipped(Vec3 point) const;
    void finishShape(Vec3 end);
    QJsonObject shapeCommand(Vec3 end) const;
    QJsonObject extrusionCommand(double distance) const;
    void previewCommand(const QJsonObject &command);
    void updateToolPreview(QPointF point);
    void finishExtrusion(double distance);
    void clearPreview();
};
} // namespace sketchy
