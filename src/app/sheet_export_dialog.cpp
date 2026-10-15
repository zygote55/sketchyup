#include "app/sheet_export_dialog.hpp"
#include "app/interface_preferences.hpp"
#include "core/drawing_sheets.hpp"
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPageLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace sketchy {
namespace {
enum class Icon { Broken, Changed, Warning, Scale, Raster };
enum class Tone { Failure, Warning, Info };
// The design's 18 px finding icons, drawn in the theme's state colours so they stay sharp at
// fractional scales.
QPixmap findingIcon(Icon icon, const ThemeColors &colors, qreal ratio) {
    QPixmap pixmap(QSize(18, 18) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    auto pen = [&](const QColor &color, double width) {
        painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
    };
    switch (icon) {
    case Icon::Broken: {
        pen(colors.failure, 1.5);
        painter.drawEllipse(QPointF(9, 9), 7.5, 7.5);
        painter.drawLine(QPointF(6, 6), QPointF(12, 12));
        painter.drawLine(QPointF(12, 6), QPointF(6, 12));
        break;
    }
    case Icon::Changed: {
        // The design's 12-unit refresh glyph at 18 px.
        painter.scale(1.5, 1.5);
        pen(colors.warning, 1.2);
        QPainterPath path;
        path.arcMoveTo(QRectF(2, 2, 8, 8), 0);
        path.arcTo(QRectF(2, 2, 8, 8), 0, -315);
        path.moveTo(10, 2);
        path.lineTo(10, 4.5);
        path.lineTo(7.5, 4.5);
        painter.drawPath(path);
        break;
    }
    case Icon::Warning: {
        pen(colors.warning, 1.5);
        QPainterPath path;
        path.moveTo(9, 2);
        path.lineTo(16.5, 15);
        path.lineTo(1.5, 15);
        path.closeSubpath();
        painter.drawPath(path);
        painter.drawLine(QPointF(9, 7), QPointF(9, 11));
        painter.drawLine(QPointF(9, 12.5), QPointF(9, 13.5));
        break;
    }
    case Icon::Scale: {
        painter.scale(1.5, 1.5);
        pen(colors.dark ? colors.accent : QColor("#3f6043"), 1);
        painter.drawEllipse(QPointF(6, 6), 5, 5);
        QPainterPath tick;
        tick.moveTo(3.5, 6);
        tick.lineTo(5.5, 8);
        tick.lineTo(8.5, 4);
        painter.drawPath(tick);
        break;
    }
    case Icon::Raster: {
        pen(colors.muted, 1.5);
        painter.drawRect(QRectF(2.5, 3.5, 13, 11));
        pen(colors.muted, .8);
        for (const double y : {8., 11.})
            painter.drawLine(QPointF(2.5, y), QPointF(15.5, y));
        for (const double x : {6., 10.})
            painter.drawLine(QPointF(x, 3.5), QPointF(x, 14.5));
        break;
    }
    }
    return pixmap;
}
QString plural(size_t count, const QString &one, const QString &many) {
    return QString::number(count) + ' ' + (count == 1 ? one : many);
}
// Up to four lines of a group's findings, then a count of the rest.
QString lines(const QStringList &messages) {
    constexpr qsizetype shown = 4;
    auto result = messages.mid(0, shown);
    if (messages.size() > shown)
        result << QString("…and %1 more.").arg(messages.size() - shown);
    return result.join('\n');
}
QString withoutPrefix(const QString &message, const QString &prefix) {
    return message.startsWith(prefix) ? message.mid(prefix.size()) : message;
}
QString millimetres(double width, double height) {
    return QString("%1 × %2 mm").arg(std::lround(width)).arg(std::lround(height));
}
} // namespace

// The findings list: as tall as its content for the dialog's width, scrolling only when the
// screen cannot fit it.
namespace {
class ChecksArea final : public QScrollArea {
  public:
    ChecksArea() {
        setObjectName("sheetExportChecks");
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    bool hasHeightForWidth() const override { return widget() != nullptr; }
    int heightForWidth(int width) const override {
        auto *content = widget();
        if (!content)
            return QScrollArea::heightForWidth(width);
        const auto inner = std::max(1, width - 2 * frameWidth());
        return (content->hasHeightForWidth() ? content->heightForWidth(inner)
                                             : content->sizeHint().height()) +
               2 * frameWidth();
    }
    QSize sizeHint() const override {
        const auto width = std::max(this->width(), 400);
        return {width, heightForWidth(width)};
    }
    QSize minimumSizeHint() const override {
        return {120, std::min(heightForWidth(std::max(width(), 400)), 120)};
    }
};
} // namespace

// A finding's controls in a row that wraps when the dialog is narrow (the design's flex-wrap),
// so a long opt-in label never widens the list past the dialog.
namespace {
class WrapRow final : public QLayout {
  public:
    explicit WrapRow(int spacing) { setSpacing(spacing); }
    ~WrapRow() override {
        while (auto *item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem *item) override { items_.push_back(item); }
    void addLayout(QLayout *layout) {
        addChildLayout(layout);
        addItem(layout);
    }
    int count() const override { return int(items_.size()); }
    QLayoutItem *itemAt(int index) const override {
        return index >= 0 && index < count() ? items_[size_t(index)] : nullptr;
    }
    QLayoutItem *takeAt(int index) override {
        if (index < 0 || index >= count())
            return nullptr;
        auto *item = items_[size_t(index)];
        items_.erase(items_.begin() + index);
        return item;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return place({0, 0, width, 0}, false); }
    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        place(rect, true);
    }
    QSize sizeHint() const override {
        int width = 0, height = 0;
        for (auto *item : items_)
            if (!item->isEmpty()) {
                width += item->sizeHint().width() + (width ? spacing() : 0);
                height = std::max(height, item->sizeHint().height());
            }
        return {width, height};
    }
    QSize minimumSize() const override {
        QSize result;
        for (auto *item : items_)
            if (!item->isEmpty())
                result = result.expandedTo(item->minimumSize());
        return result;
    }

  private:
    // Lays the items out in lines, each item centred on its line's height.
    int place(const QRect &rect, bool apply) const {
        std::vector<std::vector<std::pair<QLayoutItem *, QSize>>> lines(1);
        int x = rect.x();
        for (auto *item : items_) {
            if (item->isEmpty())
                continue;
            const auto hint = item->sizeHint();
            if (x > rect.x() && x + hint.width() > rect.right() + 1) {
                lines.emplace_back();
                x = rect.x();
            }
            lines.back().push_back({item, hint});
            x += hint.width() + spacing();
        }
        int y = rect.y();
        for (const auto &line : lines) {
            if (line.empty())
                continue;
            int height = 0;
            for (const auto &[item, hint] : line)
                height = std::max(height, hint.height());
            int left = rect.x();
            for (const auto &[item, hint] : line) {
                if (apply)
                    item->setGeometry({QPoint(left, y + (height - hint.height()) / 2), hint});
                left += hint.width() + spacing();
            }
            y += height + spacing();
        }
        return std::max(0, y - spacing() - rect.y());
    }
    std::vector<QLayoutItem *> items_;
};
} // namespace

// The output path on one line, elided in the middle when the dialog is narrow; the full
// path is the tooltip and the Choose… button's description.
namespace {
class ElidedLabel final : public QLabel {
  public:
    ElidedLabel() {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    void setFullText(const QString &text) {
        full_ = text;
        elide();
    }
    QSize sizeHint() const override {
        return {fontMetrics().horizontalAdvance(full_) + 4, QLabel::sizeHint().height()};
    }
    QSize minimumSizeHint() const override { return {40, QLabel::minimumSizeHint().height()}; }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        elide();
    }

  private:
    void elide() {
        setText(fontMetrics().elidedText(full_, Qt::ElideMiddle, std::max(0, width())));
    }
    QString full_;
};
} // namespace

// One group of findings: icon, a bold headline and its lines, and the group's controls.
class SheetExportFindingBlock final : public QFrame {
  public:
    explicit SheetExportFindingBlock(const QString &code) {
        setObjectName("sheetExportFinding");
        setProperty("code", code);
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(12, 10, 12, 10);
        row->setSpacing(10);
        icon = new QLabel;
        icon->setObjectName("sheetExportFindingIcon");
        icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        icon->setFixedWidth(18);
        row->addWidget(icon, 0, Qt::AlignTop);
        auto *column = new QVBoxLayout;
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(6);
        text = new QLabel;
        text->setObjectName("sheetExportFindingText");
        text->setTextFormat(Qt::RichText);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::NoTextInteraction);
        column->addWidget(text);
        actions = new WrapRow(8);
        actions->setContentsMargins(0, 0, 0, 0);
        column->addLayout(actions);
        row->addLayout(column, 1);
    }
    void set(Tone tone, Icon kind, const ThemeColors &colors, const QString &headline,
             const QString &body) {
        tone_ = tone;
        icon_ = kind;
        headline_ = headline;
        body_ = body;
        setProperty("tone", tone == Tone::Failure   ? "failure"
                            : tone == Tone::Warning ? "warning"
                                                    : "info");
        layout()->setContentsMargins(12, tone == Tone::Info ? 8 : 10, 12,
                                     tone == Tone::Info ? 8 : 10);
        repaintFor(colors);
    }
    void repaintFor(const ThemeColors &colors) {
        icon->setPixmap(findingIcon(icon_, colors, devicePixelRatioF()));
        const auto colour = tone_ == Tone::Failure ? colors.failure : colors.warning;
        QString html;
        if (!headline_.isEmpty())
            html = QString("<b style=\"color:%1\">%2</b> ")
                       .arg(colour.name(), headline_.toHtmlEscaped());
        html += body_.toHtmlEscaped().replace('\n', "<br>");
        text->setText(html);
        // The words without markup, for assistive technology and tests.
        text->setAccessibleName(headline_.isEmpty() ? body_ : headline_ + ' ' + body_);
        style()->unpolish(this);
        style()->polish(this);
        for (auto *child : findChildren<QWidget *>()) {
            style()->unpolish(child);
            style()->polish(child);
        }
    }
    QLabel *icon{}, *text{};
    QLayout *actions{};

  private:
    Tone tone_{Tone::Info};
    Icon icon_{Icon::Scale};
    QString headline_, body_;
};

SheetExportDialog::SheetExportDialog(const Document &doc, Setup setup, QWidget *parent)
    : Settled<QDialog>(parent), doc_(doc), setup_(std::move(setup)), path_(setup_.path) {
    pool_.setMaxThreadCount(1);
    rerun_.setSingleShot(true);
    connect(&rerun_, &QTimer::timeout, this, [this] { start(); });
    build();
    setTheme(setup_.colors);
    refreshPages();
    refreshTarget();
    refreshList();
    refreshFooter();
    schedule();
}
SheetExportDialog::~SheetExportDialog() {
    rerun_.stop();
    // A running preflight posts back through a guarded pointer; wait so it never outlives
    // the document it reads (it holds its own snapshot, so this is only for shutdown order).
    pool_.waitForDone();
}

void SheetExportDialog::build() {
    const bool print = setup_.mode == Mode::Print;
    setObjectName(print ? "sheetPrintDialog" : "sheetExportDialog");
    setWindowTitle(print ? "Print sheets" : "Export sheets as PDF");
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto rule = [&] {
        auto *line = new QFrame;
        line->setObjectName("sheetExportRule");
        line->setFixedHeight(1);
        layout->addWidget(line);
    };

    // Title and subline.
    auto *header = new QVBoxLayout;
    header->setContentsMargins(22, 18, 22, 12);
    header->setSpacing(4);
    auto *title = new QLabel(windowTitle());
    title->setObjectName("sheetExportTitle");
    header->addWidget(title);
    subline_ = new QLabel;
    subline_->setObjectName("sheetExportSubline");
    subline_->setTextFormat(Qt::PlainText);
    header->addWidget(subline_);
    layout->addLayout(header);
    rule();

    // Pages, and the file or printer.
    auto *grid = new QGridLayout;
    grid->setContentsMargins(22, 14, 22, 14);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->setColumnMinimumWidth(0, 110);
    grid->setColumnStretch(1, 1);
    auto caption = [](const QString &text) {
        auto *label = new QLabel(text);
        label->setObjectName("sheetExportFieldLabel");
        return label;
    };
    grid->addWidget(caption("Pages"), 0, 0);
    auto *pagesRow = new WrapRow(14);
    const auto count = orderedDrawingSheets(doc_).size();
    all_ = new QRadioButton(QString("All %1").arg(count));
    all_->setObjectName("sheetExportAll");
    all_->setAccessibleName(count == 1 ? QString("All 1 page")
                                       : QString("All %1 pages").arg(count));
    current_ = new QRadioButton("Current page");
    current_->setObjectName("sheetExportCurrent");
    range_ = new QRadioButton("Range");
    range_->setObjectName("sheetExportRange");
    range_->setAccessibleName("Range of pages");
    rangeText_ = new QLineEdit;
    rangeText_->setObjectName("sheetExportRangeText");
    rangeText_->setAccessibleName("Page range");
    rangeText_->setPlaceholderText("A-101–A-301");
    rangeText_->setToolTip("Sheet numbers or page positions: A-101–A-301, or 1, 3–4");
    rangeText_->setMinimumWidth(QFontMetrics(rangeText_->font()).horizontalAdvance("A-101–A-301") +
                                24);
    pages_ = new QButtonGroup(this);
    pages_->addButton(all_, 0);
    pages_->addButton(current_, 1);
    pages_->addButton(range_, 2);
    all_->setChecked(true);
    current_->setEnabled(doc_.drawingSheets().contains(setup_.currentSheet));
    // The radio buttons stay on one line, so the arrow keys move between them in order; the
    // range field wraps below them on a narrow screen.
    auto *radios = new QHBoxLayout;
    radios->setSpacing(14);
    radios->addWidget(all_);
    radios->addWidget(current_);
    radios->addWidget(range_);
    pagesRow->addLayout(radios);
    pagesRow->addWidget(rangeText_);
    grid->addLayout(pagesRow, 0, 1);
    grid->addWidget(caption(print ? "Printer" : "File"), 1, 0);
    auto *targetRow = new QHBoxLayout;
    targetRow->setSpacing(8);
    targetLabel_ = new ElidedLabel;
    targetLabel_->setObjectName(print ? "sheetPrintPrinter" : "sheetExportPath");
    targetLabel_->setTextFormat(Qt::PlainText);
    targetLabel_->setTextInteractionFlags(Qt::NoTextInteraction);
    targetRow->addWidget(targetLabel_, 1);
    choose_ = new QPushButton("Choose…");
    choose_->setObjectName(print ? "sheetPrintChoose" : "sheetExportChoose");
    choose_->setAccessibleName(print ? "Choose printer and paper" : "Choose output file");
    choose_->setAutoDefault(false);
    targetRow->addWidget(choose_);
    grid->addLayout(targetRow, 1, 1);
    layout->addLayout(grid);
    rule();

    // Checked before publishing.
    auto *checks = new QVBoxLayout;
    checks->setContentsMargins(22, 14, 22, 14);
    checks->setSpacing(10);
    auto *heading = new QLabel("Checked before publishing");
    heading->setObjectName("sheetExportHeading");
    checks->addWidget(heading);
    // The list scrolls when the screen is too small for it (keyboard focus scrolls with it).
    auto *area = new ChecksArea;
    checksArea_ = area;
    auto *content = new QWidget;
    content->setObjectName("sheetExportChecksContent");
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    list_ = new QVBoxLayout;
    list_->setSpacing(8);
    column->addLayout(list_);
    checkingLabel_ = new QLabel("Checking the drawing set…");
    checkingLabel_->setObjectName("sheetExportChecking");
    checkingLabel_->setWordWrap(true);
    column->addWidget(checkingLabel_);
    column->addStretch(1);
    area->setWidget(content);
    checks->addWidget(area, 1);
    layout->addLayout(checks, 1);
    rule();

    // Footer: what to fix, Cancel and the confirming button.
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(22, 12, 22, 18);
    footer->setSpacing(10);
    hint_ = new QLabel;
    hint_->setObjectName("sheetExportHint");
    hint_->setTextFormat(Qt::PlainText);
    hint_->setWordWrap(true);
    footer->addWidget(hint_, 1);
    cancel_ = new QPushButton("Cancel");
    cancel_->setObjectName("sheetExportCancel");
    cancel_->setAutoDefault(false);
    footer->addWidget(cancel_);
    confirm_ = new QPushButton(print ? "Print" : "Export PDF");
    confirm_->setObjectName("sheetExportConfirm");
    confirm_->setProperty("accentFill", true); // The shared focus rule (R091.h).
    confirm_->setAutoDefault(false);
    confirm_->setDefault(true);
    footer->addWidget(confirm_);
    layout->addLayout(footer);

    connect(pages_, &QButtonGroup::idToggled, this, [this](int, bool checked) {
        if (!checked)
            return;
        refreshPages();
        schedule();
    });
    connect(rangeText_, &QLineEdit::textEdited, this, [this] {
        if (!range_->isChecked())
            range_->setChecked(true); // Typing a range chooses Range.
        refreshPages();
        schedule(300);
    });
    connect(choose_, &QPushButton::clicked, this, [this] {
        if (setup_.mode == Mode::Print)
            choosePrinter();
        else
            chooseFile();
    });
    connect(cancel_, &QPushButton::clicked, this, [this] { close(Outcome::Cancel, 0); });
    connect(confirm_, &QPushButton::clicked, this, [this] {
        if (confirm_->isEnabled())
            close(Outcome::Confirm, 0);
    });
    // The design's 760 px sheet, within the screen.
    const auto *screen =
        parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
    const int available = screen ? screen->availableGeometry().width() - 32 : 760;
    setMinimumWidth(std::clamp(available, 280, 760));
}

void SheetExportDialog::setTheme(const ThemeColors &colors) {
    setup_.colors = colors;
    setStyleSheet(styleSheetFor(colors));
    for (auto &[key, block] : blocks_)
        block->repaintFor(colors);
}
QString SheetExportDialog::styleSheetFor(const ThemeColors &colors) const {
    const auto percent = interfaceTextPercent();
    auto style = QStringLiteral(R"(
#sheetExportTitle {font-size:$titlePx;font-weight:700;}
#sheetExportHeading {font-size:$headingPx;font-weight:700;}
#sheetExportSubline,#sheetExportFieldLabel,#sheetExportHint,#sheetExportChecking {color:$muted;}
#sheetExportPath {font-family:'DejaVu Sans Mono','Noto Sans Mono',monospace;}
#sheetExportRule {background:$border;border:0;}
QFrame#sheetExportFinding {border:1px solid transparent;border-radius:8px;background:transparent;}
QFrame#sheetExportFinding[tone="failure"] {border:2px solid $failure;background:$failureSurface;}
QFrame#sheetExportFinding[tone="warning"] {border:1px solid $warningBorder;background:$warningSurface;}
#sheetExportFinding QLabel {background:transparent;}
QFrame#sheetExportFinding[tone="failure"] QCheckBox {background:$failureSurface;}
QFrame#sheetExportFinding[tone="warning"] QCheckBox {background:$warningSurface;}
#sheetExportChoose,#sheetPrintChoose,#sheetExportGoToView,#sheetExportCancel {background:$input;border:1px solid $border;border-radius:6px;padding:2px 10px;}
#sheetExportCancel {padding:6px 14px;}
#sheetExportChoose:focus,#sheetPrintChoose:focus,#sheetExportGoToView:focus,#sheetExportCancel:focus {border:1px solid $accent;}
#sheetExportReview,#sheetExportConfirm {background:$accent;color:$onAccent;font-weight:600;border:2px solid $accent;border-radius:6px;padding:1px 10px;}
#sheetExportConfirm {padding:4px 14px;}
#sheetExportReview:focus,#sheetExportConfirm:focus {border:2px solid $surface;}
#sheetExportConfirm:disabled {background:$hover;color:$muted;border-color:$hover;}
#sheetExportChooseFont {background:transparent;border:1px solid transparent;border-radius:4px;padding:0 2px;color:$link;text-decoration:underline;}
#sheetExportChooseFont:focus {border:1px solid $accent;}
)");
    style.replace("$titlePx", QString::number(interfaceExtent(18, percent)) + "px");
    style.replace("$headingPx", QString::number(interfaceExtent(14, percent)) + "px");
    style.replace("$onAccent", colors.dark ? "#172019" : "#ffffff");
    style.replace("$link", colors.dark ? colors.accent.name() : QString("#3f6043"));
    style.replace("$failureSurface", colors.failureSurface.name());
    style.replace("$failure", colors.failure.name());
    style.replace("$warningBorder", colors.warningBorder.name());
    style.replace("$warningSurface", colors.warningSurface.name());
    style.replace("$surface", colors.surface.name());
    style.replace("$border", colors.border.name());
    style.replace("$muted", colors.muted.name());
    style.replace("$hover", colors.hover.name());
    style.replace("$input", colors.input.name());
    style.replace("$accent", colors.accent.name());
    return style;
}

QString SheetExportDialog::pageLabel(Id sheet) const {
    const auto &record = *doc_.drawingSheets().at(sheet);
    if (!record.number.empty())
        return QString::fromStdString(record.number);
    const auto order = orderedDrawingSheets(doc_);
    const auto at = std::find(order.begin(), order.end(), sheet);
    return QString("page %1").arg(at - order.begin() + 1);
}
std::optional<std::vector<Id>> SheetExportDialog::selectedSheets(QString *error) const {
    if (current_->isChecked()) {
        if (!doc_.drawingSheets().contains(setup_.currentSheet)) {
            if (error)
                *error = "The current page no longer exists. Choose All or a range.";
            return std::nullopt;
        }
        return std::vector<Id>{setup_.currentSheet};
    }
    if (range_->isChecked()) {
        const auto text = rangeText_->text().trimmed();
        if (text.isEmpty()) {
            if (error)
                *error = "Type a page range, such as A-101–A-301.";
            return std::nullopt;
        }
        try {
            return resolveSheetPageRange(doc_, text.toStdString());
        } catch (const std::exception &e) {
            if (error)
                *error = QString::fromUtf8(e.what()) + '.';
            return std::nullopt;
        }
    }
    return orderedDrawingSheets(doc_);
}
SheetExportOptions SheetExportDialog::options() const {
    SheetExportOptions result;
    if (const auto sheets = selectedSheets(nullptr))
        result.sheets = *sheets;
    result.allowBroken = allowBroken_;
    result.allowChanged = allowChanged_;
    return result;
}

void SheetExportDialog::schedule(int delayMs) { rerun_.start(delayMs); }
void SheetExportDialog::documentChanged() {
    current_->setEnabled(doc_.drawingSheets().contains(setup_.currentSheet));
    refreshPages();
    if (doc_.revision() != revision_ || !preflight_)
        schedule(150);
}
void SheetExportDialog::start() {
    QString error;
    const auto sheets = selectedSheets(&error);
    if (!sheets) {
        // Nothing to check until the pages are valid; the footer says why.
        ++generation_;
        preflight_.reset();
        preflightError_.clear();
        refreshList();
        refreshFooter();
        return;
    }
    if (running_) {
        pending_ = true;
        refreshFooter();
        return;
    }
    running_ = true;
    pending_ = false;
    const auto generation = ++generation_;
    revision_ = doc_.revision();
    auto options = this->options();
    options.sheets = *sheets;
    // The desktop renders raster views with its viewport (Viewport::renderDrawingViewRaster),
    // so raster views are never rasterUnavailable here. The preflight only asks whether a
    // renderer exists; the window supplies the real one when it exports.
    options.rasterRenderer = [](const DrawingViewEvaluationInput &, QSize) { return QImage(); };
    auto snapshot = std::make_shared<const Document>(doc_.readSnapshot());
    QPointer<SheetExportDialog> guard(this);
    refreshFooter();
    pool_.start([snapshot, options, generation, guard] {
        std::optional<SheetExportPreflight> result;
        QString failure;
        try {
            result = preflightSheetExport(*snapshot, options);
        } catch (const std::exception &e) {
            failure = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(
            qApp,
            [guard, generation, result = std::move(result), failure] {
                if (guard)
                    guard->finished(generation, result, failure);
            },
            Qt::QueuedConnection);
    });
}
void SheetExportDialog::finished(std::uint64_t generation,
                                 std::optional<SheetExportPreflight> result, QString error) {
    running_ = false;
    if (generation == generation_) {
        preflight_ = std::move(result);
        preflightError_ = error;
    }
    if (pending_ || doc_.revision() != revision_) {
        pending_ = false;
        start();
    }
    refreshPages();
    refreshList();
    refreshFooter();
}

void SheetExportDialog::refreshPages() {
    QString error;
    const auto sheets = selectedSheets(&error);
    rangeText_->setProperty("invalid", range_->isChecked() && !sheets);
    rangeText_->setAccessibleDescription(range_->isChecked() && !sheets ? error : QString{});
    rangeText_->style()->unpolish(rangeText_);
    rangeText_->style()->polish(rangeText_);
    QStringList parts{setup_.documentName};
    if (sheets)
        parts << plural(sheets->size(), "page", "pages");
    parts << (setup_.mode == Mode::Print ? "printed at actual size on the printer's paper"
                                         : "printed at actual size");
    subline_->setText(parts.join(" · "));
}
void SheetExportDialog::refreshTarget() {
    if (setup_.mode == Mode::Print) {
        QString text = "No printer";
        if (auto *printer = setup_.printer) {
            const auto paper = printer->pageLayout().fullRect(QPageLayout::Millimeter);
            const auto name = printer->outputFormat() == QPrinter::PdfFormat
                                  ? "PDF file " + QFileInfo(printer->outputFileName()).fileName()
                                  : printer->printerName();
            text = (name.trimmed().isEmpty() ? QString("Default printer") : name) + " · " +
                   printer->pageLayout().pageSize().name() + ", " +
                   millimetres(paper.width(), paper.height());
        }
        static_cast<ElidedLabel *>(targetLabel_)->setFullText(text);
        choose_->setAccessibleDescription(text);
        return;
    }
    auto shown = QDir::toNativeSeparators(path_);
    const auto home = QDir::homePath();
    if (shown.startsWith(home + QDir::separator()))
        shown = "~" + shown.mid(home.size());
    static_cast<ElidedLabel *>(targetLabel_)
        ->setFullText(shown.isEmpty() ? QString("No file chosen") : shown);
    targetLabel_->setToolTip(QDir::toNativeSeparators(path_));
    choose_->setAccessibleDescription(QDir::toNativeSeparators(path_));
}
std::vector<Id> SheetExportDialog::oversizedPages() const {
    std::vector<Id> result;
    if (setup_.mode != Mode::Print || !setup_.printer || !preflight_)
        return result;
    const auto paper = setup_.printer->pageLayout().fullRect(QPageLayout::Millimeter);
    // Qt keeps page boxes in whole points: allow half a millimetre.
    for (const auto &page : preflight_->pages)
        if (page.widthMm > paper.width() + .5 || page.heightMm > paper.height() + .5)
            result.push_back(page.sheet);
    return result;
}

void SheetExportDialog::refreshList() {
    struct Group {
        QString key;
        Tone tone;
        Icon icon;
        QString headline;
        QStringList lines;
        std::vector<const SheetExportFinding *> findings;
    };
    const bool print = setup_.mode == Mode::Print;
    const QString verb = print ? "print" : "export", Verb = print ? "Print" : "Export";
    std::vector<Group> groups;
    auto group = [&](const QString &key) -> Group & {
        for (auto &entry : groups)
            if (entry.key == key)
                return entry;
        groups.push_back({key, Tone::Info, Icon::Scale, {}, {}, {}});
        return groups.back();
    };
    if (preflight_)
        for (const auto &finding : preflight_->findings)
            group(QString::fromStdString(finding.code)).findings.push_back(&finding);
    for (auto &entry : groups) {
        const auto count = entry.findings.size();
        const bool blocking =
            std::any_of(entry.findings.begin(), entry.findings.end(), [](const auto *finding) {
                return finding->severity == SheetExportSeverity::Blocking;
            });
        for (const auto *finding : entry.findings)
            entry.lines << QString::fromStdString(finding->message);
        if (entry.key == "viewBroken") {
            entry.tone = Tone::Failure;
            entry.icon = Icon::Broken;
            if (blocking)
                entry.headline =
                    plural(count, "broken view blocks", "broken views block") + ' ' + verb + '.';
            else if (count == 1)
                entry.headline = "1 broken view " + verb + "s as an empty, labelled frame.";
            else
                entry.headline =
                    QString("%1 broken views %2 as empty, labelled frames.").arg(count).arg(verb);
        } else if (entry.key == "rasterUnavailable") {
            entry.tone = Tone::Failure;
            entry.icon = Icon::Broken;
        } else if (entry.key == "viewChanged") {
            entry.tone = Tone::Warning;
            entry.icon = Icon::Changed;
            const auto views = entry.findings.front()->views.size();
            entry.headline = plural(views, "view", "views") + " changed since accepted.";
            entry.lines = {withoutPrefix(entry.lines.front(), plural(views, "view", "views") +
                                                                  " changed since accepted: ")};
            if (print) // The engine's advice names exporting; printing has the same choice.
                entry.lines.front().replace(", or export them as they are now.",
                                            ", or print them as they are now.");
        } else if (entry.key == "fontSubstituted") {
            entry.tone = Tone::Warning;
            entry.icon = Icon::Warning;
            entry.headline = count == 1 ? QString("1 font is not installed.")
                                        : QString("%1 fonts are not installed.").arg(count);
            for (auto &line : entry.lines)
                line = withoutPrefix(line, "1 font is not installed. ");
        } else if (entry.key == "scaleSummary") {
            entry.icon = Icon::Scale;
        } else if (entry.key == "rasterViews") {
            entry.icon = Icon::Raster;
        } else {
            entry.tone = blocking ? Tone::Failure : Tone::Warning;
            entry.icon = blocking ? Icon::Broken : Icon::Warning;
        }
    }
    // Print: a page larger than the paper prints at 100% and is cut off; never scaled to fit.
    if (const auto oversized = oversizedPages(); !oversized.empty()) {
        const auto paper = setup_.printer->pageLayout().fullRect(QPageLayout::Millimeter);
        QStringList names;
        for (const auto sheet : oversized) {
            const auto &record = *doc_.drawingSheets().at(sheet);
            names << pageLabel(sheet) + " (" +
                         millimetres(record.paper.widthMm, record.paper.heightMm) + ")";
        }
        Group entry{"paperSmaller", Tone::Warning, Icon::Warning, {}, {}, {}};
        entry.headline = oversized.size() == 1
                             ? QString("1 page is larger than the paper.")
                             : QString("%1 pages are larger than the paper.").arg(oversized.size());
        const bool one = oversized.size() == 1;
        entry.lines << names.join(", ") + (one ? " does" : " do") + " not fit on " +
                           setup_.printer->pageLayout().pageSize().name() + " (" +
                           millimetres(paper.width(), paper.height()) + "). " +
                           (one ? "It prints at 100% and is" : "They print at 100% and are") +
                           " cut off at the paper's edge; nothing is scaled to fit. Choose larger "
                           "paper to print " +
                           (one ? "it" : "them") + " whole.";
        groups.push_back(entry);
    }
    // The design's order: what blocks, what changed, fonts and other warnings, then the notes.
    static const QStringList order{
        "viewBroken",   "rasterUnavailable", "viewChanged", "fontSubstituted",
        "fontOutlined", "missingGlyphs",     "itemBroken",  "sceneReferencesMissing",
        "paperSmaller", "scaleSummary",      "rasterViews"};
    std::stable_sort(groups.begin(), groups.end(), [](const Group &a, const Group &b) {
        auto rank = [](const QString &key) {
            const auto at = order.indexOf(key);
            return at < 0 ? order.size() : at;
        };
        return rank(a.key) < rank(b.key);
    });

    // Reuse each group's block, so a control keeps keyboard focus while the list refreshes.
    std::map<QString, SheetExportFindingBlock *> kept;
    for (const auto &entry : groups) {
        auto found = blocks_.find(entry.key);
        SheetExportFindingBlock *block{};
        if (found != blocks_.end()) {
            block = found->second;
            blocks_.erase(found);
        } else {
            block = new SheetExportFindingBlock(entry.key);
            if (entry.key == "viewBroken" || entry.key == "rasterUnavailable") {
                if (entry.key == "viewBroken") {
                    auto *go = new QPushButton("Go to view");
                    go->setObjectName("sheetExportGoToView");
                    go->setAutoDefault(false);
                    block->actions->addWidget(go);
                    connect(go, &QPushButton::clicked, this, [this] {
                        if (!preflight_)
                            return;
                        for (const auto &finding : preflight_->findings)
                            if (finding.code == "viewBroken" && !finding.views.empty())
                                return close(Outcome::GoToView, finding.views.front());
                    });
                }
                auto *allow = new QCheckBox;
                allow->setObjectName(entry.key == "viewBroken" ? "sheetExportAllowBroken"
                                                               : "sheetExportAllowRaster");
                block->actions->addWidget(allow);
                connect(allow, &QCheckBox::toggled, this, [this](bool checked) {
                    allowBroken_ = checked;
                    for (auto *other : findChildren<QCheckBox *>())
                        if (other->objectName() == "sheetExportAllowBroken" ||
                            other->objectName() == "sheetExportAllowRaster") {
                            QSignalBlocker quiet(other);
                            other->setChecked(checked);
                        }
                    schedule();
                });
            } else if (entry.key == "viewChanged") {
                auto *review = new QPushButton("Review and accept");
                review->setObjectName("sheetExportReview");
                review->setProperty("accentFill", true);
                review->setAutoDefault(false);
                block->actions->addWidget(review);
                connect(review, &QPushButton::clicked, this, [this] {
                    if (!preflight_)
                        return;
                    for (const auto &finding : preflight_->findings)
                        if (finding.code == "viewChanged" && !finding.views.empty())
                            return close(Outcome::ReviewChanged, finding.views.front());
                });
                auto *allow = new QCheckBox;
                allow->setObjectName("sheetExportAllowChanged");
                block->actions->addWidget(allow);
                connect(allow, &QCheckBox::toggled, this, [this](bool checked) {
                    allowChanged_ = checked;
                    schedule();
                });
            } else if (entry.key == "fontSubstituted") {
                auto *choose = new QPushButton("Choose another font");
                choose->setObjectName("sheetExportChooseFont");
                choose->setAutoDefault(false);
                choose->setCursor(Qt::PointingHandCursor);
                block->actions->addWidget(choose);
                connect(choose, &QPushButton::clicked, this, [this] {
                    if (!preflight_)
                        return;
                    for (const auto &finding : preflight_->findings)
                        if (finding.code == "fontSubstituted" && !finding.items.empty())
                            return close(Outcome::ChooseFont, finding.items.front());
                });
            }
        }
        kept[entry.key] = block;
        // Opt-in wording follows the count and the mode.
        const auto views = entry.findings.empty() ? size_t{} : [&] {
            size_t total = 0;
            for (const auto *finding : entry.findings)
                total += std::max<size_t>(1, finding->views.size());
            return total;
        }();
        if (auto *allow = block->findChild<QCheckBox *>("sheetExportAllowBroken")) {
            QSignalBlocker quiet(allow);
            allow->setChecked(allowBroken_);
            allow->setText(views == 1 ? Verb + " it as an empty, labelled frame"
                                      : Verb + " them as empty, labelled frames");
        }
        if (auto *allow = block->findChild<QCheckBox *>("sheetExportAllowRaster")) {
            QSignalBlocker quiet(allow);
            allow->setChecked(allowBroken_);
            allow->setText(views == 1 ? Verb + " it as an empty, labelled frame"
                                      : Verb + " them as empty, labelled frames");
        }
        if (auto *allow = block->findChild<QCheckBox *>("sheetExportAllowChanged")) {
            QSignalBlocker quiet(allow);
            allow->setChecked(allowChanged_);
            const bool one = entry.findings.front()->views.size() == 1;
            // Only an export writes the report that names them.
            allow->setText(print ? (one ? QString("Print it as it is now")
                                        : QString("Print them as they are now"))
                                 : (one ? QString("Export it as it is now (named in the export "
                                                  "report)")
                                        : QString("Export them as they are now (named in the "
                                                  "export report)")));
        }
        if (auto *choose = block->findChild<QPushButton *>("sheetExportChooseFont")) {
            const bool items = std::any_of(entry.findings.begin(), entry.findings.end(),
                                           [](const auto *f) { return !f->items.empty(); });
            choose->setVisible(items);
        }
        if (auto *go = block->findChild<QPushButton *>("sheetExportGoToView"))
            go->setAccessibleDescription(lines(entry.lines));
        if (auto *review = block->findChild<QPushButton *>("sheetExportReview"))
            review->setAccessibleDescription(lines(entry.lines));
        block->set(entry.tone, entry.icon, setup_.colors, entry.headline, lines(entry.lines));
    }
    // Groups that no longer apply: keep focus inside the dialog when one held it.
    for (auto &[key, block] : blocks_) {
        if (block->isAncestorOf(QApplication::focusWidget()))
            (confirm_->isEnabled() ? static_cast<QWidget *>(confirm_) : cancel_)
                ->setFocus(Qt::OtherFocusReason);
        block->hide();
        block->deleteLater();
    }
    blocks_ = std::move(kept);
    for (const auto &entry : groups) {
        auto *block = blocks_.at(entry.key);
        list_->removeWidget(block);
        list_->addWidget(block);
        block->show();
    }
    // Tab follows reading order: the pages, the file or printer, each finding's controls in
    // list order, then Cancel and the confirming button.
    std::vector<QWidget *> chain{all_, current_, range_, rangeText_, choose_};
    for (const auto &entry : groups) {
        auto *actions = blocks_.at(entry.key)->actions;
        for (int i = 0; i < actions->count(); ++i)
            if (auto *control = actions->itemAt(i)->widget())
                chain.push_back(control);
    }
    chain.push_back(cancel_);
    chain.push_back(confirm_);
    for (size_t i = 1; i < chain.size(); ++i)
        QWidget::setTabOrder(chain[i - 1], chain[i]);
    checksArea_->updateGeometry();
    checkingLabel_->setVisible(!preflight_);
    checkingLabel_->setText(!preflightError_.isEmpty()  ? preflightError_
                            : checking() || !preflight_ ? QString("Checking the drawing set…")
                                                        : QString());
    QString error;
    if (!selectedSheets(&error))
        checkingLabel_->setText("Choose the pages to check.");
    adjustSize();
}

void SheetExportDialog::refreshFooter() {
    const bool print = setup_.mode == Mode::Print;
    const QString verb = print ? "print" : "export";
    QString hint;
    QString rangeError;
    const bool pagesValid = selectedSheets(&rangeError).has_value();
    bool targetValid = true;
    if (!print) {
        const QFileInfo file(path_);
        if (path_.isEmpty()) {
            targetValid = false;
            hint = "Choose a file to export to.";
        } else if (file.exists() || file.isSymLink()) {
            targetValid = false;
            hint = "“" + file.fileName() +
                   "” already exists. Choose a new name; an export never replaces a file.";
        } else if (!file.dir().exists()) {
            targetValid = false;
            hint = "The folder for “" + file.fileName() + "” does not exist.";
        }
    } else if (!setup_.printer || !setup_.printer->isValid()) {
        targetValid = false;
        hint = "No printer is available.";
    }
    size_t broken = 0, changed = 0, raster = 0;
    if (preflight_)
        for (const auto &finding : preflight_->findings) {
            if (finding.severity != SheetExportSeverity::Blocking)
                continue;
            if (finding.code == "viewBroken")
                broken += std::max<size_t>(1, finding.views.size());
            else if (finding.code == "viewChanged")
                changed += finding.views.size();
            else if (finding.code == "rasterUnavailable")
                raster += finding.views.size();
        }
    if (!pagesValid)
        hint = rangeError;
    else if (!preflightError_.isEmpty())
        hint = preflightError_;
    else if (broken > 0)
        hint = broken == 1 ? "Fix the broken view, or choose to " + verb + " it empty."
                           : "Fix the broken views, or choose to " + verb + " them empty.";
    else if (changed > 0)
        hint = changed == 1
                   ? "Accept the changed view, or choose to " + verb + " it as it is now."
                   : "Accept the changed views, or choose to " + verb + " them as they are now.";
    else if (raster > 0)
        hint = "Raster views cannot be drawn here; choose to " + verb + " them as empty frames.";
    else if (targetValid && !preflight_)
        hint = "Checking the drawing set…";
    hint_->setText(hint);
    const bool ready = pagesValid && targetValid && preflight_ && preflightError_.isEmpty() &&
                       !preflight_->blocked() && !checking();
    confirm_->setEnabled(ready);
    confirm_->setAccessibleDescription(ready ? QString{} : hint);
}

void SheetExportDialog::chooseFile() {
    QFileDialog dialog(this, "Export sheets as PDF", path_, "PDF document (*.pdf)");
    dialog.setObjectName("sheetExportFileDialog");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix("pdf");
    // An export never replaces a file; the footer says so instead of a replace prompt.
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) {
        path_ = dialog.selectedFiles().front();
        if (!path_.endsWith(".pdf", Qt::CaseInsensitive))
            path_ += ".pdf";
    }
    choose_->setFocus(Qt::OtherFocusReason);
    refreshTarget();
    refreshFooter();
}
void SheetExportDialog::choosePrinter() {
    auto *printer = setup_.printer;
    if (!printer)
        return;
    const auto order = orderedDrawingSheets(doc_);
    QPrintDialog dialog(printer, this);
    dialog.setObjectName("sheetPrintSystemDialog");
    dialog.setOptions(QAbstractPrintDialog::PrintPageRange |
                      QAbstractPrintDialog::PrintCurrentPage | QAbstractPrintDialog::PrintToFile |
                      QAbstractPrintDialog::PrintShowPageSize);
    dialog.setMinMax(1, int(order.size()));
    // The page choice goes to the print dialog and comes back from it.
    if (const auto sheets = selectedSheets(nullptr); sheets && !sheets->empty()) {
        const auto first = std::find(order.begin(), order.end(), sheets->front()) - order.begin();
        const auto last = std::find(order.begin(), order.end(), sheets->back()) - order.begin();
        if (current_->isChecked())
            printer->setPrintRange(QPrinter::CurrentPage);
        else if (range_->isChecked() && size_t(last - first + 1) == sheets->size()) {
            printer->setPrintRange(QPrinter::PageRange);
            printer->setFromTo(int(first) + 1, int(last) + 1);
        } else
            printer->setPrintRange(QPrinter::AllPages);
    }
    if (dialog.exec() == QDialog::Accepted) {
        switch (dialog.printRange()) {
        case QAbstractPrintDialog::CurrentPage:
            if (current_->isEnabled())
                current_->setChecked(true);
            break;
        case QAbstractPrintDialog::PageRange: {
            const auto from = std::clamp(dialog.fromPage(), 1, int(order.size()));
            const auto to = std::clamp(dialog.toPage(), from, int(order.size()));
            rangeText_->setText(from == to ? pageLabel(order[size_t(from - 1)])
                                           : pageLabel(order[size_t(from - 1)]) + "–" +
                                                 pageLabel(order[size_t(to - 1)]));
            range_->setChecked(true);
            break;
        }
        default:
            all_->setChecked(true);
        }
        // Printing is always at actual size on the chosen paper.
        printer->setFullPage(true);
        printer->setPageMargins(QMarginsF(), QPageLayout::Millimeter);
    }
    choose_->setFocus(Qt::OtherFocusReason);
    refreshTarget();
    refreshPages();
    refreshList();
    schedule();
}

void SheetExportDialog::close(Outcome outcome, Id target) {
    outcome_ = outcome;
    target_ = target;
    if (outcome == Outcome::Cancel)
        reject();
    else
        accept();
}
} // namespace sketchy
