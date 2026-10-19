#pragma once
// R095.e1 (ADR 0168 "Decisions > Interaction design"): the desktop terrain actions behind the
// Entity info Terrain section, the tool refusal's Explode link and the viewport context menu.
// Each action runs its command through the batch API (the same core path as the command):
//   Terrain settings…   terrain.set_parameters, one "Terrain settings" edit, after a dry run
//                       (runTerrainRegenerate) whose report the dialog shows;
//   Regenerate terrain  terrain.regenerate, one "Regenerate terrain" edit, confirmed first when
//                       paint on the tiles would be dropped;
//   Explode terrain…    terrain.explode, after a confirmation showing the effect on the
//                       modelling and terrain budgets (Cancel is the default);
//   Create terrain from selection
//                       terrain.create_from_edges on the selected edges, one "Create terrain"
//                       edit; a refusal or notices show the bounded report. R095.h: when the
//                       selection has a closed loop or spans more than one tag, a roles step
//                       (TerrainSelectionRolesDialog) gives each tag a role first.
#include "app/settled_dialog.hpp"
#include "core/selection.hpp"
#include "core/terrain.hpp"
#include "automation/terrain_commands.hpp"
#include "io/terrain_import.hpp"
#include <QDialog>
#include <QJsonObject>
#include <QThreadPool>
#include <QTimer>
#include <memory>
#include <optional>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QPushButton;
class QTreeWidget;
namespace sketchy {
class TerrainRoleTable;
class Viewport;

// The terrain a body belongs to: the body when it is a terrain, its terrain when it is a tile,
// otherwise 0.
Id terrainOfBody(const Document &doc, Id body);
// The terrain the selection names: one entity, a terrain or anything in one of its tiles.
Id selectedTerrain(const Document &doc, const Selection &selection);
// The selected edges (curves select their edges) as terrain.create_from_edges references.
QJsonArray selectedTerrainEdges(const Selection &selection);
// Flat triangles left in the mesh: three contour vertices at one elevation (within 1e-9 m), the
// triangulation's own definition, with their planar area in square metres.
struct TerrainFlatArea {
    std::size_t triangles{};
    double area{};
};
TerrainFlatArea terrainFlatArea(const Document &doc, Id terrain);
// The status-bar text of a tool that refuses a terrain tile.
QString terrainToolRefusal(const QString &tool);

// A bounded terrain report after an action: a banner (refused: nothing was changed; otherwise
// done, with notices), the grouped report, Copy report and Close (the default).
class TerrainReportDialog final : public Settled<QDialog> {
  public:
    TerrainReportDialog(const QString &title, const QString &banner, bool refused,
                        const QJsonObject &report, QWidget *parent);
    QTreeWidget *reportList() const { return list_; }
    QString reportText() const;

  private:
    QString title_;
    QJsonObject report_;
    QTreeWidget *list_{};
};

// Explode terrain… confirmation. `faceLimit` replaces the modelling face limit (tests only).
class TerrainExplodeDialog final : public Settled<QDialog> {
  public:
    TerrainExplodeDialog(const Document &doc, Id terrain, QWidget *parent,
                         std::optional<std::size_t> faceLimit = {});
    const TerrainExplodeBudget &budget() const { return budget_; }
    bool canExplode() const { return budget_.fits(); }

  private:
    TerrainExplodeBudget budget_;
};

// Regenerate's confirmation when paint on the tiles would be dropped (R095.c2 paint notices).
class TerrainRegenerateDialog final : public Settled<QDialog> {
  public:
    TerrainRegenerateDialog(const TerrainPaint &paint, QWidget *parent);
};

struct TerrainSettingsDryRun;
// Terrain settings…: boundary mode, maximum edge length, flat-triangle flip and Steiner fill,
// with a dry run of the regeneration (on a worker, debounced, stale results dropped) and its
// report. Apply is enabled for a dry run of the current choices that would change the terrain.
class TerrainSettingsDialog final : public Settled<QDialog> {
  public:
    TerrainSettingsDialog(const Document &doc, Id terrain, QWidget *parent);
    ~TerrainSettingsDialog() override;
    // The terrain.set_parameters command the choices stand for.
    QJsonObject command() const;
    bool checking() const;
    bool canApply() const;
    // The latest dry run's report (empty before the first one or for an invalid choice).
    QJsonObject report() const;
    QString problem() const { return problem_; }
    QTreeWidget *reportList() const { return list_; }

