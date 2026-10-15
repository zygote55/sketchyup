#include "app/sheet_workspace.hpp"
#include "app/context_label.hpp"
#include "app/keyboard_navigation.hpp"
#include "app/sheet_text.hpp"
#include "core/drawing_sheets.hpp"
#include <QHBoxLayout>
#include <QPainter>
#include <QScopedValueRollback>
#include <QSettings>
#include <QVBoxLayout>
namespace sketchy {
namespace {
// A label that elides instead of widening the bar at narrow window sizes.
class ElidingLabel final : public QLabel {
  public:
    using QLabel::QLabel;
    QSize minimumSizeHint() const override { return {40, QLabel::minimumSizeHint().height()}; }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(contentsRect(), Qt::AlignVCenter | Qt::AlignLeft,
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};
} // namespace
SheetWorkspace::SheetWorkspace(const Document &doc, DrawingViewRenderCache &cache, QWidget *parent)
    : QWidget(parent), doc_(doc) {
    setObjectName("sheetWorkspace");
    setAccessibleName("Sheets");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *top = new QWidget;
    top->setObjectName("sheetTopBar");
    top->setAttribute(Qt::WA_StyledBackground);
    auto *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(12, 4, 12, 4);
    topLayout->setSpacing(16);
    breadcrumb_ = new QLabel;
    breadcrumb_->setObjectName("sheetBreadcrumb");
    breadcrumb_->setAccessibleName("Sheets breadcrumb");
    breadcrumb_->setTextFormat(Qt::RichText);
    breadcrumb_->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                         Qt::LinksAccessibleByKeyboard);
    breadcrumb_->setFocusPolicy(Qt::StrongFocus);
    breadcrumb_->setToolTip("Sheets: open the Pages list");
    connect(breadcrumb_, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link == "sheet")
            emit leaveEditViewRequested();
        else
            emit pagesRequested();
    });
    backtabOutOfLinks(breadcrumb_);
    topLayout->addWidget(breadcrumb_);
    paper_ = new ElidingLabel;
    paper_->setObjectName("sheetPaperLine");
    paper_->setTextFormat(Qt::PlainText);
    paper_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    topLayout->addWidget(paper_, 1);
    grid_ = new QCheckBox("Snap to grid 5 mm");
    grid_->setObjectName("sheetGridToggle");
    grid_->setChecked(QSettings("SketchyUp", "SketchyUp").value("sheets/grid", true).toBool());
    topLayout->addWidget(grid_);
    fit_ = new QPushButton("Fit page");
    fit_->setObjectName("sheetFitPage");
    fit_->setAutoDefault(false);
    connect(fit_, &QPushButton::clicked, this, [this] { emit fitRequested(); });
    topLayout->addWidget(fit_);
    layout->addWidget(top);
    canvas_ = new SheetCanvas(doc, cache);
    canvas_->setGrid(grid_->isChecked());
    connect(grid_, &QCheckBox::toggled, this, [this](bool on) {
        canvas_->setGrid(on);
        QSettings("SketchyUp", "SketchyUp").setValue("sheets/grid", on);
    });
    layout->addWidget(canvas_, 1);
    auto *bar = new QWidget;
    bar->setObjectName("sheetPageBar");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(12, 4, 12, 4);
    barLayout->setSpacing(4);
    tabs_ = new QTabBar;
    tabs_->setObjectName("sheetPageTabs");
    tabs_->setAccessibleName("Pages");
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);
    tabs_->setUsesScrollButtons(true);
    tabs_->setElideMode(Qt::ElideRight);
    tabs_->setFocusPolicy(Qt::TabFocus);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (syncing_ || index < 0 || size_t(index) >= order_.size())
            return;
        setCurrentSheet(order_[size_t(index)]);
    });
    barLayout->addWidget(tabs_, 0);
    addPage_ = new QPushButton("+ Page");
    addPage_->setObjectName("sheetAddPage");
    addPage_->setAutoDefault(false);
    connect(addPage_, &QPushButton::clicked, this, [this] { emit addPageRequested(); });
    barLayout->addWidget(addPage_);
    barLayout->addStretch(1);
    layout->addWidget(bar);
    refresh();
}
void SheetWorkspace::setShortcutHints(const QKeySequence &fit, const QKeySequence &addPage) {
    const auto fitText = fit.toString(QKeySequence::NativeText);
    fit_->setText(fitText.isEmpty() ? "Fit page" : "Fit page  " + fitText);
    fit_->setAccessibleName("Fit page");
    fit_->setToolTip(fitText.isEmpty() ? "Fit page" : "Fit page (" + fitText + ")");
    const auto addText = addPage.toString(QKeySequence::NativeText);
    addPage_->setAccessibleName(addText.isEmpty() ? "Add page" : "Add page (" + addText + ")");
    addPage_->setToolTip(addPage_->accessibleName());
}
void SheetWorkspace::setTheme(const ThemeColors &colors) {
    colors_ = colors;
    canvas_->setTheme(colors);
    setContextLabelColor(breadcrumb_, colors.dark ? colors.accent : QColor("#3f6043"));
    auto palette = paper_->palette();
    palette.setColor(QPalette::WindowText, colors.muted);
    paper_->setPalette(palette);
    refreshLabels();
}
void SheetWorkspace::setCurrentSheet(Id sheet) {
    if (!doc_.drawingSheets().contains(sheet))
        sheet = 0;
    const bool changed = sheet != canvas_->sheet();
    if (sheet)
        lastPosition_ = int(doc_.drawingSheets().at(sheet)->position);
    canvas_->setSheet(sheet);
    {
        QScopedValueRollback guard(syncing_, true);
        const auto at = std::find(order_.begin(), order_.end(), sheet);
        if (at != order_.end())
            tabs_->setCurrentIndex(int(at - order_.begin()));
    }
    refreshLabels();
    if (changed)
        emit currentSheetChanged(sheet);
}
void SheetWorkspace::refresh() {
    const auto order = orderedDrawingSheets(doc_);
    {
        QScopedValueRollback guard(syncing_, true);
        if (order != order_ || tabs_->count() != int(order.size())) {
            while (tabs_->count())
                tabs_->removeTab(0);
            for (const auto id : order)
                tabs_->addTab(sheetTitle(*doc_.drawingSheets().at(id)));
        } else
            for (size_t i = 0; i < order.size(); ++i)
                tabs_->setTabText(int(i), sheetTitle(*doc_.drawingSheets().at(order[i])));
        for (size_t i = 0; i < order.size(); ++i)
            tabs_->setTabToolTip(int(i), sheetTitle(*doc_.drawingSheets().at(order[i])));
        order_ = order;
    }
    tabs_->setVisible(!order.empty());
    auto current = canvas_->sheet();
    if (!doc_.drawingSheets().contains(current))
        // The page was deleted: show its neighbour at the same position.
        current =
            order.empty() ? 0 : order[size_t(std::clamp(lastPosition_, 0, int(order.size()) - 1))];
    if (current != canvas_->sheet() || !current)
        setCurrentSheet(current);
    else {
        QScopedValueRollback guard(syncing_, true);
        const auto at = std::find(order_.begin(), order_.end(), current);
        tabs_->setCurrentIndex(int(at - order_.begin()));
        lastPosition_ = int(at - order_.begin());
    }
    refreshLabels();
}
void SheetWorkspace::setEditView(Id view) {
    editView_ = view;
    refreshLabels();
}
void SheetWorkspace::refreshLabels() {
    const auto found = doc_.drawingSheets().find(canvas_->sheet());
    QString markup = "<a href=\"pages\">Sheets</a>";
    const auto views = canvas_->views();
    const auto at = std::find(views.begin(), views.end(), editView_);
    if (found != doc_.drawingSheets().end() && editView_ && at != views.end()) {
        // Edit-view: the page crumb leaves the context, as a group's parent crumb does.
        const auto &sheet = *found->second;
        const auto page = QString::fromStdString(sheet.number.empty() ? sheet.name : sheet.number);
        markup += " &rsaquo; <a href=\"sheet\">" + page.toHtmlEscaped() + "</a> &rsaquo; <b>View " +
                  QString::number(at - views.begin() + 1) + " · " +
                  QString::fromStdString(doc_.drawingViews().at(editView_)->name).toHtmlEscaped() +
                  "</b>";
        paper_->setText(sheetPaperLine(sheet, doc_.drawingSheets().size()));
    } else if (found != doc_.drawingSheets().end()) {
        markup += " &rsaquo; <b>" + sheetTitle(*found->second).toHtmlEscaped() + "</b>";
        paper_->setText(sheetPaperLine(*found->second, doc_.drawingSheets().size()));
    } else
        paper_->setText(doc_.drawingSheets().empty() ? "No pages" : QString{});
    paper_->setAccessibleName(paper_->text());
    paper_->setToolTip(paper_->text());
    setContextLabelText(breadcrumb_, markup);
}
} // namespace sketchy
