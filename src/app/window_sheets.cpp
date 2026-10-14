#include "app/interface_preferences.hpp"
#include "app/keyboard_navigation.hpp"
#include "app/sheet_evaluation.hpp"
#include "app/sheet_text.hpp"
#include "app/sheet_workspace.hpp"
#include "app/sheets_tray.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/drawing_sheets.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIconEngine>
#include <QJsonArray>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
namespace sketchy {
namespace {
// Original 1.5 px line icons for the sheet rail (UX_DESIGN §7), drawn in an 18-unit box in the
// current ink so they stay sharp at fractional scales and follow the theme.
class LineIconEngine final : public QIconEngine {
  public:
    LineIconEngine(QPainterPath path, QColor ink, QColor disabled)
        : path_(std::move(path)), ink_(std::move(ink)), disabled_(std::move(disabled)) {}
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / 18., rect.height() / 18.);
        painter->setPen(QPen(mode == QIcon::Disabled ? disabled_ : ink_, 1.5, Qt::SolidLine,
                             Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path_);
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override {
        QPixmap result(size * scale);
        result.fill(Qt::transparent);
        {
            QPainter painter(&result);
            paint(&painter, QRect(QPoint(), size * scale), mode, state);
        }
        result.setDevicePixelRatio(scale);
        return result;
    }
    QIconEngine *clone() const override { return new LineIconEngine(*this); }

  private:
    QPainterPath path_;
    QColor ink_, disabled_;
};
QPainterPath selectIcon() {
    QPainterPath path;
    path.moveTo(4, 2);
    for (const auto &point : {QPointF(13, 8), QPointF(9, 9), QPointF(11, 14), QPointF(9, 15),
                              QPointF(7, 10), QPointF(4, 13)})
        path.lineTo(point);
    path.closeSubpath();
    return path;
}
QPainterPath panIcon() {
    QPainterPath path;
    path.moveTo(6, 9);
    path.lineTo(6, 4);
    path.arcTo(QRectF(6, 3, 2, 2), 180, -180);
    path.lineTo(8, 8);
    path.moveTo(8, 8);
    path.lineTo(8, 3);
    path.arcTo(QRectF(8, 2, 2, 2), 180, -180);
    path.lineTo(10, 8);
    path.moveTo(10, 8);
    path.lineTo(10, 4);
    path.arcTo(QRectF(10, 3, 2, 2), 180, -180);
    path.lineTo(12, 10);
    path.cubicTo(12, 13, 10, 15, 7, 15);
    path.cubicTo(4, 15, 3, 13, 2, 11);
    path.lineTo(1, 9);
    path.cubicTo(.7, 8, 2.2, 7, 3, 8);
    path.lineTo(4, 10);
    return path;
}
QPainterPath zoomIcon() {
    QPainterPath path;
    path.addEllipse(QPointF(8, 8), 5, 5);
    path.moveTo(12, 12);
    path.lineTo(16, 16);
    return path;
}
const char *instruction(SheetCanvas::Tool tool) {
    switch (tool) {
    case SheetCanvas::Tool::Pan:
        return "Drag to pan · Arrows pan · Esc returns to Select";
    case SheetCanvas::Tool::Zoom:
        return "Drag up to zoom in, down to zoom out · Type a zoom such as 150% · Esc returns to "
               "Select";
    case SheetCanvas::Tool::Select:
        break;
    }
    return "Click a view to select it · Wheel zooms · Middle drag pans · Shift+Z fits the page";
}
} // namespace
bool Window::sheetsWorkspaceEnabled() {
    return qEnvironmentVariable("SKETCHYUP_SHEETS_WORKSPACE") == QLatin1String("1");
}
ShortcutScope Window::actionScope(const QAction *action) const {
    return ShortcutScope(action->property("shortcutScope").toInt());
}
void Window::setupSheets(QWidget *header, QMenu *view) {
    auto *canvas = sheets_->canvas();
    // Model | Sheets: two checkable buttons in an exclusive group, backed by actions.
    workspaceToggle_ = new QWidget;
    workspaceToggle_->setObjectName("workspaceToggle");
    workspaceToggle_->setAccessibleName("Workspace");
    auto *toggleLayout = new QHBoxLayout(workspaceToggle_);
    toggleLayout->setContentsMargins(0, 0, 0, 0);
    toggleLayout->setSpacing(0);
    auto *buttons = new QButtonGroup(workspaceToggle_);
    auto *workspaces = new QActionGroup(this);
    view->addSeparator();
    for (const bool sheets : {false, true}) {
        auto *entry = action(sheets ? "view.workspace.sheets" : "view.workspace.model",
                             sheets ? "Sheets workspace" : "Model workspace", {}, [this, sheets] {
                                 setWorkspace(sheets ? Workspace::Sheets : Workspace::Model);
                             });
        entry->setCheckable(true);
        entry->setChecked(!sheets);
        workspaces->addAction(entry);
        view->addAction(entry);
        auto *button = new QPushButton(sheets ? "Sheets" : "Model");
        button->setObjectName(sheets ? "workspaceSheets" : "workspaceModel");
        button->setCheckable(true);
        button->setChecked(!sheets);
        button->setAutoDefault(false);
        button->setAccessibleName(sheets ? "Sheets workspace" : "Model workspace");
        buttons->addButton(button);
        toggleLayout->addWidget(button);
        connect(button, &QPushButton::clicked, entry, &QAction::trigger);
        connect(entry, &QAction::toggled, button, &QPushButton::setChecked);
    }
    view->addAction(action(
        "view.workspace.toggle", "Switch Model and Sheets", QKeySequence("Ctrl+Alt+S"),
        [this] {
            setWorkspace(workspace_ == Workspace::Model ? Workspace::Sheets : Workspace::Model);
        },
        ShortcutScope::Global));
    if (auto *layout = qobject_cast<QHBoxLayout *>(header->layout()))
        layout->insertWidget(1, workspaceToggle_);

    // The sheet rail: Select, Pan and Zoom (R091.e); other sheet tools arrive in R091.f/g.
    sheetRail_ = new QToolBar;
    sheetRail_->setObjectName("sheetRail");
    sheetRail_->setAccessibleName("Sheet tools");
    sheetRail_->setOrientation(Qt::Vertical);
    sheetRail_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sheetRail_->setIconSize(QSize(18, 18));
    railStack_->addWidget(sheetRail_);
    auto *page = menuBar()->addMenu("&Page");
    page->setObjectName("pageMenu");
    page->menuAction()->setVisible(false);
    auto *tools = new QActionGroup(this);
    auto addTool = [&](const char *id, const QString &name, const char *key,
                       SheetCanvas::Tool tool) {
        auto *entry = action(
            id, name, QKeySequence(key), [this, tool] { sheetTool(int(tool)); },
            ShortcutScope::Sheets);
        entry->setCheckable(true);
        entry->setProperty("sheetTool", int(tool));
        tools->addAction(entry);
        sheetRail_->addAction(entry);
        page->addAction(entry);
        return entry;
    };
    addTool("sheet.tool.select", "Select", "Space", SheetCanvas::Tool::Select)->setChecked(true);
    sheetRail_->addSeparator();
    addTool("sheet.tool.pan", "Pan", "H", SheetCanvas::Tool::Pan);
    addTool("sheet.tool.zoom", "Zoom", "Z", SheetCanvas::Tool::Zoom);
    installToolbarNavigation(sheetRail_, tools);
    page->addSeparator();
    page->addAction(action(
        "sheet.view.fit", "Fit page", QKeySequence("Shift+Z"),
        [this] { sheets_->canvas()->fitPage(); }, ShortcutScope::Sheets));
    page->addSeparator();
    page->addAction(action(
        "sheet.page.add", "Add page", QKeySequence("Ctrl+Shift+N"), [this] { addSheetPage(); },
        ShortcutScope::Sheets));
    page->addAction(action(
        "sheet.page.previous", "Previous page", QKeySequence("Ctrl+PgUp"),
        [this] { moveSheetPage(0, -1); }, ShortcutScope::Sheets));
    page->addAction(action(
        "sheet.page.next", "Next page", QKeySequence("Ctrl+PgDown"),
        [this] { moveSheetPage(0, 1); }, ShortcutScope::Sheets));
    page->addAction(action(
        "sheet.page.rename", "Rename page…", {},
        [this] { sheetsTray_->editPageName(sheets_->currentSheet()); }, ShortcutScope::Sheets));
    page->addAction(action(
        "sheet.page.moveEarlier", "Move page earlier", {},
        [this] { moveSheetPage(sheets_->currentSheet(), -1); }, ShortcutScope::Sheets));
    page->addAction(action(
        "sheet.page.moveLater", "Move page later", {},
        [this] { moveSheetPage(sheets_->currentSheet(), 1); }, ShortcutScope::Sheets));
    page->addAction(action(
        "sheet.page.delete", "Delete page", {},
        [this] {
            if (const auto sheet = sheets_->currentSheet())
                sheetCommands({QJsonObject{{"command", "drawing_sheet.delete"},
                                           {"sheet", QString::number(sheet)}}},
                              "Delete page");
        },
        ShortcutScope::Sheets));
    page->addSeparator();
    page->addAction(action(
        "sheet.view.acceptAll", "Accept all changed views", {},
        [this] {
            size_t changed = 0;
            for (const auto &[id, record] : doc_.drawingViews()) {
                (void)record;
                if (const auto status = sheetCache_->status(id);
                    status && status->state == DrawingViewState::Stale)
                    ++changed;
            }
            sheetCommands(
                {QJsonObject{{"command", "drawing_view.accept"}, {"all", true}}},
                QString("Accept %1 %2").arg(changed).arg(changed == 1 ? "view" : "views"));
        },
        ShortcutScope::Sheets));
    auto *grid = action(
        "sheet.grid", "Snap to grid 5 mm", {},
        [this] {
            sheets_->gridToggle()->setChecked(findChild<QAction *>("sheet.grid")->isChecked());
        },
        ShortcutScope::Sheets);
    grid->setCheckable(true);
    grid->setChecked(sheets_->gridToggle()->isChecked());
    connect(sheets_->gridToggle(), &QCheckBox::toggled, grid, &QAction::setChecked);
    page->addAction(grid);

    // Canvas, page tabs and Tray wiring.
    sheets_->canvas()->setRasterRenderer(
        [this](const DrawingViewEvaluationInput &input, QSize pixels) {
            return viewport_->renderDrawingViewRaster(input, pixels);
        });
    connect(sheetCache_.get(), &DrawingViewRenderCache::evaluated, this, [this](qulonglong) {
        sheets_->canvas()->contentChanged();
        sheetsTray_->refresh(sheets_->currentSheet(), sheets_->canvas()->selectedView());
        syncSheets();
    });
    connect(sheets_, &SheetWorkspace::currentSheetChanged, this, [this](qulonglong) {
        if (workspace_ == Workspace::Sheets)
            sheetCache_->refresh(sheets_->currentSheet());
        sheetsTray_->refresh(sheets_->currentSheet(), sheets_->canvas()->selectedView());
        syncSheets();
    });
    connect(sheets_, &SheetWorkspace::addPageRequested, this,
            [this] { findChild<QAction *>("sheet.page.add")->trigger(); });
    connect(sheets_, &SheetWorkspace::fitRequested, this,
            [this] { findChild<QAction *>("sheet.view.fit")->trigger(); });
    connect(sheets_, &SheetWorkspace::pagesRequested, this,
            [this] { sheetsTray_->pages()->setFocus(Qt::ShortcutFocusReason); });
    connect(canvas, &SheetCanvas::selectionChanged, this, [this](qulonglong) {
        sheetsTray_->refresh(sheets_->currentSheet(), sheets_->canvas()->selectedView());
    });
    connect(canvas, &SheetCanvas::escapeToSelect, this,
            [this] { findChild<QAction *>("sheet.tool.select")->trigger(); });
    connect(canvas, &SheetCanvas::zoomChanged, this, [this](double zoom) {
        if (workspace_ == Workspace::Sheets &&
            sheets_->canvas()->tool() == SheetCanvas::Tool::Zoom && !measurements_->hasFocus())
            measurements_->setText(QString::number(qRound(zoom * 100)) + "%");
    });
    connect(canvas, &SheetCanvas::measurementsRequested, this, [this](const QString &text) {
        measurements_->setFocus();
        measurements_->setText(text);
        measurements_->setCursorPosition(int(text.size()));
    });
    sheetsTray_->setHistoryFocusTarget(canvas);
    connect(sheetsTray_, &SheetsTray::sheetChosen, this, [this](qulonglong sheet) {
        sheets_->setCurrentSheet(sheet);
        sheets_->canvas()->setSelectedView(0);
    });
    connect(sheetsTray_, &SheetsTray::viewChosen, this, [this](qulonglong view) {
        const auto found = doc_.drawingViews().find(view);
        if (found == doc_.drawingViews().end())
            return;
        sheets_->setCurrentSheet(found->second->sheet);
        sheets_->canvas()->setSelectedView(view);
    });
    connect(sheetsTray_, &SheetsTray::pageActivated, this, [this](qulonglong sheet) {
        sheets_->setCurrentSheet(sheet);
        sheets_->canvas()->setFocus(Qt::OtherFocusReason);
    });
    connect(sheetsTray_, &SheetsTray::renamePage, this,
            [this](qulonglong sheet, const QString &name) {
                if (name.isEmpty()) {
                    status_->setText("A page name cannot be empty.");
                    syncSheets();
                    return;
                }
                run([&] {
                    sheetCommands({QJsonObject{{"command", "drawing_sheet.rename"},
                                               {"sheet", QString::number(sheet)},
                                               {"name", name}}},
                                  "Rename page");
                });
                sheetsTray_->pages()->setFocus(Qt::OtherFocusReason);
            });
    connect(sheetsTray_, &SheetsTray::movePage, this,
            [this](qulonglong sheet, int delta) { run([&] { moveSheetPage(sheet, delta); }); });
    connect(sheetsTray_, &SheetsTray::deletePage, this, [this](qulonglong sheet) {
        run([&] {
            sheetCommands({QJsonObject{{"command", "drawing_sheet.delete"},
                                       {"sheet", QString::number(sheet)}}},
                          "Delete page");
        });
    });
    connect(sheetsTray_, &SheetsTray::acceptAll, this,
            [this] { findChild<QAction *>("sheet.view.acceptAll")->trigger(); });
    sheetRegions_ = {findChild<QPushButton *>("commandSearch"),
                     sheetRail_,
                     canvas,
                     sheets_->pageTabs(),
                     sheetsTray_,
                     assistant_,
                     measurements_};
}
void Window::themeSheets(const ThemeColors &colors) {
    // The checked workspace button is bold; size both for bold text so neither clips.
    for (auto *button : workspaceToggle_->findChildren<QPushButton *>()) {
        button->ensurePolished();
        auto bold = button->font();
        bold.setWeight(QFont::DemiBold);
        button->setMinimumWidth(QFontMetrics(bold).horizontalAdvance(button->text()) + 32);
    }
    sheets_->setTheme(colors);
    sheetsTray_->setTheme(colors);
    for (const auto &[id, path] :
         std::vector<std::pair<const char *, QPainterPath>>{{"sheet.tool.select", selectIcon()},
                                                            {"sheet.tool.pan", panIcon()},
                                                            {"sheet.tool.zoom", zoomIcon()}})
        findChild<QAction *>(id)->setIcon(
            QIcon(new LineIconEngine(path, colors.ink, colors.muted)));
}
void Window::sheetTool(int value) {
    if (!sheets_)
        return;
    const auto tool = SheetCanvas::Tool(value);
    sheets_->canvas()->setTool(tool);
    for (auto *entry : sheetRail_->actions())
        if (entry->property("sheetTool").isValid() && entry->property("sheetTool").toInt() == value)
            entry->setChecked(true);
    if (workspace_ != Workspace::Sheets)
        return;
    activeTool_->setText(tool == SheetCanvas::Tool::Pan    ? "Pan"
                         : tool == SheetCanvas::Tool::Zoom ? "Zoom"
                                                           : "Select");
    status_->setText(instruction(tool));
    const bool zoom = tool == SheetCanvas::Tool::Zoom;
    measurements_->setEnabled(zoom && !assistantFenced_);
    measurements_->setPlaceholderText(zoom ? "150% or 1.5" : "");
    measurements_->setText(zoom ? QString::number(qRound(sheets_->canvas()->zoom() * 100)) + "%"
                                : QString{});
    measurementError(false);
    sheets_->canvas()->setFocus(Qt::OtherFocusReason);
}
bool Window::sheetMeasurements(const QString &text) {
    QString error;
    if (sheets_->canvas()->measurements(text, error))
        return true;
    status_->setText(error);
    return false;
}
void Window::sheetCommands(const QJsonArray &commands, const QString &label) {
    if (assistant_ && assistant_->uncertain()) {
        status_->setText("Reconcile the assistant outcome before editing this model.");
        return;
    }
    viewport_->cancel();
    executeBatch(doc_, {{"apiVersion", 1},
                        {"documentId", QString::fromStdString(doc_.identity())},
                        {"expectedRevision", QString::number(doc_.revision())},
                        {"commands", commands},
                        {"history", QJsonObject{{"label", label}}}});
    sync();
}
void Window::addSheetPage() {
    DrawingPaper paper;
    if (const auto found = doc_.drawingSheets().find(sheets_->currentSheet());
        found != doc_.drawingSheets().end())
        paper = found->second->paper;
    const auto result = [&] {
        viewport_->cancel();
        auto response = executeBatch(
            doc_,
            {{"apiVersion", 1},
             {"documentId", QString::fromStdString(doc_.identity())},
             {"expectedRevision", QString::number(doc_.revision())},
             {"commands", QJsonArray{QJsonObject{
                              {"command", "drawing_sheet.create"},
                              {"name", "Page " + QString::number(doc_.drawingSheets().size() + 1)},
                              {"paper", QJsonObject{{"widthMm", paper.widthMm},
                                                    {"heightMm", paper.heightMm}}}}}},
             {"history", QJsonObject{{"label", "Add page"}}}});
        return response;
    }();
    const auto created = result["createdDrawingSheets"].toArray();
    sync();
    if (!created.isEmpty())
        sheets_->setCurrentSheet(created.first().toString().toULongLong());
    sheets_->canvas()->setFocus(Qt::OtherFocusReason);
}
void Window::moveSheetPage(Id sheet, int delta) {
    auto order = orderedDrawingSheets(doc_);
    if (!sheet) {
        // Previous or next page: change the current page, not the order.
        const auto at = std::find(order.begin(), order.end(), sheets_->currentSheet());
        if (at == order.end())
            return;
        const auto index = int(at - order.begin()) + delta;
        if (index >= 0 && index < int(order.size()))
            sheets_->setCurrentSheet(order[size_t(index)]);
        return;
    }
    const auto at = std::find(order.begin(), order.end(), sheet);
    if (at == order.end())
        return;
    const auto index = int(at - order.begin());
    const auto target = index + delta;
    if (target < 0 || target >= int(order.size())) {
        status_->setText(delta < 0 ? "This is already the first page."
                                   : "This is already the last page.");
        return;
    }
    std::swap(order[size_t(index)], order[size_t(target)]);
    QJsonArray ids;
    for (const auto id : order)
        ids.append(QString::number(id));
    sheetCommands({QJsonObject{{"command", "drawing_sheet.reorder"}, {"order", ids}}}, "Move page");
}
void Window::focusWorkspace() {
    if (workspace_ == Workspace::Sheets && sheets_)
        sheets_->canvas()->setFocus(Qt::OtherFocusReason);
    else
        viewport_->setFocus();
}
void Window::setWorkspace(Workspace workspace) {
    if (!sheets_)
        return;
    if (workspace == workspace_) {
        focusWorkspace();
        return;
    }
    viewport_->cancel();
    const bool sheets = workspace == Workspace::Sheets;
    if (sheets) {
        modelStatus_ = status_->text();
        modelPlaceholder_ = measurements_->placeholderText();
    }
    workspace_ = workspace;
    workspaceStack_->setCurrentIndex(sheets ? 1 : 0);
    railStack_->setCurrentIndex(sheets ? 1 : 0);
    railStack_->setFixedWidth(railStack_->currentWidget()->maximumWidth());
    for (auto *widget : modelTrayWidgets_)
        widget->setVisible(!sheets);
    sheetsTray_->setVisible(sheets);
    if (const auto index = sideTabs_->indexOf(tray_); index >= 0)
        sideTabs_->setTabText(index, sheets ? "Sheets" : "Model");
    activeTool_->setVisible(sheets);
    focusRegions_ = sheets ? sheetRegions_ : modelRegions_;
    if (auto *page = findChild<QMenu *>("pageMenu"))
        page->menuAction()->setVisible(sheets);
    findChild<QAction *>(sheets ? "view.workspace.sheets" : "view.workspace.model")
        ->setChecked(true);
    syncWorkspaceActions();
    sync();
    if (sheets) {
        sheetCache_->refresh(sheets_->currentSheet());
        sheetTool(int(sheets_->canvas()->tool()));
    } else {
        status_->setText(modelStatus_);
        measurements_->setPlaceholderText(modelPlaceholder_);
        measurements_->clear();
        measurementError(false);
        measurements_->setEnabled(viewport_->tool() != Viewport::Tool::Paint && !assistantFenced_);
    }
    focusWorkspace();
}
void Window::syncWorkspaceActions() {
    for (auto *entry : publicActions_) {
        const auto scope = actionScope(entry);
        const bool inactive = (scope == ShortcutScope::Model && workspace_ == Workspace::Sheets) ||
                              (scope == ShortcutScope::Sheets && workspace_ == Workspace::Model);
        if (inactive) {
            if (entry->isEnabled()) {
                workspaceSuspended_.insert(entry);
                entry->setEnabled(false);
            }
        } else if (workspaceSuspended_.erase(entry))
            entry->setEnabled(true);
    }
}
void Window::syncSheets() {
    if (!sheets_)
        return;
    const auto name = path_.isEmpty()
                          ? (recoveredName_.isEmpty() ? QString("Untitled") : recoveredName_)
                          : QFileInfo(path_).fileName();
    sheets_->refresh();
    sheetCache_->rememberScenes();
    if (workspace_ == Workspace::Sheets)
        sheetCache_->refresh(sheets_->currentSheet());
    sheets_->canvas()->contentChanged();
    sheetsTray_->setDocumentName(name);
    sheetsTray_->refresh(sheets_->currentSheet(), sheets_->canvas()->selectedView());
    const auto key = [this](const char *id) { return findChild<QAction *>(id)->shortcut(); };
    sheets_->setShortcutHints(key("sheet.view.fit"), key("sheet.page.add"));
    const bool page = doc_.drawingSheets().contains(sheets_->currentSheet());
    const auto order = orderedDrawingSheets(doc_);
    const auto at = std::find(order.begin(), order.end(), sheets_->currentSheet());
    const auto index = at == order.end() ? -1 : int(at - order.begin());
    size_t changed = 0;
    for (const auto &[id, record] : doc_.drawingViews()) {
        (void)record;
        if (const auto status = sheetCache_->status(id);
            status && status->state == DrawingViewState::Stale)
            ++changed;
    }
    const std::vector<std::pair<const char *, bool>> states{
        {"sheet.view.fit", page},
        {"sheet.page.rename", page},
        {"sheet.page.delete", page},
        {"sheet.page.moveEarlier", page && index > 0},
        {"sheet.page.moveLater", page && index + 1 < int(order.size())},
        {"sheet.page.previous", page && index > 0},
        {"sheet.page.next", page && index + 1 < int(order.size())},
        {"sheet.view.acceptAll", changed > 0},
        {"sheet.tool.select", true},
        {"sheet.tool.pan", true},
        {"sheet.tool.zoom", true},
        {"sheet.page.add", true},
        {"sheet.grid", true}};
    // In Model the sheet actions stay suspended; switching to Sheets runs this again.
    if (workspace_ == Workspace::Sheets && !assistantFenced_)
        for (const auto &[id, enabled] : states)
            findChild<QAction *>(id)->setEnabled(enabled);
    sheets_->fitPageButton()->setEnabled(page);
}
} // namespace sketchy
