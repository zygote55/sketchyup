#include "app/quantities_panel.hpp"
#include "app/quantities_dialogs.hpp"
#include "app/quantities_job.hpp"
#include "automation/inspection.hpp"
#include "automation/report_commands.hpp"
#include "automation/report_schedules.hpp"
#include "core/report.hpp"
#include "core/selection.hpp"
#include <QAbstractButton>
#include <QAccessible>
#include <QAccessibleEvent>
#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace sketchy {
namespace {

// The measure as the locale groups its digits (§19.5). The engine's text has the precision's
// decimals, and that count is kept.
QString groupedDigits(const QString &value) {
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok)
        return value;
    const auto dot = value.indexOf('.');
    const int decimals = dot < 0 ? 0 : int(value.size() - dot - 1);
    return QLocale().toString(number, 'f', decimals);
}

// "m2" reads as m², "mm3" as mm³ (§19.5). The engine's codes name the document's units.
QString unitText(const QString &code) {
    if (code.endsWith('2'))
        return code.chopped(1) + QChar(0x00B2);
    if (code.endsWith('3'))
        return code.chopped(1) + QChar(0x00B3);
    return code;
}

// Accessible names say the unit in words (§19.5).
QString unitWords(const QString &code) {
    if (code == "m2")
        return "square metres";
    if (code == "m3")
        return "cubic metres";
    if (code == "mm2")
        return "square millimetres";
    if (code == "mm3")
        return "cubic millimetres";
    if (code == "ft2")
        return "square feet";
    if (code == "ft3")
        return "cubic feet";
    return code;
}

QString measureText(const QString &value, const QString &code) {
    return groupedDigits(value) + " " + unitText(code);
}

QString measureWords(const QString &value, const QString &code) {
    return groupedDigits(value) + " " + unitWords(code);
}

// The engine's group names in the panel's words (§19.2).
QString groupLabel(const QString &name, const QString &groupBy) {
    if (name == "untagged")
        return "Untagged";
    if (name == "—")
        return groupBy == "definition" ? "No definition" : "No material";
    if (name.isEmpty())
        return name;
    return name.at(0).toUpper() + name.mid(1);
}

QString countText(qulonglong value, const char *one, const char *many) {
    return QString::number(value) + " " + (value == 1 ? QString(one) : QString(many));
}

QString clockText(qint64 milliseconds) {
    const auto seconds = milliseconds / 1000;
    return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

// The refusal words of the schedule actions (§19.9). Each says that nothing was saved, as §16.6
// asks.
QString scheduleRefusal(const InspectionError &error) {
    const auto message = QString::fromStdString(error.what());
    const auto code = QString::fromStdString(error.code());
    if (code == "LIMIT_EXCEEDED") {
        if (message.contains("64") || message.contains("models"))
            return "Nothing was saved. This computer's schedule file holds 64 models, its limit. "
                   "Open "
                   "each model whose schedules you no longer need and delete them there.";
        return "Nothing was saved. This model has 32 schedules. Delete one to save another.";
    }
    if (code == "FILE_CHANGED")
        return "The schedule file changed in another process. Nothing was saved. Try again.";
    if (code == "OUTPUT_ERROR" && message.contains("in progress"))
        return "Another SketchyUp window is changing schedules. Try again.";
    return "Nothing was saved. " + message;
}

// The export refusals (§19.9): each names the file and says that nothing was written.
QString exportRefusal(const ReportJob &job, const QString &path) {
    if (job.code() == "FILE_EXISTS")
        return QString("Nothing was written. %1 already exists in this folder. An existing file is "
                       "never replaced.")
            .arg(QFileInfo(path).fileName());
    if (job.code() == "LIMIT_EXCEEDED")
        return "Nothing was written. This report is over the 256 MiB limit. Group less, pick a "
               "tag, "
               "or choose Visible only.";
    return "Nothing was written. " + job.message();
}

// The stale references of a schedule, in the store's words: "tag 7", "material 3" (§19.7).
QStringList missingParts(const QJsonObject &missing) {
    QStringList parts;
    const std::pair<const char *, const char *> kinds[]{
        {"tags", "tag"}, {"materials", "material"}, {"definitions", "definition"}};
    for (const auto &[field, word] : kinds)
        for (const auto &value : missing[field].toArray())
            parts << QString(word) + " " + value.toString();
    return parts;
}

QString joinWords(const QStringList &parts) {
    if (parts.size() <= 1)
        return parts.join(QString());
    return parts.mid(0, parts.size() - 1).join(", ") + " and " + parts.last();
}

// A schedule's summary: "Definition · Included · all metrics · precision 2" (§19.6).
QString settingWords(const QJsonObject &request) {
    const auto group = request["groupBy"].toString("none");
    const QString groupName = group == "kind"         ? "Kind"
                              : group == "tag"        ? "Tag"
                              : group == "material"   ? "Material"
                              : group == "definition" ? "Definition"
                                                      : "None";
    const auto visible = request["filters"].toObject()["visibility"].toString() == "visible";
    const auto precision = request["precision"];
    QString precisionText = "follows the model";
    if (precision.isString())
        precisionText = precision.toString();
    else if (precision.isDouble())
        precisionText = QString::number(precision.toInt());
    return QString("%1 · %2 · all metrics · precision %3")
        .arg(groupName, visible ? "Visible only" : "Included", precisionText);
}

// The words a stored schedule shows in the Save dialog (§19.2): grouping, hidden content, metrics,
// precision pinned. The selection is never stored.
QStringList storedWords(const QJsonObject &request, int documentPrecision) {
    const auto group = settingWords(request).split(" · ").first();
    const auto visible = request["filters"].toObject()["visibility"].toString() == "visible";
    const auto precision = request["precision"];
    QString precisionText = precision.isString()   ? precision.toString()
                            : precision.isDouble() ? QString::number(precision.toInt())
                                                   : (documentPrecision == fullDisplayPrecision
                                                          ? QString("full")
                                                          : QString::number(documentPrecision));
    return QStringList{
        "Grouping: " + group,
        QString("Hidden content: %1").arg(visible ? "Visible only" : "Include hidden"),
        "Metrics: all four tables", "Precision: " + precisionText + " (pinned at save)"};
}

QString idList(const std::set<Id> &ids) {
    QStringList parts;
    for (auto id : ids)
        parts << QString::number(id);
    return parts.join(",");
}

// The IDs in a JSON array of decimal strings.
std::set<Id> idsFrom(const QJsonArray &values) {
    std::set<Id> result;
    for (const auto &value : values) {
        bool ok = false;
        const auto id = value.toString().toULongLong(&ok);
        if (ok)
            result.insert(id);
    }
    return result;
}

void announce(QWidget *widget) {
    QAccessibleEvent event(widget, QAccessible::Alert);
    QAccessible::updateAccessibility(&event);
}

} // namespace

