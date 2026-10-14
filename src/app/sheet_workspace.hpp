#pragma once
#include "app/sheet_canvas.hpp"
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTabBar>
#include <QWidget>
namespace sketchy {
// The Sheets page of the workspace stack (ADR 0164 §7, R091.e): the breadcrumb and paper
// line with Snap to grid and Fit page, the page canvas, and the page tabs with "+ Page".
class SheetWorkspace : public QWidget {
    Q_OBJECT
  public:
    SheetWorkspace(const Document &doc, DrawingViewRenderCache &cache, QWidget *parent = nullptr);
    SheetCanvas *canvas() const { return canvas_; }
    QTabBar *pageTabs() const { return tabs_; }
    QPushButton *addPageButton() const { return addPage_; }
    QPushButton *fitPageButton() const { return fit_; }
    QCheckBox *gridToggle() const { return grid_; }
    QLabel *breadcrumb() const { return breadcrumb_; }
    QLabel *paperLine() const { return paper_; }
    // Re-reads the pages; keeps the current page, or its nearest neighbour when it is gone.
    void refresh();
    Id currentSheet() const { return canvas_->sheet(); }
    void setCurrentSheet(Id sheet);
    void setTheme(const ThemeColors &colors);
    // The current bindings shown on Fit page and + Page.
    void setShortcutHints(const QKeySequence &fit, const QKeySequence &addPage);

  signals:
    void currentSheetChanged(qulonglong sheet);
    void addPageRequested();
    void fitRequested();
    void pagesRequested();

  private:
    const Document &doc_;
    SheetCanvas *canvas_{};
    QTabBar *tabs_{};
    QPushButton *addPage_{}, *fit_{};
    QCheckBox *grid_{};
    QLabel *breadcrumb_{}, *paper_{};
    std::vector<Id> order_;
    int lastPosition_{};
    bool syncing_{};
    ThemeColors colors_{themeColors(false)};
    void refreshLabels();
};
} // namespace sketchy
