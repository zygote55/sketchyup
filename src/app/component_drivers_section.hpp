#pragma once
#include "app/collapsible_section.hpp"
#include "app/component_definition_section.hpp"
#include "app/viewport.hpp"
#include <QJsonObject>
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
namespace sketchy {
class FormulaField;
// "Drivers" (ADR 0166 §3.3, §3.4): the open definition's drivers in a tree (kind, target,
// formula and state: "Bound", or amber "Changed" when the member was edited after its driver
// was set), the driver form below it, and Rebind (the selected Changed drivers) and Rebind all.
// A Changed row also has an in-row Rebind button for the pointer, which takes no focus.
// Selecting a row selects its member in the viewport and opens the form. Delete removes the
// driver. Add driver (Shift+D, + Driver, the rail) opens the form for the one selected member
// with only the kinds the core accepts for it. Every form change is checked by a dry run of
// component_parameter.drive (150 ms); Enter or Add commits one entry ("Add Stretch driver",
// "Edit Stretch driver"). The Stretch form shows the plane in the viewport, set by dragging
// it, by the Plane at field or by the Measurements box ("Plane").
class ComponentDriversSection : public CollapsibleSection {
    Q_OBJECT
  public:
    ComponentDriversSection(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    void refresh();
    void detach();
    authoring::Scope scope() const { return scope_; }
    QTreeWidget *tree() const { return tree_; }
    QPushButton *addButton() const { return add_; }
    QPushButton *rebindButton() const { return rebind_; }
    QPushButton *rebindAllButton() const { return rebindAll_; }
    QLabel *changedNote() const { return changedNote_; }
    QWidget *editor() const { return editor_; }
    QComboBox *kindChoice() const { return kind_; }
    QComboBox *axisChoice() const { return axis_; }
    QComboBox *childChoice() const { return child_; }
    FormulaField *formulaField() const { return formula_; }
    FormulaField *spacingField() const { return spacing_; }
    QLineEdit *planeEdit() const { return plane_; }
    QLabel *movingLabel() const { return moving_; }
    QSpinBox *maxCountSpin() const { return maxCount_; }
    QLabel *editorError() const { return error_; }
    QPushButton *editorAction() const { return action_; }
    QPushButton *commitButton() const { return commit_; }
    // The kinds the core accepts for a definition member (ADR 0166 §3.3), in the form's
    // order: stretch, position, rotation, visibility, repeat, childParameter. Empty for the
    // root and for a repeat copy, with the reason in `why`.
    static QStringList kindsFor(const Document &doc, Id definition, Id member, QString *why = {});
    static QString kindTitle(const QString &kind);
    // Add driver: opens the form for the selected member and returns empty, or returns the
    // status sentence saying what to select.
    QString addDriver();
    void selectDriver(Id driver);
    Id currentDriver() const { return driver_; }
    Id member() const { return member_; }
    bool creating() const { return creating_; }
    // Commits the form (Enter, Add); returns the refusal's sentence, empty on success.
    QString commit();
    // Closes the form (Esc).
    void cancel();
    QString removeCurrent();
    // Rebind the selected Changed driver, or all Changed drivers.
    QString rebind(bool all);
    // The Measurements box while the Stretch form is open (ADR 0166 §3.4): "Plane" and the
    // plane's member-local offset. Enter moves the plane; it does not commit the driver.
    bool planeActive() const;
    QString planeText() const;
    bool setPlaneText(const QString &text, QString &error);
    // The form's dry-run check now (tests; the timer runs it 150 ms after a change).
    QString checkForm();

  signals:
    // The Measurements box routing changed (the plane form opened, closed or moved).
    void planeChanged();

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    Document &doc_;
    Viewport &view_;
    authoring::Scope scope_;
    QJsonObject description_;
    QString cacheKey_, signature_;
    Id driver_{}, member_{};
    bool creating_{}, detached_{}, busy_{}, syncing_{};
    double at_{};
    QPushButton *add_{}, *rebind_{}, *rebindAll_{}, *commit_{};
    QTreeWidget *tree_{};
    QLabel *changedNote_{}, *empty_{}, *editorTitle_{}, *error_{}, *moving_{};
    QWidget *editor_{};
    QComboBox *kind_{}, *axis_{}, *child_{};
    FormulaField *formula_{}, *spacing_{};
    QLineEdit *plane_{};
    QSpinBox *maxCount_{};
    QWidget *kindRow_{}, *axisRow_{}, *planeRow_{}, *spacingRow_{}, *maxRow_{}, *childRow_{};
    QTimer *check_{};
    void rebuild();
    QJsonObject driverNamed(Id driver) const;
    QString memberName(Id member) const;
    QString targetText(const QJsonObject &driver) const;
    QString kind() const;
    void openForm(Id member, Id driver);
    void syncForm();
    void syncPlane();
    QStringList axesFor(const QString &kind) const;
    QString defaultFormula(const QString &kind) const;
    QJsonObject driveCommand(const QString &formula, const QString &spacing,
                             QString &problem) const;
    QJsonObject checkFormula(const QString &text, bool spacing) const;
    QString present(const QJsonObject &error);
    // R094.e: the form message's action ("Select #1") and the message it acts on.
    QPushButton *action_{};
    ParameterMessage message_;
    QString labelOf(const QString &name) const;
};
} // namespace sketchy
