#pragma once
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
    enum class Tool { Select, Rectangle, Circle, Extrude, Orbit, Pan };
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    void setSelection(Id body, Id face = 0);
    Id selectedBody() const { return selected_; }
    Id selectedFace() const { return selectedFace_; }
    void refresh();
    void fit();
    void standardView(int view);
    void cancel();
    void measurements(const QString &value);
    QPointF project(Vec3 p) const;
    std::pair<Id, Id> pick(QPointF point) const;
    bool rendererReady() const { return ready_; }
    QString graphicsDescription() const { return graphics_; }
    void benchmark(int triangles);
    double lastFrameMs() const { return frameMs_; }
  signals:
    void selected(qulonglong body, qulonglong face);
    void changed();
    void message(const QString &text);

  protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    struct Vertex {
        float x, y, z, r, g, b;
    };
    struct HitTriangle {
        Triangle triangle;
        Id body;
    };
    Document &doc_;
    Tool tool_{Tool::Select};
    QOpenGLShaderProgram shader_;
    QOpenGLBuffer buffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao_;
    std::vector<Vertex> triangles_, lines_;
    std::vector<HitTriangle> picking_;
    Id selected_{}, selectedFace_{};
    bool ready_{false}, cacheDirty_{true}, dragging_{false};
    Qt::MouseButton dragButton_{Qt::NoButton};
    QPointF previous_;
    std::optional<Vec3> anchor_, cursor_;
    QVector3D target_{0, 0, 0};
    float yaw_{-45}, pitch_{35}, distance_{14};
    bool ortho_{false};
    int instances_{0};
    double frameMs_{};
    QString graphics_;
    QMatrix4x4 matrix() const;
    std::pair<Vec3, Vec3> ray(QPointF p) const;
    std::optional<Vec3> ground(QPointF p) const;
    void rebuild();
    void draw(const std::vector<Vertex> &vertices, GLenum mode, int instances = 1);
    void finishShape(Vec3 end);
};
} // namespace sketchy
