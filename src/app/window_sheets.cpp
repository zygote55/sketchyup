#include "app/interface_preferences.hpp"
#include "app/keyboard_navigation.hpp"
#include "app/settled_dialog.hpp"
#include "app/sheet_evaluation.hpp"
#include "app/sheet_item_info.hpp"
#include "app/sheet_items.hpp"
#include "app/sheet_text.hpp"
#include "app/sheet_workspace.hpp"
#include "app/sheets_tray.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/scenes_io.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIconEngine>
#include <QInputDialog>
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
QPainterPath modelViewIcon() {
    QPainterPath path;
    path.addRoundedRect(QRectF(2, 3, 14, 12), 1, 1);
    path.moveTo(5, 12);
    path.lineTo(8, 8);
    path.lineTo(10, 10);
    path.lineTo(13, 6);
    return path;
}
QPainterPath moveIcon() {
    QPainterPath path;
    path.moveTo(9, 2);
    path.lineTo(9, 16);
    path.moveTo(2, 9);
    path.lineTo(16, 9);
    for (const auto &[tip, a, b] : {std::tuple{QPointF(9, 2), QPointF(7, 4), QPointF(11, 4)},
                                    std::tuple{QPointF(9, 16), QPointF(7, 14), QPointF(11, 14)},
                                    std::tuple{QPointF(2, 9), QPointF(4, 7), QPointF(4, 11)},
                                    std::tuple{QPointF(16, 9), QPointF(14, 7), QPointF(14, 11)}}) {
        path.moveTo(a);
        path.lineTo(tip);
        path.lineTo(b);
    }
    return path;
}
// The page-item tools (R091.g), from the R091 design's rail.
QPainterPath lineIcon() {
    QPainterPath path;
    path.moveTo(3, 15);
    path.lineTo(15, 3);
    return path;
}
QPainterPath rectangleIcon() {
    QPainterPath path;
    path.addRect(QRectF(3, 5, 12, 8));
    return path;
}
QPainterPath textIcon() {
    QPainterPath path;
    path.moveTo(4, 4);
    path.lineTo(14, 4);
    path.moveTo(9, 4);
    path.lineTo(9, 15);
    return path;
}
QPainterPath labelIcon() {
    QPainterPath path;
    path.moveTo(3, 15);
    path.lineTo(9, 9);
    path.lineTo(15, 9);
    path.addEllipse(QPointF(3, 15), 1.2, 1.2);
    return path;
}
QPainterPath dimensionIcon() {
    QPainterPath path;
    path.moveTo(2, 9);
    path.lineTo(16, 9);
    path.moveTo(2, 6);
    path.lineTo(2, 12);
    path.moveTo(16, 6);
    path.lineTo(16, 12);
    path.moveTo(5, 7);
    path.lineTo(2, 9);
    path.lineTo(5, 11);
    path.moveTo(13, 7);
    path.lineTo(16, 9);
    path.lineTo(13, 11);
    return path;
}
// The scale a new view gets: the scale every measurable view on the page shares, else the
// first view's, else 1:50. True when it comes from the page.
std::pair<double, bool> pageScale(const Document &doc, DrawingViewRenderCache &cache, Id sheet) {
    std::optional<double> found;
    for (const auto view : drawingViewsOnSheet(doc, sheet)) {
        const auto status = cache.status(view);
        if (status && status->notToScale)
            continue;
        if (!found)
            found = doc.drawingViews().at(view)->scaleDenominator;
    }
    return found ? std::pair{*found, true} : std::pair{50., false};
}
} // namespace
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
    addTool("sheet.tool.view", "Model view", "W", SheetCanvas::Tool::View);
    sheetRail_->addSeparator();
    // Page items (R091.g).
    addTool("sheet.tool.line", "Line", "L", SheetCanvas::Tool::Line);
    addTool("sheet.tool.rectangle", "Rectangle", "R", SheetCanvas::Tool::Rectangle);
    addTool("sheet.tool.text", "Text", "X", SheetCanvas::Tool::Text);
    addTool("sheet.tool.label", "Label", "K", SheetCanvas::Tool::Label);
    addTool("sheet.tool.dimension", "Dimension", "D", SheetCanvas::Tool::Dimension);
    sheetRail_->addSeparator();
    addTool("sheet.tool.move", "Move", "M", SheetCanvas::Tool::Move);
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
    // Views (R091.f).
    page->addAction(action(
        "sheet.view.accept", "Accept view", QKeySequence("U"),
        [this] {
            if (const auto view = sheets_->canvas()->selectedView())
                run([&] {
                    sheetCommands({QJsonObject{{"command", "drawing_view.accept"},
                                               {"view", QString::number(view)}}},
                                  "Accept 1 view");
                });
        },
        ShortcutScope::Sheets));
    page->addAction(action(
        "sheet.view.edit", "Edit view", {}, [this] { sheets_->canvas()->enterEditView(); },
        ShortcutScope::Sheets));
    // Delete (and Backspace on the canvas) deletes the selected view or item; never inside
    // edit-view.
    auto *deleteView = action(
        "sheet.view.delete", "Delete", QKeySequence::Delete, [this] { deleteSheetView(); },
        ShortcutScope::Sheets);
    page->addAction(deleteView);
    for (auto *menu : menuBar()->findChildren<QMenu *>())
        if (auto *modelDelete = findChild<QAction *>("edit.delete");
            menu->actions().contains(modelDelete)) {
            // Edit › Delete: the view in Sheets, the selection in Model.
            const auto actions = menu->actions();
            const auto at = actions.indexOf(modelDelete);
            menu->insertAction(at + 1 < actions.size() ? actions[at + 1] : nullptr, deleteView);
            deleteView->setVisible(false);
            break;
        }
    page->addAction(action(
        "sheet.view.chooseScene", "Choose scene…", {},
        [this] { chooseViewScene(sheets_->canvas()->selectedView()); }, ShortcutScope::Sheets));
    page->addAction(action(
        "sheet.view.saveScene", "Save as new scene…", {},
        [this] { saveViewScene(sheets_->canvas()->editView()); }, ShortcutScope::Sheets));
    page->addAction(action(
        "sheet.view.showScene", "Show scene in model", {},
        [this] { showViewScene(sheets_->canvas()->selectedView()); }, ShortcutScope::Sheets));
    // Items (R091.g): in-place text editing and Reattach.
    page->addAction(action(
        "sheet.item.editText", "Edit text", {},
        [this] {
            sheets_->canvas()->setFocus(Qt::OtherFocusReason);
            sheets_->canvas()->beginTextEdit();
        },
        ShortcutScope::Sheets));
    // No ellipsis: Reattach proposes on the canvas and Enter confirms; no dialog opens.
    page->addAction(action(
        "sheet.item.reattach", "Reattach", {},
        [this] {
            sheets_->canvas()->setFocus(Qt::OtherFocusReason);
            sheets_->canvas()->beginReattach();
        },
        ShortcutScope::Sheets));
    page->addSeparator();
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

    // Insert and Arrange (the R091 design's menu bar): the title block and the stacking order.
    auto *insert = menuBar()->insertMenu(page->menuAction(), new QMenu("&Insert", this))->menu();
    insert->setObjectName("insertMenu");
    insert->menuAction()->setVisible(false);
    insert->addAction(action(
        "sheet.insert.titleBlock", "Title block", {}, [this] { insertTitleBlock(); },
        ShortcutScope::Sheets));
    auto *arrange = menuBar()->insertMenu(page->menuAction(), new QMenu("&Arrange", this))->menu();
    arrange->setObjectName("arrangeMenu");
    arrange->menuAction()->setVisible(false);
    arrange->addAction(action(
        "sheet.item.forward", "Bring forward", QKeySequence("Ctrl+]"),
        [this] { restackSheetItem(sheets_->canvas()->selectedItem(), 1); }, ShortcutScope::Sheets));
    arrange->addAction(action(
        "sheet.item.backward", "Send backward", QKeySequence("Ctrl+["),
        [this] { restackSheetItem(sheets_->canvas()->selectedItem(), -1); },
        ShortcutScope::Sheets));

    // Canvas, page tabs and Tray wiring.
    sheets_->canvas()->setRasterRenderer(
        [this](const DrawingViewEvaluationInput &input, QSize pixels) {
            return viewport_->renderDrawingViewRaster(input, pixels);
        });
    connect(sheetCache_.get(), &DrawingViewRenderCache::evaluated, this, [this](qulonglong) {
        sheets_->canvas()->contentChanged();
        sheetsTray_->refresh(sheets_->currentSheet(), sheets_->canvas()->selectedView(),
                             sheets_->canvas()->selectedItem());
        syncSheets();
    });
    connect(sheets_, &SheetWorkspace::currentSheetChanged, this, [this](qulonglong) {
        if (workspace_ == Workspace::Sheets)
            sheetCache_->refresh(sheets_->currentSheet());
        sheetsTray_->refresh(sheets_->currentSheet(), sheets_->canvas()->selectedView(),
                             sheets_->canvas()->selectedItem());
        syncSheets();
    });
    connect(sheets_, &SheetWorkspace::addPageRequested, this,
            [this] { findChild<QAction *>("sheet.page.add")->trigger(); });
    connect(sheets_, &SheetWorkspace::fitRequested, this,
            [this] { findChild<QAction *>("sheet.view.fit")->trigger(); });
    connect(sheets_, &SheetWorkspace::pagesRequested, this,
            [this] { sheetsTray_->pages()->setFocus(Qt::ShortcutFocusReason); });
    connect(canvas, &SheetCanvas::itemSelectionChanged, this, [this](qulonglong) {
        sheetsTray_->refresh(sheets_->currentSheet(), sheets_->canvas()->selectedView(),
                             sheets_->canvas()->selectedItem());
        syncSheetActions();
    });
    connect(canvas, &SheetCanvas::selectionChanged, this, [this](qulonglong) {
        sheetsTray_->refresh(sheets_->currentSheet(), sheets_->canvas()->selectedView(),
                             sheets_->canvas()->selectedItem());
        syncSheetActions();
    });
    canvas->setCommitter(
        [this](const QJsonArray &commands, const QString &label, const QString &amendKey) {
            return sheetCommit(commands, label, amendKey);
        });
    connect(canvas, &SheetCanvas::stepChanged, this, [this] { refreshSheetStep(); });
    connect(canvas, &SheetCanvas::message, this, [this](const QString &text) {
        if (workspace_ == Workspace::Sheets)
            status_->setText(text);
    });
    connect(canvas, &SheetCanvas::editViewChanged, this, [this](qulonglong view) {
        sheets_->setEditView(view);
        syncSheetActions();
    });
    connect(canvas, &SheetCanvas::chooseSceneRequested, this,
            [this](qulonglong view) { chooseViewScene(view); });
    connect(canvas, &SheetCanvas::deleteRequested, this,
            [this] { findChild<QAction *>("sheet.view.delete")->trigger(); });
    connect(canvas, &SheetCanvas::saveSceneRequested, this,
            [this](qulonglong view) { saveViewScene(view); });
    connect(sheets_, &SheetWorkspace::leaveEditViewRequested, this, [this] {
        sheets_->canvas()->leaveEditView();
        sheets_->canvas()->setFocus(Qt::OtherFocusReason);
    });
    auto *place = sheetsTray_->placePanel();
    connect(place, &SheetPlacePanel::placementChanged, this, [this] { refreshPlacement(); });
    connect(place, &SheetPlacePanel::placeRequested, this, [this] {
        refreshPlacement();
        sheets_->canvas()->placeView();
        sheets_->canvas()->setFocus(Qt::OtherFocusReason);
    });
    connect(place, &SheetPlacePanel::cancelRequested, this, [this] {
        auto *canvas = sheets_->canvas();
        if (canvas->operationActive())
            canvas->cancelOperation();
        else
            findChild<QAction *>("sheet.tool.select")->trigger();
        canvas->setFocus(Qt::OtherFocusReason);
    });
    connect(sheetsTray_, &SheetsTray::renameView, this,
            [this](qulonglong view, const QString &name) {
                if (name.isEmpty()) {
                    status_->setText("A view title cannot be empty.");
                    syncSheets();
                    return;
                }
                run([&] {
                    sheetCommands({QJsonObject{{"command", "drawing_view.update"},
                                               {"view", QString::number(view)},
                                               {"name", name}}},
                                  "Rename view");
                });
            });
    connect(sheetsTray_, &SheetsTray::setViewScale, this, [this](qulonglong view, double scale) {
        run([&] {
            sheetCommands({QJsonObject{{"command", "drawing_view.update"},
                                       {"view", QString::number(view)},
                                       {"scaleDenominator", scale}}},
                          "Change scale");
        });
    });
    connect(sheetsTray_, &SheetsTray::setViewStyle, this,
            [this](qulonglong view, int content, bool hidden) {
                run([&] {
                    sheetCommands({QJsonObject{{"command", "drawing_view.update"},
                                               {"view", QString::number(view)},
                                               {"content", content == 1 ? "rasterAppearance"
                                                                        : "technicalLines"},
                                               {"hiddenLines", hidden}}},
                                  "Change style");
                });
            });
    connect(sheetsTray_, &SheetsTray::editViewRequested, this, [this] {
        sheets_->canvas()->setFocus(Qt::OtherFocusReason);
        sheets_->canvas()->enterEditView();
    });
    connect(sheetsTray_, &SheetsTray::showSceneRequested, this,
            [this](qulonglong view) { showViewScene(view); });
    connect(sheetsTray_, &SheetsTray::chooseSceneRequested, this,
            [this](qulonglong view) { chooseViewScene(view); });
    connect(canvas, &SheetCanvas::escapeToSelect, this,
            [this] { findChild<QAction *>("sheet.tool.select")->trigger(); });
    connect(canvas, &SheetCanvas::zoomChanged, this, [this](double) { refreshSheetStep(); });
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
    connect(sheetsTray_, &SheetsTray::itemChosen, this, [this](qulonglong item) {
        const auto found = doc_.drawingItems().find(item);
        if (found == doc_.drawingItems().end())
            return;
        sheets_->setCurrentSheet(found->second->sheet);
        sheets_->canvas()->setSelectedItem(item);
    });
    connect(sheetsTray_, &SheetsTray::itemActivated, this, [this](qulonglong item) {
        // Enter on an item row: select it on the page and, for text, edit it in place.
        const auto found = doc_.drawingItems().find(item);
        if (found == doc_.drawingItems().end())
            return;
        sheets_->setCurrentSheet(found->second->sheet);
        auto *canvas = sheets_->canvas();
        canvas->setSelectedItem(item);
        canvas->setFocus(Qt::OtherFocusReason);
        const auto kind = found->second->kind();
        if (kind == DrawingItemKind::Text || kind == DrawingItemKind::Label ||
            kind == DrawingItemKind::TitleBlock)
            canvas->beginTextEdit();
    });
    connect(sheetsTray_, &SheetsTray::deleteItem, this, [this](qulonglong item) {
        run([&] { deleteSheetItem(item); });
        sheetsTray_->pages()->setFocus(Qt::OtherFocusReason);
    });
    connect(sheetsTray_, &SheetsTray::setDimensionFormat, this,
            [this](qulonglong sheet, const QJsonObject &fields) {
                run([&] {
                    sheetCommands({QJsonObject{{"command", "drawing_sheet.update"},
                                               {"sheet", QString::number(sheet)},
                                               {"dimensionFormat", fields}}},
                                  "Change dimension format");
                });
            });
    connect(sheetsTray_, &SheetsTray::reorderItem, this, [this](qulonglong item, int delta) {
        run([&] { restackSheetItem(item, delta); });
        sheetsTray_->pages()->setFocus(Qt::OtherFocusReason);
    });
    connect(sheetsTray_->itemInfo(), &SheetItemInfo::changeItem, this,
            [this](qulonglong item, const QString &kind, const QJsonObject &data,
                   const QString &label) {
                run([&] {
                    sheetCommands({QJsonObject{{"command", "drawing_item.update"},
                                               {"item", QString::number(item)},
                                               {"kind", kind},
                                               {"data", data}}},
                                  label);
                });
            });
    connect(sheetsTray_->itemInfo(), &SheetItemInfo::changeSheetNumber, this,
            [this](qulonglong sheet, const QString &number) {
                run([&] {
                    sheetCommands({QJsonObject{{"command", "drawing_sheet.update"},
                                               {"sheet", QString::number(sheet)},
                                               {"number", number}}},
                                  "Change sheet number");
                });
            });
    connect(sheetsTray_->itemInfo(), &SheetItemInfo::reattachRequested, this, [this](qulonglong) {
        sheets_->canvas()->setFocus(Qt::OtherFocusReason);
        sheets_->canvas()->beginReattach();
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
    for (const auto &[id, path] : std::vector<std::pair<const char *, QPainterPath>>{
             {"sheet.tool.select", selectIcon()},
             {"sheet.tool.view", modelViewIcon()},
             {"sheet.tool.line", lineIcon()},
             {"sheet.tool.rectangle", rectangleIcon()},
             {"sheet.tool.text", textIcon()},
             {"sheet.tool.label", labelIcon()},
             {"sheet.tool.dimension", dimensionIcon()},
             {"sheet.tool.move", moveIcon()},
             {"sheet.tool.pan", panIcon()},
             {"sheet.tool.zoom", zoomIcon()}})
        findChild<QAction *>(id)->setIcon(
            QIcon(new LineIconEngine(path, colors.ink, colors.muted)));
}
void Window::sheetTool(int value) {
    const auto tool = SheetCanvas::Tool(value);
    sheets_->canvas()->setTool(tool);
    for (auto *entry : sheetRail_->actions())
        if (entry->property("sheetTool").isValid() && entry->property("sheetTool").toInt() == value)
            entry->setChecked(true);
    // The Model view tool shows the "New model view" panel in the Tray.
    sheetsTray_->setPlacing(tool == SheetCanvas::Tool::View);
    if (workspace_ != Workspace::Sheets)
        return;
    if (tool == SheetCanvas::Tool::View) {
        sideTabs_->show();
        sideTabs_->setCurrentWidget(tray_);
        refreshPlacement();
    }
    sheetInstruction_.clear();
    measurementError(false);
    measurements_->clear();
    refreshSheetStep();
    sheets_->canvas()->setFocus(Qt::OtherFocusReason);
}
void Window::refreshSheetStep() {
    if (!sheets_ || workspace_ != Workspace::Sheets)
        return;
    auto *canvas = sheets_->canvas();
    activeTool_->setText(canvas->toolName());
    // A message stays until the instruction changes.
    if (const auto instruction = canvas->instruction(); instruction != sheetInstruction_) {
        sheetInstruction_ = instruction;
        status_->setText(instruction);
    }
    const auto label = canvas->measurementLabel();
    measurementStep_->setText(label);
    measurementStep_->setVisible(!label.isEmpty());
    measurements_->setEnabled(!label.isEmpty() && !assistantFenced_);
    measurements_->setPlaceholderText(label == "Zoom"       ? "150% or 1.5"
                                      : label == "Size"     ? "width,height"
                                      : label == "Scale"    ? "1:50 or width,height"
                                      : label == "Distance" ? "dx,dy"
                                      : label == "Length"   ? "length, <dx,dy> or [x,y]"
                                      : label == "Offset"   ? "offset in mm"
                                      : label == "Position" ? "[x,y] or 10pt"
                                      : label == "Move"     ? "<dx,dy>, [x,y] or 10pt"
                                                            : QString{});
    if (!measurements_->hasFocus())
        measurements_->setText(canvas->measurementValue());
}
void Window::refreshPlacement() {
    auto *panel = sheetsTray_->placePanel();
    const auto sheet = sheets_->currentSheet();
    const auto [scale, fromPage] = pageScale(doc_, *sheetCache_, sheet);
    const auto name = path_.isEmpty()
                          ? (recoveredName_.isEmpty() ? QString("Untitled") : recoveredName_)
                          : QFileInfo(path_).fileName();
    panel->refresh(name, sheet, scale, fromPage);
    try {
        sheets_->canvas()->setPlacement(sheetPlacement(doc_, panel->scene(), panel->scale()));
    } catch (const std::exception &error) {
        status_->setText(error.what());
    }
}
bool Window::sheetMeasurements(const QString &text) {
    QString error;
    sheetCommitError_.clear();
    if (sheets_->canvas()->measurements(text, error)) {
        refreshSheetStep();
        return true;
    }
    // A refused command explains itself better than the generic reason.
    status_->setText(sheetCommitError_.isEmpty() ? error : sheetCommitError_);
    return false;
}
QJsonObject Window::sheetCommands(const QJsonArray &commands, const QString &label) {
    if (assistant_ && assistant_->uncertain()) {
        status_->setText("Reconcile the assistant outcome before editing this model.");
        return {};
    }
    viewport_->cancel();
    sheetAmendKey_.clear();
    auto result = executeBatch(doc_, {{"apiVersion", 1},
                                      {"documentId", QString::fromStdString(doc_.identity())},
                                      {"expectedRevision", QString::number(doc_.revision())},
                                      {"commands", commands},
                                      {"history", QJsonObject{{"label", label}}}});
    sync();
    return result;
}
QJsonObject Window::sheetCommit(const QJsonArray &commands, const QString &label,
                                const QString &amendKey) {
    if (assistant_ && assistant_->uncertain()) {
        sheetCommitError_ = "Reconcile the assistant outcome before editing this model.";
        status_->setText(sheetCommitError_);
        return {};
    }
    try {
        viewport_->cancel();
        const QJsonObject request{{"apiVersion", 1},
                                  {"documentId", QString::fromStdString(doc_.identity())},
                                  {"expectedRevision", QString::number(doc_.revision())},
                                  {"commands", commands},
                                  {"history", QJsonObject{{"label", label}}}};
        // Re-entry and consecutive nudges amend the entry they continue.
        auto result =
            !amendKey.isEmpty() && amendKey == sheetAmendKey_ && doc_.canAmend(sheetAmendStamp_)
                ? executeAmend(doc_, sheetAmendStamp_, request)
                : executeBatch(doc_, request);
        sheetAmendKey_ = amendKey;
        sheetAmendStamp_ = doc_.amendmentStamp();
        sync();
        if (result.isEmpty())
            result["status"] = "committed";
        return result;
    } catch (const std::exception &error) {
        sheetAmendKey_.clear();
        sheetCommitError_ = QString::fromUtf8(error.what());
        status_->setText(sheetCommitError_);
        sync();
        return {};
    }
}
void Window::deleteSheetItem(Id item) {
    const auto found = doc_.drawingItems().find(item);
    if (found == doc_.drawingItems().end())
        return;
    sheetCommands(
        {QJsonObject{{"command", "drawing_item.delete"}, {"item", QString::number(item)}}},
        "Delete " + sheetItemKindName(found->second->kind()).toLower());
}
void Window::restackSheetItem(Id item, int delta) {
    const auto found = doc_.drawingItems().find(item);
    if (found == doc_.drawingItems().end()) {
        status_->setText("Select an item to change its stacking order.");
        return;
    }
    auto order = drawingItemsOnSheet(doc_, found->second->sheet);
    const auto at = std::find(order.begin(), order.end(), item);
    const auto index = int(at - order.begin());
    const auto target = index + delta;
    if (target < 0 || target >= int(order.size())) {
        status_->setText(delta > 0 ? "The item is already in front of the others."
                                   : "The item is already behind the others.");
        return;
    }
    std::swap(order[size_t(index)], order[size_t(target)]);
    QJsonArray ids;
    for (const auto id : order)
        ids.append(QString::number(id));
    sheetCommands({QJsonObject{{"command", "drawing_item.reorder"},
                               {"sheet", QString::number(found->second->sheet)},
                               {"order", ids}}},
                  delta > 0 ? "Bring forward" : "Send backward");
    syncSheetActions();
}
// A title block for a page: bottom right inside the margin, 160 × 30 mm or what fits.
static QJsonObject titleBlockFrame(const DrawingSheetRecord &sheet) {
    const auto margin = sheet.marginMm;
    const auto width = std::min(160., sheet.paper.widthMm - 2 * margin);
    const auto height = std::min(30., sheet.paper.heightMm - 2 * margin);
    return {{"xMm", sheet.paper.widthMm - margin - width},
            {"yMm", sheet.paper.heightMm - margin - height},
            {"widthMm", width},
            {"heightMm", height}};
}
void Window::insertTitleBlock() {
    const auto sheet = sheets_->currentSheet();
    const auto found = doc_.drawingSheets().find(sheet);
    if (found == doc_.drawingSheets().end()) {
        status_->setText("Add a page before inserting a title block.");
        return;
    }
    run([&] {
        const auto result = sheetCommands(
            {QJsonObject{{"command", "drawing_item.create"},
                         {"sheet", QString::number(sheet)},
                         {"kind", "titleBlock"},
                         {"data", QJsonObject{{"frame", titleBlockFrame(*found->second)}}}}},
            "Add title block");
        const auto created = result["createdDrawingItems"].toArray();
        if (!created.isEmpty()) {
            sheets_->canvas()->setSelectedItem(created.first().toString().toULongLong());
            status_->setText("Inserted a title block. The sheet number, page and scale fill in "
                             "automatically; press Enter to type the project.");
        }
    });
    focusWorkspace();
}
void Window::deleteSheetView() {
    auto *canvas = sheets_->canvas();
    if (const auto item = canvas->selectedItem(); item && !canvas->editView()) {
        run([&] { deleteSheetItem(item); });
        focusWorkspace();
        return;
    }
    const auto view = canvas->selectedView();
    if (!view || canvas->editView() || !doc_.drawingViews().contains(view))
        return;
    run([&] {
        // ADR 0164 §3: the dimensions and labels bound to the view go in the same entry.
        sheetCommands(
            {QJsonObject{{"command", "drawing_view.delete"}, {"view", QString::number(view)}}},
            "Delete view");
    });
    focusWorkspace();
}
void Window::chooseViewScene(Id view) {
    const auto found = doc_.drawingViews().find(view);
    if (found == doc_.drawingViews().end()) {
        status_->setText("Select a view to choose its scene.");
        return;
    }
    const auto record = *found->second;
    QStringList items;
    std::vector<Id> ids;
    for (const auto id : orderedScenes(doc_)) {
        const auto &scene = *doc_.scenes().at(id);
        if (!scene.snapshot.camera)
            continue;
        items << QString::fromStdString(scene.name) + " · " + sceneProjection(doc_, id);
        ids.push_back(id);
    }
    if (items.isEmpty()) {
        status_->setText("This model has no scenes with a camera. Save a scene in the Model "
                         "workspace first.");
        return;
    }
    SettledInputDialog dialog(this);
    dialog.setObjectName("chooseSceneDialog");
    dialog.setWindowTitle("Choose scene");
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setComboBoxEditable(false);
    dialog.setComboBoxItems(items);
    dialog.setLabelText("Scene for “" + QString::fromStdString(record.name) +
                        "”. The view keeps its frame and scale.");
    dialog.setOkButtonText("Choose");
    const auto current = std::find(ids.begin(), ids.end(), record.source.scene);
    if (current != ids.end())
        dialog.setTextValue(items[int(current - ids.begin())]);
    if (auto *combo = dialog.findChild<QComboBox *>()) {
        combo->setObjectName("chooseSceneList");
        combo->setAccessibleName("Scene");
    }
    const bool accepted = dialog.exec() == QDialog::Accepted;
    // A closed prompt returns focus to the page (UX_DESIGN §4.1). Reactivate the window first:
    // without a window manager (X11 under Xvfb) nothing hands activation back to it.
    activateWindow();
    focusWorkspace();
    if (!accepted)
        return;
    const auto index = items.indexOf(dialog.textValue());
    if (index < 0 || !doc_.drawingViews().contains(view) ||
        doc_.drawingViews().at(view)->source.scene == ids[size_t(index)]) {
        // The same scene: nothing changes, so nothing is committed.
        focusWorkspace();
        return;
    }
    run([&] {
        sheetCommands(
            {QJsonObject{{"command", "drawing_view.update"},
                         {"view", QString::number(view)},
                         {"source", QJsonObject{{"kind", "scene"},
                                                {"scene", QString::number(ids[size_t(index)])}}}}},
            "Choose scene");
    });
    focusWorkspace();
}
void Window::saveViewScene(Id view) {
    const auto found = doc_.drawingViews().find(view);
    if (found == doc_.drawingViews().end()) {
        status_->setText("Open a view with Enter to save its framing as a scene.");
        return;
    }
    const auto record = *found->second;
    const auto scene = doc_.scenes().find(record.source.scene);
    if (scene == doc_.scenes().end() || !scene->second->snapshot.camera ||
        !scene->second->snapshot.camera->orthographic) {
        status_->setText("Only a view drawn from an orthographic scene can be saved as a scene.");
        return;
    }
    std::set<std::string> names;
    for (const auto &[id, saved] : doc_.scenes())
        names.insert(saved->name);
    const auto &sheet = *doc_.drawingSheets().at(record.sheet);
    const auto base = QString::fromStdString(scene->second->name) + " (" +
                      QString::fromStdString(sheet.number.empty() ? sheet.name : sheet.number) +
                      ")";
    auto proposed = base;
    for (int n = 2; names.contains(proposed.toStdString()); ++n)
        proposed = base + " " + QString::number(n);
    SettledInputDialog dialog(this);
    dialog.setObjectName("saveViewSceneDialog");
    dialog.setWindowTitle("Save as new scene");
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setLabelText("Scene name. The new scene keeps this view's framing in the model, and "
                        "the view then uses it.");
    dialog.setTextValue(proposed);
    dialog.setOkButtonText("Save scene");
    if (auto *edit = dialog.findChild<QLineEdit *>()) {
        edit->setObjectName("saveViewSceneName");
        edit->setAccessibleName("Scene name");
    }
    const bool accepted = dialog.exec() == QDialog::Accepted;
    // A closed prompt returns focus to the page (UX_DESIGN §4.1). Reactivate the window first:
    // without a window manager (X11 under Xvfb) nothing hands activation back to it.
    activateWindow();
    focusWorkspace();
    if (!accepted)
        return;
    const auto name = dialog.textValue().trimmed();
    if (name.isEmpty()) {
        status_->setText("A scene name cannot be empty.");
        return;
    }
    run([&] {
        // The new scene's target is the point drawn at the frame centre, so the view draws the
        // same with no offset. Decision 8: two entries, the model edit first, because one
        // history entry cannot change both the model and the sheets.
        auto snapshot = scene->second->snapshot;
        const auto projection = drawingViewProjection(record, renderSceneCamera(*snapshot.camera));
        snapshot.camera->target = projection.page.origin;
        const auto created =
            sheetCommands({QJsonObject{{"command", "saved_scene.create"},
                                       {"name", name},
                                       {"snapshot", encodeSceneSnapshot(snapshot)}}},
                          "Save as new scene");
        const auto scenes = created["createdScenes"].toArray();
        if (scenes.isEmpty())
            throw std::runtime_error("The scene was not created");
        sheetCommands({QJsonObject{{"command", "drawing_view.update"},
                                   {"view", QString::number(view)},
                                   {"source", QJsonObject{{"kind", "scene"},
                                                          {"scene", scenes.first().toString()}}},
                                   {"offsetMm", QJsonArray{0, 0}}}},
                      "Use new scene");
        status_->setText("Saved scene “" + name + "”; the view now uses it.");
    });
    focusWorkspace();
}
void Window::showViewScene(Id view) {
    const auto found = doc_.drawingViews().find(view);
    if (found == doc_.drawingViews().end())
        return;
    const auto scene = found->second->source.scene;
    if (!doc_.scenes().contains(scene)) {
        status_->setText("This view's scene was deleted.");
        return;
    }
    const auto name = QString::fromStdString(doc_.scenes().at(scene)->name);
    run(
        [&] {
            // Decision 9: the camera only, never a full recall of style or visibility.
            setWorkspace(Workspace::Model);
            viewport_->showSceneCamera(scene);
            viewport_->setFocus();
            status_->setText("Showing the camera of scene “" + name + "”.");
        },
        true);
}
void Window::addSheetPage() {
    DrawingPaper paper;
    QJsonArray commands{QJsonObject{}};
    if (const auto found = doc_.drawingSheets().find(sheets_->currentSheet());
        found != doc_.drawingSheets().end()) {
        paper = found->second->paper;
        // The new page gets a copy of the current page's title block in the same entry (ADR
        // 0164); the batch creates the page first, with the next sheet ID.
        for (const auto item : drawingItemsOnSheet(doc_, found->first))
            if (const auto &record = *doc_.drawingItems().at(item);
                record.kind() == DrawingItemKind::TitleBlock)
                commands.append(QJsonObject{{"command", "drawing_item.create"},
                                            {"sheet", QString::number(doc_.nextDrawingSheetId())},
                                            {"kind", "titleBlock"},
                                            {"data", sheetItemData(record)}});
    }
    commands[0] = QJsonObject{
        {"command", "drawing_sheet.create"},
        {"name", "Page " + QString::number(doc_.drawingSheets().size() + 1)},
        {"paper", QJsonObject{{"widthMm", paper.widthMm}, {"heightMm", paper.heightMm}}}};
    const auto result = [&] {
        viewport_->cancel();
        auto response = executeBatch(doc_, {{"apiVersion", 1},
                                            {"documentId", QString::fromStdString(doc_.identity())},
                                            {"expectedRevision", QString::number(doc_.revision())},
                                            {"commands", commands},
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
    if (workspace_ == Workspace::Sheets && sheets_->canvas()->textEditing())
        // A typed position opened the in-place editor (R091.g): typing continues there.
        sheets_->canvas()->textEditor()->setFocus(Qt::OtherFocusReason);
    else if (workspace_ == Workspace::Sheets && sheets_)
        sheets_->canvas()->setFocus(Qt::OtherFocusReason);
    else
        viewport_->setFocus();
}
void Window::setWorkspace(Workspace workspace) {
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
    for (const char *menu : {"pageMenu", "insertMenu", "arrangeMenu"})
        if (auto *entry = findChild<QMenu *>(menu))
            entry->menuAction()->setVisible(sheets);
    findChild<QAction *>("edit.delete")->setVisible(!sheets);
    findChild<QAction *>("sheet.view.delete")->setVisible(sheets);
    findChild<QAction *>(sheets ? "view.workspace.sheets" : "view.workspace.model")
        ->setChecked(true);
    syncWorkspaceActions();
    sync();
    if (sheets) {
        sheetCache_->refresh(sheets_->currentSheet());
        sheetInstruction_.clear();
        sheetTool(int(sheets_->canvas()->tool()));
    } else {
        measurementStep_->hide();
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
    const auto name = path_.isEmpty()
                          ? (recoveredName_.isEmpty() ? QString("Untitled") : recoveredName_)
                          : QFileInfo(path_).fileName();
    sheets_->refresh();
    sheetCache_->rememberScenes();
    if (workspace_ == Workspace::Sheets)
        sheetCache_->refresh(sheets_->currentSheet());
    sheets_->canvas()->contentChanged();
    sheetsTray_->setDocumentName(name);
    sheetsTray_->refresh(sheets_->currentSheet(), sheets_->canvas()->selectedView(),
                         sheets_->canvas()->selectedItem());
    const auto key = [this](const char *id) { return findChild<QAction *>(id)->shortcut(); };
    sheets_->setShortcutHints(key("sheet.view.fit"), key("sheet.page.add"));
    syncSheetActions();
    if (sheetsTray_->placing())
        refreshPlacement();
    refreshSheetStep();
}
void Window::syncSheetActions() {
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
    auto *canvas = sheets_->canvas();
    const auto selected = canvas->selectedView();
    const auto selectedStatus = selected ? sheetCache_->status(selected) : std::nullopt;
    const bool hasCamera =
        selected && doc_.drawingViews().contains(selected) &&
        doc_.scenes().contains(doc_.drawingViews().at(selected)->source.scene) &&
        doc_.scenes().at(doc_.drawingViews().at(selected)->source.scene)->snapshot.camera;
    // The selected item and its place in the page's stacking order.
    const auto item = canvas->selectedItem();
    const auto itemOrder =
        item ? drawingItemsOnSheet(doc_, sheets_->currentSheet()) : std::vector<Id>{};
    const auto itemAt = std::find(itemOrder.begin(), itemOrder.end(), item);
    const auto itemIndex = itemAt == itemOrder.end() ? -1 : int(itemAt - itemOrder.begin());
    const auto itemCount = int(itemOrder.size());
    bool textItem = false, brokenBound = false;
    if (const auto found = doc_.drawingItems().find(item); found != doc_.drawingItems().end()) {
        const auto kind = found->second->kind();
        textItem = kind == DrawingItemKind::Text || kind == DrawingItemKind::Label ||
                   kind == DrawingItemKind::TitleBlock;
        try {
            brokenBound = drawingItemView(*found->second) &&
                          evaluateDrawingItem(doc_, item).state == DrawingItemState::Broken;
        } catch (const std::exception &) {
        }
    }
    const std::vector<std::pair<const char *, bool>> states{
        {"sheet.view.fit", page},
        {"sheet.tool.line", page},
        {"sheet.tool.rectangle", page},
        {"sheet.tool.text", page},
        {"sheet.tool.label", page},
        {"sheet.tool.dimension", page},
        {"sheet.insert.titleBlock", page},
        {"sheet.item.forward", item && itemIndex + 1 < itemCount},
        {"sheet.item.backward", item && itemIndex > 0},
        {"sheet.item.editText", textItem && !canvas->textEditing()},
        {"sheet.item.reattach", brokenBound},
        {"sheet.view.accept", selectedStatus && selectedStatus->state == DrawingViewState::Stale},
        {"sheet.view.edit",
         hasCamera && !(selectedStatus && selectedStatus->notToScale) && !canvas->editView()},
        {"sheet.view.chooseScene", selected != 0},
        {"sheet.view.delete", (selected != 0 || item != 0) && !canvas->editView()},
        {"sheet.view.saveScene", canvas->editView() != 0},
        {"sheet.view.showScene", hasCamera},
        {"sheet.tool.view", page},
        {"sheet.tool.move", true},
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
