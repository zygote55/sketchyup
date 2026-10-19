#pragma once
#include "app/collapsible_section.hpp"
#include "app/viewport.hpp"
class QFormLayout;
class QFrame;
namespace sketchy {
// R095.e1 (ADR 0168 "Decisions > Interaction design", Entity info, Terrain section): shown
// while a terrain (or anything in one of its tiles) is selected. Name, triangle and tile
// counts, source file and digest, sources by role, elevation range, generator and the flat
// areas left; Terrain settings… and Explode terrain…; and an amber "Changed since generated"
// chip with Regenerate when the mesh digest no longer matches the record.
//
// The Display controls (contour lines, shading) are left to R095.e2, which draws them; see ADR
// 0168, "Implementation notes (R095.e1)".
class TerrainSection : public CollapsibleSection {
  public:
    TerrainSection(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    // Rereads the selected terrain; cached by document, revision, terrain and display units.
    void refresh();
    Id terrain() const { return terrain_; }
    QFrame *changedChip() const { return chip_; }
    QPushButton *regenerateButton() const { return regenerate_; }
    QPushButton *settingsButton() const { return settings_; }
    QPushButton *explodeButton() const { return explode_; }

  private:
    void run(const QString &id, const std::function<void()> &action);
    Document &doc_;
    Viewport &view_;
    Id terrain_{};
    QString cacheKey_;
    QFrame *chip_{};
    QLabel *chipText_{};
    QPushButton *regenerate_{}, *settings_{}, *explode_{};
    QLabel *name_{}, *triangles_{}, *source_{}, *sources_{}, *elevation_{}, *generator_{},
        *flat_{}, *error_{};
};
} // namespace sketchy
