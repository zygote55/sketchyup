#pragma once
// R096.f1 (UX_DESIGN §15.1, §15.2, §15.7): the tool options strip across the top of the viewport
// while a terrain tool is active. It holds the switcher (Sculpt · Smooth; f2 adds Contours, Pad and
// Drape) and the tool's options: Scope (Brush or Region), Radius, Falloff, Falloff width (region),
// Amount (Sculpt) or Strength and Passes (Smooth). It is one row where it fits and wraps to a
// second row where it does not. Its length fields take the Measurements box's syntax and units
// (350cm, 12'6", 3/4"); a field that cannot be parsed keeps its old value, is marked and says why.
// Every control is a Tab stop with an accessible name; Escape in a field restores it and returns
// focus to the viewport (UX §4.2's keyboard ownership rule).
#include "app/terrain_tool.hpp"
#include "app/theme.hpp"
#include <QFrame>
#include <functional>
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QToolButton;
namespace sketchy {
class Viewport;
class TerrainToolStrip final : public QFrame {
    Q_OBJECT
  public:
    explicit TerrainToolStrip(Viewport &viewport);
    // Shows the strip for a terrain tool and fills it from the viewport's options.
    void sync();
    // Places the strip across the top of the viewport, below `top` (logical pixels).
    void place(int top);
    void setColors(const ThemeColors &colors);
    QWidget *firstField() const;
    QLineEdit *radiusField() const { return radius_; }
    QLineEdit *amountField() const { return amount_; }
    QLineEdit *strengthField() const { return strength_; }
    QLineEdit *widthField() const { return width_; }
    QSpinBox *passesField() const { return passes_; }
    QComboBox *falloffField() const { return falloff_; }
    QToolButton *toolButton(int tool) const;
    QToolButton *scopeButton(TerrainToolOptions::Scope scope) const;
    // R096.f2 (UX §15.3): Edit contours: the selected source, its Role and Elevation, and the
    // Add contour, Add breakline, Add spot, Promote and Remove buttons.
    QLabel *sourceSummary() const { return sourceSummary_; }
    QComboBox *roleField() const { return role_; }
    QLineEdit *elevationField() const { return elevation_; }
    QToolButton *contourButton(const QString &name) const;
    // R096.f2 (UX §15.4): Stamp pad: the footprint, Daylight or Fixed width, Cut, Fill, Max
    // distance or Width, Pad offset and the cut and fill legend.
    QLabel *padSummary() const { return padSummary_; }
    QToolButton *padModeButton(TerrainStampMode mode) const;
    QLineEdit *cutField() const { return cut_; }
    QLineEdit *fillField() const { return fill_; }
    QLineEdit *maxDistanceField() const { return maxDistance_; }
    QLineEdit *padWidthField() const { return padWidth_; }
    QLineEdit *padOffsetField() const { return padOffset_; }
    QWidget *legend() const { return legend_; }
    // R096.f2 (UX §15.5): Drape: Imprint or Overlay, Paint regions, Lift and the summary.
    QToolButton *drapeModeButton(bool overlay) const;
    QCheckBox *paintRegionsField() const { return paintRegions_; }
    QLineEdit *liftField() const { return lift_; }
    QLabel *drapeSummary() const { return drapeSummary_; }

  signals:
    // The switcher asked for a terrain tool (a Viewport::Tool value).
    void toolRequested(int tool);
    // A field's value was refused (the reason), or accepted (empty).
    void problem(const QString &text);

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    // Keys its controls leave unhandled stop here: they never reach the viewport (UX §4.2).
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

  private:
    void apply(QLineEdit *field);
    QString lengthText(double metres) const;
    QString elevationText(double metres) const;
    Viewport &viewport_;
    QButtonGroup *tools_{}, *scopes_{};
    QWidget *radiusBox_{}, *widthBox_{}, *amountBox_{}, *strengthBox_{}, *passesBox_{};
    QLineEdit *radius_{}, *amount_{}, *strength_{}, *width_{};
    QSpinBox *passes_{};
    QComboBox *falloff_{};
    QLabel *hint_{};
    // R096.f2: Edit contours.
    QList<QWidget *> sculptControls_, contourControls_;
    QLabel *sourceSummary_{};
    QComboBox *role_{};
    QLineEdit *elevation_{};
    QWidget *roleBox_{}, *elevationBox_{};
    QToolButton *addContour_{}, *addBreakline_{}, *addSpot_{}, *promote_{}, *removeSource_{};
    // R096.f2: Stamp pad and Drape.
    QList<QWidget *> padControls_, drapeControls_;
    QLabel *padSummary_{}, *drapeSummary_{};
    QButtonGroup *padModes_{}, *drapeModes_{};
    QLineEdit *cut_{}, *fill_{}, *maxDistance_{}, *padWidth_{}, *padOffset_{}, *lift_{};
    QWidget *cutBox_{}, *fillBox_{}, *maxDistanceBox_{}, *padWidthBox_{}, *liftBox_{}, *legend_{};
    QCheckBox *paintRegions_{};
    bool syncing_{};
};
} // namespace sketchy
