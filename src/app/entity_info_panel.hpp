#pragma once
#include "app/viewport.hpp"
#include "core/entity_measure.hpp"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
class QFormLayout;
namespace sketchy {
class CollapsibleSection;
class ComponentParametersSection;
class ComponentDefinitionSection;
class ComponentDriversSection;
class TerrainSection;
class TerrainEditsSection;
// The Info tab (ADR 0166 §1.1): a vertical stack of collapsible sections, "Entity info" (the
// selection's measurements and the Edit entity dialog) and, for a parametric placement,
// "Parameters · this placement"; in a definition context, "Parameters · definition" and
// "Drivers" (§3, R094.d).
class EntityInfoPanel : public QWidget {
  public:
    EntityInfoPanel(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    void refresh();
    ComponentParametersSection *parameters() const { return parameters_; }
    ComponentDefinitionSection *definition() const { return definition_; }
    ComponentDriversSection *drivers() const { return drivers_; }
    TerrainSection *terrain() const { return terrain_; }
    TerrainEditsSection *terrainEdits() const { return edits_; }

  protected:
    void showEvent(QShowEvent *event) override;

  private:
    Document &doc_;
    Viewport &view_;
    QComboBox *frame_{};
    QLabel *name_{}, *type_{}, *position_{}, *dimensions_{}, *length_{}, *area_{}, *volume_{};
    QLabel *tag_{}, *color_{}, *properties_{}, *error_{};
    // R095.e2: the origin (or an edge's endpoints) in site coordinates, with a site origin.
    QLabel *site_{};
    QFormLayout *form_{};
    QPushButton *edit_{}, *problems_{};
    QAction *editAction_{};
    CollapsibleSection *info_{};
    ComponentParametersSection *parameters_{};
    ComponentDefinitionSection *definition_{};
    ComponentDriversSection *drivers_{};
    TerrainSection *terrain_{};
    TerrainEditsSection *edits_{};
    std::optional<SelectedEntity> entity_;
    std::optional<EntityMeasures> measured_;
    Document::SaveStamp stamp_;
    void edit();
    void inspectProblems();
};
} // namespace sketchy