QuantitiesPanel::QuantitiesPanel(Document &document, Viewport &viewport, QWidget *parent)
    : QWidget(parent), doc_(document), view_(viewport) {
    setObjectName("quantitiesPanel");
    setAccessibleName("Quantities");
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("quantitiesScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *body = new QWidget;
    scroll->setWidget(body);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(4, 4, 4, 4);

    // The heading, and the line that says a schedule is the view (§19.6).
    auto *heading = new QHBoxLayout;
    header_ = new QLabel("Quantities");
    header_->setObjectName("quantitiesHeader");
    header_->setStyleSheet("font-weight: bold;");
    heading->addWidget(header_, 1);
    headerRevision_ = new QLabel;
    headerRevision_->setObjectName("quantitiesHeaderRevision");
    heading->addWidget(headerRevision_);
    layout->addLayout(heading);
    backLive_ = new QLabel;
    backLive_->setObjectName("quantitiesScheduleView");
    backLive_->setWordWrap(true);
    layout->addWidget(backLive_);
    backButton_ = new QPushButton("Back to the live view");
    backButton_->setObjectName("quantitiesBackToLive");
    backButton_->setAutoDefault(false);
    layout->addWidget(backButton_, 0, Qt::AlignLeft);
    connect(backButton_, &QPushButton::clicked, this, [this] { backToLive(); });

    // Group by (§19.2).
    auto *group = new QHBoxLayout;
    group->addWidget(new QLabel("Group by"));
    groupBy_ = new QComboBox;
    groupBy_->setObjectName("quantitiesGroupBy");
    groupBy_->setAccessibleName("Group by");
    groupBy_->addItem("None", "none");
    groupBy_->addItem("Kind", "kind");
    groupBy_->addItem("Tag", "tag");
    groupBy_->addItem("Material", "material");
    groupBy_->addItem("Definition", "definition");
    group->addWidget(groupBy_, 1);
    layout->addLayout(group);
    // Keyboard focus lands on Group by when the section is opened (§19.10).
    setFocusProxy(groupBy_);
    connect(groupBy_, &QComboBox::activated, this, [this] { requestRun(true); });

    // Hidden content (§19.2): two radio buttons, one Tab stop, the arrows choose.
    auto *hidden = new QHBoxLayout;
    includeHidden_ = new QRadioButton("Include hidden");
    includeHidden_->setObjectName("quantitiesIncludeHidden");
    includeHidden_->setAccessibleName("Hidden content: Include hidden");
    visibleOnly_ = new QRadioButton("Visible only");
    visibleOnly_->setObjectName("quantitiesVisibleOnly");
    visibleOnly_->setAccessibleName("Hidden content: Visible only");
    auto *choice = new QButtonGroup(this);
    choice->addButton(includeHidden_);
    choice->addButton(visibleOnly_);
    includeHidden_->setChecked(true);
    hidden->addWidget(includeHidden_);
    hidden->addWidget(visibleOnly_);
    layout->addLayout(hidden);
    visibleCaption_ = new QLabel("Hidden tags, groups and bodies are left out. Geometry hidden for "
                                 "this session only is still counted.");
    visibleCaption_->setObjectName("quantitiesVisibleCaption");
    visibleCaption_->setWordWrap(true);
    layout->addWidget(visibleCaption_);
    connect(choice, &QButtonGroup::buttonToggled, this,
            [this](QAbstractButton *, bool) { requestRun(true); });

    // The pickers (§19.2): a row that names its value and opens a popover of check rows.
    auto picker = [this, layout](const QString &name, const QString &note, const QString &kind) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(note.isEmpty() ? name : name + "  " + note), 1);
        auto *value = new QPushButton("none ▸");
        value->setObjectName("quantities" + kind + "Value");
        value->setFlat(true);
        value->setAutoDefault(false);
        row->addWidget(value);
        layout->addLayout(row);
        connect(value, &QPushButton::clicked, this,
                [this, kind, value] { pickerMenu(kind, value); });
        return value;
    };
    tagsValue_ = picker("Tags", {}, "Tags");
    materialsValue_ = picker("Materials", "areas only", "Materials");
    definitionsValue_ = picker("Definitions", {}, "Definitions");

    // A filter that names a tag, material or definition that no longer exists (§19.2).
    missing_ = new QWidget;
    missing_->setObjectName("quantitiesMissing");
    missingLayout_ = new QVBoxLayout(missing_);
    missingLayout_->setContentsMargins(0, 0, 0, 0);
    missing_->hide();
    layout->addWidget(missing_);

    // Selection only (§19.2): greyed out, with the hint, while nothing is selected.
    selectionOnly_ = new QCheckBox;
    selectionOnly_->setObjectName("quantitiesSelectionOnly");
    selectionOnly_->setAccessibleName("Selection only");
    layout->addWidget(selectionOnly_);
    connect(selectionOnly_, &QCheckBox::toggled, this, [this] { requestRun(true); });
    selectionHint_ = new QLabel("Select bodies in the model to use this.");
    selectionHint_->setObjectName("quantitiesSelectionHint");
    selectionHint_->setWordWrap(true);
    layout->addWidget(selectionHint_);

    error_ = new QLabel;
    error_->setObjectName("quantitiesError");
    error_->setWordWrap(true);
    error_->setStyleSheet("color: #a8322b;");
    error_->hide();
    layout->addWidget(error_);
    status_ = new QLabel;
    status_->setObjectName("quantitiesStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);

    // The computing line, the progress and Cancel (§19.3). Cancel is not the default.
    computing_ = new QWidget;
    computing_->setObjectName("quantitiesComputing");
    auto *computingLayout = new QVBoxLayout(computing_);
    computingLayout->setContentsMargins(0, 0, 0, 0);
    computingText_ = new QLabel;
    computingText_->setObjectName("quantitiesComputingText");
    computingText_->setWordWrap(true);
    computingLayout->addWidget(computingText_);
    bar_ = new QProgressBar;
    bar_->setObjectName("quantitiesProgress");
    bar_->setRange(0, 0);
    bar_->setTextVisible(false);
    computingLayout->addWidget(bar_);
    auto *cancelRow = new QHBoxLayout;
    cancel_ = new QPushButton("Cancel");
    cancel_->setObjectName("quantitiesCancel");
    cancel_->setAutoDefault(false);
    cancelRow->addWidget(cancel_);
    auto *escHint = new QLabel("Esc also cancels");
    escHint->setObjectName("quantitiesCancelHint");
    cancelRow->addWidget(escHint, 1);
    computingLayout->addLayout(cancelRow);
    computing_->hide();
    layout->addWidget(computing_);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (!job_)
            return;
        cancelledSignature_ = jobSignature_;
        queued_ = false;
        job_->requestCancel();
    });

    buildTotals();
    layout->addWidget(totals_);

    // The export row (§19.8).
    auto *exports = new QHBoxLayout;
    exportCsv_ = new QPushButton("Export CSV…");
    exportCsv_->setObjectName("quantitiesExportCsv");
    exportJson_ = new QPushButton("Export JSON…");
    exportJson_->setObjectName("quantitiesExportJson");
    exports->addWidget(exportCsv_);
    exports->addWidget(exportJson_);
    layout->addLayout(exports);
    connect(exportCsv_, &QPushButton::clicked, this, [this] { exportFile(true, scheduleName_); });
    connect(exportJson_, &QPushButton::clicked, this, [this] { exportFile(false, scheduleName_); });

    buildSchedules();
    layout->addWidget(schedules_);
    layout->addStretch(1);

    ticker_ = new QTimer(this);
    ticker_->setInterval(1000);
    connect(ticker_, &QTimer::timeout, this, [this] {
        // The computing line appears after about a second, and its elapsed time counts up (§19.3).
        if (job_)
            showComputing(true);
    });
    syncControls();
}

QuantitiesPanel::~QuantitiesPanel() {
    if (job_) {
        job_->requestCancel();
        job_->wait();
    }
}

