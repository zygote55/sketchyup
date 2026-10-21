#pragma once
// R096.f2 (UX_DESIGN §15.6, §15.7): the Terrain edits section on the Info tab, after the Terrain
// section, while a terrain is selected.
// - The header counts the changes ("5 changes"). Rows, in the order the changes apply, show a kind
//   glyph, the title ("Pad · Building A"), the key values on a second line ("106.00 m · daylight,
//   cut 1:2, fill 1:2") and the ID, muted; a caption gives the use ("5 of 4,096 changes").
// - Selecting a row outlines its region in the viewport with a label; double-click or Enter frames
//   it.
// - Remove (or Delete) removes the change and replays the others; Edit pad… changes a pad's
//   elevation, grading, slopes and offset; Restamp re-reads its footprint (unavailable, with the
//   reason, when the source body is gone); Reset edits… asks first, Cancel the default. Each is one
//   undoable edit, replayed as a job with progress and Cancel in the status bar.
// - A change a replay skipped stays in the list, marked "⚠ Skipped" with the reason, until it is
//   removed or the replay is undone.
// - The notices of the last change or replay show under the list, each with Show report (the
//   R095.c2 report tree).
#include "app/collapsible_section.hpp"
#include "app/viewport.hpp"
#include <QDialog>
#include <map>
#include <set>
class QDoubleSpinBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
namespace sketchy {
// One row of the list: the text the section shows and reads out for a change.
struct TerrainEditRow {
    std::uint32_t edit{};
    std::string kind;
    QString glyph, title, detail, accessible;
    bool skipped{};
    QString reason; // why it was skipped
};
// The rows of a terrain's edit list, in order. `skips` names the changes a replay skipped, with
// their reasons (stamps and drapes that did not apply are skipped without one).
std::vector<TerrainEditRow> terrainEditRows(const Document &doc, Id terrain,
                                            const std::map<std::uint32_t, QString> &skips = {});

class TerrainEditsSection : public CollapsibleSection {
    Q_OBJECT
  public:
    TerrainEditsSection(Document &doc, Viewport &view, QWidget *parent = nullptr);
    void refresh();
    Id terrain() const { return terrain_; }
    QListWidget *list() const { return list_; }
    QLabel *caption() const { return caption_; }
    QPushButton *editPadButton() const { return editPad_; }
    QPushButton *restampButton() const { return restamp_; }
    QPushButton *removeButton() const { return remove_; }
    QPushButton *resetButton() const { return reset_; }
    QFrame *notice() const { return notice_; }
    QLabel *noticeText() const { return noticeText_; }
    QPushButton *showReportButton() const { return showReport_; }
    // The selected change (0: none).
    std::uint32_t selectedEdit() const;
    // Actions, as the buttons run them (tests call them directly).
    void removeSelected();
    void restampSelected();
    void resetEdits();
    void frameSelected();

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    void selectionChanged(bool announce);
    void updateButtons();
    void fitList();
    void startReplay(const QJsonObject &command, const QString &title);
    Document &doc_;
    Viewport &view_;
    Id terrain_{};
    QString cacheKey_;
    std::vector<TerrainEditRow> rows_;
    QListWidget *list_{};
    QLabel *caption_{}, *noticeText_{};
    QPushButton *editPad_{}, *restamp_{}, *remove_{}, *reset_{}, *showReport_{};
    QFrame *notice_{};
    bool filling_{};
};

// Edit pad…: the pad's elevation, grading (daylight or fixed width), slopes or width and its
// offset below the source footprint (when the footprint has a height). OK runs terrain.update_edit.
class TerrainEditPadDialog final : public QDialog {
  public:
    TerrainEditPadDialog(const Document &doc, Id terrain, std::uint32_t edit, QWidget *parent);
    // The terrain.update_edit command the form stands for (empty when nothing changed).
    QJsonObject command() const;
    QString problem() const { return problem_; }
    QLineEdit *elevationField() const { return elevation_; }
    QComboBox *modeField() const { return mode_; }
    QLineEdit *cutField() const { return cut_; }
    QLineEdit *fillField() const { return fill_; }
    QLineEdit *distanceField() const { return distance_; }
    QLineEdit *widthField() const { return width_; }
    QLineEdit *offsetField() const { return offset_; }
    QPushButton *okButton() const { return ok_; }

  private:
    void changed(QLineEdit *source);
    const Document &doc_;
    Id terrain_{};
    std::uint32_t edit_{};
    std::optional<double> footprintZ_;
    double elevationModel_{};
    QString problem_;
    QLineEdit *elevation_{}, *cut_{}, *fill_{}, *distance_{}, *width_{}, *offset_{};
    QComboBox *mode_{};
    QLabel *status_{};
    // The fields as filled: an untouched field keeps its exact value (the text is rounded).
    std::map<QLineEdit *, QString> initial_;
    std::set<QLineEdit *> userEdited_; // the fields the user typed in (textEdited)
    int initialMode_{};
    int precision_{2};
    QPushButton *ok_{};
};
} // namespace sketchy
