#pragma once
#include "app/sheet_evaluation.hpp"
#include "app/theme.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
class QLabel;
class QPushButton;
namespace sketchy {
// The Sheets workspace page canvas (ADR 0164 §1): paper millimetres are the model space, and
// one transform maps them to logical pixels. Page content is painted by the shared compositor
// (paintDrawingSheet); the canvas then paints the screen-only decorations: grid, selection and
// handles, snapping guides and hints, the "Changed" chip, the broken hatch and card, and the
// dimmed surround of edit-view.
// - R091.e: Select picks a view, Pan and Zoom move the page.
// - R091.f (sheet_tools.cpp): the Model view tool (W) places a view; Select and Move (M) move
//   views, the corner handles crop them, arrows nudge, Tab cycles the views, Enter opens
//   edit-view (pan changes the offset, the wheel changes the crop at the same scale), and the
//   Measurements box takes Size (w,h), Scale (1:N) and Move (<dx,dy>, [x,y]) in paper mm.
//   Every change is committed through the committer, one labelled sheet-only entry, and
//   nothing is committed while a drag or preview is in progress.
class SheetCanvas : public QWidget {
    Q_OBJECT
  public:
    // The integer values are the sheet rail actions' "sheetTool" property.
    enum class Tool { Select, Pan, Zoom, View, Move };
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
    QPointF widgetPoint(QPointF paperMm) const { return paperToWidget().map(paperMm); }
    void setSelectedView(Id view);
    Id selectedView() const { return selectedView_; }
    // The Measurements box for the active tool and step: Zoom ("150%"), Size ("w,h"), Scale
    // ("1:50", or "w,h" for the crop, "<dx,dy>" or "[x,y]" to move), Distance (Move tool). False
    // with a reason for input that does not apply.
    bool measurements(const QString &text, QString &error);
    // The box's label and value for the current step; an empty label disables the box.
    QString measurementLabel() const;
    QString measurementValue() const;
    // The status-bar instruction and active-tool name for the current tool and step.
    QString instruction() const;
    QString toolName() const;
    // "View 1 · Ground floor plan · 1:50 · Changed since accepted".
    QString describeView(Id view) const;
    QString pageName() const;
    // The broken or failed card's message, empty for a view that draws.
    QString brokenMessage(Id view) const;
    // Views in paint order, for the accessible children and Tab cycling.
    std::vector<Id> views() const;
    // Widget coordinates of a view's frame, following a drag in progress.
    QRectF viewRect(Id view) const;
    // The frame of the painted "Choose scene…" button on a broken view's card, if shown.
    QRectF chooseSceneButton(Id view) const;
    using RasterRenderer = std::function<QImage(const DrawingViewEvaluationInput &, QSize)>;
    void setRasterRenderer(RasterRenderer renderer) { renderer_ = std::move(renderer); }
    // The document or an evaluation changed: repaint, schedule raster content, announce.
    void contentChanged();
    // Raster images painted from the cache in the last paint, for tests.
    size_t paintedRasters() const { return paintedRasters_; }

    // ---- R091.f editing ----------------------------------------------------------------
    // Commits sheet-only commands as one labelled history entry. With an amend key equal to
    // the previous commit's key, and nothing changed since, the commit amends that entry
    // instead (nudges, Measurements re-entry). Returns the batch result, or an empty object
    // when the commit was refused (the committer reports why).
    using Committer = std::function<QJsonObject(const QJsonArray &commands, const QString &label,
                                                const QString &amendKey)>;
    void setCommitter(Committer committer) { committer_ = std::move(committer); }
    // What the Model view tool places, from the "New model view" panel.
    struct Placement {
        Id scene{};
        QString name;               // The view title; made unique among the views on commit.
        double scaleDenominator{50};
        bool perspective{};         // Places as not to scale, with rendered content.
        QSizeF naturalMm{120, 80};  // The content's extent at the scale, plus a border.
        QPointF contentCentreMm;    // Content centre relative to the scene target, paper mm.
    };
    void setPlacement(const Placement &placement);
    const Placement &placement() const { return placement_; }
    // Places a view: the anchored preview frame, else the natural size at the last pointer
    // position (or the top-left of the margin). Enter and the panel's Place button.
    bool placeView(std::optional<QSizeF> size = std::nullopt);
    // An operation (frame, drag or pick-up) is in progress.
    bool operationActive() const { return drag_ != Drag::None || anchored_; }
    // Cancels the operation in progress; nothing is committed.
    void cancelOperation();
    // The frame being placed, or the dragged view's frame, in paper mm.
    std::optional<QRectF> previewFrame() const;
    // "Snapped to the top edge of view 1 and the 5 mm grid", or empty.
    QString snapHint() const { return snapHint_; }
    // Moves the selected view by a paper-millimetre step (arrows: 1 mm, Shift: 10 mm).
    bool nudge(QPointF deltaMm);
    // Tab and Shift+Tab: the next or previous view in paint order.
    void cycleSelection(bool backward);
    // Edit-view (design §7): the selected, drawable, to-scale view. Esc or Done leaves.
    bool enterEditView();
    void leaveEditView();
    Id editView() const { return editView_; }
    QWidget *editBanner() const { return banner_; }
    QPushButton *saveSceneButton() const { return saveScene_; }
    QPushButton *doneButton() const { return done_; }
    // Crop zoom in edit-view: the frame scales about a paper point; the scale stays.
    bool zoomCrop(double factor, std::optional<QPointF> aboutMm = std::nullopt);
    // Pan in edit-view: the drawing moves by a paper-millimetre delta inside the frame.
    bool panContent(QPointF deltaMm, const QString &amendKey);
    // Ends Measurements re-entry and nudge amending (a new operation starts).
    void endRevision() { revision_ = {}; }

