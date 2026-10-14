#pragma once
#include "app/collapsible_section.hpp"
#include "app/sheet_evaluation.hpp"
#include "app/theme.hpp"
#include <QButtonGroup>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QWidget>
namespace sketchy {
class HistoryPanel;
class Viewport;
// Common drawing scales offered by the scale selects ("1:1" … "1:2500").
const std::vector<double> &sheetScaleChoices();
// "1:50" or "50" (the decimal mark follows the locale); nullopt for anything else.
std::optional<double> parseSheetScale(const QString &text);
// "Top", "Front", "Back", "Left", "Right", "Bottom", "Section", "Perspective", "Parallel" or
// "No camera": how a saved scene projects.
QString sceneProjection(const Document &doc, Id scene);
// The "New model view" panel of the Model view tool (design §3): this model, a scene list with
// each scene's projection, the scale (the page's scale by default), the style from the scene,
// the title, the perspective note, and Place · Enter / Cancel · Esc.
class SheetPlacePanel : public QWidget {
    Q_OBJECT
  public:
    explicit SheetPlacePanel(const Document &doc, QWidget *parent = nullptr);
    // Re-reads the scenes; keeps the chosen scene while it exists.
    void refresh(const QString &documentName, Id sheet, double pageScale, bool pageHasScale);
    Id scene() const;
    void setScene(Id scene);
    double scale() const;
    QComboBox *scaleChoice() const { return scale_; }
    QPushButton *placeButton() const { return place_; }
    QPushButton *cancelButton() const { return cancel_; }
    QButtonGroup *scenes() const { return scenes_; }

  signals:
    void placementChanged();
    void placeRequested();
    void cancelRequested();

  private:
    const Document &doc_;
    QComboBox *model_{}, *scale_{};
    QWidget *sceneList_{};
    QButtonGroup *scenes_{};
    QLabel *style_{}, *title_{}, *empty_{};
    QPushButton *place_{}, *cancel_{};
    Id sheet_{};
    std::vector<Id> order_;
    void refreshDetails();
};
// The Tray of the Sheets workspace (ADR 0164 §4, decision 4; R091.e). Collapsible sections:
// - Entity info: the selected view (R091.f: the title, scale and style are editable, each
//   edit one history entry; Edit view, Show scene in model and, for a broken view, Choose
//   scene…); crop and status are read only;
// - References: the source model, the "changed since accepted" banner and Accept all;
// - Pages: a keyboard tree of pages, each expanding to its views and items in paint order
//   with their state. It is the screen-reader alternative to the canvas. F2 renames a page,
//   Alt+Up and Alt+Down move it, Delete deletes it and Enter shows it on the canvas;
// - History: the document's single labelled history.
class SheetsTray : public QWidget {
    Q_OBJECT
  public:
    SheetsTray(Document &doc, Viewport &viewport, DrawingViewRenderCache &cache,
               QWidget *parent = nullptr);
    void setDocumentName(const QString &name) { documentName_ = name; }
    void setTheme(const ThemeColors &colors);
    // Rebuilds what changed for the current page and selected view.
    void refresh(Id currentSheet, Id selectedView);
    QTreeWidget *pages() const { return pages_; }
    HistoryPanel *history() const { return history_; }
    SheetPlacePanel *placePanel() const { return place_; }
    // The Model view tool shows the "New model view" panel in place of the sections.
    void setPlacing(bool placing);
    bool placing() const { return placing_; }
    void setHistoryFocusTarget(QWidget *target);
    // Starts renaming a page in place, as F2 does.
    void editPageName(Id sheet);

  signals:
    void sheetChosen(qulonglong sheet);
    void viewChosen(qulonglong view);
    void pageActivated(qulonglong sheet);
    void renamePage(qulonglong sheet, const QString &name);
    void movePage(qulonglong sheet, int delta);
    void deletePage(qulonglong sheet);
    void acceptAll();
    void renameView(qulonglong view, const QString &name);
    void setViewScale(qulonglong view, double scaleDenominator);
    void setViewStyle(qulonglong view, int content, bool hiddenLines);
    void editViewRequested();
    void showSceneRequested(qulonglong view);
    void chooseSceneRequested(qulonglong view);

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    Document &doc_;
    DrawingViewRenderCache &cache_;
    ThemeColors colors_{themeColors(false)};
    QString documentName_;
    CollapsibleSection *infoSection_{}, *referencesSection_{}, *pagesSection_{}, *historySection_{};
    QLabel *infoEmpty_{}, *infoSource_{}, *infoCrop_{}, *infoStatus_{}, *infoStatusIcon_{};
    QLineEdit *infoTitle_{};
    QComboBox *infoScale_{}, *infoStyle_{};
    QPushButton *infoEdit_{}, *infoShowScene_{}, *infoChooseScene_{};
    QWidget *infoFields_{};
    SheetPlacePanel *place_{};
    std::vector<QWidget *> sections_;
    bool placing_{};
    void commitTitle();
    void commitScale();
    QLabel *referencesModel_{}, *referencesBannerText_{}, *referencesBannerIcon_{};
    QWidget *referencesBanner_{};
    QPushButton *acceptAll_{};
    QTreeWidget *pages_{};
    HistoryPanel *history_{};
    Id sheet_{}, view_{};
    bool syncing_{};
    QString signature_;
    void refreshInfo();
    void refreshReferences();
    void refreshPages();
    QTreeWidgetItem *pageItem(Id sheet) const;
    QTreeWidgetItem *currentPageItem() const;
};
// A 12 px state icon (check, refresh, cross or clock) for the view state, painted at the
// screen's device pixel ratio.
QPixmap sheetStateIcon(DrawingViewState state, bool pending, const ThemeColors &colors,
                       qreal ratio);
} // namespace sketchy