  private:
    void changed(int delayMs);
    void start();
    void finished(std::uint64_t generation, std::shared_ptr<TerrainSettingsDryRun> result);
    void refresh();
    TerrainParameters chosen() const;

    const Document &doc_;
    Id terrain_{};
    std::shared_ptr<const TerrainRecord> record_;
    TerrainPaint paint_;
    std::size_t otherTriangles_{};
    QThreadPool pool_;
    QTimer rerun_;
    bool running_{};
    std::uint64_t generation_{};
    std::shared_ptr<TerrainSettingsDryRun> dryRun_;
    QString problem_;
    QComboBox *boundary_{};
    QDoubleSpinBox *maxEdge_{};
    QCheckBox *flip_{}, *steiner_{};
    QLabel *status_{};
    QTreeWidget *list_{};
    QPushButton *copy_{}, *apply_{};
};

struct TerrainSelectionDryRun;
// R095.h: Create terrain from selection's roles step. Each tag in the selection is a row of the
// shared role table (Automatic, Contour, Breakline, Boundary, Hole, Ignore; Automatic takes the
// role from Z as the one-click path does). The roles are checked as the command checks them:
// terrainTagRoleProblems names a Boundary that is not one closed loop, a Hole with open chains
// or more than one Boundary tag before any work, then the command's own dry run
// (parseTerrainEdgeRequest, runTerrainEdgeRequest on a worker, debounced, stale results
// dropped) gives its refusals (nested holes, crossings) and notices. Create, the default, is
// enabled for a dry run of the current roles that would create a terrain.
class TerrainSelectionRolesDialog final : public Settled<QDialog> {
  public:
    TerrainSelectionRolesDialog(const Document &doc, const std::vector<TerrainEdgeRef> &edges,
                                QWidget *parent);
    ~TerrainSelectionRolesDialog() override;
    // The terrain.create_from_edges command the roles stand for.
    QJsonObject command() const;
    bool checking() const;
    bool canCreate() const;
    // The problems named before any work (empty when none), and the status line's text.
    QString problem() const { return problem_; }
    QString status() const;
    // The latest dry run's report (empty before the first one or with a problem).
    QJsonObject report() const;
    const std::vector<TerrainTagEdges> &tags() const { return tags_; }
    TerrainRoleTable *roleTable() const { return table_; }
    QTreeWidget *reportList() const { return list_; }

  private:
    void changed(int delayMs);
    void start();
    void finished(std::uint64_t generation, std::shared_ptr<TerrainSelectionDryRun> result);
    void refresh();
    std::map<Id, TerrainImportRole> chosen() const;

    const Document &doc_;
    std::vector<TerrainTagEdges> tags_;
    QJsonObject command_;
    std::optional<TerrainEdgeRequest> request_;
    QThreadPool pool_;
    QTimer rerun_;
    bool running_{};
    std::uint64_t generation_{};
    std::shared_ptr<TerrainSelectionDryRun> dryRun_;
    QString problem_;
    TerrainRoleTable *table_{};
    QLabel *status_{};
    QTreeWidget *list_{};
    QPushButton *create_{};
};

// The actions. Each returns true when the document changed; refusals are shown to the user
// (a report dialog or a message box) and change nothing.
bool explodeTerrainInteractive(QWidget *parent, Document &doc, Viewport &view, Id terrain);
bool terrainSettingsInteractive(QWidget *parent, Document &doc, Viewport &view, Id terrain);
bool regenerateTerrainInteractive(QWidget *parent, Document &doc, Viewport &view, Id terrain);
bool createTerrainFromSelectionInteractive(QWidget *parent, Document &doc, Viewport &view);
} // namespace sketchy