void QuantitiesPanel::buildTotals() {
    totals_ = new QWidget;
    totals_->setObjectName("quantitiesTotals");
    auto *layout = new QVBoxLayout(totals_);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *titleRow = new QHBoxLayout;
    totalsTitle_ = new QLabel("All");
    totalsTitle_->setObjectName("quantitiesTotalsTitle");
    totalsTitle_->setStyleSheet("font-weight: bold;");
    titleRow->addWidget(totalsTitle_, 1);
    precision_ = new QLabel;
    precision_->setObjectName("quantitiesPrecision");
    titleRow->addWidget(precision_);
    layout->addLayout(titleRow);
    empty_ = new QLabel("No bodies in this model. Draw something, or import a model.");
    empty_->setObjectName("quantitiesEmpty");
    empty_->setWordWrap(true);
    empty_->hide();
    layout->addWidget(empty_);

    auto *grid = new QGridLayout;
    auto row = [grid](int at, const QString &name, const QString &caption, QLabel *&value,
                      QLabel *&captionLabel) {
        captionLabel = new QLabel(caption);
        captionLabel->setObjectName(name + "Caption");
        grid->addWidget(captionLabel, at, 0);
        value = new QLabel;
        value->setObjectName(name);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(value, at, 1);
    };
    QLabel *unused = nullptr;
    row(0, "quantitiesArea", "Area", area_, unused);
    row(1, "quantitiesVolume", "Volume", volume_, unused);
    row(2, "quantitiesContexts", "Contexts", contexts_, unused);
    row(3, "quantitiesInstances", "Instances", instances_, unused);
    row(4, "quantitiesDefinitions", "Definitions", definitionCount_, definitionsCaption_);
    row(5, "quantitiesUnvalidated", "Not counted in volume", unvalidated_, unvalidatedCaption_);
    layout->addLayout(grid);

    groups_ = new QTreeWidget;
    groups_->setObjectName("quantitiesGroups");
    groups_->setAccessibleName("Totals and groups");
    groups_->setHeaderLabels({"Group", "Area", "Volume"});
    groups_->setRootIsDecorated(false);
    groups_->setSelectionMode(QAbstractItemView::SingleSelection);
    groups_->header()->setStretchLastSection(false);
    groups_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    groups_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    groups_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    groups_->hide();
    layout->addWidget(groups_);

    revisionLine_ = new QLabel;
    revisionLine_->setObjectName("quantitiesRevision");
    layout->addWidget(revisionLine_);

    notSolids_ = new QWidget;
    notSolids_->setObjectName("quantitiesNotSolids");
    auto *notSolidsOuter = new QVBoxLayout(notSolids_);
    notSolidsOuter->setContentsMargins(0, 0, 0, 0);
    auto *notSolidsHeading = new QHBoxLayout;
    auto *title = new QLabel("Not solids");
    title->setStyleSheet("font-weight: bold;");
    notSolidsHeading->addWidget(title);
    auto *words = new QLabel("area counted, volume not");
    words->setObjectName("quantitiesNotSolidsWords");
    notSolidsHeading->addWidget(words, 1, Qt::AlignRight);
    notSolidsOuter->addLayout(notSolidsHeading);
    notSolidsLayout_ = new QVBoxLayout;
    notSolidsOuter->addLayout(notSolidsLayout_);
    notSolids_->hide();
    layout->addWidget(notSolids_);

    terrain_ = new QWidget;
    terrain_->setObjectName("quantitiesTerrain");
    terrainLayout_ = new QVBoxLayout(terrain_);
    terrainLayout_->setContentsMargins(0, 0, 0, 0);
    terrain_->hide();
    layout->addWidget(terrain_);
}

void QuantitiesPanel::buildSchedules() {
    schedules_ = new QWidget;
    schedules_->setObjectName("quantitiesSchedules");
    auto *layout = new QVBoxLayout(schedules_);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QHBoxLayout;
    auto *title = new QLabel("Saved schedules");
    title->setStyleSheet("font-weight: bold;");
    heading->addWidget(title, 1);
    scheduleCount_ = new QLabel;
    scheduleCount_->setObjectName("quantitiesScheduleCount");
    heading->addWidget(scheduleCount_);
    layout->addLayout(heading);
    storeError_ = new QLabel;
    storeError_->setObjectName("quantitiesStoreError");
    storeError_->setWordWrap(true);
    storeError_->setStyleSheet("color: #a8322b;");
    storeError_->hide();
    layout->addWidget(storeError_);
    scheduleList_ = new QListWidget;
    scheduleList_->setObjectName("quantitiesScheduleList");
    scheduleList_->setAccessibleName("Saved schedules");
    scheduleList_->setSelectionMode(QAbstractItemView::SingleSelection);
    scheduleList_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(scheduleList_);
    // Enter runs, F2 renames, Delete asks first, and Shift+F10 or the menu key opens Actions
    // (§19.10).
    auto *rename = new QAction(scheduleList_);
    rename->setShortcut(Qt::Key_F2);
    rename->setShortcutContext(Qt::WidgetShortcut);
    scheduleList_->addAction(rename);
    connect(rename, &QAction::triggered, this, [this] {
        if (auto *item = scheduleList_->currentItem())
            renameSchedule(item->data(Qt::UserRole).toString());
    });
    auto *remove = new QAction(scheduleList_);
    remove->setShortcut(Qt::Key_Delete);
    remove->setShortcutContext(Qt::WidgetShortcut);
    scheduleList_->addAction(remove);
    connect(remove, &QAction::triggered, this, [this] {
        if (auto *item = scheduleList_->currentItem())
            deleteSchedule(item->data(Qt::UserRole).toString());
    });
    connect(scheduleList_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { runSchedule(item->data(Qt::UserRole).toString()); });
    connect(scheduleList_, &QListWidget::customContextMenuRequested, this, [this](QPoint at) {
        if (auto *item = scheduleList_->itemAt(at))
            scheduleActions(item->data(Qt::UserRole).toString(),
                            scheduleList_->visualItemRect(item).bottomLeft());
    });
    otherModels_ = new QLabel;
    otherModels_->setObjectName("quantitiesOtherModels");
    otherModels_->setWordWrap(true);
    otherModels_->hide();
    layout->addWidget(otherModels_);
    scheduleNote_ = new QLabel;
    scheduleNote_->setObjectName("quantitiesScheduleNote");
    scheduleNote_->setWordWrap(true);
    scheduleNote_->hide();
    layout->addWidget(scheduleNote_);
    saveSchedule_ = new QPushButton("Save as schedule…");
    saveSchedule_->setObjectName("quantitiesSaveSchedule");
    layout->addWidget(saveSchedule_);
    connect(saveSchedule_, &QPushButton::clicked, this,
            [this] { saveSchedule(liveRequest(false), {}); });
}

void QuantitiesPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // Opening the section computes the totals (§19.1). A pass cancelled by hiding starts again
    // here.
    cancelledSignature_.clear();
    reloadSchedules();
    requestRun(false);
    syncControls();
}

void QuantitiesPanel::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    // The section computes only while it is open (§19.1). A running pass stops, quietly.
    if (job_ && !jobExport_) {
        queued_ = false;
        job_->requestCancel();
    }
}