  signals:
    void selectionChanged(qulonglong view);
    void zoomChanged(double zoom);
    void toolChanged(int tool);
    void measurementsRequested(const QString &text);
    void escapeToSelect();
    // The instruction, active tool or Measurements step changed.
    void stepChanged();
    void editViewChanged(qulonglong view);
    void chooseSceneRequested(qulonglong view);
    void saveSceneRequested(qulonglong view);
    // A status message for the user (refusals and results).
    void message(const QString &text);

  protected:
    bool event(QEvent *) override;
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
    enum class Drag { None, Pan, Zoom, Place, Move, Crop, EditPan } drag_{Drag::None};
    QPointF dragStart_, dragLast_;
    double dragZoom_{1};
    RasterRenderer renderer_;
    QTimer rasterTimer_;
    size_t paintedRasters_{};
    std::shared_ptr<const SheetEvaluation> painted_;
    std::map<Id, QRectF> chooseButtons_;
    // R091.f state.
    Committer committer_;
    Placement placement_;
    bool anchored_{};       // Click–move–click: the first corner or pick-up is down.
    bool dragMoved_{};      // The pointer moved past the drag threshold since the press.
    QPointF anchorMm_, cornerMm_;
    std::optional<QPointF> hoverMm_;
    int cropCorner_{};      // 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left.
    // The dragged view's record while a move, crop or edit-view pan previews.
    std::optional<DrawingViewRecord> preview_;
    QString snapHint_;
    std::vector<QLineF> guides_; // Alignment guides of the last snap, paper mm.
    struct Revision {
        QString key;            // Amend key of the last commit; empty when none.
        DrawingViewRecord base; // The view before the operation being revised.
        QPointF direction;      // Unit drag direction of a move, for a typed distance.
        QJsonObject create;     // The last placement's create command.
    } revision_;
    Id editView_{};
    QWidget *banner_{};
    QLabel *bannerText_{};
    QPushButton *saveScene_{}, *done_{};
    const DrawingSheetRecord *record() const;
    QRectF paperRect() const;
    void renderRasters();
    void announce();
    void setPixelsPerMm(double value, QPointF anchor);
    void paintDecorations(QPainter &painter, const PreparedDrawingView &view);
    // sheet_tools.cpp
    const DrawingViewRecord *liveView(Id view) const;
    DrawingViewRecord effectiveView(Id view) const;
    // The prepared sheet with views moved to their current or previewed records; nullopt
    // when nothing differs.
    std::optional<PreparedDrawingSheet> adjusted(const PreparedDrawingSheet &prepared) const;
    void paintEditing(QPainter &painter);
    void layoutBanner();
    void themeBanner();
    QPointF toPaper(QPointF widget) const;
    double tolerance() const;
    Id viewAt(QPointF paperMm) const;
    int handleAt(QPointF widget) const;
    QPointF snapPoint(QPointF paperMm, bool snapping);
    QRectF snapMove(QRectF frame, bool snapping);
    QRectF clampFrame(QRectF frame) const;
    QRectF placeFrame(QPointF corner, bool proportional) const;
    QRectF cropFrame(QPointF corner) const;
    bool commitView(const DrawingViewRecord &before, const DrawingViewRecord &after,
                    const QString &label, const QString &amendKey);
    bool commitPlacement(QRectF frame, bool amend = false);
    QJsonObject commit(const QJsonArray &commands, const QString &label, const QString &amendKey);
    void pressEditing(QMouseEvent *event);
    void moveEditing(QMouseEvent *event);
    void releaseEditing(QMouseEvent *event);
    bool keyEditing(QKeyEvent *event);
    void clearPreview();
};
// What the Model view tool places for a scene at a scale: the scene name, whether it is a
// perspective, and the visible geometry's extent around the scene target (plus a 10 mm border,
// rounded up to 5 mm) so the drawing starts centred in a frame that fits it.
SheetCanvas::Placement sheetPlacement(const Document &doc, Id scene, double scaleDenominator);
// A frame that keeps a view's drawing where it is on paper: the offset is adjusted so the
// scene target stays at the same paper point when the frame (the crop) changes.
DrawingViewRecord croppedView(const DrawingViewRecord &view, const DrawingFrame &frame);
} // namespace sketchy
