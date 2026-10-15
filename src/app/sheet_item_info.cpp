#include "app/sheet_item_info.hpp"
#include "app/sheet_items.hpp"
#include "app/sheet_text.hpp"
#include "app/sheets_tray.hpp"
#include "io/drawing_sheet_paint.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <cmath>
namespace sketchy {
namespace {
QLabel *valueLabel(const QString &name) {
    auto *label = new QLabel;
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setAccessibleName(name);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
QString millimetres(double value) { return paperMillimetres(std::round(value * 10) / 10); }
QJsonObject fontJson(const QString &family, double size) {
    return {{"family", family}, {"sizePt", size}};
}
const DrawingFont *fontOf(const DrawingItemRecord &record) {
    return std::visit(
        [](const auto &data) -> const DrawingFont * {
            if constexpr (requires { data.font; })
                return &data.font;
            else
                return nullptr;
        },
        record.data);
}
} // namespace
SheetItemInfo::SheetItemInfo(const Document &doc, QWidget *parent) : QWidget(parent), doc_(doc) {
    setObjectName("sheetItemInfo");
    grid_ = new QGridLayout(this);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(8);
    grid_->setVerticalSpacing(6);
    grid_->setColumnStretch(1, 1);
    text_ = new QPlainTextEdit;
    text_->setObjectName("sheetItemText");
    text_->setAccessibleName("Text");
    text_->setTabChangesFocus(true);
    text_->setFixedHeight(64);
    text_->installEventFilter(this);
    addRow("Text", text_);
    value_ = valueLabel("Value");
    value_->setObjectName("sheetItemValue");
    addRow("Value", value_);
    view_ = valueLabel("View");
    view_->setObjectName("sheetItemView");
    addRow("View", view_);
    target_ = valueLabel("Points to");
    target_->setObjectName("sheetItemTarget");
    addRow("Points to", target_);
    orientation_ = new QComboBox;
    orientation_->setObjectName("sheetItemOrientation");
    orientation_->setAccessibleName("Orientation");
    orientation_->addItem("Aligned", "aligned");
    orientation_->addItem("Horizontal", "horizontal");
    orientation_->addItem("Vertical", "vertical");
    addRow("Orientation", orientation_);
    connect(orientation_, &QComboBox::activated, this, [this] {
        emitChange({{"orientation", orientation_->currentData().toString()}}, "Change orientation");
    });
    offset_ = new QDoubleSpinBox;
    offset_->setObjectName("sheetItemOffset");
    offset_->setAccessibleName("Offset");
    offset_->setRange(-1000, 1000);
    offset_->setDecimals(1);
    offset_->setSuffix(" mm");
    offset_->setKeyboardTracking(false);
    addRow("Offset", offset_);
    connect(offset_, &QDoubleSpinBox::editingFinished, this, [this] {
        const auto *item = record();
        if (const auto *d = item ? std::get_if<DrawingDimensionItem>(&item->data) : nullptr;
            d && std::abs(d->offsetMm - offset_->value()) > 1e-9)
            emitChange({{"offsetMm", offset_->value()}}, "Move dimension");
    });
    prefix_ = new QLineEdit;
    prefix_->setObjectName("sheetItemPrefix");
    prefix_->setAccessibleName("Prefix");
    prefix_->setMaxLength(int(drawingItemShortTextLimit));
    addRow("Prefix", prefix_);
    connect(prefix_, &QLineEdit::editingFinished, this, [this] {
        const auto *item = record();
        if (const auto *d = item ? std::get_if<DrawingDimensionItem>(&item->data) : nullptr;
            d && prefix_->text().toStdString() != d->prefix)
            emitChange({{"prefix", prefix_->text()}}, "Change prefix");
    });
    font_ = new QComboBox;
    font_->setObjectName("sheetItemFont");
    font_->setAccessibleName("Font");
    font_->setEditable(true);
    font_->setInsertPolicy(QComboBox::NoInsert);
    // A short list of the export face's family; any installed family can be typed.
    for (const auto *family : {"DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono"})
        font_->addItem(family);
    auto *completer = new QCompleter(QFontDatabase::families(), font_);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    font_->setCompleter(completer);
    font_->lineEdit()->setAccessibleName("Font");
    addRow("Font", font_);
    connect(font_, &QComboBox::activated, this, [this] { commitFont(); });
    connect(font_->lineEdit(), &QLineEdit::editingFinished, this, [this] { commitFont(); });
    size_ = new QDoubleSpinBox;
    size_->setObjectName("sheetItemSize");
    size_->setAccessibleName("Text size");
    size_->setRange(drawingFontMinPt, drawingFontMaxPt);
    size_->setDecimals(1);
    size_->setSuffix(" pt");
    size_->setKeyboardTracking(false);
    addRow("Size", size_);
    connect(size_, &QDoubleSpinBox::editingFinished, this, [this] {
        const auto *item = record();
        const auto *font = item ? fontOf(*item) : nullptr;
        if (font && std::abs(font->sizePt - size_->value()) > 1e-9)
            emitChange({{"font", fontJson(QString::fromStdString(font->family), size_->value())}},
                       "Change text size");
    });
    align_ = new QComboBox;
    align_->setObjectName("sheetItemAlign");
    align_->setAccessibleName("Alignment");
    align_->addItem("Left", "left");
    align_->addItem("Centre", "center");
    align_->addItem("Right", "right");
    addRow("Align", align_);
    connect(align_, &QComboBox::activated, this, [this] {
        emitChange({{"align", align_->currentData().toString()}}, "Change alignment");
    });
    leader_ = new QCheckBox("Show leader");
    leader_->setObjectName("sheetItemLeader");
    leader_->setAccessibleName("Leader");
    addRow("Leader", leader_);
    connect(leader_, &QCheckBox::clicked, this,
            [this](bool on) { emitChange({{"leader", on}}, "Change leader"); });
    stroke_ = new QDoubleSpinBox;
    stroke_->setObjectName("sheetItemStroke");
    stroke_->setAccessibleName("Stroke");
    stroke_->setRange(drawingStrokeMinMm, drawingStrokeMaxMm);
    stroke_->setDecimals(2);
    stroke_->setSingleStep(.05);
    stroke_->setSuffix(" mm");
    stroke_->setKeyboardTracking(false);
    addRow("Stroke", stroke_);
    connect(stroke_, &QDoubleSpinBox::editingFinished, this, [this] {
        const auto *item = record();
        double stroke = -1;
        if (const auto *line = item ? std::get_if<DrawingLineItem>(&item->data) : nullptr)
            stroke = line->strokeMm;
        else if (const auto *r = item ? std::get_if<DrawingRectangleItem>(&item->data) : nullptr)
            stroke = r->strokeMm;
        if (stroke >= 0 && std::abs(stroke - stroke_->value()) > 1e-9)
            emitChange({{"strokeMm", stroke_->value()}}, "Change stroke");
    });
    dash_ = new QComboBox;
    dash_->setObjectName("sheetItemDash");
    dash_->setAccessibleName("Line style");
    dash_->addItem("Solid", "solid");
    dash_->addItem("Dashed", "dashed");
    addRow("Style", dash_);
    connect(dash_, &QComboBox::activated, this, [this] {
        emitChange({{"dash", dash_->currentData().toString()}}, "Change line style");
    });
    geometry_ = valueLabel("Position");
    geometry_->setObjectName("sheetItemGeometry");
    addRow("Position", geometry_);
    auto titleField = [&](QLineEdit *&edit, const char *name, const char *caption,
                          const char *field) {
        edit = new QLineEdit;
        edit->setObjectName(QString("sheetItemTitle_") + field);
        edit->setAccessibleName(name);
        edit->setMaxLength(int(drawingItemShortTextLimit));
        addRow(caption, edit);
        connect(edit, &QLineEdit::editingFinished, this,
                [this, edit, field] { commitTitle(edit, field); });
    };
    titleField(project_, "Project", "Project", "project");
    titleField(drawing_, "Drawing title", "Drawing", "drawingTitle");
    titleField(date_, "Date", "Date", "date");
    titleField(revision_, "Revision", "Revision", "revision");
    number_ = new QLineEdit;
    number_->setObjectName("sheetItemSheetNumber");
    number_->setAccessibleName("Sheet number");
    number_->setMaxLength(int(drawingSheetNumberLimit));
    number_->setToolTip("The page's number; every title block on the page shows it");
    addRow("Sheet", number_);
    connect(number_, &QLineEdit::editingFinished, this, [this] {
        const auto *item = record();
        if (syncing_ || !item)
            return;
        const auto &sheet = *doc_.drawingSheets().at(item->sheet);
        if (number_->text().trimmed().toStdString() != sheet.number)
            emit changeSheetNumber(item->sheet, number_->text().trimmed());
    });
    page_ = valueLabel("Page");
    page_->setObjectName("sheetItemPage");
    addRow("Page", page_);
    scale_ = valueLabel("Scale");
    scale_->setObjectName("sheetItemScale");
    addRow("Scale", scale_);
    stateRow_ = new QWidget;
    auto *stateLayout = new QHBoxLayout(stateRow_);
    stateLayout->setContentsMargins(0, 0, 0, 0);
    stateLayout->setSpacing(6);
    stateIcon_ = new QLabel;
    stateIcon_->setFixedSize(12, 12);
    stateLayout->addWidget(stateIcon_);
    state_ = valueLabel("Status");
    state_->setObjectName("sheetItemStatus");
    stateLayout->addWidget(state_, 1);
    addRow("Status", stateRow_);
    rows_.back().caption->setBuddy(state_);
    reattach_ = new QPushButton("Reattach");
    reattach_->setObjectName("sheetItemReattach");
    reattach_->setAccessibleName("Reattach");
    reattach_->setAutoDefault(false);
    reattach_->setToolTip("Attach the broken point to the nearest vertex or edge in the view; "
                          "you confirm on the page");
    grid_->addWidget(reattach_, int(rows_.size()), 0, 1, 2);
    connect(reattach_, &QPushButton::clicked, this, [this] { emit reattachRequested(item_); });
    refresh(0, std::nullopt, {});
}
QWidget *SheetItemInfo::addRow(const QString &caption, QWidget *field) {
    auto *label = new QLabel(caption);
    label->setObjectName("sheetInfoCaption");
    label->setBuddy(field);
    const auto row = int(rows_.size());
    grid_->addWidget(label, row, 0, Qt::AlignTop | Qt::AlignLeft);
    grid_->addWidget(field, row, 1);
    rows_.push_back({label, field});
    return field;
}
void SheetItemInfo::show(QWidget *field, bool visible) {
    if (visible)
        shown_.insert(field);
    else
        shown_.erase(field);
}
void SheetItemInfo::applyRows() {
    // Only rows whose visibility changes are touched: hiding the focused field would move focus.
    for (const auto &row : rows_) {
        const bool visible = shown_.contains(row.field);
        if (row.field->isVisibleTo(this) != visible) {
            row.caption->setVisible(visible);
            row.field->setVisible(visible);
        }
    }
}
const DrawingItemRecord *SheetItemInfo::record() const {
    const auto found = doc_.drawingItems().find(item_);
    return found == doc_.drawingItems().end() ? nullptr : found->second.get();
}
QString SheetItemInfo::title() const {
    const auto *item = record();
    return item ? "Entity info · " + sheetItemKindName(item->kind()) : QString("Entity info");
}
void SheetItemInfo::setTheme(const ThemeColors &colors) { colors_ = colors; }
void SheetItemInfo::emitChange(const QJsonObject &data, const QString &label) {
    const auto *item = record();
    if (syncing_ || !item)
        return;
    emit changeItem(item_, sheetItemKindCode(item->kind()), data, label);
}
void SheetItemInfo::commitText() {
    const auto *item = record();
    if (syncing_ || !item)
        return;
    std::string old;
    if (const auto *t = std::get_if<DrawingTextItem>(&item->data))
        old = t->text;
    else if (const auto *l = std::get_if<DrawingLabelItem>(&item->data))
        old = l->text;
    else
        return;
    const auto text = text_->toPlainText();
    if (text.toStdString() == old)
        return;
    emitChange({{"text", text}},
               item->kind() == DrawingItemKind::Text ? "Edit text" : "Edit label");
}
void SheetItemInfo::commitFont() {
    const auto *item = record();
    const auto *font = item ? fontOf(*item) : nullptr;
    const auto family = font_->currentText().trimmed();
    if (!font || family.isEmpty() || family.toStdString() == font->family)
        return;
    emitChange({{"font", fontJson(family, font->sizePt)}}, "Change font");
}
void SheetItemInfo::commitTitle(QLineEdit *edit, const char *field) {
    const auto *item = record();
    const auto *block = item ? std::get_if<DrawingTitleBlockItem>(&item->data) : nullptr;
    if (!block)
        return;
    const QString name(field);
    const auto &fields = block->fields;
    const auto &old = name == "project"        ? fields.project
                      : name == "drawingTitle" ? fields.drawingTitle
                      : name == "date"         ? fields.date
                                               : fields.revision;
    if (edit->text().toStdString() == old)
        return;
    emitChange({{"fields", QJsonObject{{name, edit->text()}}}}, "Edit title block");
}
bool SheetItemInfo::eventFilter(QObject *object, QEvent *event) {
    if (object == text_ && event->type() == QEvent::FocusOut)
        commitText();
    return QWidget::eventFilter(object, event);
}
void SheetItemInfo::refresh(Id item, const std::optional<DrawingItemEvaluation> &evaluation,
                            const QString &viewName) {
    item_ = item;
    const auto *record = this->record();
    QScopedValueRollback guard(syncing_, true);
    shown_.clear();
    if (!record) {
        reattach_->hide();
        applyRows();
        return;
    }
    const auto kind = record->kind();
    // Never replace what the user is typing in the field for this item.
    auto keep = [&](QWidget *field) {
        return field->hasFocus() && field->property("item").toULongLong() == item;
    };
    auto setLine = [&](QLineEdit *edit, const std::string &value) {
        if (!keep(edit))
            edit->setText(QString::fromStdString(value));
        edit->setProperty("item", QVariant::fromValue<qulonglong>(item));
        show(edit, true);
    };
    if (const auto *font = fontOf(*record)) {
        if (!keep(font_) && !font_->lineEdit()->hasFocus())
            font_->setEditText(QString::fromStdString(font->family));
        font_->setProperty("item", QVariant::fromValue<qulonglong>(item));
        if (!keep(size_)) {
            QSignalBlocker blocker(size_);
            size_->setValue(font->sizePt);
        }
        size_->setProperty("item", QVariant::fromValue<qulonglong>(item));
        show(font_, true);
        show(size_, true);
    }
    auto frameText = [](const DrawingItemFrame &frame) {
        return millimetres(frame.xMm) + ", " + millimetres(frame.yMm) + " · " +
               millimetres(frame.widthMm) + " × " + millimetres(frame.heightMm) + " mm";
    };
    if (kind == DrawingItemKind::Text || kind == DrawingItemKind::Label) {
        const auto &text = kind == DrawingItemKind::Text
                               ? std::get<DrawingTextItem>(record->data).text
                               : std::get<DrawingLabelItem>(record->data).text;
        if (!keep(text_))
            text_->setPlainText(QString::fromStdString(text));
        text_->setProperty("item", QVariant::fromValue<qulonglong>(item));
        show(text_, true);
    }
    if (const auto *t = std::get_if<DrawingTextItem>(&record->data)) {
        align_->setCurrentIndex(int(t->align));
        show(align_, true);
        geometry_->setText(frameText(t->frame));
        show(geometry_, true);
    } else if (const auto *l = std::get_if<DrawingLabelItem>(&record->data)) {
        leader_->setChecked(l->leader);
        show(leader_, true);
        target_->setText(l->target.kind == DrawingLabelTargetKind::Paper
                             ? "Paper · " + millimetres(l->target.pointMm[0]) + ", " +
                                   millimetres(l->target.pointMm[1]) + " mm"
                             : "Model point in " + viewName);
        show(target_, true);
        geometry_->setText("Text at " + millimetres(l->textAt[0]) + ", " +
                           millimetres(l->textAt[1]) + " mm");
        show(geometry_, true);
    } else if (const auto *d = std::get_if<DrawingDimensionItem>(&record->data)) {
        value_->setText(evaluation ? QString::fromStdString(evaluation->text)
                                   : QString("Updating…"));
        show(value_, true);
        view_->setText(viewName);
        show(view_, true);
        orientation_->setCurrentIndex(int(d->orientation));
        show(orientation_, true);
        if (!keep(offset_)) {
            QSignalBlocker blocker(offset_);
            offset_->setValue(d->offsetMm);
        }
        offset_->setProperty("item", QVariant::fromValue<qulonglong>(item));
        show(offset_, true);
        setLine(prefix_, d->prefix);
    } else if (const auto *line = std::get_if<DrawingLineItem>(&record->data)) {
        geometry_->setText(
            millimetres(line->from[0]) + ", " + millimetres(line->from[1]) + " → " +
            millimetres(line->to[0]) + ", " + millimetres(line->to[1]) + " mm · " +
            millimetres(std::hypot(line->to[0] - line->from[0], line->to[1] - line->from[1])) +
            " mm long");
        show(geometry_, true);
        if (!keep(stroke_)) {
            QSignalBlocker blocker(stroke_);
            stroke_->setValue(line->strokeMm);
        }
        show(stroke_, true);
        dash_->setCurrentIndex(int(line->dash));
        show(dash_, true);
    } else if (const auto *r = std::get_if<DrawingRectangleItem>(&record->data)) {
        geometry_->setText(frameText(r->frame));
        show(geometry_, true);
        if (!keep(stroke_)) {
            QSignalBlocker blocker(stroke_);
            stroke_->setValue(r->strokeMm);
        }
        show(stroke_, true);
        dash_->setCurrentIndex(int(r->dash));
        show(dash_, true);
    } else if (const auto *b = std::get_if<DrawingTitleBlockItem>(&record->data)) {
        setLine(project_, b->fields.project);
        setLine(drawing_, b->fields.drawingTitle);
        const auto &sheet = *doc_.drawingSheets().at(record->sheet);
        setLine(number_, sheet.number);
        setLine(date_, b->fields.date);
        setLine(revision_, b->fields.revision);
        const auto derived = evaluation && evaluation->titleBlock ? *evaluation->titleBlock
                                                                  : DrawingTitleBlockDerived{};
        page_->setText(QString::fromStdString(derived.page));
        page_->setAccessibleDescription("Filled in automatically");
        show(page_, true);
        scale_->setText(QString::fromStdString(derived.scale));
        scale_->setAccessibleDescription("Filled in automatically from the page's views");
        show(scale_, true);
        geometry_->setText(frameText(b->frame));
        show(geometry_, true);
    }
    if (evaluation) {
        state_->setText(sheetItemStateText(*evaluation));
        const bool broken = evaluation->state == DrawingItemState::Broken;
        stateIcon_->setPixmap(
            sheetStateIcon(broken ? DrawingViewState::Broken : DrawingViewState::Current, false,
                           colors_, devicePixelRatioF()));
        show(stateRow_, true);
        reattach_->setVisible(broken && drawingItemView(*record));
    } else
        reattach_->hide();
    applyRows();
}
} // namespace sketchy