void QuantitiesPanel::keyPressEvent(QKeyEvent *event) {
    // Esc cancels a run while focus is in the section (§19.10).
    if (event->key() == Qt::Key_Escape && job_) {
        cancelledSignature_ = jobSignature_;
        queued_ = false;
        job_->requestCancel();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void QuantitiesPanel::focusGroupBy() {
    // View > Quantities re-reads the schedules, even when the section is already shown (§19.6).
    reloadSchedules();
    requestRun(false);
    syncControls();
    groupBy_->setFocus(Qt::OtherFocusReason);
}

void QuantitiesPanel::refresh() {
    if (!isVisible())
        return;
    // A new revision may have changed the schedules' stale state and the missing filters (§19.7).
    if (!listed_ || listedRevision_ != doc_.revision()) {
        listed_ = true;
        listedRevision_ = doc_.revision();
        reloadSchedules();
    }
    // requestRun returns at once when nothing it depends on has changed.
    requestRun(false);
    syncControls();
}

QJsonObject QuantitiesPanel::liveRequest(bool withSelection) const {
    QJsonArray tags, materials, definitions;
    for (auto id : tags_)
        tags.append(QString::number(id));
    for (auto id : materials_)
        materials.append(QString::number(id));
    for (auto id : definitions_)
        definitions.append(QString::number(id));
    QJsonObject filters{{"visibility", visibleOnly_->isChecked() ? "visible" : "all"},
                        {"tags", tags},
                        {"materials", materials},
                        {"definitions", definitions}};
    if (withSelection && selectionOnly_->isChecked())
        filters["selection"] = true;
    return QJsonObject{
        {"metrics", "all"}, {"groupBy", groupBy_->currentData().toString()}, {"filters", filters}};
}

std::set<Id> QuantitiesPanel::selectedBodies() const {
    std::set<Id> result;
    for (const auto &entity : view_.selectionState().entities())
        result.insert(entity.body);
    return result;
}

bool QuantitiesPanel::filtersResolved() const {
    for (auto id : tags_)
        if (!doc_.tags().contains(id))
            return false;
    for (auto id : materials_)
        if (!doc_.materials().contains(id))
            return false;
    for (auto id : definitions_)
        if (!doc_.definitions().contains(id))
            return false;
    return true;
}

void QuantitiesPanel::requestRun(bool announceRun) {
    if (!isVisible() || (job_ && jobExport_))
        return; // an export owns the job until its file is written
    const bool scheduled = !scheduleName_.isEmpty();
    const auto revision = doc_.revision();
    const QJsonObject request = scheduled ? scheduleRequest_ : liveRequest(true);
    const bool useSelection = !scheduled && selectionOnly_->isChecked();
    const auto bodies = useSelection ? selectedBodies() : std::set<Id>{};
    const auto signature =
        QString("%1|%2|%3|%4")
            .arg(revision)
            .arg(scheduled ? "schedule:" + scheduleName_ : QString("live"))
            .arg(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)))
            .arg(useSelection ? idList(bodies) : QString("-"));
    if (job_) {
        if (jobSignature_ == signature)
            return;
        // A newer revision, filter or selection supersedes the pass in progress (§19.3). Its
        // numbers are dropped when it ends, and a pass starts for the newest request.
        queued_ = true;
        jobAnnounce_ = jobAnnounce_ || announceRun;
        job_->requestCancel();
        return;
    }
    if (signature == cancelledSignature_)
        return; // Cancel stands until the model, the filters or the selection change
    if (shown_.valid && shown_.signature == signature)
        return;
    if (!scheduled && !filtersResolved()) {
        setError("Remove the missing filters before the totals can run. Save and Run wait for it.");
        return;
    }
    try {
        auto snapshot = std::make_shared<const Document>(doc_.readSnapshot());
        const auto parsed = reportRequestFrom(*snapshot, request, !scheduled);
        auto gate = runGateForTesting;
        auto *job = new ReportJob(
            [snapshot, parsed, bodies, useSelection, gate](const ReportHooks &hooks) {
                if (gate)
                    gate(*hooks.cancel);
                return reportDescription(*snapshot, parsed, useSelection ? &bodies : nullptr,
                                         hooks);
            },
            this);
        jobSignature_ = signature;
        jobRevision_ = revision;
        setError({});
        startJob(job, false, announceRun);
    } catch (const InspectionError &error) {
        setError(QString::fromStdString(error.what()) + ".");
    }
}

void QuantitiesPanel::startJob(ReportJob *job, bool exporting, bool announceRun) {
    job_ = job;
    jobExport_ = exporting;
    jobAnnounce_ = announceRun;
    queued_ = false;
    bar_->setRange(0, 0);
    clock_.start();
    connect(job, &ReportJob::progressed, this, [this](qulonglong done, qulonglong total) {
        if (!job_ || jobExport_)
            return;
        // The bar becomes determinate once the engine reports the records it has visited (§19.3).
        bar_->setRange(0, int(std::min<qulonglong>(total, 2147483647ULL)));
        bar_->setValue(int(std::min<qulonglong>(done, 2147483647ULL)));
    });
    connect(job, &QThread::finished, this, [this] { jobFinished(); });
    syncControls();
    job->start();
    ticker_->start();
}

void QuantitiesPanel::showComputing(bool visible) {
    computing_->setVisible(visible);
    if (visible) {
        const auto elapsed = clockText(clock_.elapsed());
        computingText_->setText(
            jobExport_
                ? QString("Writing the file… elapsed %1").arg(elapsed)
                : QString("● Computing revision %1 · elapsed %2").arg(jobRevision_).arg(elapsed));
    }
    // The old totals stay readable and dimmed while a run is in progress (§19.3).
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(totals_->graphicsEffect());
    if (visible && !effect) {
        effect = new QGraphicsOpacityEffect(totals_);
        totals_->setGraphicsEffect(effect);
    }
    if (effect)
        effect->setOpacity(visible ? 0.45 : 1.0);
}

void QuantitiesPanel::jobFinished() {
    auto *job = job_;
    if (!job)
        return;
    job_ = nullptr;
    ticker_->stop();
    showComputing(false);
    const auto revision = jobRevision_;
    const auto signature = jobSignature_;
    const auto announceRun = jobAnnounce_;
    if (jobExport_) {
        finishExport(job);
        return;
    }
    if (job->cancelled()) {
        job->deleteLater();
        if (queued_) {
            queued_ = false;
            requestRun(announceRun);
            return;
        }
        // The person pressed Cancel. The last complete result stays, with its own revision (§19.3).
        status_->setText(
            shown_.valid
                ? QString("Not updated: cancelled. Showing revision %1.").arg(shown_.revision)
                : QString("Not updated: cancelled. No totals are shown yet."));
        if (announceRun)
            announce(status_);
        syncControls();
        return;
    }
    if (job->failed()) {
        const auto message = QString("The totals could not be computed: %1.").arg(job->message());
        job->deleteLater();
        if (queued_) {
            queued_ = false;
            requestRun(announceRun);
            return;
        }
        setError(message);
        syncControls();
        return;
    }
    const auto data = job->data();
    job->deleteLater();
    if (queued_ || revision != doc_.revision()) {
        // Superseded by a newer revision, filter or selection: its numbers are dropped (§19.3).
        queued_ = false;
        requestRun(announceRun);
        return;
    }
    shown_ = Result{revision, signature, data, true};
    setError({});
    showResult(shown_);
    if (announceRun) {
        // One announcement per run the person waited for (§19.10).
        const auto units = data["metadata"].toObject()["units"].toObject();
        const auto areas = data["areas"].toArray();
        const auto volumes = data["volumes"].toArray();
        const auto invalid = data["invalid"].toArray().size();
        QStringList parts{QString("Computed at revision %1").arg(revision)};
        if (areas.size() == 1)
            parts << "area " + measureWords(areas.first().toObject()["area"].toString(),
                                            units["area"].toString());
        if (volumes.size() == 1)
            parts << "volume " + measureWords(volumes.first().toObject()["volume"].toString(),
                                              units["volume"].toString());
        status_->setText(
            parts.join(", ") +
            QString(". %1 not %2.").arg(invalid).arg(invalid == 1 ? "solid" : "solids"));
        announce(status_);
    } else {
        status_->clear();
    }
    syncControls();
}

void QuantitiesPanel::finishExport(ReportJob *job) {
    const auto path = exportPath_;
    const auto schedule = exportSchedule_;
    const auto csv = exportingCsv_;
    const auto revision = jobRevision_;
    const auto data = job->data();
    const auto cancelled = job->cancelled();
    const auto failed = job->failed();
    const auto refusal = failed ? exportRefusal(*job, path) : QString();
    job->deleteLater();
    if (cancelled) {
        status_->setText("Cancelled. Nothing was written.");
        announce(status_);
    } else if (failed) {
        setError(refusal);
        announce(error_);
    } else {
        status_->setText(
            QString("Written: %1. The model is unchanged.").arg(QFileInfo(path).fileName()));
        announce(status_);
        exportReport(path, csv, schedule, data, revision);
    }
    syncControls();
}

