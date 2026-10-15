#include "app/sheets_tray.hpp"
#include "app/history_panel.hpp"
#include "app/sheet_text.hpp"
#include "core/drawing_item_records.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include <QApplication>
#include <QGroupBox>
#include <QLocale>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QStyle>
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
    auto field = [&](const QString &name, QWidget *value) {
        auto *caption = new QLabel(name);
        caption->setObjectName("sheetInfoCaption");
        caption->setBuddy(value);
        grid->addWidget(caption, row, 0, Qt::AlignTop | Qt::AlignLeft);
        grid->addWidget(value, row, 1);
        ++row;
        return value;
    };
    // Title, scale and style are editable; each change is one history entry (R091.f).
    infoTitle_ = new QLineEdit;
    infoTitle_->setObjectName("sheetInfoTitle");
    infoTitle_->setAccessibleName("View title");
    infoTitle_->setMaxLength(1024);
    field("Title", infoTitle_);
    connect(infoTitle_, &QLineEdit::editingFinished, this, &SheetsTray::commitTitle);
    infoSource_ = valueLabel("Source");
    infoSource_->setTextFormat(Qt::RichText);
    infoSource_->setObjectName("sheetInfoSource");
    field("Source", infoSource_);
    infoScale_ = new QComboBox;
    infoScale_->setObjectName("sheetInfoScale");
    infoScale_->setAccessibleName("Scale");
    infoScale_->setEditable(true);
    infoScale_->setInsertPolicy(QComboBox::NoInsert);
    for (const auto scale : sheetScaleChoices())
        infoScale_->addItem("1:" + QString::number(scale, 'g', 10));
    infoScale_->lineEdit()->setAccessibleName("Scale");
    infoScale_->setToolTip("Choose a scale or type 1:N");
    field("Scale", infoScale_);
    connect(infoScale_, &QComboBox::activated, this, [this] { commitScale(); });
    connect(infoScale_->lineEdit(), &QLineEdit::editingFinished, this, &SheetsTray::commitScale);
    infoStyle_ = new QComboBox;
    infoStyle_->setObjectName("sheetInfoStyle");
    infoStyle_->setAccessibleName("Style");
    infoStyle_->addItem("Hidden line", QVariantList{0, false});
    infoStyle_->addItem("Hidden line · hidden edges dashed", QVariantList{0, true});
    infoStyle_->addItem("Rendered appearance", QVariantList{1, false});
    field("Style", infoStyle_);
    connect(infoStyle_, &QComboBox::activated, this, [this](int index) {
        if (syncing_ || !doc_.drawingViews().contains(view_))
            return;
        const auto data = infoStyle_->itemData(index).toList();
        const auto &view = *doc_.drawingViews().at(view_);
        const int content = data.value(0).toInt();
        const bool hidden = data.value(1).toBool();
        if (int(view.content) == content && view.hiddenLines == hidden)
            return;
        emit setViewStyle(view_, content, hidden);
    });
    infoCrop_ = valueLabel("Crop");
    infoCrop_->setObjectName("sheetInfoCrop");
    field("Crop", infoCrop_);
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
    // Edit view (Enter on the canvas), Show scene in model and, for a broken view, Choose
    // scene… The buttons wrap onto lines of their own in the narrow Tray.
    auto *actions = new QWidget;
    actions->setObjectName("sheetInfoActions");
    auto *actionsLayout = new QVBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 4, 0, 0);
    actionsLayout->setSpacing(6);
    infoEdit_ = new QPushButton("Edit view  Enter");
    infoEdit_->setObjectName("sheetInfoEditView");
    infoEdit_->setAccessibleName("Edit view");
    infoEdit_->setToolTip("Pan and zoom inside the frame (Enter on the page)");
    infoShowScene_ = new QPushButton("Show scene in model");
    infoShowScene_->setObjectName("sheetInfoShowScene");
    infoShowScene_->setToolTip("Switch to Model and show the scene's camera");
    infoChooseScene_ = new QPushButton("Choose scene…");
    infoChooseScene_->setObjectName("sheetInfoChooseScene");
    infoChooseScene_->setToolTip("Link this view to another scene; its frame and scale stay");
    for (auto *button : {infoEdit_, infoShowScene_, infoChooseScene_}) {
        button->setAutoDefault(false);
        actionsLayout->addWidget(button);
    }
    connect(infoEdit_, &QPushButton::clicked, this, [this] { emit editViewRequested(); });
    connect(infoShowScene_, &QPushButton::clicked, this,
            [this] { emit showSceneRequested(view_); });
    connect(infoChooseScene_, &QPushButton::clicked, this,
            [this] { emit chooseSceneRequested(view_); });
    infoLayout->addWidget(actions);
    place_ = new SheetPlacePanel(doc);
    place_->hide();
    layout->addWidget(place_, 1);
    infoSection_ = new CollapsibleSection("Entity info", info);
    infoSection_->setObjectName("sheetInfoSection");
    // Its fields never shrink below their height; Pages and History scroll instead.
    layout->addWidget(infoSection_);
    connect(infoSection_->header(), &QPushButton::toggled, this, [this] { fitInfo(); });
    sections_.push_back(infoSection_);
    auto separator = [&] {
        auto *line = new QFrame;
        line->setObjectName("sheetTraySeparator");
        line->setFixedHeight(1);
        layout->addWidget(line);
        sections_.push_back(line);
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
    sections_.push_back(referencesSection_);
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
    connect(pages_->itemDelegate(), &QAbstractItemDelegate::closeEditor, this,
            [this] { QTimer::singleShot(0, this, [this] { refresh(sheet_, view_); }); });
    // F6 into the Tray lands in Pages, the keyboard alternative to the canvas (focusInEvent).
    // Not a focus proxy: Tab would then jump over Entity info and References to Pages.
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
    sections_.push_back(pagesSection_);
    pagesSection_->setExpandedStretch(1);
    separator();

    history_ = new HistoryPanel(doc, viewport);
    history_->setObjectName("sheetHistoryPanel");
    historySection_ = new CollapsibleSection("History", history_, false);
    historySection_->setObjectName("sheetHistorySection");
    layout->addWidget(historySection_);
    sections_.push_back(historySection_);
    historySection_->setExpandedStretch(1);
    connect(historySection_->header(), &QPushButton::toggled, this, [this](bool open) {
        if (open)
            history_->refresh(true);
    });
    layout->addStretch(0);
}
void SheetsTray::setHistoryFocusTarget(QWidget *target) { history_->setFocusTarget(target); }
void SheetsTray::setPlacing(bool placing) {
    if (placing == placing_)
        return;
    placing_ = placing;
    const bool hadFocus = isAncestorOf(QApplication::focusWidget());
    for (auto *section : sections_)
        section->setVisible(!placing);
    place_->setVisible(placing);
    // F6 into the Tray lands in the panel while it shows, else in Pages.
    if (hadFocus)
        focusInEvent(nullptr);
}
void SheetsTray::commitTitle() {
    if (syncing_ || !doc_.drawingViews().contains(view_))
        return;
    const auto name = infoTitle_->text().trimmed();
    const auto &view = *doc_.drawingViews().at(view_);
    if (name == QString::fromStdString(view.name))
        return;
    // Synchronous, so the editingFinished that follows on focus loss sees the new name.
    emit renameView(view_, name);
}
void SheetsTray::commitScale() {
    if (syncing_ || !doc_.drawingViews().contains(view_) || !infoScale_->isEnabled())
        return;
    const auto &view = *doc_.drawingViews().at(view_);
    const auto value = parseSheetScale(infoScale_->currentText());
    if (!value) {
        // Not a scale: show the view's scale again.
        QSignalBlocker blocker(infoScale_);
        infoScale_->setEditText(viewScaleText(view, false));
        return;
    }
    if (*value == view.scaleDenominator)
        return;
    emit setViewScale(view_, *value);
}
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
void SheetsTray::focusInEvent(QFocusEvent *) {
    if (placing_)
        place_->findChild<QComboBox *>("sheetPlaceModel")->setFocus(Qt::OtherFocusReason);
    else
        pages_->setFocus(Qt::OtherFocusReason);
}
void SheetsTray::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    fitInfo();
}
void SheetsTray::fitInfo() {
    const auto width = infoSection_->width();
    auto *layout = infoSection_->layout();
    if (width <= 0 || !layout)
        return;
    // Wrapped values report one line to the grid; reserve the lines they wrap to.
    for (auto *label : infoFields_->findChildren<QLabel *>())
        if (label->wordWrap() && label->width() > 0)
            label->setMinimumHeight(label->heightForWidth(label->width()));
    layout->invalidate();
    infoSection_->setMinimumHeight(layout->hasHeightForWidth() ? layout->heightForWidth(width)
                                                                : layout->sizeHint().height());
}
void SheetsTray::refresh(Id currentSheet, Id selectedView) {
    sheet_ = currentSheet;
    view_ = selectedView;
    refreshInfo();
    fitInfo();
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
    QScopedValueRollback guard(syncing_, true);
    // Never replace what the user is typing.
    if (!infoTitle_->hasFocus() || infoTitle_->property("view").toULongLong() != view.id)
        infoTitle_->setText(QString::fromStdString(view.name));
    infoTitle_->setProperty("view", QVariant::fromValue<qulonglong>(view.id));
    const auto scene = doc_.scenes().find(view.source.scene);
    const auto model = documentName_.toHtmlEscaped();
    infoSource_->setText(scene == doc_.scenes().end()
                             ? model + " › scene missing"
                             : model + " › scene <b>" +
                                   QString::fromStdString(scene->second->name).toHtmlEscaped() +
                                   "</b>");
    infoSource_->setAccessibleDescription(
        infoSource_->text().remove(QRegularExpression("<[^>]*>")));
    {
        const bool perspective = status && status->notToScale;
        QSignalBlocker blocker(infoScale_);
        infoScale_->setEnabled(!perspective);
        if (!infoScale_->lineEdit()->hasFocus() || perspective)
            infoScale_->setEditText(viewScaleText(view, perspective));
        infoScale_->setAccessibleDescription(perspective ? "Perspective views are not to scale"
                                                         : QString{});
        QSignalBlocker styleBlocker(infoStyle_);
        infoStyle_->setCurrentIndex(view.content == DrawingViewContent::RasterAppearance ? 2
                                    : view.hiddenLines                                 ? 1
                                                                                       : 0);
    }
    const bool broken = status && (status->state == DrawingViewState::Broken ||
                                   status->state == DrawingViewState::Failed);
    infoEdit_->setVisible(!broken);
    infoEdit_->setEnabled(!(status && status->notToScale));
    infoShowScene_->setEnabled(scene != doc_.scenes().end() && scene->second->snapshot.camera);
    infoChooseScene_->setVisible(broken && status->state == DrawingViewState::Broken);
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
const std::vector<double> &sheetScaleChoices() {
    static const std::vector<double> scales{1,   2,   5,   10,  20,   25,   50,
                                            100, 200, 250, 500, 1000, 1250, 2500};
    return scales;
}
std::optional<double> parseSheetScale(const QString &text) {
    const auto point = QRegularExpression::escape(QLocale().decimalPoint());
    const QRegularExpression pattern(R"(^\s*(?:1\s*:\s*)?([0-9]+(?:)" + point +
                                     R"([0-9]+)?)\s*(?:\(.*\))?\s*$)");
    const auto match = pattern.match(text);
    if (!match.hasMatch())
        return std::nullopt;
    bool ok = false;
    const auto value = QLocale().toDouble(match.captured(1), &ok);
    if (!ok || !(value >= .001 && value <= 1e9))
        return std::nullopt;
    return value;
}
QString sceneProjection(const Document &doc, Id scene) {
    const auto found = doc.scenes().find(scene);
    if (found == doc.scenes().end() || !found->second->snapshot.camera)
        return "No camera";
    const auto &snapshot = found->second->snapshot;
    const auto &camera = *snapshot.camera;
    if (!camera.orthographic)
        return "Perspective";
    if (snapshot.section && snapshot.section->plane)
        return "Section";
    if (camera.pitch >= 85)
        return "Top";
    if (camera.pitch <= -85)
        return "Bottom";
    if (std::abs(camera.pitch) <= 5) {
        // The standard views: Front looks from -Y (yaw -90), Right from +X (yaw 0), Back from
        // +Y (yaw 90), Left from -X (yaw 180).
        const auto yaw = std::remainder(camera.yaw, 360.);
        if (std::abs(yaw + 90) <= 5)
            return "Front";
        if (std::abs(yaw) <= 5)
            return "Right";
        if (std::abs(yaw - 90) <= 5)
            return "Back";
        if (std::abs(std::abs(yaw) - 180) <= 5)
            return "Left";
    }
    return "Parallel";
}
SheetPlacePanel::SheetPlacePanel(const Document &doc, QWidget *parent)
    : QWidget(parent), doc_(doc) {
    setObjectName("sheetPlacePanel");
    setAccessibleName("New model view");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(12);
    auto *heading = new QLabel("New model view");
    heading->setObjectName("sheetPlaceHeading");
    layout->addWidget(heading);
    auto *modelCaption = new QLabel("Model");
    modelCaption->setObjectName("sheetInfoCaption");
    model_ = new QComboBox;
    model_->setObjectName("sheetPlaceModel");
    model_->setAccessibleName("Model");
    modelCaption->setBuddy(model_);
    auto *modelBox = new QVBoxLayout;
    modelBox->setSpacing(4);
    modelBox->addWidget(modelCaption);
    modelBox->addWidget(model_);
    layout->addLayout(modelBox);
    auto *sceneBox = new QGroupBox("Scene");
    sceneBox->setObjectName("sheetPlaceScenes");
    sceneList_ = new QWidget;
    auto *sceneLayout = new QVBoxLayout(sceneBox);
    sceneLayout->setContentsMargins(8, 4, 8, 6);
    sceneLayout->addWidget(sceneList_);
    new QVBoxLayout(sceneList_);
    sceneList_->layout()->setContentsMargins(0, 0, 0, 0);
    sceneList_->layout()->setSpacing(4);
    scenes_ = new QButtonGroup(this);
    scenes_->setExclusive(true);
    connect(scenes_, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        // The chosen scene's row is highlighted, as in the design.
        if (auto *button = scenes_->button(id); button && button->parentWidget()) {
            auto *row = button->parentWidget();
            row->setProperty("chosen", checked);
            row->style()->unpolish(row);
            row->style()->polish(row);
        }
        if (checked) {
            refreshDetails();
            emit placementChanged();
        }
    });
    empty_ = new QLabel("This model has no saved scenes. Save a scene in the Model workspace "
                        "to place it as a view.");
    empty_->setObjectName("sheetInfoEmpty");
    empty_->setWordWrap(true);
    sceneLayout->addWidget(empty_);
    layout->addWidget(sceneBox);
    auto *details = new QGridLayout;
    details->setHorizontalSpacing(8);
    details->setVerticalSpacing(8);
    details->setColumnStretch(1, 1);
    auto caption = [&](const QString &text, QWidget *buddy, int row) {
        auto *label = new QLabel(text);
        label->setObjectName("sheetInfoCaption");
        label->setBuddy(buddy);
        details->addWidget(label, row, 0);
        details->addWidget(buddy, row, 1);
    };
    scale_ = new QComboBox;
    scale_->setObjectName("sheetPlaceScale");
    scale_->setAccessibleName("Scale");
    {
        // Above its select, like Model, so "1:50 (matches this page)" fits the Tray.
        auto *label = new QLabel("Scale");
        label->setObjectName("sheetInfoCaption");
        label->setBuddy(scale_);
        details->addWidget(label, 0, 0, 1, 2);
        details->addWidget(scale_, 1, 0, 1, 2);
    }
    connect(scale_, &QComboBox::currentIndexChanged, this, [this] { emit placementChanged(); });
    connect(scale_, &QComboBox::activated, this, [this] { scale_->setProperty("userChose", true); });
    style_ = new QLabel;
    style_->setObjectName("sheetPlaceStyle");
    style_->setWordWrap(true);
    caption("Style", style_, 2);
    title_ = new QLabel;
    title_->setObjectName("sheetPlaceTitle");
    title_->setWordWrap(true);
    caption("Title", title_, 3);
    layout->addLayout(details);
    auto *note = new QLabel("Perspective scenes have no measurable scale. They place as “Not to "
                            "scale”, and Dimension refuses to measure them.");
    note->setObjectName("sheetInfoEmpty");
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    place_ = new QPushButton("Place · Enter");
    place_->setObjectName("sheetPlace");
    place_->setAccessibleName("Place the view");
    place_->setToolTip("Place the view at its natural size, or the frame you drew (Enter)");
    place_->setAutoDefault(false);
    cancel_ = new QPushButton("Cancel · Esc");
    cancel_->setObjectName("sheetPlaceCancel");
    cancel_->setAccessibleName("Cancel placing a view");
    cancel_->setToolTip("Cancel the frame, or return to Select (Esc)");
    cancel_->setAutoDefault(false);
    buttons->addWidget(place_, 1);
    buttons->addWidget(cancel_);
    layout->addLayout(buttons);
    connect(place_, &QPushButton::clicked, this, [this] { emit placeRequested(); });
    connect(cancel_, &QPushButton::clicked, this, [this] { emit cancelRequested(); });
}
Id SheetPlacePanel::scene() const {
    const auto id = scenes_->checkedId();
    return id > 0 ? Id(id) : Id{};
}
void SheetPlacePanel::setScene(Id scene) {
    if (auto *button = scenes_->button(int(scene)); button && button->isEnabled())
        button->setChecked(true);
}
double SheetPlacePanel::scale() const {
    return scale_->currentData().isValid() ? scale_->currentData().toDouble() : 50;
}
void SheetPlacePanel::refresh(const QString &documentName, Id sheet, double pageScale,
                              bool pageHasScale) {
    sheet_ = sheet;
    {
        QSignalBlocker blocker(model_);
        model_->clear();
        // External model files are not offered until that feature exists (decision 10).
        model_->addItem(documentName + " (this model)");
    }
    const auto order = orderedScenes(doc_);
    const auto chosen = scene();
    bool rebuild = order != order_;
    for (size_t i = 0; !rebuild && i < order.size(); ++i)
        if (auto *button = scenes_->button(int(order[i])))
            rebuild = button->text() != QString::fromStdString(doc_.scenes().at(order[i])->name) ||
                      button->property("projection").toString() !=
                          sceneProjection(doc_, order[i]);
    if (rebuild) {
        QSignalBlocker blocker(scenes_);
        const bool hadFocus = sceneList_->isAncestorOf(QApplication::focusWidget());
        for (auto *button : scenes_->buttons())
            scenes_->removeButton(button);
        while (auto *item = sceneList_->layout()->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        order_ = order;
        for (const auto id : order) {
            const auto name = QString::fromStdString(doc_.scenes().at(id)->name);
            const auto projection = sceneProjection(doc_, id);
            auto *row = new QWidget;
            row->setObjectName("sheetPlaceSceneRow");
            row->setAttribute(Qt::WA_StyledBackground);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);
            rowLayout->setSpacing(8);
            auto *radio = new QRadioButton(name);
            radio->setObjectName("sheetPlaceScene");
            radio->setProperty("projection", projection);
            radio->setAccessibleName(name + ", " + projection);
            radio->setEnabled(projection != "No camera");
            auto *kind = new QLabel(projection);
            kind->setObjectName("sheetInfoCaption");
            rowLayout->addWidget(radio, 1);
            rowLayout->addWidget(kind);
            sceneList_->layout()->addWidget(row);
            scenes_->addButton(radio, int(id));
        }
        Id keep = chosen && doc_.scenes().contains(chosen) ? chosen : Id{};
        if (!keep)
            for (const auto id : order)
                if (scenes_->button(int(id))->isEnabled()) {
                    keep = id;
                    break;
                }
        if (keep) {
            // Signals are blocked while rebuilding; mark the chosen row by hand.
            scenes_->button(int(keep))->setChecked(true);
            auto *row = scenes_->button(int(keep))->parentWidget();
            row->setProperty("chosen", true);
            row->style()->unpolish(row);
            row->style()->polish(row);
        }
        if (hadFocus && scenes_->checkedButton())
            scenes_->checkedButton()->setFocus(Qt::OtherFocusReason);
    }
    empty_->setVisible(order.empty());
    {
        // The page's scale first ("matches this page"), then the common scales.
        QSignalBlocker blocker(scale_);
        const auto previous = scale_->count() ? scale_->currentData().toDouble() : 0.;
        const bool keepPrevious = scale_->property("userChose").toBool() && previous > 0;
        scale_->clear();
        auto scales = sheetScaleChoices();
        if (std::find(scales.begin(), scales.end(), pageScale) == scales.end())
            scales.push_back(pageScale);
        std::sort(scales.begin(), scales.end());
        for (const auto value : scales)
            scale_->addItem("1:" + QString::number(value, 'g', 10) +
                                (pageHasScale && value == pageScale ? " (matches this page)"
                                                                    : QString{}),
                            value);
        const auto wanted = keepPrevious ? previous : pageScale;
        scale_->setCurrentIndex(std::max(0, scale_->findData(wanted)));
    }
    refreshDetails();
}
void SheetPlacePanel::refreshDetails() {
    const auto id = scene();
    const bool perspective = sceneProjection(doc_, id) == "Perspective";
    scale_->setEnabled(id && !perspective);
    scale_->setToolTip(perspective ? "Perspective views are not to scale" : QString{});
    style_->setText(perspective ? "From scene · Rendered" : "From scene · Hidden line");
    place_->setEnabled(id != 0);
    if (!id) {
        title_->setText("—");
        return;
    }
    std::set<std::string> names;
    for (const auto &[view, record] : doc_.drawingViews())
        names.insert(record->name);
    const auto base = QString::fromStdString(doc_.scenes().at(id)->name);
    auto name = base;
    for (int n = 2; names.contains(name.toStdString()); ++n)
        name = base + " " + QString::number(n);
    const auto number = drawingViewsOnSheet(doc_, sheet_).size() + 1;
    title_->setText(QString::number(number) + " · " + name);
}
} // namespace sketchy
