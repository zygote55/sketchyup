#pragma once
#include "app/sheet_evaluation.hpp"
#include "app/theme.hpp"
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
namespace sketchy {
// The Sheets workspace page canvas (ADR 0164 §1, R091.e): paper millimetres are the model
// space, and one transform maps them to logical pixels. Page content is painted by the shared
// compositor (paintDrawingSheet); the canvas then paints the screen-only decorations: grid,
// selection, the "Changed" chip and the broken hatch and card. It is read-only in R091.e:
// Select picks a view, Pan and Zoom move the page.
class SheetCanvas : public QWidget {
    Q_OBJECT
  public:
    enum class Tool { Select, Pan, Zoom };
    SheetCanvas(const Document &doc, DrawingViewRenderCache &cache, QWidget *parent = nullptr);
    ~SheetCanvas() override;
    void setSheet(Id sheet);
    Id sheet() const { return sheet_; }
    void setTheme(const ThemeColors &colors);
    void setGrid(bool shown);
    bool grid() const { return grid_; }
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    // Fits the page with a 24 px pasteboard border; the page stays fitted while the canvas
    // resizes until the next zoom or pan.
    void fitPage();
    bool fitted() const { return fitted_; }
    // 1 is actual size (96 logical pixels per inch).
    double zoom() const;
    void setZoom(double zoom, std::optional<QPointF> anchor = std::nullopt);
    void panBy(QPointF delta);
    double pixelsPerMm() const { return pixelsPerMm_; }
    QPointF paperOrigin() const { return origin_; }
    QTransform paperToWidget() const;
    void setSelectedView(Id view);
    Id selectedView() const { return selectedView_; }
    // Zoom tool grammar: "150%" or a factor ("1.5"). False with a reason for other input.
    bool measurements(const QString &text, QString &error);
    // "View 1 · Ground floor plan · 1:50 · Changed since accepted".
    QString describeView(Id view) const;
    QString pageName() const;
    // The broken or failed card's message, empty for a view that draws.
    QString brokenMessage(Id view) const;
    // Views in paint order, for the accessible children.
    std::vector<Id> views() const;
    QRectF viewRect(Id view) const; // Widget coordinates.
    using RasterRenderer = std::function<QImage(const DrawingViewEvaluationInput &, QSize)>;
    void setRasterRenderer(RasterRenderer renderer) { renderer_ = std::move(renderer); }
    // The document or an evaluation changed: repaint, schedule raster content, announce.
    void contentChanged();
    // Raster images painted from the cache in the last paint, for tests.
    size_t paintedRasters() const { return paintedRasters_; }

  signals:
    void selectionChanged(qulonglong view);
    void zoomChanged(double zoom);
    void toolChanged(int tool);
    void measurementsRequested(const QString &text);
    void escapeToSelect();

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

  private:
    const Document &doc_;
    DrawingViewRenderCache &cache_;
    ThemeColors colors_{themeColors(false)};
    Id sheet_{}, selectedView_{};
    Tool tool_{Tool::Select};
    bool grid_{true}, fitted_{true};
    double pixelsPerMm_{1};
    QPointF origin_;
    enum class Drag { None, Pan, Zoom } drag_{Drag::None};
    QPointF dragStart_, dragLast_;
    double dragZoom_{1};
    RasterRenderer renderer_;
    QTimer rasterTimer_;
    size_t paintedRasters_{};
    std::shared_ptr<const SheetEvaluation> painted_;
    const DrawingSheetRecord *record() const;
    QRectF paperRect() const;
    void renderRasters();
    void announce();
    void setPixelsPerMm(double value, QPointF anchor);
    void paintDecorations(QPainter &painter, const PreparedDrawingView &view);
};
} // namespace sketchy