void QuantitiesPanel::showResult(const Result &result) {
    const auto &data = result.data;
    const auto metadata = data["metadata"].toObject();
    const auto units = metadata["units"].toObject();
    const auto areaCode = units["area"].toString();
    const auto volumeCode = units["volume"].toString();
    const auto groupBy = metadata["groupBy"].toString("none");
    const auto precisionValue = metadata["precision"];
    precision_->setText("precision " + (precisionValue.isString()
                                            ? precisionValue.toString()
                                            : QString::number(precisionValue.toInt())));
    headerRevision_->setText(QString("revision %1").arg(result.revision));
    revisionLine_->setText(QString("Computed at revision %1").arg(result.revision));

    // The rows of the three tables, merged by key (§19.2). A group with no key of its own is "All".
    // The areas table has a row per material within a group, so a group's area is the sum of them.
    struct Group {
        QString key, name, areaText, volumeText;
        qulonglong contexts{}, instances{}, definitions{}, unvalidated{};
        double area{}, volume{};
        int areaDecimals{}, volumeDecimals{};
        bool hasArea{}, hasVolume{};
    };
    std::vector<Group> groups;
    auto find = [&groups](const QString &key, const QString &name) -> Group & {
        for (auto &group : groups)
            if (group.key == key)
                return group;
        groups.push_back(Group{key, name, {}, {}});
        return groups.back();
    };
    for (const auto &value : data["counts"].toArray()) {
        const auto row = value.toObject();
        auto &group = find(row["key"].toString(), row["name"].toString());
        group.contexts = qulonglong(row["contexts"].toInt());
        group.instances = qulonglong(row["instances"].toInt());
        group.definitions = qulonglong(row["definitions"].toInt());
    }
    auto decimalsOf = [](const QString &text) {
        const auto dot = text.indexOf('.');
        return dot < 0 ? 0 : int(text.size() - dot - 1);
    };
    for (const auto &value : data["areas"].toArray()) {
        const auto row = value.toObject();
        auto &group = find(row["key"].toString(), row["name"].toString());
        const auto text = row["area"].toString();
        group.area += text.toDouble();
        group.areaDecimals = std::max(group.areaDecimals, decimalsOf(text));
        group.hasArea = true;
    }
    for (const auto &value : data["volumes"].toArray()) {
        const auto row = value.toObject();
        auto &group = find(row["key"].toString(), row["name"].toString());
        const auto text = row["volume"].toString();
        group.volume += text.toDouble();
        group.volumeDecimals = std::max(group.volumeDecimals, decimalsOf(text));
        group.hasVolume = true;
        group.unvalidated = qulonglong(row["unvalidated"].toInt());
    }
    // Each figure keeps the precision's decimals, as the engine wrote them.
    for (auto &group : groups) {
        if (group.hasArea)
            group.areaText = QString::number(group.area, 'f', group.areaDecimals);
        if (group.hasVolume)
            group.volumeText = QString::number(group.volume, 'f', group.volumeDecimals);
    }

    const bool grouped = groupBy != "none";
    const bool visibleOnly = visibleOnly_->isChecked();
    totalsTitle_->setText(visibleOnly ? QString("Visible only")
                          : grouped   ? QString("Totals")
                                      : QString("All"));
    empty_->setVisible(doc_.bodies().empty());

    // The engine's "All" row is the whole model's total in every grouping (ADR 0175 §1), so the
    // totals come from it, and the group table lists the other rows.
    QString areaValue, volumeValue;
    qulonglong contexts = 0, instances = 0, definitions = 0, unvalidated = 0;
    for (const auto &group : groups)
        if (group.key == "All") {
            areaValue = group.areaText;
            volumeValue = group.volumeText;
            contexts = group.contexts;
            instances = group.instances;
            definitions = group.definitions;
            unvalidated = group.unvalidated;
        }
    // With nothing to count the engine emits no rows for the measure; the All row still shows zeros
    // (§19.1). A report that does not ask for the measure has no figure at all, and shows a dash.
    const auto decimals = precisionValue.isDouble() ? precisionValue.toInt() : 2;
    if (areaValue.isEmpty() && data.contains("areas"))
        areaValue = QString::number(0.0, 'f', decimals);
    if (volumeValue.isEmpty() && data.contains("volumes"))
        volumeValue = QString::number(0.0, 'f', decimals);
    const auto dashed = QString("—");
    area_->setText(areaValue.isEmpty() ? dashed : measureText(areaValue, areaCode));
    area_->setAccessibleName(areaValue.isEmpty() ? QString("Area, not reported")
                                                 : "Area, " + measureWords(areaValue, areaCode));
    volume_->setText(volumeValue.isEmpty() ? dashed : measureText(volumeValue, volumeCode));
    volume_->setAccessibleName(volumeValue.isEmpty()
                                   ? QString("Volume, not reported")
                                   : "Volume, " + measureWords(volumeValue, volumeCode));
    contexts_->setText(QString::number(contexts));
    instances_->setText(QString::number(instances));
    definitionCount_->setText(grouped ? dashed : QString::number(definitions));
    definitionCount_->setVisible(!grouped);
    definitionsCaption_->setVisible(!grouped);
    unvalidated_->setText(QString::number(unvalidated));
    unvalidated_->setAccessibleName(QString("Not counted in volume, %1").arg(unvalidated));

    // The group table (§19.2): the group, its area and its volume. The counts are in the tooltip.
    groups_->clear();
    groups_->setVisible(grouped);
    if (grouped) {
        for (const auto &group : groups) {
            if (group.key == "All")
                continue;
            auto *item = new QTreeWidgetItem(groups_);
            item->setText(0, groupLabel(group.name, groupBy));
            item->setText(1, group.areaText.isEmpty() ? dashed
                                                      : measureText(group.areaText, areaCode));
            item->setText(
                2, group.volumeText.isEmpty() ? dashed : measureText(group.volumeText, volumeCode));
            const auto detail = QString("%1, %2, %3")
                                    .arg(countText(group.instances, "instance", "instances"))
                                    .arg(countText(group.contexts, "context", "contexts"))
                                    .arg(countText(group.definitions, "definition", "definitions"));
            item->setToolTip(0, detail);
            item->setData(
                0, Qt::AccessibleTextRole,
                QString("%1, area %2, volume %3, %4")
                    .arg(groupLabel(group.name, groupBy))
                    .arg(group.areaText.isEmpty() ? QString("not reported")
                                                  : measureWords(group.areaText, areaCode))
                    .arg(group.volumeText.isEmpty() ? QString("not reported")
                                                    : measureWords(group.volumeText, volumeCode))
                    .arg(detail));
        }
    }

    // The invalid list (§19.4): the words, the count, and the bodies behind them.
    while (auto *item = notSolidsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto invalid = data["invalid"].toArray();
    for (int i = 0; i < invalid.size(); ++i) {
        const auto row = invalid[i].toObject();
        const auto words = row["label"].toString();
        const auto contextCount = row["contexts"].toInt();
        const auto bodies = row["bodies"].toArray();
        const auto truncated = row["truncated"].toBool();
        const auto ids = idsFrom(bodies);
        auto *box = new QWidget;
        box->setObjectName(QString("quantitiesNotSolid%1").arg(i));
        auto *line = new QHBoxLayout(box);
        line->setContentsMargins(0, 2, 0, 2);
        auto *left = new QVBoxLayout;
        auto *title = new QLabel(QString::fromUtf8("⚠ ") + words);
        title->setObjectName(QString("quantitiesNotSolidWords%1").arg(i));
        title->setWordWrap(true);
        title->setAccessibleName(QString("Warning: %1, %2").arg(words).arg(contextCount));
        left->addWidget(title);
        const auto count = truncated ? QString("more than %1 bodies").arg(bodies.size())
                                     : countText(ids.size(), "body", "bodies");
        auto *detail = new QLabel(count + " · area counted, no volume");
        detail->setWordWrap(true);
        left->addWidget(detail);
        if (!ids.empty()) {
            auto *select = new QPushButton("Select in model");
            select->setObjectName(QString("quantitiesSelectInModel%1").arg(i));
            select->setAutoDefault(false);
            left->addWidget(select, 0, Qt::AlignLeft);
            connect(select, &QPushButton::clicked, this, [this, ids] {
                SelectionSet entities;
                for (auto id : ids)
                    entities.insert({id, SelectionKind::Body, 0});
                view_.selectEntities(entities);
            });
        }
        line->addLayout(left, 1);
        auto *countLabel = new QLabel(QString::number(contextCount));
        countLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);
        line->addWidget(countLabel);
        notSolidsLayout_->addWidget(box);
    }
    notSolids_->setVisible(!invalid.isEmpty());

    // Terrain has its own line, and is not a not-solid (§19.4, ADR 0175 §2.5).
    while (auto *item = terrainLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto terrain = data["terrain"].toArray();
    for (const auto &value : terrain) {
        const auto row = value.toObject();
        auto *title = new QLabel(QString("%1 · terrain").arg(row["name"].toString()));
        title->setStyleSheet("font-weight: bold;");
        terrainLayout_->addWidget(title);
        auto *detail = new QLabel(
            QString("%1 · %2 triangles · %3 · no volume (not a solid)")
                .arg(countText(qulonglong(row["contexts"].toInt()), "context", "contexts"))
                .arg(groupedDigits(QString::number(row["triangles"].toInt())))
                .arg(measureText(row["area"].toString(), areaCode)));
        detail->setWordWrap(true);
        terrainLayout_->addWidget(detail);
    }
    terrain_->setVisible(!terrain.isEmpty());
}

void QuantitiesPanel::syncControls() {
    const bool scheduled = !scheduleName_.isEmpty();
    header_->setText(scheduled ? QString("Schedule · %1").arg(scheduleName_)
                               : QString("Quantities"));
    backLive_->setText(
        scheduled
            ? QString("Run made %1 the view. Its own request is computed, without the selection.")
                  .arg(scheduleName_)
            : QString());
    backLive_->setVisible(scheduled);
    backButton_->setVisible(scheduled);
    // The live controls describe the live view only. A schedule keeps its own request (§19.6).
    for (auto *widget :
         std::initializer_list<QWidget *>{groupBy_, includeHidden_, visibleOnly_, tagsValue_,
                                          materialsValue_, definitionsValue_, selectionOnly_}) {
        if (widget == selectionOnly_)
            continue;
        widget->setEnabled(!scheduled);
    }

    const auto selected = selectedBodies();
    if (selected.empty()) {
        QSignalBlocker blocker(selectionOnly_);
        selectionOnly_->setChecked(false);
    }
    selectionOnly_->setEnabled(!scheduled && !selected.empty());
    selectionOnly_->setText(selected.empty() ? QString("Selection only · none selected")
                                             : QString("Selection only · %1")
                                                   .arg(countText(selected.size(), "body selected",
                                                                  "bodies selected")));
    selectionHint_->setVisible(!scheduled && selected.empty());
    visibleCaption_->setVisible(!scheduled && visibleOnly_->isChecked());

    // A picker names its value: "none", the one name, or the count (§19.2).
    auto value = [](QPushButton *button, const QString &kind, const QStringList &names) {
        QString text = "none";
        if (names.size() == 1)
            text = names.first();
        else if (names.size() > 1)
            text = QString("%1 selected").arg(names.size());
        button->setText(text + " ▸");
        button->setAccessibleName(QString("%1 filter: %2").arg(kind, text));
    };
    QStringList tagNames, materialNames, definitionNames;
    for (auto id : tags_)
        tagNames << (doc_.tags().contains(id) ? QString::fromStdString(doc_.tags().at(id)->name)
                                              : QString("tag %1").arg(id));
    for (auto id : materials_)
        materialNames << (doc_.materials().contains(id)
                              ? QString::fromStdString(doc_.materials().at(id)->name)
                              : QString("material %1").arg(id));
    for (auto id : definitions_)
        definitionNames << (doc_.definitions().contains(id)
                                ? QString::fromStdString(doc_.definitions().at(id)->name)
                                : QString("definition %1").arg(id));
    value(tagsValue_, "Tags", tagNames);
    value(materialsValue_, "Materials", materialNames);
    value(definitionsValue_, "Definitions", definitionNames);
    refreshMissing();

    // Save needs a saved model, a readable store and room for another schedule (§19.6).
    const auto path = modelPath_ ? modelPath_() : QString();
    const bool room = int(rows_.size()) < 32;
    const bool resolved = filtersResolved();
    QString reason;
    if (scheduled) {
        reason.clear();
    } else if (path.isEmpty()) {
        reason = "Save the model first. A schedule belongs to a saved model.";
    } else if (!storeFault_.isEmpty()) {
        reason.clear();
    } else if (!room) {
        reason = QString("This model has 32 schedules. Delete one to save another.");
    } else if (!resolved) {
        reason = "Remove the missing filters first.";
    }
    scheduleNote_->setText(reason);
    scheduleNote_->setVisible(!reason.isEmpty());
    saveSchedule_->setEnabled(!scheduled && !path.isEmpty() && storeFault_.isEmpty() && room &&
                              resolved);

    // An export waits for the totals; a run never starts an export (§19.8).
    const bool busy = job_ != nullptr;
    exportCsv_->setEnabled(!busy);
    exportJson_->setEnabled(!busy);
    const auto waiting = busy ? QString("Wait for the totals to finish, then export.") : QString();
    exportCsv_->setToolTip(waiting);
    exportJson_->setToolTip(waiting);
}

void QuantitiesPanel::refreshMissing() {
    // The rows are rebuilt only when the set of missing IDs changes, so a focused Remove keeps
    // focus.
    struct Missing {
        QString word;
        std::set<Id> *ids;
        bool exists(const Document &doc, Id id) const {
            if (word == "tag")
                return doc.tags().contains(id);
            if (word == "material")
                return doc.materials().contains(id);
            return doc.definitions().contains(id);
        }
    };
    const Missing kinds[]{
        {"tag", &tags_}, {"material", &materials_}, {"definition", &definitions_}};
    QStringList found;
    for (const auto &kind : kinds)
        for (auto id : *kind.ids)
            if (!kind.exists(doc_, id))
                found << QString("%1 %2").arg(kind.word).arg(id);
    const auto signature = found.join("|");
    if (signature == missingSignature_)
        return;
    missingSignature_ = signature;
    while (auto *item = missingLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const auto &kind : kinds)
        for (auto id : *kind.ids) {
            if (kind.exists(doc_, id))
                continue;
            auto *row = new QWidget;
            row->setObjectName(QString("quantitiesMissing%1%2").arg(kind.word).arg(id));
            auto *line = new QHBoxLayout(row);
            line->setContentsMargins(0, 0, 0, 0);
            auto *text = new QLabel(QString("Missing %1 %2").arg(kind.word).arg(id));
            text->setStyleSheet("color: #7a4f00;");
            line->addWidget(text, 1);
            auto *remove = new QPushButton("Remove");
            remove->setObjectName(QString("quantitiesMissingRemove%1%2").arg(kind.word).arg(id));
            remove->setAutoDefault(false);
            line->addWidget(remove);
            missingLayout_->addWidget(row);
            connect(remove, &QPushButton::clicked, this, [this, set = kind.ids, id] {
                set->erase(id);
                syncControls();
                requestRun(true);
            });
        }
    missing_->setVisible(!found.isEmpty());
}

QFrame *QuantitiesPanel::buildPicker(const QString &kind) {
    // The popover is a popup of check rows: Up and Down move, Space checks, Esc closes (§19.10).
    auto *popup = new QFrame(this, Qt::Popup);
    popup->setObjectName("quantities" + kind + "Popup");
    popup->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(6, 6, 6, 6);
    std::set<Id> *selected = kind == "Tags"        ? &tags_
                             : kind == "Materials" ? &materials_
                                                   : &definitions_;
    const QString noun = kind == "Tags" ? "Tag" : kind == "Materials" ? "Material" : "Definition";
    auto add = [&](Id id, const QString &text, const QString &accessible) {
        auto *check = new QCheckBox(text);
        check->setObjectName(QString("quantities%1Check%2").arg(kind).arg(id));
        check->setAccessibleName(accessible);
        check->setChecked(selected->contains(id));
        layout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, selected, id](bool on) {
            if (on)
                selected->insert(id);
            else
                selected->erase(id);
            syncControls();
            requestRun(true);
        });
    };
    if (kind == "Tags") {
        // A folder includes every tag under it, and its row says so (§19.2). Each row counts the
        // bodies it holds, nested ones included. Untagged is a group of its own, not a filter.
        std::map<Id, std::size_t> counts;
        for (const auto &[id, body] : doc_.bodies())
            for (auto tag = body->tag; tag;
                 tag = doc_.tags().contains(tag) ? doc_.tags().at(tag)->parent : Id{0})
                ++counts[tag];
        for (const auto &[id, record] : doc_.tags()) {
            const auto name = QString::fromStdString(record->name);
            const auto text = name + "  " + QString::number(counts[id]) +
                              (record->folder ? "  (includes its tags)" : QString());
            add(id, text,
                QString("%1 %2, %3 bodies%4")
                    .arg(noun, name)
                    .arg(counts[id])
                    .arg(record->folder ? ", includes its tags" : ""));
        }
    } else if (kind == "Materials") {
        for (const auto &[id, record] : doc_.materials()) {
            const auto name = QString::fromStdString(record->name);
            add(id, name, QString("%1 %2").arg(noun, name));
        }
    } else {
        std::map<Id, std::size_t> placements;
        for (const auto &[id, instance] : doc_.instances())
            ++placements[instance->definition];
        for (const auto &[id, record] : doc_.definitions()) {
            const auto name = QString::fromStdString(record->name);
            add(id, name + "  " + QString::number(placements[id]),
                QString("%1 %2, %3 placements").arg(noun, name).arg(placements[id]));
        }
    }
    if (kind == "Materials") {
        auto *note = new QLabel("Counts and volumes ignore this filter.");
        note->setObjectName("quantitiesMaterialsNote");
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    return popup;
}

