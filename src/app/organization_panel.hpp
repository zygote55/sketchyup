#pragma once
#include "app/viewport.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <QTreeWidget>
namespace sketchy {
class EntityInfoPanel;
class MaterialsPanel;
class HistoryPanel;
class StylesPanel;
class ScenesPanel;
class SectionsPanel;
class AnnotationsPanel;
class TextPanel;
class SolarPanel;
class ReferenceImagesPanel;
class ComponentsTab;
class QuantitiesPanel;
class OrganizationPanel : public QWidget {
  public:
    OrganizationPanel(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    QTreeWidget *outliner() const { return outliner_; }
    void refresh();
    void showOutliner();
    void showTags();
    void showInfo();
    void showMaterials();
    void showHistory();
    // R101.e2 (UX_DESIGN §19.1): the Quantities section, between Materials and History.
    void showQuantities();
    // The model's file, for the Quantities section's schedules and export names ("" when unsaved).
    void setModelPath(std::function<QString()> path);
    void showStyles();
    void showScenes();
    void showSections();
    void showAnnotations();
    void showText();
    void showSolar();
    void showReferenceImages();
    // ADR 0166 §6.3 (R094.f): the Components tab, index 12.
    void showComponents();

  private:
    Document &doc_;
    Viewport &view_;
    QTreeWidget *outliner_{}, *tags_{};
    QLineEdit *search_{};
    QLabel *error_{};
    QTabWidget *tabs_{};
    EntityInfoPanel *info_{};
    MaterialsPanel *materials_{};
    QuantitiesPanel *quantities_{};
    HistoryPanel *history_{};
    StylesPanel *styles_{};
    ScenesPanel *scenes_{};
    SectionsPanel *sections_{};
    AnnotationsPanel *annotations_{};
    TextPanel *text_{};
    SolarPanel *solar_{};
    ReferenceImagesPanel *references_{};
    ComponentsTab *components_{};
    bool syncing_{};
    std::set<Id> knownBodies_, knownTags_;
    Document::SaveStamp session_;
    // The rows a tree shows, as its last rebuild built them. refresh() rebuilds a tree only when
    // these change, so an edit that changes no displayed field keeps every QTreeWidgetItem (D-048).
    struct EntityRow {
        Id id{}, parent{};
        std::string name, tag; // column 0, and the tooltip's tag name ("" when untagged)
        std::string_view kind; // the tooltip's kind, a string literal
        bool hidden{}, locked{}, context{};
        bool operator==(const EntityRow &) const = default;
    };
    struct TagRow {
        Id id{}, parent{};
        std::string name;
        bool folder{}, visible{}, effective{};
        bool operator==(const TagRow &) const = default;
    };
    std::vector<EntityRow> entityRows() const;
    std::vector<TagRow> tagRows() const;
    bool selectionShown() const;
    std::vector<EntityRow> shownEntities_;
    std::vector<TagRow> shownTags_;
    bool attempt(const std::function<void()> &operation);
    void filter();
    void enter();
    std::set<Id> selectedBodies() const;
    void rename(bool tag);
    void moveDialog(bool tag);
    void move(const std::set<Id> &ids, Id parent, bool tag);
    void state(bool lock);
    void createTag(bool folder);
    void assignTag();
    void deleteTag();
};
} // namespace sketchy
