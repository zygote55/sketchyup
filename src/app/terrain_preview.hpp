#pragma once
#include "app/theme.hpp"
#include "core/terrain_records.hpp"
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QWidget>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QPainter;

namespace sketchy {
// R107.b4 (UX §20.2, §20.7 item 12): the GeoTIFF window's overlays, in the local frame, drawn over
// the data: the file's extent (dashed), the window (solid, the failure colour when it leaves the
// file), the grid (a light fill), the site origin (a cross) and the void posts (✕). Each carries a
// label, so the dash, the fill and the glyph are never the only carriers of their meaning.
struct TerrainPreviewOverlays {
    std::vector<QPointF> file;    // a closed outline; empty when there is no file
    QString fileLabel;
    std::vector<QPointF> window;  // a closed outline; empty when there is no window
    QString windowLabel;
    bool windowOutside{};         // the window leaves the file: the failure colour
    std::vector<QPointF> grid;    // a closed outline of the posts' extent
    QString gridLabel;
    std::optional<QPointF> origin;
    std::vector<QPointF> voids;   // the void posts, as far as they are listed
    QString voidLabel;
    bool empty() const {
        return file.empty() && window.empty() && grid.empty() && !origin && voids.empty();
    }
};

// R095.d: what the terrain import preview draws, in the terrain's local frame (model metres,
// top-down, +Y north). Built on the dry-run worker from the prepared sources and the dry-run
// mesh, then shared read-only with the UI thread.
struct TerrainPreviewData {
    struct Line {
        TerrainSourceRole role{TerrainSourceRole::Contour};
        std::vector<QPointF> points;
        float z{}; // the first vertex's elevation, for elevation shading
    };
    std::vector<Line> lines;      // contours, breaklines, boundary and holes
    std::vector<QPointF> points;  // ground points
    std::vector<float> pointZ;    // their elevations
    std::vector<QPointF> vertices; // mesh vertices
    std::vector<float> vertexZ;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::vector<std::array<std::uint32_t, 2>> edges; // unique mesh edges
    QRectF bounds;                                   // everything above
    float zMin{}, zMax{};
    std::size_t contours{}, breaklines{}, boundaries{}, holes{};
};
// Builds the unique edges, bounds and elevation range of `data` from its lines, points and
// triangles (the worker calls it once).
void finishTerrainPreview(TerrainPreviewData &data);

// The dialog's live preview: a plain top-down QPainter view, because the preview needs no
// depth, lighting or picking and the dialog must not share the main viewport's GL context.
// Sources draws contours, breaklines, boundary, holes and ground points by role; Surface the
// dry run's triangles. Elevation shading colours them by height. Drag or the arrow keys pan,
// the wheel or + and - zoom, Shift+Z or Home fits. A chosen report location is ringed in the
// failure colour. Large meshes are drawn once into a cached image per view.
class TerrainPreview final : public QWidget {
  public:
    enum class Mode { Sources, Surface };
    explicit TerrainPreview(QWidget *parent = nullptr);
    void setData(std::shared_ptr<const TerrainPreviewData> data);
    const TerrainPreviewData *data() const { return data_.get(); }
    // Shown instead of the drawing while there is nothing to draw.
    void setPlaceholder(const QString &text);
    const QString &placeholder() const { return placeholder_; }
    void setMode(Mode mode);
    Mode mode() const { return mode_; }
    void setShading(bool shading);
    bool shading() const { return shading_; }
    void setColors(const ThemeColors &colors);
    // R107.b4: the overlays, and the summary the accessible description starts with.
    void setOverlays(TerrainPreviewOverlays overlays);
    const TerrainPreviewOverlays &overlays() const { return overlays_; }
    void setSummary(const QString &summary);
    // Fits the data and the overlays.
    void fit();
    // Centres `local` and zooms so about `span` metres fit across, and rings it.
    void focusLocation(QPointF local, double span);
    void clearMarker();
    std::optional<QPointF> marker() const { return marker_; }
    // The view: pixels per metre and the local point at the centre.
    double scale() const { return scale_; }
    QPointF centre() const { return centre_; }
    QPointF toScreen(QPointF local) const;

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

  private:
    void changed();
    void zoomBy(double factor, QPointF anchor);
    void render(QImage &image) const;
    void paintOverlays(QPainter &painter) const;
    QRectF contentBounds() const;
    void describe();
    QColor elevationColour(float z) const;
    std::shared_ptr<const TerrainPreviewData> data_;
    TerrainPreviewOverlays overlays_;
    QString summary_;
    QString placeholder_;
    Mode mode_{Mode::Sources};
    bool shading_{};
    ThemeColors colors_{themeColors(false)};
    double scale_{1};
    QPointF centre_;
    bool autoFit_{true}; // the view follows the data and the size until the user moves it
    std::optional<QPointF> marker_;
    std::optional<QPointF> drag_;
    QImage cache_;
    bool cacheValid_{};
};
} // namespace sketchy