void QuantitiesPanel::pickerMenu(const QString &kind, QWidget *anchor) {
    auto *popup = buildPicker(kind);
    popup->adjustSize();
    popup->move(anchor->mapToGlobal(QPoint(0, anchor->height())));
    popup->show();
    // The first row takes focus, so Up, Down and Space work at once.
    if (auto *first = popup->findChild<QCheckBox *>())
        first->setFocus(Qt::PopupFocusReason);
}

void QuantitiesPanel::setError(const QString &text) {
    error_->setText(text);
    error_->setVisible(!text.isEmpty());
    if (!text.isEmpty())
        announce(error_);
}

void QuantitiesPanel::reloadSchedules() {
    rows_.clear();
    scheduleList_->clear();
    storeFault_.clear();
    storeError_->hide();
    otherModels_->hide();
    try {
        storePath_ = reportScheduleStorePath();
        const ReportScheduleStore store(storePath_);
        const auto id = doc_.identity();
        for (const auto &schedule : store.schedules(id)) {
            Row row;
            row.name = schedule.name;
            row.request = schedule.request;
            row.summary = settingWords(schedule.request);
            row.saved = QDateTime::fromString(schedule.createdAt, Qt::ISODate)
                            .toLocalTime()
                            .toString("d MMMM yyyy");
            try {
                requireResolvedScheduleIds(doc_, schedule.request);
            } catch (const InspectionError &error) {
                // A stale schedule names its missing IDs and never runs with fewer filters (§19.7).
                const auto parts = missingParts(error.details()["missing"].toObject());
                row.staleText = parts.size() == 1
                                    ? QString("Stale: %1 no longer exists").arg(joinWords(parts))
                                    : QString("Stale: %1 no longer exist").arg(joinWords(parts));
            }
            row.refused = refusals_.count(row.name) > 0;
            rows_.push_back(row);
        }
        const auto outside = store.schedulesOutside(id);
        if (outside > 0) {
            otherModels_->setText(
                QString("Other models on this computer have %1. They are kept with "
                        "those models and are not listed here.")
                    .arg(countText(outside, "schedule", "schedules")));
            otherModels_->show();
        }
    } catch (const InspectionError &error) {
        // A store that cannot be read is shown in its block; the model's own numbers keep working
        // (§19.7).
        storeFault_ = QString::fromStdString(error.what());
        storeError_->setText(
            QString("✕ Schedules cannot be read\n%1\nTotals, groups and exports still work. "
                    "SketchyUp never resets this file: move it aside yourself, then reopen "
                    "this model.")
                .arg(storePath_.isEmpty() ? storeFault_ : storePath_ + ": " + storeFault_));
        storeError_->show();
    }
    scheduleCount_->setText(rows_.empty() ? QString()
                                          : QString("%1 for this model").arg(rows_.size()));
    for (const auto &row : rows_) {
        auto *item = new QListWidgetItem;
        item->setData(Qt::UserRole, row.name);
        QString accessible = QString("%1, saved %2, %3").arg(row.name, row.saved, row.summary);
        if (row.refused)
            accessible += ". Not run";
        else if (!row.staleText.isEmpty())
            accessible += ". Warning: " + row.staleText;
        item->setData(Qt::AccessibleTextRole, accessible + ". Run");
        scheduleList_->addItem(item);

        auto *widget = new QWidget;
        widget->setObjectName(QString("quantitiesSchedule%1").arg(scheduleList_->count() - 1));
        auto *layout = new QVBoxLayout(widget);
        layout->setContentsMargins(4, 4, 4, 4);
        auto *top = new QHBoxLayout;
        auto *name = new QLabel(row.name);
        name->setStyleSheet("font-weight: bold;");
        name->setWordWrap(true);
        top->addWidget(name, 1);
        auto *run = new QPushButton("Run");
        run->setObjectName("quantitiesScheduleRun");
        run->setFocusPolicy(Qt::NoFocus);
        top->addWidget(run);
        auto *actions = new QToolButton;
        actions->setText("⋯ Actions");
        actions->setObjectName("quantitiesScheduleActions");
        actions->setFocusPolicy(Qt::NoFocus);
        top->addWidget(actions);
        layout->addLayout(top);
        auto *summary = new QLabel(QString("%1 · saved %2").arg(row.summary, row.saved));
        summary->setWordWrap(true);
        layout->addWidget(summary);
        if (row.refused) {
            auto *refusal = new QLabel(QString("✕ Not run.\n%1").arg(refusals_.at(row.name)));
            refusal->setStyleSheet("color: #a8322b;");
            refusal->setWordWrap(true);
            refusal->setObjectName("quantitiesScheduleRefusal");
            layout->addWidget(refusal);
        } else if (!row.staleText.isEmpty()) {
            auto *stale = new QLabel(QString::fromUtf8("⚠ ") + row.staleText);
            stale->setStyleSheet("color: #7a4f00;");
            stale->setWordWrap(true);
            stale->setObjectName("quantitiesScheduleStale");
            layout->addWidget(stale);
        }
        scheduleList_->setItemWidget(item, widget);
        item->setSizeHint(widget->sizeHint());
        connect(run, &QPushButton::clicked, this, [this, name = row.name] { runSchedule(name); });
        connect(actions, &QToolButton::clicked, this, [this, name = row.name, actions] {
            scheduleActions(name, QPoint(0, actions->height()));
        });
    }
}

