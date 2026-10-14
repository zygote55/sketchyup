#include "app/sheets_tray.hpp"
#include "app/history_panel.hpp"
#include "app/sheet_text.hpp"
#include "core/drawing_item_records.hpp"
#include "core/drawing_sheets.hpp"
#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QTimer>
#include <QVBoxLayout>
#include <set>
namespace sketchy {
namespace {
enum Role { KindRole = Qt::UserRole, IdRole };
enum Kind { PageRow = 1, ViewRow, ItemRow };
QLabel *valueLabel(const QString &name) {
    auto *label = new QLabel;
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setAccessibleName(name);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
QString itemSummary(const DrawingItemRecord &item) {
    switch (item.kind()) {
    case DrawingItemKind::Text:
        return "Text · " + QString::fromStdString(std::get<DrawingTextItem>(item.data).text)
                               .section('\n', 0, 0)
                               .left(40);
    case DrawingItemKind::Label:
        return "Label · " + QString::fromStdString(std::get<DrawingLabelItem>(item.data).text)
                                .section('\n', 0, 0)
                                .left(40);
    case DrawingItemKind::Dimension:
        return "Dimension";
    case DrawingItemKind::Line:
        return "Line";
    case DrawingItemKind::Rectangle:
        return "Rectangle";
    case DrawingItemKind::TitleBlock:
        return "Title block";
    }
    return "Item";
}
} // namespace
QPixmap sheetStateIcon(DrawingViewState state, bool pending, const ThemeColors &colors,
                       qreal ratio) {
    QPixmap pixmap(QSize(12, 12) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    QColor colour;
    if (pending) {
        colour = colors.muted;
        path.addEllipse(QRectF(1, 1, 10, 10));
        path.moveTo(6, 3.5);
        path.lineTo(6, 6);
        path.lineTo(8, 7.2);
    } else if (state == DrawingViewState::Current) {
        colour = colors.dark ? colors.accent : QColor("#3f6043");
        path.addEllipse(QRectF(1, 1, 10, 10));
        path.moveTo(3.5, 6);
        path.lineTo(5.5, 8);
        path.lineTo(8.5, 4);
    } else if (state == DrawingViewState::Stale) {
        colour = colors.warning;
        path.arcMoveTo(QRectF(2, 2, 8, 8), 0);
        path.arcTo(QRectF(2, 2, 8, 8), 0, 315);
        path.moveTo(10, 2);
        path.lineTo(10, 4.5);
        path.lineTo(7.5, 4.5);
    } else {
        colour = colors.failure;
        path.addEllipse(QRectF(1, 1, 10, 10));
        path.moveTo(4, 4);
        path.lineTo(8, 8);
        path.moveTo(8, 4);
        path.lineTo(4, 8);
    }
    painter.setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    return pixmap;
}
SheetsTray::SheetsTray(Document &doc, Viewport &viewport, DrawingViewRenderCache &cache,
                       QWidget *parent)
    : QWidget(parent), doc_(doc), cache_(cache) {
    setObjectName("sheetsTray");
    setAccessibleName("Sheets panel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *info = new QWidget;
    info->setObjectName("sheetEntityInfo");
    auto *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(4, 2, 4, 6);
    infoEmpty_ = new QLabel("Select a view on the page or in Pages.");
    infoEmpty_->setObjectName("sheetInfoEmpty");
    infoEmpty_->setWordWrap(true);
    infoLayout->addWidget(infoEmpty_);
    infoFields_ = new QWidget;
    auto *grid = new QGridLayout(infoFields_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    int row = 0;
    auto field = [&](const QString &name, QLabel *value) {
        auto *caption = new QLabel(name);
        caption->setObjectName("sheetInfoCaption");
        caption->setBuddy(value);
        grid->addWidget(caption, row, 0, Qt::AlignTop);
        grid->addWidget(value, row, 1);
        ++row;
        return value;
    };
    infoTitle_ = field("Title", valueLabel("View title"));
    infoTitle_->setObjectName("sheetInfoTitle");
    infoSource_ = field("Source", valueLabel("Source"));
    infoSource_->setTextFormat(Qt::RichText);
    infoSource_->setObjectName("sheetInfoSource");
    infoScale_ = field("Scale", valueLabel("Scale"));
    infoScale_->setObjectName("sheetInfoScale");
    infoStyle_ = field("Style", valueLabel("Style"));
    infoStyle_->setObjectName("sheetInfoStyle");
    infoCrop_ = field("Crop", valueLabel("Crop"));
    infoCrop_->setObjectName("sheetInfoCrop");
    auto *status = new QWidget;
    auto *statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(6);
    infoStatusIcon_ = new QLabel;
    infoStatusIcon_->setFixedSize(12, 12);
    statusLayout->addWidget(infoStatusIcon_);
    infoStatus_ = valueLabel("Status");
    infoStatus_->setObjectName("sheetInfoStatus");
    statusLayout->addWidget(infoStatus_, 1);
    auto *statusCaption = new QLabel("Status");
    statusCaption->setObjectName("sheetInfoCaption");
    statusCaption->setBuddy(infoStatus_);
    grid->addWidget(statusCaption, row, 0, Qt::AlignTop);
    grid->addWidget(status, row, 1);
    infoLayout->addWidget(infoFields_);
    infoSection_ = new CollapsibleSection("Entity info", info);
    infoSection_->setObjectName("sheetInfoSection");
    layout->addWidget(infoSection_);
    auto separator = [&] {
        auto *line = new QFrame;
        line->setObjectName("sheetTraySeparator");
        line->setFixedHeight(1);
        layout->addWidget(line);
    };
    separator();

    auto *references = new QWidget;
    references->setObjectName("sheetReferences");
    auto *referencesLayout = new QVBoxLayout(references);
    referencesLayout->setContentsMargins(4, 2, 4, 6);
    referencesLayout->setSpacing(8);
    auto *model = new QHBoxLayout;
    referencesModel_ = new QLabel;
    referencesModel_->setObjectName("referencesModel");
    referencesModel_->setTextFormat(Qt::RichText);
    referencesModel_->setWordWrap(true);
    model->addWidget(referencesModel_, 1);
    auto *thisModel = new QLabel("this model");
    thisModel->setObjectName("sheetInfoCaption");
    model->addWidget(thisModel, 0, Qt::AlignTop);
    referencesLayout->addLayout(model);
    referencesBanner_ = new QWidget;
    referencesBanner_->setObjectName("referencesBanner");
    referencesBanner_->setAttribute(Qt::WA_StyledBackground);
    auto *banner = new QHBoxLayout(referencesBanner_);
    banner->setContentsMargins(8, 6, 8, 6);
    banner->setSpacing(8);
    referencesBannerIcon_ = new QLabel;
    referencesBannerIcon_->setFixedSize(12, 12);
    banner->addWidget(referencesBannerIcon_);
    referencesBannerText_ = new QLabel;
    referencesBannerText_->setObjectName("referencesBannerText");
    referencesBannerText_->setWordWrap(true);
    referencesBannerText_->setTextFormat(Qt::PlainText);
    banner->addWidget(referencesBannerText_, 1);
    acceptAll_ = new QPushButton("Accept all");
    acceptAll_->setObjectName("sheetAcceptAll");
    acceptAll_->setAutoDefault(false);
    banner->addWidget(acceptAll_);
    connect(acceptAll_, &QPushButton::clicked, this, [this] { emit acceptAll(); });
    referencesLayout->addWidget(referencesBanner_);
    referencesSection_ = new CollapsibleSection("References", references);
    referencesSection_->setObjectName("sheetReferencesSection");
    layout->addWidget(referencesSection_);
    separator();

    pages_ = new QTreeWidget;
    pages_->setObjectName("sheetPagesTree");
    pages_->setAccessibleName("Pages");
    pages_->setAccessibleDescription("Pages expand to their views and items. F2 renames a page, "
                                     "Alt+Up and Alt+Down move it, Delete deletes it.");
    pages_->setColumnCount(3);
    pages_->setHeaderLabels({"Number", "Name", "State"});
    pages_->setHeaderHidden(true);
    pages_->header()->setStretchLastSection(false);
    pages_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    pages_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    pages_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    pages_->setSelectionMode(QAbstractItemView::SingleSelection);
    pages_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pages_->setUniformRowHeights(true);
    pages_->installEventFilter(this);
    connect(pages_->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this] {
        QTimer::singleShot(0, this, [this] { refresh(sheet_, view_); });
    });
    // F6 into the Tray lands in Pages, the keyboard alternative to the canvas.
    setFocusProxy(pages_);
    connect(pages_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (syncing_ || !item)
            return;
        const auto id = item->data(0, IdRole).toULongLong();
        switch (item->data(0, KindRole).toInt()) {
        case PageRow:
            emit sheetChosen(id);
            break;
        case ViewRow:
            emit viewChosen(id);
            break;
        default:
            if (item->parent())
                emit sheetChosen(item->parent()->data(0, IdRole).toULongLong());
        }
    });
    connect(pages_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (syncing_ || column != 1 || item->data(0, KindRole).toInt() != PageRow)
            return;
        const auto sheet = item->data(0, IdRole).toULongLong();
        const auto found = doc_.drawingSheets().find(sheet);
        const auto name = item->text(1).trimmed();
        if (found == doc_.drawingSheets().end() ||
            name == QString::fromStdString(found->second->name))
            return;
        // Rejected names (empty or too long) are restored by the next refresh. The edit is
        // committed after the editor closes: the refresh rebuilds this tree.
        signature_.clear();
        QMetaObject::invokeMethod(
            this, [this, sheet, name] { emit renamePage(sheet, name); }, Qt::QueuedConnection);
    });
    pagesSection_ = new CollapsibleSection("Pages", pages_);
    pagesSection_->setObjectName("sheetPagesSection");
    layout->addWidget(pagesSection_);
    pagesSection_->setExpandedStretch(1);
    separator();

    history_ = new HistoryPanel(doc, viewport);
    history_->setObjectName("sheetHistoryPanel");
    historySection_ = new CollapsibleSection("History", history_, false);
    historySection_->setObjectName("sheetHistorySection");
    layout->addWidget(historySection_);
    historySection_->setExpandedStretch(1);
    connect(historySection_->header(), &QPushButton::toggled, this, [this](bool open) {
        if (open)
            history_->refresh(true);
    });
    layout->addStretch(0);
}
void SheetsTray::setHistoryFocusTarget(QWidget *target) { history_->setFocusTarget(target); }
void SheetsTray::setTheme(const ThemeColors &colors) {
    colors_ = colors;
    referencesBanner_->setStyleSheet(
        QString("#referencesBanner {background:%1;border:1px solid %2;border-radius:6px;}"
                "#referencesBanner QLabel {color:%3;background:transparent;}"
                "#referencesBanner QPushButton {background:%4;color:%5;border:1px solid %2;"
                "border-radius:6px;padding:2px 8px;}"
                "#referencesBanner QPushButton:focus {border:1px solid %6;}")
            .arg(colors.warningSurface.name(), colors.warningBorder.name(), colors.warning.name(),
                 colors.input.name(), colors.ink.name(), colors.accent.name()));
    referencesBannerIcon_->setPixmap(
        sheetStateIcon(DrawingViewState::Stale, false, colors, devicePixelRatioF()));
    signature_.clear();
    refresh(sheet_, view_);
}
QTreeWidgetItem *SheetsTray::pageItem(Id sheet) const {
    for (int i = 0; i < pages_->topLevelItemCount(); ++i)
        if (pages_->topLevelItem(i)->data(0, IdRole).toULongLong() == sheet)
            return pages_->topLevelItem(i);
    return nullptr;
}
QTreeWidgetItem *SheetsTray::currentPageItem() const {
    auto *item = pages_->currentItem();
    while (item && item->parent())
        item = item->parent();
    return item;
}
void SheetsTray::editPageName(Id sheet) {
    if (auto *item = pageItem(sheet)) {
        pagesSection_->setExpanded(true);
        pages_->setCurrentItem(item, 1);
        pages_->setFocus(Qt::ShortcutFocusReason);
        pages_->editItem(item, 1);
    }
}
bool SheetsTray::eventFilter(QObject *object, QEvent *event) {
    if (object == pages_ && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        auto *item = pages_->currentItem();
        const bool page = item && item->data(0, KindRole).toInt() == PageRow;
        const auto id = item ? item->data(0, IdRole).toULongLong() : 0;
        // Keys bubbling up from the in-place name editor stay with the editor.
        if (QApplication::focusWidget() != pages_)
            return false;
        if (page && key->key() == Qt::Key_F2 && key->modifiers() == Qt::NoModifier) {
            pages_->editItem(item, 1);
            return true;
        }
        if (page && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) &&
            key->modifiers() == Qt::AltModifier) {
            const int delta = key->key() == Qt::Key_Up ? -1 : 1;
            // Commands rebuild this tree; run them after the key event.
            QMetaObject::invokeMethod(
                this, [this, id, delta] { emit movePage(id, delta); }, Qt::QueuedConnection);
            return true;
        }
        if (page && key->key() == Qt::Key_Delete && key->modifiers() == Qt::NoModifier) {
            QMetaObject::invokeMethod(
                this, [this, id] { emit deletePage(id); }, Qt::QueuedConnection);
            return true;
        }
        if (item && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            key->modifiers() == Qt::NoModifier) {
            if (auto *top = currentPageItem())
                emit pageActivated(top->data(0, IdRole).toULongLong());
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}
void SheetsTray::refresh(Id currentSheet, Id selectedView) {
    sheet_ = currentSheet;
    view_ = selectedView;
    refreshInfo();
    refreshReferences();
    refreshPages();
    if (historySection_->expanded())
        history_->refresh();
}
void SheetsTray::refreshInfo() {
    const auto found = doc_.drawingViews().find(view_);
    const bool shown = found != doc_.drawingViews().end();
    infoSection_->setTitle(shown ? "Entity info · Model view" : "Entity info");
    infoEmpty_->setVisible(!shown);
    infoFields_->setVisible(shown);
    if (!shown)
        return;
    const auto &view = *found->second;
    const auto status = cache_.status(view.id);
    const auto evaluation = cache_.sheet(view.sheet);
    const bool pending = !cache_.current(view.sheet) || !status;
    infoTitle_->setText(QString::fromStdString(view.name));
    const auto scene = doc_.scenes().find(view.source.scene);
    const auto model = documentName_.toHtmlEscaped();
    infoSource_->setText(scene == doc_.scenes().end()
                             ? model + " › scene missing"
                             : model + " › scene <b>" +
                                   QString::fromStdString(scene->second->name).toHtmlEscaped() +
                                   "</b>");
    infoSource_->setAccessibleDescription(
        infoSource_->text().remove(QRegularExpression("<[^>]*>")));
    infoScale_->setText(viewScaleText(view, status && status->notToScale));
    infoStyle_->setText(view.content == DrawingViewContent::RasterAppearance
                            ? QString("Rendered appearance")
                        : view.hiddenLines ? QString("Hidden line · hidden edges dashed")
                                           : QString("Hidden line"));
    infoCrop_->setText("On · " + paperMillimetres(view.frame.widthMm) + " × " +
                       paperMillimetres(view.frame.heightMm) + " mm on paper");
    QString state;
    if (pending)
        state = status ? viewStateSentence(*status) + " · Updating…" : QString("Updating…");
    else if (status->state == DrawingViewState::Current)
        state = "Current · model revision " + QString::number(status->revision);
    else if (status->state == DrawingViewState::Failed)
        state =
            "Could not draw" +
            (status->detail.empty() ? QString{} : ": " + QString::fromStdString(status->detail));
    else
        state = viewStateSentence(*status);
    (void)evaluation;
    infoStatus_->setText(state);
    infoStatusIcon_->setPixmap(sheetStateIcon(status ? status->state : DrawingViewState::Current,
                                              !status, colors_, devicePixelRatioF()));
}
void SheetsTray::refreshReferences() {
    size_t views = doc_.drawingViews().size(), changed = 0;
    for (const auto &[id, view] : doc_.drawingViews()) {
        (void)view;
        if (const auto status = cache_.status(id);
            status && status->state == DrawingViewState::Stale)
            ++changed;
    }
    referencesModel_->setText("<b>" + documentName_.toHtmlEscaped() + "</b> · " +
                              QString::number(views) + (views == 1 ? " view" : " views"));
    referencesBanner_->setVisible(changed > 0);
    referencesBannerText_->setText(
        QString("%1 %2 changed since accepted").arg(changed).arg(changed == 1 ? "view" : "views"));
    acceptAll_->setAccessibleDescription(referencesBannerText_->text());
}
void SheetsTray::refreshPages() {
    // Rebuild only when something shown here changed.
    // Selection changes never rebuild: the tree may be inside its own item signal then.
    QString signature = QString::fromStdString(doc_.identity()) + ":" +
                        QString::number(doc_.revision()) + ":" +
                        QString::number(cache_.evaluations()) + ":" + documentName_ + ":" +
                        QString::number(colors_.dark);
    // Never rebuild under the in-place name editor; refresh once it closes.
    if (auto *focus = QApplication::focusWidget();
        focus && focus != pages_ && pages_->isAncestorOf(focus))
        return;
    QScopedValueRollback guard(syncing_, true);
    if (signature != signature_) {
        signature_ = signature;
        std::set<Id> expanded;
        for (int i = 0; i < pages_->topLevelItemCount(); ++i)
            if (pages_->topLevelItem(i)->isExpanded())
                expanded.insert(pages_->topLevelItem(i)->data(0, IdRole).toULongLong());
        const bool hadFocus = pages_->hasFocus();
        pages_->clear();
        const auto sheets = orderedDrawingSheets(doc_);
        for (const auto id : sheets) {
            const auto &sheet = *doc_.drawingSheets().at(id);
            auto *page = new QTreeWidgetItem(pages_);
            page->setData(0, KindRole, PageRow);
            page->setData(0, IdRole, QVariant::fromValue<qulonglong>(id));
            page->setText(0, QString::fromStdString(sheet.number));
            page->setText(1, QString::fromStdString(sheet.name));
            page->setFlags(page->flags() | Qt::ItemIsEditable);
            page->setForeground(0, colors_.muted);
            size_t changed = 0, broken = 0;
            int number = 0;
            for (const auto view : drawingViewsOnSheet(doc_, id)) {
                const auto &record = *doc_.drawingViews().at(view);
                const auto status = cache_.status(view);
                auto *row = new QTreeWidgetItem(page);
                row->setData(0, KindRole, ViewRow);
                row->setData(0, IdRole, QVariant::fromValue<qulonglong>(view));
                row->setText(0, QString::number(++number));
                row->setText(1, QString::fromStdString(record.name) + " · " +
                                    viewScaleText(record, status && status->notToScale));
                row->setText(2, status ? viewStateText(*status) : QString("Updating…"));
                if (status && status->state == DrawingViewState::Stale) {
                    ++changed;
                    row->setForeground(2, colors_.warning);
                } else if (status && (status->state == DrawingViewState::Broken ||
                                      status->state == DrawingViewState::Failed)) {
                    ++broken;
                    row->setForeground(2, colors_.failure);
                }
                row->setToolTip(1, row->text(1) + " · " + row->text(2));
            }
            const auto evaluation = cache_.sheet(id);
            for (const auto item : drawingItemsOnSheet(doc_, id)) {
                auto *row = new QTreeWidgetItem(page);
                row->setData(0, KindRole, ItemRow);
                row->setData(0, IdRole, QVariant::fromValue<qulonglong>(item));
                row->setText(1, itemSummary(*doc_.drawingItems().at(item)));
                if (evaluation)
                    for (const auto &prepared : evaluation->prepared.items)
                        if (prepared.record.id == item &&
                            prepared.evaluation.state == DrawingItemState::Broken) {
                            row->setText(2, "Broken: " +
                                                (prepared.evaluation.reasons.empty()
                                                     ? QString("reference missing")
                                                     : brokenReasonText(
                                                           prepared.evaluation.reasons.front())));
                            row->setForeground(2, colors_.failure);
                        }
            }
            QStringList state;
            if (changed)
                state << QString::number(changed) + " changed";
            if (broken)
                state << QString::number(broken) + " broken";
            page->setText(2, state.join(" · "));
            page->setForeground(2, broken ? colors_.failure : colors_.warning);
            page->setToolTip(1, sheetTitle(sheet) + (state.isEmpty() ? "" : " · " + page->text(2)));
            page->setExpanded(expanded.contains(id) || id == sheet_);
        }
        if (hadFocus && !pages_->hasFocus())
            pages_->setFocus(Qt::OtherFocusReason);
    }
    // Mirror the canvas: the selected view's row, else the current page's row. An item row the
    // user moved to on the current page stays current.
    auto *page = pageItem(sheet_);
    if (!page)
        return;
    QTreeWidgetItem *current = page;
    for (int i = 0; i < page->childCount(); ++i)
        if (view_ && page->child(i)->data(0, KindRole).toInt() == ViewRow &&
            page->child(i)->data(0, IdRole).toULongLong() == view_)
            current = page->child(i);
    auto *now = pages_->currentItem();
    if (!view_ && now && now->parent() == page && now->data(0, KindRole).toInt() == ItemRow)
        return;
    if (now != current) {
        if (current->parent())
            current->parent()->setExpanded(true);
        pages_->setCurrentItem(current, 1);
        pages_->scrollToItem(current);
    }
}
} // namespace sketchy
