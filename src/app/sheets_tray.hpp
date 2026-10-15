#pragma once
#include "app/collapsible_section.hpp"
#include "app/sheet_evaluation.hpp"
#include "app/theme.hpp"
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QWidget>
namespace sketchy {
class HistoryPanel;
class Viewport;
// The Tray of the Sheets workspace (ADR 0164 §4, decision 4; R091.e). Collapsible sections:
// - Entity info: the selected view, read only (title, source, scale, style, crop, status);
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

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    Document &doc_;
    DrawingViewRenderCache &cache_;
    ThemeColors colors_{themeColors(false)};
    QString documentName_;
    CollapsibleSection *infoSection_{}, *referencesSection_{}, *pagesSection_{}, *historySection_{};
    QLabel *infoEmpty_{}, *infoTitle_{}, *infoSource_{}, *infoScale_{}, *infoStyle_{}, *infoCrop_{},
        *infoStatus_{}, *infoStatusIcon_{};
    QWidget *infoFields_{};
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