void QuantitiesPanel::runSchedule(const QString &name) {
    try {
        const ReportScheduleStore store(storePath_);
        const auto saved = store.lookup(doc_.identity(), name);
        try {
            requireResolvedScheduleIds(doc_, saved.request);
        } catch (const InspectionError &error) {
            // A stale schedule is refused in its row. Nothing runs, and the totals stay as they are
            // (§19.7).
            const auto parts = missingParts(error.details()["missing"].toObject());
            refusals_[name] =
                QString("It refers to %1, which no longer %2 in this model. Nothing was run, "
                        "and the totals above are unchanged.")
                    .arg(joinWords(parts), parts.size() == 1 ? "exists" : "exist");
            reloadSchedules();
            announce(scheduleList_);
            return;
        }
        refusals_.erase(name);
        scheduleName_ = name;
        scheduleRequest_ = saved.request;
        reloadSchedules();
        syncControls();
        requestRun(true);
    } catch (const InspectionError &error) {
        setError(scheduleRefusal(error));
    }
}

void QuantitiesPanel::backToLive() {
    scheduleName_.clear();
    scheduleRequest_ = {};
    syncControls();
    requestRun(true);
}

void QuantitiesPanel::scheduleActions(const QString &name, const QPoint &at) {
    QMenu menu(this);
    menu.setObjectName("quantitiesScheduleMenu");
    const auto stale = std::any_of(rows_.begin(), rows_.end(), [&](const Row &row) {
        return row.name == name && !row.staleText.isEmpty();
    });
    auto *csv = menu.addAction("Export CSV…");
    csv->setObjectName("quantitiesActionExportCsv");
    auto *json = menu.addAction("Export JSON…");
    json->setObjectName("quantitiesActionExportJson");
    auto *rename = menu.addAction("Rename…");
    rename->setObjectName("quantitiesActionRename");
    auto *remove = menu.addAction("Delete…");
    remove->setObjectName("quantitiesActionDelete");
    QAction *saveCurrent = nullptr;
    if (stale) {
        menu.addSeparator();
        saveCurrent = menu.addAction("Save current filters as…");
        saveCurrent->setObjectName("quantitiesActionSaveCurrent");
    }
    const auto chosen = menu.exec(mapToGlobal(at));
    if (chosen == csv)
        exportFile(true, name);
    else if (chosen == json)
        exportFile(false, name);
    else if (chosen == rename)
        renameSchedule(name);
    else if (chosen == remove)
        deleteSchedule(name);
    else if (chosen == saveCurrent)
        saveSchedule(liveRequest(false), {});
}

