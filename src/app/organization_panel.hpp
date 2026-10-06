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
class OrganizationPanel : public QWidget {
  public:
    OrganizationPanel(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    QTreeWidget *outliner() const { return outliner_; }
    void refresh();
    void showMaterials();
    void showHistory();
    void showStyles();

  private:
    Document &doc_;
    Viewport &view_;
    QTreeWidget *outliner_{}, *tags_{};
    QLineEdit *search_{};
    QLabel *error_{};
    QTabWidget *tabs_{};
    EntityInfoPanel *info_{};
    MaterialsPanel *materials_{};
    HistoryPanel *history_{};
    StylesPanel *styles_{};
    bool syncing_{};
    std::set<Id> knownBodies_, knownTags_;
    Document::SaveStamp session_;
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