void QuantitiesPanel::saveSchedule(const QJsonObject &request, const QString &rename) {
    // A schedule never stores the selection (§19.2): the request is taken without it. A rename
    // keeps the request the schedule already has.
    std::set<QString> taken;
    for (const auto &row : rows_)
        if (row.name != rename)
            taken.insert(row.name);
    QJsonObject stored = request;
    if (!rename.isEmpty())
        for (const auto &row : rows_)
            if (row.name == rename)
                stored = row.request;
    ScheduleNameDialog dialog(this, !rename.isEmpty(), rename, taken,
                              storedWords(stored, doc_.displayPrecision()));
    while (dialog.exec() == QDialog::Accepted) {
        const auto name = dialog.name();
        if (!rename.isEmpty() && name == rename) {
            dialog.accept();
            return;
        }
        try {
            if (rename.isEmpty())
                saveReportSchedule(doc_,
                                   storePath_.isEmpty() ? reportScheduleStorePath() : storePath_,
                                   name, request);
            else
                renameReportSchedule(doc_, storePath_, rename, name);
            refusals_.erase(rename);
            reloadSchedules();
            status_->setText(rename.isEmpty() ? QString("Saved schedule %1.").arg(name)
                                              : QString("Renamed the schedule to %1.").arg(name));
            announce(status_);
            syncControls();
            return;
        } catch (const InspectionError &error) {
            dialog.showError(scheduleRefusal(error));
        }
    }
}

void QuantitiesPanel::renameSchedule(const QString &name) { saveSchedule({}, name); }

void QuantitiesPanel::deleteSchedule(const QString &name) {
    if (!confirmScheduleDelete(this, name))
        return;
    try {
        deleteReportSchedule(doc_, storePath_, name);
        refusals_.erase(name);
        if (scheduleName_ == name)
            backToLive();
        reloadSchedules();
        status_->setText(QString("Deleted the schedule %1. The model is not changed.").arg(name));
        announce(status_);
        syncControls();
    } catch (const InspectionError &error) {
        setError(scheduleRefusal(error));
    }
}

QString QuantitiesPanel::modelName() const {
    const auto path = modelPath_ ? modelPath_() : QString();
    return path.isEmpty() ? QStringLiteral("model") : QFileInfo(path).completeBaseName();
}

void QuantitiesPanel::exportFile(bool csv, const QString &schedule) {
    if (job_) {
        setError("Wait for the totals to finish, then export.");
        return;
    }
    const auto path = modelPath_ ? modelPath_() : QString();
    const auto suggested = (path.isEmpty() ? QString() : QFileInfo(path).absolutePath() + "/") +
                           modelName() + (csv ? "-takeoff.csv" : "-takeoff.json");
    // No overwrite prompt: an existing file is refused after the dialog, and nothing is replaced
    // (§19.8). The system Save dialog (§19.8), as the file choosers of the other panels are opened.
    // No overwrite prompt: an existing file is refused after the dialog, and nothing is replaced.
    const auto chosen =
        QFileDialog::getSaveFileName(this, csv ? "Export CSV" : "Export JSON", suggested,
                                     csv ? "CSV files (*.csv)" : "JSON files (*.json)", nullptr,
                                     QFileDialog::DontConfirmOverwrite);
    if (chosen.isEmpty())
        return;
    ReportFileOptions options;
    options.path = chosen;
    options.format = csv ? "csv" : "json";
    std::set<Id> bodies;
    if (!schedule.isEmpty()) {
        options.schedule = schedule; // a schedule's own request, with its marker (§19.8)
    } else {
        options.groupBy = groupBy_->currentData().toString();
        options.visible = visibleOnly_->isChecked();
        for (auto id : tags_)
            options.tags << QString::number(id);
        for (auto id : materials_)
            options.materials << QString::number(id);
        for (auto id : definitions_)
            options.definitions << QString::number(id);
        options.selection = selectionOnly_->isChecked();
        if (options.selection)
            bodies = selectedBodies();
    }
    auto snapshot = std::make_shared<const Document>(doc_.readSnapshot());
    auto gate = runGateForTesting;
    auto *job = new ReportJob(
        [snapshot, options, bodies, gate](const ReportHooks &hooks) {
            if (gate)
                gate(*hooks.cancel);
            return writeReportFile(*snapshot, options, options.selection ? &bodies : nullptr,
                                   hooks);
        },
        this);
    exportPath_ = chosen;
    exportSchedule_ = schedule;
    exportingCsv_ = csv;
    jobRevision_ = doc_.revision();
    jobSignature_ = "export|" + chosen;
    startJob(job, true, true);
}

void QuantitiesPanel::exportReport(const QString &path, bool csv, const QString &schedule,
                                   const QJsonObject &exportData, std::uint64_t revision) {
    const auto exported = exportData["exportReport"].toObject();
    QStringList written{
        QString("Written to %1").arg(path),
        QString("Format: %1").arg(csv ? "CSV" : "JSON"),
        QString("Bytes: %1")
            .arg(groupedDigits(QString::number(exported["bytes"].toDouble(), 'f', 0))),
        QString("Revision: %1").arg(revision),
        schedule.isEmpty() ? QString("Schedule: none (ad hoc)")
                           : QString("Schedule: %1").arg(schedule),
        QString("Grouping: %1").arg(groupBy_->currentText()),
        QString("Hidden content: %1")
            .arg(visibleOnly_->isChecked() ? "Visible only" : "Include hidden"),
        QString("Metrics: all four tables"),
    };
    // The attention list comes from the totals on screen, when they were computed at this revision.
    std::vector<ExportAttention> attention;
    QStringList notices;
    if (shown_.valid && shown_.revision == revision && schedule.isEmpty()) {
        written << QString("Precision: %1")
                       .arg(shown_.data["metadata"].toObject()["precision"].toVariant().toString());
        for (const auto &value : shown_.data["invalid"].toArray()) {
            const auto row = value.toObject();
            ExportAttention item;
            item.words = row["label"].toString();
            item.count = qulonglong(row["contexts"].toInt());
            item.bodies = idsFrom(row["bodies"].toArray());
            item.detail = QString("%1 · area counted, no volume")
                              .arg(countText(item.bodies.size(), "body", "bodies"));
            attention.push_back(item);
        }
        for (const auto &value : shown_.data["terrain"].toArray())
            notices << QString("%1 is terrain, not a solid. Its area is counted; it has no volume.")
                           .arg(value.toObject()["name"].toString());
    }
    ExportReportDialog dialog(this, path, written, attention, notices);
    dialog.exec();
    if (!dialog.selectedBodies().empty()) {
        SelectionSet entities;
        for (auto id : dialog.selectedBodies())
            entities.insert({id, SelectionKind::Body, 0});
        view_.selectEntities(entities);
    }
}

} // namespace sketchy
