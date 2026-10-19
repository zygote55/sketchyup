#include "app/terrain_import_dialog.hpp"
#include "app/accessible_names.hpp"
#include "app/interface_preferences.hpp"
#include "app/unit_display.hpp"
#include "automation/terrain_commands.hpp"
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSplitter>
#include <QStringDecoder>
#include <QStyle>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace sketchy {

// One dry run: the core's terrain.import without the edit, for one set of settings.
struct TerrainImportDryRun {
    QJsonObject command;
    bool unitsChosen{};
    std::uint64_t revision{};
    QString error;              // the source could not be read or the request was refused
    bool unitsMissing{};        // DXF: no usable $INSUNITS, so units must be chosen
    QString headerUnit;         // DXF: the unit $INSUNITS declares, when it does
    std::vector<TerrainSourceLayer> layers;
    QJsonObject sourceReport;   // the parser's report (delimiter, header, columns, ...)
    double metresPerUnit{1};
    std::optional<TerrainImportResult> result; // when units are chosen and the source parsed
    std::string name{"Terrain"};
    std::shared_ptr<const TerrainPreviewData> preview;
};

namespace {
QString count(double value) { return QLocale().toString(qulonglong(std::llround(value))); }
QString plural(double value, const QString &one, const QString &many) {
    return count(value) + ' ' + (std::llround(value) == 1 ? one : many);
}
QString decimal(double value, int places = 2) { return QLocale().toString(value, 'f', places); }

struct UnitChoice {
    const char *code, *label;
};
constexpr UnitChoice unitChoices[]{{"mm", "Millimetres"},        {"cm", "Centimetres"},
                                   {"m", "Metres"},              {"in", "Inches"},
                                   {"ft", "Feet (international)"}, {"usft", "US survey feet"}};
QString unitLabel(const QString &code) {
    for (const auto &choice : unitChoices)
        if (code == choice.code)
            return QString(choice.label).toLower();
    return code;
}

struct RoleChoice {
    const char *code, *label;
};
constexpr RoleChoice roleChoices[]{{"auto", "Automatic"}, {"contour", "Contour"},
                                   {"breakline", "Breakline"}, {"boundary", "Boundary"},
                                   {"hole", "Hole"},           {"ground", "Ground points"},
                                   {"ignore", "Ignore"}};
QString roleLabel(const QString &code) {
    for (const auto &choice : roleChoices)
        if (code == choice.code)
            return choice.label;
    return code;
}

constexpr const char *columnNames[]{"point", "easting", "northing", "elevation", "description",
                                    "ignore"};
constexpr const char *columnLabels[]{"Point number", "Easting", "Northing", "Elevation",
                                     "Description", "Ignore"};

TerrainSourceRole sourceRole(TerrainImportRole role, const TerrainSourcePolyline &line) {
    switch (role) {
    case TerrainImportRole::Boundary:
        return TerrainSourceRole::Boundary;
    case TerrainImportRole::Hole:
        return TerrainSourceRole::Hole;
    case TerrainImportRole::Breakline:
        return TerrainSourceRole::Breakline;
    default:
        break;
    }
    const bool constant = std::all_of(line.points.begin(), line.points.end(),
                                      [&](const TerrainVec3 &p) { return p.z == line.points[0].z; });
    return constant ? TerrainSourceRole::Contour : TerrainSourceRole::Breakline;
}

// The preview of a dry run, in the local frame: the prepared sources when preparation got that
// far, otherwise the parsed source converted with the dry run's origin.
std::shared_ptr<const TerrainPreviewData> previewOf(const TerrainSourceRequest &request,
                                                    const TerrainImportResult &result) {
    auto data = std::make_shared<TerrainPreviewData>();
    if (!result.record.sources.empty()) {
        for (const auto &source : result.record.sources) {
            if (source.role == TerrainSourceRole::Points) {
                for (const auto &p : source.points) {
                    data->points.emplace_back(p.x, p.y);
                    data->pointZ.push_back(float(p.z));
                }
                continue;
            }
            TerrainPreviewData::Line line;
            line.role = source.role;
            line.z = source.points.empty() ? 0.f : float(source.points.front().z);
            line.points.reserve(source.points.size());
            for (const auto &p : source.points)
                line.points.emplace_back(p.x, p.y);
            data->lines.push_back(std::move(line));
        }
    } else {
        const double scale = request.set.unit.metresPerUnit;
        const auto origin = result.origin;
        auto local = [&](const TerrainVec3 &p) {
            return QPointF(p.x * scale - origin.easting, p.y * scale - origin.northing);
        };
        for (const auto &polyline : request.set.polylines) {
            const auto &layer = request.set.layers[polyline.layer].name;
            const auto found = request.options.roles.find(layer);
            const auto role =
                found == request.options.roles.end() ? TerrainImportRole::Auto : found->second;
            if (role == TerrainImportRole::Ignore || polyline.points.empty())
                continue;
            TerrainPreviewData::Line line;
            line.role = sourceRole(role, polyline);
            line.z = float(polyline.points.front().z * scale - origin.elevation);
            for (const auto &p : polyline.points)
                line.points.push_back(local(p));
            if (polyline.closed)
                line.points.push_back(local(polyline.points.front()));
            data->lines.push_back(std::move(line));
        }
        for (const auto &point : request.set.points) {
            data->points.push_back(local(point.position));
            data->pointZ.push_back(float(point.position.z * scale - origin.elevation));
        }
    }
    data->vertices.reserve(result.mesh.vertices.size());
    data->vertexZ.reserve(result.mesh.vertices.size());
    for (const auto &v : result.mesh.vertices) {
        data->vertices.emplace_back(v.x, v.y);
        data->vertexZ.push_back(float(v.z));
    }
    data->triangles = result.mesh.triangles;
    finishTerrainPreview(*data);
    return data;
}

// The worker: parse and dry-run exactly as terrain.inspect_source and terrain.import do.
std::shared_ptr<TerrainImportDryRun> dryRun(const QByteArray &bytes, QJsonObject command,
                                            bool unitsChosen, std::optional<SiteOrigin> site,
                                            std::size_t otherTriangles, std::uint64_t revision) {
    auto run = std::make_shared<TerrainImportDryRun>();
    run->command = command;
    run->unitsChosen = unitsChosen;
    run->revision = revision;
    const bool dxf = command["format"].toString() == "dxf";
    auto parse = [&](const QJsonObject &request) {
        return parseTerrainSourceRequest(bytes, request, true);
    };
    std::optional<TerrainSourceRequest> request;
    try {
        if (unitsChosen || dxf)
            request = parse(command); // DXF without a choice reads $INSUNITS
        else {
            // A point file has no units: read it in metres only to list its codes and columns.
            auto provisional = command;
            provisional["units"] = "m";
            const auto listed = parse(provisional);
            run->layers = listed.set.layers;
            run->sourceReport = listed.set.report;
            return run;
        }
    } catch (const std::exception &error) {
        if (dxf && !unitsChosen) {
            // Without usable $INSUNITS the DXF needs a unit; read it in metres to list layers.
            try {
                auto provisional = command;
                provisional["units"] = "m";
                const auto listed = parse(provisional);
                run->unitsMissing = true;
                run->layers = listed.set.layers;
                run->sourceReport = listed.set.report;
                return run;
            } catch (const std::exception &) {
            }
        }
        run->error = QString::fromUtf8(error.what());
        return run;
    }
    run->layers = request->set.layers;
    run->sourceReport = request->set.report;
    run->metresPerUnit = request->set.unit.metresPerUnit;
    run->name = request->name;
    if (dxf && !command.contains("units"))
        run->headerUnit = request->set.unit.name; // $INSUNITS gave the unit
    // The document's other terrains count against the 250,000-triangle limit, so a mesh that
    // would pass it is refused in the report instead of by the edit.
    request->options.otherTerrainTriangles = otherTriangles;
    try {
        run->result =
            runTerrainImport(request->set, request->options, site, request->provenance);
    } catch (const std::exception &error) {
        run->error = QString::fromUtf8(error.what());
        return run;
    }
    run->preview = previewOf(*request, *run->result);
    return run;
}

QString extentsText(const QJsonObject &extents, const QString &unit) {
    const auto min = extents["min"].toArray(), max = extents["max"].toArray();
    if (min.size() != 3 || max.size() != 3)
        return QStringLiteral("—");
    QStringList parts;
    const char *axes[]{"E", "N", "Z"};
    for (int i = 0; i < 3; ++i)
        parts << QString("%1 %2 to %3").arg(axes[i], decimal(min[i].toDouble()),
                                            decimal(max[i].toDouble()));
    return parts.join(" · ") + ' ' + unit;
}

// Group: refusals, merges (notices whose kind starts "merged-") and the other notices.
enum class Group { Refusals, Merges, Notices };
QJsonArray groupKinds(const QJsonObject &terrain, Group group) {
    QJsonArray kinds;
    if (group == Group::Refusals)
        return terrain["refusals"].toArray();
    for (const auto &value : terrain["notices"].toArray()) {
        const bool merge = value.toObject()["kind"].toString().startsWith("merged-");
        if (merge == (group == Group::Merges))
            kinds.append(value);
    }
    return kinds;
}
QString kindText(const QJsonObject &kind) {
    auto message = kind["message"].toString();
    if (message.endsWith('.'))
        message.chop(1);
    const auto total = kind["count"].toDouble();
    const auto listed = kind["locations"].toArray().size();
    // The adapter's messages lead with their count; the parser's describe the kind.
    QString text = message.startsWith(count(total) + ' ') || message.startsWith(QString::number(total) + ' ')
                       ? message
                       : message + " · " + count(total);
    if (listed && kind["locationsOmitted"].toDouble() > 0)
        text += QString(" (%1 listed)").arg(listed);
    return text;
}
QString locationText(const QJsonObject &location, const QString &units) {
    QString text = location["reference"].toString();
    if (location.contains("reference2"))
        text += " and " + location["reference2"].toString();
    const auto xy = location["xy"].toArray();
    if (xy.size() == 2)
        text += QString(" · E %1, N %2 %3")
                    .arg(decimal(xy[0].toDouble(), 3), decimal(xy[1].toDouble(), 3), units);
    if (location.contains("detail"))
        text += " · " + location["detail"].toString();
    return text;
}

// Enter on a list chooses its current row instead of reaching the dialog's default button.
class EnterChooses final : public QObject {
  public:
    EnterChooses(QObject *parent, std::function<void()> choose)
        : QObject(parent), choose_(std::move(choose)) {}
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                choose_();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    std::function<void()> choose_;
};
} // namespace

TerrainImportDialog::TerrainImportDialog(Document &doc, Setup setup, QWidget *parent)
    : Settled<QDialog>(parent), doc_(doc), setup_(std::move(setup)) {
    pool_.setMaxThreadCount(1);
    rerun_.setSingleShot(true);
    connect(&rerun_, &QTimer::timeout, this, [this] { start(); });
    build();
    setTheme(setup_.colors);
    refresh();
    schedule(0);
}
TerrainImportDialog::~TerrainImportDialog() {
    rerun_.stop();
    // A running dry run posts back through a guarded pointer; it owns its inputs.
    pool_.waitForDone();
}

bool TerrainImportDialog::points() const { return format_->currentData().toString() == "points"; }

void TerrainImportDialog::build() {
    setObjectName("terrainImportDialog");
    setWindowTitle("Import terrain");
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto rule = [](QBoxLayout *into) {
        auto *line = new QFrame;
        line->setObjectName("terrainImportRule");
        line->setFixedHeight(1);
        into->addWidget(line);
    };
    auto caption = [](const QString &text) {
        auto *label = new QLabel(text);
        label->setObjectName("terrainImportFieldLabel");
        return label;
    };
    auto heading = [](const QString &text) {
        auto *label = new QLabel(text);
        label->setObjectName("terrainImportHeading");
        return label;
    };
    auto value = [](const QString &name) {
        auto *label = new QLabel(QStringLiteral("—"));
        label->setObjectName(name);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::NoTextInteraction);
        return label;
    };

    // Title and subline.
    auto *header = new QVBoxLayout;
    header->setContentsMargins(22, 16, 22, 10);
    header->setSpacing(4);
    auto *title = new QLabel(windowTitle());
    title->setObjectName("terrainImportTitle");
    header->addWidget(title);
    subline_ = new QLabel;
    subline_->setObjectName("terrainImportSubline");
    subline_->setTextFormat(Qt::PlainText);
    subline_->setWordWrap(true);
    header->addWidget(subline_);
    layout->addLayout(header);

    // The refusal banner: nothing was changed.
    banner_ = new QFrame;
    banner_->setObjectName("terrainImportBanner");
    auto *bannerRow = new QHBoxLayout(banner_);
    bannerRow->setContentsMargins(12, 8, 12, 8);
    bannerText_ = new QLabel;
    bannerText_->setObjectName("terrainImportBannerText");
    bannerText_->setTextFormat(Qt::PlainText);
    bannerText_->setWordWrap(true);
    bannerRow->addWidget(bannerText_, 1);
    auto *bannerHolder = new QHBoxLayout;
    bannerHolder->setContentsMargins(22, 0, 22, 10);
    bannerHolder->addWidget(banner_);
    layout->addLayout(bannerHolder);
    banner_->hide();
    rule(layout);

    auto *split = new QSplitter(Qt::Horizontal);
    split->setObjectName("terrainImportSplit");
    split->setChildrenCollapsible(false);
    layout->addWidget(split, 1);

    // ---- Settings (left) -----------------------------------------------------------------
    auto *scroll = new QScrollArea;
    scroll->setObjectName("terrainImportSettings");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFocusPolicy(Qt::NoFocus);
    auto *settings = new QWidget;
    settings->setObjectName("terrainImportSettingsContent");
    auto *column = new QVBoxLayout(settings);
    column->setContentsMargins(22, 14, 16, 14);
    column->setSpacing(8);

    auto *source = new QGridLayout;
    source->setHorizontalSpacing(10);
    source->setVerticalSpacing(8);
    source->setColumnStretch(1, 1);
    format_ = new QComboBox;
    format_->setObjectName("terrainImportFormat");
    format_->setAccessibleName("Source format");
    format_->addItem("DXF contours", "dxf");
    format_->addItem("Point file (CSV, TXT, XYZ, PTS)", "points");
    {
        const auto suffix = QFileInfo(setup_.fileName).suffix().toLower();
        format_->setCurrentIndex(suffix == "dxf" ? 0 : 1);
    }
    source->addWidget(caption("Format"), 0, 0);
    source->addWidget(format_, 0, 1);
    units_ = new QComboBox;
    units_->setObjectName("terrainImportUnits");
    units_->setAccessibleName("Source units (required)");
    source->addWidget(caption("Units"), 1, 0);
    source->addWidget(units_, 1, 1);
    column->addLayout(source);

    // Point files: delimiter, header row and a meaning per column above a sample.
    pointsBox_ = new QWidget;
    pointsBox_->setObjectName("terrainImportPointsBox");
    auto *pointsColumn = new QVBoxLayout(pointsBox_);
    pointsColumn->setContentsMargins(0, 6, 0, 0);
    pointsColumn->setSpacing(8);
    pointsColumn->addWidget(heading("Columns"));
    auto *pointsGrid = new QGridLayout;
    pointsGrid->setHorizontalSpacing(10);
    pointsGrid->setVerticalSpacing(8);
    pointsGrid->setColumnStretch(1, 1);
    preset_ = new QComboBox;
    preset_->setObjectName("terrainImportPreset");
    preset_->setAccessibleName("Column preset");
    preset_->addItem("Detect", "");
    for (const char *name : {"PNEZD", "PENZD", "ENZ", "XYZ"})
        preset_->addItem(name, name);
    preset_->addItem("Custom", "custom");
    pointsGrid->addWidget(caption("Preset"), 0, 0);
    pointsGrid->addWidget(preset_, 0, 1);
    delimiter_ = new QComboBox;
    delimiter_->setObjectName("terrainImportDelimiter");
    delimiter_->setAccessibleName("Delimiter");
    delimiter_->addItem("Detect", "");
    delimiter_->addItem("Comma", "comma");
    delimiter_->addItem("Tab", "tab");
    delimiter_->addItem("Semicolon", "semicolon");
    delimiter_->addItem("Spaces", "whitespace");
    pointsGrid->addWidget(caption("Delimiter"), 1, 0);
    pointsGrid->addWidget(delimiter_, 1, 1);
    header_ = new QComboBox;
    header_->setObjectName("terrainImportHeader");
    header_->setAccessibleName("Header row");
    header_->addItem("Detect", "");
    header_->addItem("First line is a header", "yes");
    header_->addItem("No header row", "no");
    pointsGrid->addWidget(caption("Header row"), 2, 0);
    pointsGrid->addWidget(header_, 2, 1);
    pointsColumn->addLayout(pointsGrid);
    columnsGrid_ = new QGridLayout;
    columnsGrid_->setHorizontalSpacing(6);
    columnsGrid_->setVerticalSpacing(2);
    pointsColumn->addLayout(columnsGrid_);
    sample_ = new QTableWidget;
    sample_->setObjectName("terrainImportSample");
    sample_->setAccessibleName("Sample rows");
    sample_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sample_->setTabKeyNavigation(false);
    sample_->setSelectionMode(QAbstractItemView::NoSelection);
    sample_->verticalHeader()->hide();
    sample_->setMinimumHeight(120);
    sample_->setMaximumHeight(180);
    pointsColumn->addWidget(sample_);
    column->addWidget(pointsBox_);

    // Layer (or code) roles.
    rolesHeading_ = heading("Layer roles");
    column->addWidget(rolesHeading_);
    roles_ = new QTableWidget(0, 3);
    roles_->setObjectName("terrainImportRoles");
    roles_->setAccessibleName("Layer roles");
    roles_->setHorizontalHeaderLabels({"Layer", "Contents", "Role"});
    roles_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    roles_->setSelectionBehavior(QAbstractItemView::SelectRows);
    roles_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    roles_->setTabKeyNavigation(false);
    roles_->verticalHeader()->hide();
    roles_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    roles_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    roles_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    roles_->setTextElideMode(Qt::ElideRight);
    roles_->setWordWrap(false);
    roles_->setMinimumHeight(110);
    roles_->setMaximumHeight(200);
    column->addWidget(roles_);
    auto *roleRow = new QHBoxLayout;
    roleRow->setSpacing(10);
    roleRow->addWidget(caption("Role"));
    role_ = new QComboBox;
    role_->setObjectName("terrainImportRole");
    role_->setAccessibleName("Role for the selected layers");
    for (const auto &choice : roleChoices)
        role_->addItem(choice.label, choice.code);
    roleRow->addWidget(role_, 1);
    column->addLayout(roleRow);

    // Site coordinates.
    column->addWidget(heading("Site coordinates"));
    auto *site = new QGridLayout;
    site->setHorizontalSpacing(10);
    site->setVerticalSpacing(6);
    site->setColumnStretch(1, 1);
    site->addWidget(caption("Source extents"), 0, 0, Qt::AlignTop);
    sourceExtents_ = value("terrainImportSourceExtents");
    site->addWidget(sourceExtents_, 0, 1);
    site->addWidget(caption("Origin"), 1, 0, Qt::AlignTop);
    documentOrigin_ = value("terrainImportDocumentOrigin");
    originBox_ = new QWidget;
    originBox_->setObjectName("terrainImportOriginBox");
    auto *originGrid = new QGridLayout(originBox_);
    originGrid->setContentsMargins(0, 0, 0, 0);
    originGrid->setHorizontalSpacing(6);
    originGrid->setVerticalSpacing(4);
    auto originField = [&](const QString &name, const QString &accessible) {
        auto *field = new QDoubleSpinBox;
        field->setObjectName(name);
        field->setAccessibleName(accessible);
        field->setRange(-siteCoordinateLimit, siteCoordinateLimit);
        field->setDecimals(3);
        field->setSuffix(" m");
        field->setGroupSeparatorShown(true);
        field->setKeyboardTracking(false);
        return field;
    };
    originE_ = originField("terrainImportOriginE", "Origin easting in metres");
    originN_ = originField("terrainImportOriginN", "Origin northing in metres");
    originZ_ = originField("terrainImportOriginZ", "Origin elevation in metres");
    originGrid->addWidget(caption("E"), 0, 0);
    originGrid->addWidget(originE_, 0, 1);
    originGrid->addWidget(caption("N"), 1, 0);
    originGrid->addWidget(originN_, 1, 1);
    originGrid->addWidget(caption("Z"), 2, 0);
    originGrid->addWidget(originZ_, 2, 1);
    originReset_ = new QPushButton("Use proposed origin");
    originReset_->setObjectName("terrainImportOriginReset");
    originReset_->setAccessibleName("Use the proposed origin");
    originReset_->setAutoDefault(false);
    originGrid->addWidget(originReset_, 3, 1, Qt::AlignLeft);
    originGrid->setColumnStretch(1, 1);
    auto *originColumn = new QVBoxLayout;
    originColumn->setSpacing(4);
    originColumn->addWidget(documentOrigin_);
    originColumn->addWidget(originBox_);
    site->addLayout(originColumn, 1, 1);
    site->addWidget(caption("CRS label"), 2, 0);
    crs_ = new QLineEdit;
    crs_->setObjectName("terrainImportCrs");
    crs_->setAccessibleName("CRS label (optional, informational)");
    crs_->setPlaceholderText("Optional, for example EPSG:32633");
    crs_->setMaxLength(int(siteCrsBytes));
    site->addWidget(crs_, 2, 1);
    site->addWidget(caption("Conversion"), 3, 0, Qt::AlignTop);
    formula_ = value("terrainImportFormula");
    site->addWidget(formula_, 3, 1);
    site->addWidget(caption("Local extents"), 4, 0, Qt::AlignTop);
    localExtents_ = value("terrainImportLocalExtents");
    site->addWidget(localExtents_, 4, 1);
    column->addLayout(site);

    // Triangulation (TerrainImportOptions).
    column->addWidget(heading("Triangulation"));
    auto *tri = new QGridLayout;
    tri->setHorizontalSpacing(10);
    tri->setVerticalSpacing(8);
    tri->setColumnStretch(1, 1);
    boundary_ = new QComboBox;
    boundary_->setObjectName("terrainImportBoundary");
    boundary_->setAccessibleName("Boundary mode");
    boundary_->addItem("Convex hull", "convexHull");
    boundary_->addItem("Limit edge length", "maxEdgeLength");
    boundary_->addItem("Boundary layer", "boundary");
    tri->addWidget(caption("Boundary"), 0, 0);
    tri->addWidget(boundary_, 0, 1);
    maxEdge_ = new QDoubleSpinBox;
    maxEdge_->setObjectName("terrainImportMaxEdge");
    maxEdge_->setAccessibleName("Maximum edge length in metres");
    maxEdge_->setRange(0, 2e6);
    maxEdge_->setDecimals(3);
    maxEdge_->setSuffix(" m");
    maxEdge_->setSpecialValueText("Not set");
    maxEdge_->setKeyboardTracking(false);
    tri->addWidget(caption("Max edge length"), 1, 0);
    tri->addWidget(maxEdge_, 1, 1);
    flip_ = new QCheckBox("Flip flat triangles");
    flip_->setObjectName("terrainImportFlip");
    flip_->setChecked(true);
    tri->addWidget(flip_, 2, 0, 1, 2);
    steiner_ = new QCheckBox("Fill remaining flat areas with Steiner points");
    steiner_->setObjectName("terrainImportSteiner");
    tri->addWidget(steiner_, 3, 0, 1, 2);
    column->addLayout(tri);
    column->addStretch(1);
    scroll->setWidget(settings);
    split->addWidget(scroll);

    // ---- Preview and report (right) -------------------------------------------------------
    auto *right = new QWidget;
    right->setObjectName("terrainImportRight");
    auto *rightColumn = new QVBoxLayout(right);
    rightColumn->setContentsMargins(16, 14, 22, 14);
    rightColumn->setSpacing(8);
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(12);
    sources_ = new QRadioButton("Sources");
    sources_->setObjectName("terrainImportShowSources");
    sources_->setAccessibleName("Preview sources");
    surface_ = new QRadioButton("Surface");
    surface_->setObjectName("terrainImportShowSurface");
    surface_->setAccessibleName("Preview surface");
    auto *modes = new QButtonGroup(this);
    modes->addButton(sources_, 0);
    modes->addButton(surface_, 1);
    sources_->setChecked(true);
    toolbar->addWidget(sources_);
    toolbar->addWidget(surface_);
    shading_ = new QCheckBox("Elevation shading");
    shading_->setObjectName("terrainImportShading");
    toolbar->addWidget(shading_);
    toolbar->addStretch(1);
    fit_ = new QPushButton("Fit");
    fit_->setObjectName("terrainImportFit");
    fit_->setAccessibleName("Fit preview");
    fit_->setToolTip("Fit the preview (Shift+Z)");
    fit_->setAutoDefault(false);
    toolbar->addWidget(fit_);
    rightColumn->addLayout(toolbar);
    preview_ = new TerrainPreview;
    rightColumn->addWidget(preview_, 3);

    reportPanel_ = new QWidget;
    reportPanel_->setObjectName("terrainImportReportPanel");
    auto *reportColumn = new QVBoxLayout(reportPanel_);
    reportColumn->setContentsMargins(0, 4, 0, 0);
    reportColumn->setSpacing(6);
    auto *reportHeader = new QHBoxLayout;
    reportHeader->setSpacing(8);
    reportHeader->addWidget(heading("Import report"));
    reportHeader->addStretch(1);
    copyReport_ = new QPushButton("Copy report");
    copyReport_->setObjectName("terrainImportCopyReport");
    copyReport_->setAccessibleName("Copy report as text");
    copyReport_->setAutoDefault(false);
    reportHeader->addWidget(copyReport_);
    saveReport_ = new QPushButton("Save report…");
    saveReport_->setObjectName("terrainImportSaveReport");
    saveReport_->setAccessibleName("Save report");
    saveReport_->setAutoDefault(false);
    reportHeader->addWidget(saveReport_);
    reportColumn->addLayout(reportHeader);
    reportSummary_ = new QLabel;
    reportSummary_->setObjectName("terrainImportReportSummary");
    reportSummary_->setTextFormat(Qt::PlainText);
    reportSummary_->setWordWrap(true);
    reportColumn->addWidget(reportSummary_);
    reportList_ = new QTreeWidget;
    reportList_->setObjectName("terrainImportReport");
    reportList_->setAccessibleName("Report: refusals, merges and notices");
    reportList_->setHeaderHidden(true);
    reportList_->setRootIsDecorated(true);
    reportList_->setUniformRowHeights(true);
    reportList_->setMinimumHeight(140);
    reportColumn->addWidget(reportList_, 1);
    rightColumn->addWidget(reportPanel_, 2);
    reportPanel_->hide();
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    rule(layout);

    // Footer: status, Show report, Cancel and Import.
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(22, 10, 22, 16);
    footer->setSpacing(10);
    status_ = new QLabel;
    status_->setObjectName("terrainImportStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    footer->addWidget(status_, 1);
    showReport_ = new QPushButton("Show report");
    showReport_->setObjectName("terrainImportShowReport");
    showReport_->setAutoDefault(false);
    footer->addWidget(showReport_);
    cancel_ = new QPushButton("Cancel");
    cancel_->setObjectName("terrainImportCancel");
    cancel_->setAutoDefault(false);
    footer->addWidget(cancel_);
    import_ = new QPushButton("Import");
    import_->setObjectName("terrainImportConfirm");
    import_->setProperty("accentFill", true); // The shared focus rule (R091.h).
    import_->setAutoDefault(false);
    import_->setDefault(true);
    import_->setEnabled(false);
    footer->addWidget(import_);
    layout->addLayout(footer);

    // Tab order: settings top to bottom, the preview column, then the footer.
    const std::vector<QWidget *> order{format_, units_, preset_, delimiter_, header_};
    for (size_t i = 1; i < order.size(); ++i)
        setTabOrder(order[i - 1], order[i]);

    // ---- Behaviour -------------------------------------------------------------------------
    auto changed = [this](int delay) {
        return [this, delay] {
            if (!updating_) {
                refresh();
                schedule(delay);
            }
        };
    };
    connect(format_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_)
            return;
        roles_->setRowCount(0);
        layerRoles_.clear(); // the roles of the other format do not apply
        layerNames_.clear();
        dryRun_.reset();
        refreshUnits();
        refresh();
        schedule(0);
    });
    connect(units_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_)
            return;
        unitsCleared_ = units_->currentData().toString().isEmpty();
        refresh();
        schedule(0);
    });
    connect(preset_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_)
            return;
        applyPreset();
        refresh();
        schedule(0);
    });
    connect(delimiter_, &QComboBox::currentIndexChanged, this, changed(0));
    connect(header_, &QComboBox::currentIndexChanged, this, changed(0));
    connect(role_, &QComboBox::activated, this, [this] {
        const auto code = role_->currentData().toString();
        const auto rows = roles_->selectionModel()->selectedRows();
        QList<int> chosen;
        for (const auto &row : rows)
            chosen << row.row();
        if (chosen.isEmpty() && roles_->currentRow() >= 0)
            chosen << roles_->currentRow();
        for (const int row : chosen)
            if (row >= 0 && size_t(row) < layerNames_.size()) {
                if (code == "auto")
                    layerRoles_.erase(layerNames_[size_t(row)]);
                else
                    layerRoles_[layerNames_[size_t(row)]] = code;
            }
        // A boundary layer limits the terrain; without one, Boundary mode has nothing to use.
        const bool hasBoundary = std::any_of(
            layerRoles_.begin(), layerRoles_.end(),
            [](const auto &entry) { return entry.second == "boundary"; });
        updating_ = true;
        if (hasBoundary)
            boundary_->setCurrentIndex(boundary_->findData("boundary"));
        else if (boundary_->currentData().toString() == "boundary")
            boundary_->setCurrentIndex(boundary_->findData("convexHull"));
        updating_ = false;
        refresh();
        schedule(0);
    });
    connect(roles_, &QTableWidget::currentCellChanged, this, [this] { refreshRoles(); });
    connect(roles_, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto row = roles_->currentRow();
        if (row < 0 || size_t(row) >= layerNames_.size())
            return;
        const auto found = layerRoles_.find(layerNames_[size_t(row)]);
        role_->setCurrentIndex(role_->findData(
            found == layerRoles_.end() ? QString("auto") : found->second));
    });
    for (auto *field : {originE_, originN_, originZ_})
        connect(field, &QDoubleSpinBox::valueChanged, this, [this] {
            if (updating_)
                return;
            originEdited_ = true;
            refresh();
            schedule();
        });
    connect(originReset_, &QPushButton::clicked, this, [this] {
        originEdited_ = false;
        refresh();
        schedule(0);
    });
    connect(crs_, &QLineEdit::textEdited, this, changed(400));
    connect(boundary_, &QComboBox::currentIndexChanged, this, changed(0));
    connect(maxEdge_, &QDoubleSpinBox::valueChanged, this, changed(250));
    connect(flip_, &QCheckBox::toggled, this, changed(0));
    connect(steiner_, &QCheckBox::toggled, this, changed(0));
    connect(modes, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked)
            preview_->setMode(id == 1 ? TerrainPreview::Mode::Surface
                                      : TerrainPreview::Mode::Sources);
    });
    connect(shading_, &QCheckBox::toggled, preview_, &TerrainPreview::setShading);
    connect(fit_, &QPushButton::clicked, this, [this] {
        preview_->clearMarker();
        preview_->fit();
    });
    connect(reportList_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item) { chooseLocation(item); });
    reportList_->installEventFilter(new EnterChooses(this, [this] {
        chooseLocation(reportList_->currentItem());
    }));
    roles_->installEventFilter(new EnterChooses(this, [this] { role_->setFocus(); }));
    connect(showReport_, &QPushButton::clicked, this, [this] {
        reportPanel_->setVisible(!reportPanel_->isVisible());
        refreshFooter();
        if (reportPanel_->isVisible())
            reportList_->setFocus(Qt::TabFocusReason);
    });
    connect(copyReport_, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(reportText());
        reportSummary_->setText(reportSummary_->property("base").toString() +
                                " Copied the report as text.");
    });
    connect(saveReport_, &QPushButton::clicked, this, [this] {
        const auto stem = QFileInfo(setup_.fileName).completeBaseName();
        const auto path = QFileDialog::getSaveFileName(
            this, "Save terrain import report", stem + "-terrain-report.txt",
            "Report text (*.txt);;Report JSON (*.json)");
        if (path.isEmpty())
            return;
        try {
            saveReport(path);
            reportSummary_->setText(reportSummary_->property("base").toString() + " Saved to " +
                                    QFileInfo(path).fileName() + '.');
        } catch (const std::exception &error) {
            reportSummary_->setText(reportSummary_->property("base").toString() +
                                    " Could not save the report: " +
                                    QString::fromUtf8(error.what()));
        }
    });
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    connect(import_, &QPushButton::clicked, this, [this] { importNow(); });

    // The design's wide sheet, within the screen.
    const auto *screen =
        parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
    const auto available = screen ? screen->availableGeometry().size() - QSize(48, 64)
                                  : QSize(1100, 760);
    resize(std::clamp(available.width(), 360, 1120), std::clamp(available.height(), 320, 780));
    split->setSizes({440, std::max(200, width() - 440)});
    refreshUnits();
    nameInternalEditors(this);
}

void TerrainImportDialog::setTheme(const ThemeColors &colors) {
    setup_.colors = colors;
    setStyleSheet(styleSheetFor(colors));
    preview_->setColors(colors);
}
QString TerrainImportDialog::styleSheetFor(const ThemeColors &colors) const {
    const auto percent = interfaceTextPercent();
    auto style = QStringLiteral(R"(
#terrainImportTitle {font-size:$titlePx;font-weight:700;}
#terrainImportHeading {font-size:$headingPx;font-weight:700;padding-top:6px;}
#terrainImportSubline,#terrainImportFieldLabel,#terrainImportStatus,#terrainImportReportSummary {color:$muted;}
#terrainImportFormula,#terrainImportSourceExtents,#terrainImportLocalExtents,#terrainImportDocumentOrigin {font-family:'DejaVu Sans Mono','Noto Sans Mono',monospace;}
#terrainImportRule {background:$border;border:0;}
QFrame#terrainImportBanner {border:2px solid $failure;border-radius:8px;background:$failureSurface;}
#terrainImportBannerText {background:transparent;color:$ink;}
#terrainImportStatus[refused="true"] {color:$failure;font-weight:600;}
#terrainImportConfirm {background:$accent;color:$onAccent;font-weight:600;border:2px solid $accent;border-radius:6px;padding:4px 14px;}
#terrainImportConfirm:focus {border:2px solid $surface;}
#terrainImportConfirm:disabled {background:$hover;color:$muted;border-color:$hover;}
#terrainImportCancel,#terrainImportShowReport,#terrainImportFit,#terrainImportCopyReport,#terrainImportSaveReport,#terrainImportOriginReset {background:$input;border:1px solid $border;border-radius:6px;padding:3px 10px;}
#terrainImportCancel {padding:6px 14px;}
#terrainImportCancel:focus,#terrainImportShowReport:focus,#terrainImportFit:focus,#terrainImportCopyReport:focus,#terrainImportSaveReport:focus,#terrainImportOriginReset:focus {border:1px solid $accent;}
)");
    style.replace("$titlePx", QString::number(interfaceExtent(18, percent)) + "px");
    style.replace("$headingPx", QString::number(interfaceExtent(14, percent)) + "px");
    style.replace("$onAccent", colors.dark ? "#172019" : "#ffffff");
    style.replace("$failureSurface", colors.failureSurface.name());
    style.replace("$failure", colors.failure.name());
    style.replace("$surface", colors.surface.name());
    style.replace("$border", colors.border.name());
    style.replace("$muted", colors.muted.name());
    style.replace("$hover", colors.hover.name());
    style.replace("$input", colors.input.name());
    style.replace("$accent", colors.accent.name());
    style.replace("$ink", colors.ink.name());
    return style;
}

// ---- Settings ------------------------------------------------------------------------------
QJsonObject TerrainImportDialog::command() const {
    QJsonObject command{{"command", "terrain.import"},
                        {"format", format_->currentData().toString()}};
    const auto unit = units_->currentData().toString();
    if (!unit.isEmpty() && unit != "header")
        command["units"] = unit;
    QJsonObject roles;
    for (const auto &[layer, role] : layerRoles_)
        if (std::find(layerNames_.begin(), layerNames_.end(), layer) != layerNames_.end())
            roles[layer] = role;
    if (!roles.isEmpty())
        command["roles"] = roles;
    if (originEdited_ && !doc_.site())
        command["origin"] = QJsonObject{{"easting", originE_->value()},
                                        {"northing", originN_->value()},
                                        {"elevation", originZ_->value()}};
    const auto crs = crs_->text().trimmed();
    if (!crs.isEmpty())
        command["crs"] = crs;
    if (!setup_.fileName.isEmpty())
        command["sourceName"] = QFileInfo(setup_.fileName).fileName().left(int(siteSourceBytes));
    command["flatMode"] = flip_->isChecked() ? "flip" : "reportOnly";
    command["steinerFill"] = steiner_->isChecked();
    const auto mode = boundary_->currentData().toString();
    if (mode != "convexHull" && maxEdge_->value() > 0)
        command["maxEdgeLength"] = std::max(maxEdge_->value(), 1e-3);
    if (points()) {
        const auto preset = preset_->currentData().toString();
        if (preset == "custom") {
            QJsonArray list;
            for (auto *combo : columns_)
                list.append(combo->currentData().toString());
            command["columns"] = list;
        } else if (!preset.isEmpty())
            command["columns"] = preset;
        if (const auto d = delimiter_->currentData().toString(); !d.isEmpty())
            command["delimiter"] = d;
        if (const auto h = header_->currentData().toString(); !h.isEmpty())
            command["header"] = h == "yes";
    }
    return command;
}
QString TerrainImportDialog::settingsProblem() const {
    const auto mode = boundary_->currentData().toString();
    const bool hasBoundary = std::any_of(layerRoles_.begin(), layerRoles_.end(), [](const auto &entry) {
        return entry.second == "boundary";
    });
    if (mode == "maxEdgeLength" && !(maxEdge_->value() > 0))
        return "Enter a maximum edge length, or choose another boundary mode.";
    if (mode == "boundary" && !hasBoundary)
        return "Give one layer the Boundary role, or choose another boundary mode.";
    if (mode != "boundary" && hasBoundary)
        return "A layer has the Boundary role, so the boundary limits the terrain. Choose "
               "Boundary layer, or change that layer's role.";
    return {};
}
bool TerrainImportDialog::checking() const { return running_ || pending_ || rerun_.isActive(); }
bool TerrainImportDialog::canImport() const {
    const auto unit = units_->currentData().toString();
    return !unit.isEmpty() && !checking() && dryRun_ && dryRun_->result && dryRun_->result->ok &&
           dryRun_->error.isEmpty() && dryRun_->command == command() &&
           dryRun_->revision == doc_.revision() && settingsProblem().isEmpty();
}

void TerrainImportDialog::refreshUnits() {
    const bool wasUpdating = updating_;
    updating_ = true;
    const auto previous = units_->currentData().toString();
    units_->clear();
    units_->addItem("Choose units…", "");
    if (!points()) {
        const auto header = dryRun_ ? dryRun_->headerUnit : QString();
        if (!header.isEmpty())
            units_->addItem("From the file: " + unitLabel(header), "header");
    }
    for (const auto &choice : unitChoices)
        units_->addItem(choice.label, choice.code);
    auto index = units_->findData(previous);
    // A DXF that declares its units starts with them chosen (ADR 0168 section 1: $INSUNITS or an
    // explicit choice); the user can still choose another.
    if (previous.isEmpty() && !points() && dryRun_ && !dryRun_->headerUnit.isEmpty() &&
        !unitsCleared_)
        index = units_->findData("header");
    units_->setCurrentIndex(std::max(index, 0));
    updating_ = wasUpdating;
}
void TerrainImportDialog::applyPreset() {
    const auto preset = preset_->currentData().toString();
    std::optional<std::vector<TerrainColumn>> chosen;
    if (!preset.isEmpty() && preset != "custom")
        chosen = terrainColumnPreset(preset);
    else if (preset.isEmpty() && dryRun_) {
        std::vector<TerrainColumn> detected;
        for (const auto &name : dryRun_->sourceReport["columns"].toArray())
            if (const auto column = terrainColumn(name.toString()))
                detected.push_back(*column);
        if (!detected.empty())
            chosen = detected;
    }
    if (!chosen)
        return;
    const bool wasUpdating = updating_;
    updating_ = true;
    for (size_t i = 0; i < columns_.size(); ++i) {
        const auto column = i < chosen->size() ? (*chosen)[i] : TerrainColumn::Ignore;
        columns_[i]->setCurrentIndex(
            columns_[i]->findData(QString::fromLatin1(terrainColumnName(column))));
    }
    updating_ = wasUpdating;
}
void TerrainImportDialog::refreshSample() {
    pointsBox_->setVisible(points());
    if (!points())
        return;
    // The first data lines, split as the parser splits them.
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(setup_.bytes.left(64 * 1024));
    QStringList lines;
    for (auto line : text.split('\n')) {
        if (line.endsWith('\r'))
            line.chop(1);
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            continue;
        lines << line;
        if (lines.size() >= 20)
            break;
    }
    if (!lines.isEmpty() && text.size() == 64 * 1024 && setup_.bytes.size() > 64 * 1024)
        lines.removeLast(); // possibly cut
    QString delimiter = delimiter_->currentData().toString();
    if (delimiter.isEmpty() && dryRun_)
        delimiter = dryRun_->sourceReport["delimiter"].toString();
    if (delimiter.isEmpty()) {
        delimiter = "whitespace";
        for (const auto &[name, separator] :
             {std::pair{"comma", QChar(',')}, std::pair{"tab", QChar('\t')},
              std::pair{"semicolon", QChar(';')}}) {
            const auto containing = std::count_if(lines.begin(), lines.end(), [&](const QString &l) {
                return l.contains(separator);
            });
            if (2 * containing > lines.size()) {
                delimiter = name;
                break;
            }
        }
    }
    auto split = [&](const QString &line) {
        if (delimiter == "whitespace")
            return line.simplified().split(' ', Qt::SkipEmptyParts);
        const QChar separator = delimiter == "comma" ? QChar(',')
                                : delimiter == "tab" ? QChar('\t')
                                                     : QChar(';');
        auto parts = line.split(separator);
        for (auto &part : parts)
            part = part.trimmed();
        return parts;
    };
    bool header{};
    if (const auto h = header_->currentData().toString(); !h.isEmpty())
        header = h == "yes";
    else if (dryRun_ && dryRun_->sourceReport.contains("header"))
        header = dryRun_->sourceReport["header"].toBool();
    else if (!lines.isEmpty()) {
        const auto first = split(lines.front());
        header = std::count_if(first.begin(), first.end(), [](const QString &v) {
                     bool ok{};
                     v.toDouble(&ok);
                     return ok;
                 }) < 3;
    }
    QStringList headings;
    if (header && !lines.isEmpty())
        headings = split(lines.takeFirst());
    while (lines.size() > 8)
        lines.removeLast();
    int shown = int(headings.size());
    std::vector<QStringList> rows;
    for (const auto &line : lines) {
        rows.push_back(split(line));
        shown = std::max(shown, int(rows.back().size()));
    }
    shown = std::clamp(shown, 3, 16);
    // A meaning per column.
    if (int(columns_.size()) != shown) {
        columns_.clear();
        while (auto *item = columnsGrid_->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        QWidget *previous = header_;
        for (int c = 0; c < shown; ++c) {
            auto *combo = new QComboBox;
            combo->setObjectName(QString("terrainImportColumn%1").arg(c + 1));
            combo->setAccessibleName(QString("Column %1 meaning").arg(c + 1));
            for (size_t k = 0; k < std::size(columnNames); ++k)
                combo->addItem(columnLabels[k], columnNames[k]);
            combo->setCurrentIndex(combo->findData("ignore"));
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumContentsLength(6);
            auto *label = new QLabel(QString::number(c + 1));
            label->setObjectName("terrainImportFieldLabel");
            columnsGrid_->addWidget(label, (c / 4) * 2, c % 4);
            columnsGrid_->addWidget(combo, (c / 4) * 2 + 1, c % 4);
            connect(combo, &QComboBox::activated, this, [this] {
                updating_ = true;
                preset_->setCurrentIndex(preset_->findData("custom"));
                updating_ = false;
                refresh();
                schedule(0);
            });
            setTabOrder(previous, combo);
            previous = combo;
            columns_.push_back(combo);
        }
        setTabOrder(previous, sample_);
        setStyleSheet(styleSheetFor(setup_.colors));
        applyPreset();
    } else if (preset_->currentData().toString() != "custom")
        applyPreset();
    sample_->clear();
    sample_->setColumnCount(shown);
    sample_->setRowCount(int(rows.size()));
    QStringList labels;
    for (int c = 0; c < shown; ++c)
        labels << (c < headings.size() && !headings[c].isEmpty() ? headings[c]
                                                                 : QString::number(c + 1));
    sample_->setHorizontalHeaderLabels(labels);
    for (int r = 0; r < int(rows.size()); ++r)
        for (int c = 0; c < shown && c < rows[size_t(r)].size(); ++c)
            sample_->setItem(r, c, new QTableWidgetItem(rows[size_t(r)][c]));
    sample_->resizeColumnsToContents();
    // The detected values, when Detect is chosen.
    if (dryRun_ && !dryRun_->sourceReport.isEmpty()) {
        const auto report = dryRun_->sourceReport;
        delimiter_->setItemText(0, "Detect (" +
                                       delimiter_->itemText(delimiter_->findData(
                                           report["delimiter"].toString())).toLower() +
                                       ")");
        header_->setItemText(0, report["header"].toBool() ? "Detect (header)"
                                                          : "Detect (no header)");
        const auto preset = report["columnsPreset"].toString();
        preset_->setItemText(0, preset.isEmpty() || preset_->currentData().toString() == "custom"
                                    ? QString("Detect")
                                    : "Detect (" + preset + ")");
    }
}
void TerrainImportDialog::refreshRoles() {
    rolesHeading_->setText(points() ? "Description code roles" : "Layer roles");
    roles_->setAccessibleName(points() ? "Description code roles" : "Layer roles");
    roles_->setHorizontalHeaderLabels({points() ? "Code" : "Layer", "Contents", "Role"});
    if (!dryRun_)
        return;
    std::vector<QString> names;
    for (const auto &layer : dryRun_->layers)
        names.push_back(layer.name);
    const auto current = roles_->currentRow();
    if (names != layerNames_) {
        layerNames_ = names;
        roles_->setRowCount(int(names.size()));
    }
    for (size_t i = 0; i < dryRun_->layers.size(); ++i) {
        const auto &layer = dryRun_->layers[i];
        QStringList contents;
        if (layer.polylines)
            contents << plural(double(layer.polylines), "polyline", "polylines") +
                            (layer.closed ? QString(" (%1 closed)").arg(count(double(layer.closed)))
                                          : QString());
        if (layer.points)
            contents << plural(double(layer.points), "point", "points");
        const QString automatic =
            layer.points && !layer.polylines ? "ground points"
            : !layer.varyingZ                ? "contour"
            : !layer.constantZ               ? "breakline"
                                             : "contour or breakline by Z";
        const auto found = layerRoles_.find(layer.name);
        const auto role = found == layerRoles_.end() ? QString("Automatic: ") + automatic
                                                : roleLabel(found->second);
        const QString name = layer.name.isEmpty() ? QString("(no code)") : layer.name;
        auto set = [&](int c, const QString &text) {
            auto *item = roles_->item(int(i), c);
            if (!item)
                roles_->setItem(int(i), c, item = new QTableWidgetItem);
            item->setText(text);
        };
        set(0, name);
        set(1, contents.join(", "));
        set(2, role);
    }
    if (current < 0 && roles_->rowCount() > 0) {
        roles_->setCurrentCell(0, 0);
    }
    role_->setEnabled(roles_->rowCount() > 0);
    const auto row = roles_->currentRow();
    if (row >= 0 && size_t(row) < layerNames_.size()) {
        const auto found = layerRoles_.find(layerNames_[size_t(row)]);
        role_->setCurrentIndex(
            role_->findData(found == layerRoles_.end() ? QString("auto") : found->second));
        role_->setAccessibleName("Role for " +
                                 (layerNames_[size_t(row)].isEmpty() ? QString("points with no code")
                                                                     : layerNames_[size_t(row)]));
    }
}
void TerrainImportDialog::refreshSite() {
    const auto &site = doc_.site();
    originBox_->setVisible(!site);
    documentOrigin_->setVisible(bool(site));
    if (site) {
        documentOrigin_->setText(
            QString("The document's site origin, used for this import:\nE %1 m · N %2 m · Z %3 m%4")
                .arg(decimal(site->easting, 3), decimal(site->northing, 3),
                     decimal(site->elevation, 3),
                     site->crs.empty() ? QString()
                                       : " · " + QString::fromStdString(site->crs)));
        if (crs_->text().isEmpty() && !site->crs.empty() && !crs_->isModified())
            crs_->setText(QString::fromStdString(site->crs));
    }
    originReset_->setEnabled(originEdited_);
    const auto terrain = dryRun_ && dryRun_->result
                             ? dryRun_->result->report["terrain"].toObject()
                             : QJsonObject();
    if (terrain.isEmpty()) {
        sourceExtents_->setText("—");
        formula_->setText("—");
        localExtents_->setText("—");
        return;
    }
    sourceExtents_->setText(
        extentsText(terrain["sourceExtents"].toObject(), terrain["units"].toString()));
    formula_->setText(terrain["conversion"].toString());
    const auto local = terrain["localExtents"].toObject();
    const auto min = local["min"].toArray(), max = local["max"].toArray();
    if (min.size() == 3 && max.size() == 3) {
        QStringList parts;
        const char *axes[]{"X", "Y", "Z"};
        for (int i = 0; i < 3; ++i)
            parts << QString("%1 %2 to %3")
                         .arg(axes[i],
                              displayLength(min[i].toDouble(), doc_.displayUnits(),
                                            doc_.displayPrecision()),
                              displayLength(max[i].toDouble(), doc_.displayUnits(),
                                            doc_.displayPrecision()));
        localExtents_->setText(parts.join(" · "));
    }
    if (!site && !originEdited_) {
        const auto proposed = terrain["proposedOrigin"].toObject();
        updating_ = true;
        originE_->setValue(proposed["easting"].toDouble());
        originN_->setValue(proposed["northing"].toDouble());
        originZ_->setValue(proposed["elevation"].toDouble());
        updating_ = false;
    }
}
void TerrainImportDialog::refreshTriangulation() {
    const auto mode = boundary_->currentData().toString();
    maxEdge_->setEnabled(mode != "convexHull");
    maxEdge_->setSpecialValueText(mode == "maxEdgeLength" ? "Required" : "Not set");
}

// ---- Report --------------------------------------------------------------------------------
QJsonObject TerrainImportDialog::report() const {
    return dryRun_ && dryRun_->result ? dryRun_->result->report : QJsonObject();
}
QString TerrainImportDialog::reportText() const {
    QStringList out;
    out << "Terrain import report: " + QFileInfo(setup_.fileName).fileName();
    if (dryRun_ && !dryRun_->error.isEmpty()) {
        out << "Outcome: refused. Nothing was changed.";
        out << dryRun_->error;
        return out.join('\n') + '\n';
    }
    const auto full = report();
    const auto terrain = full["terrain"].toObject();
    if (terrain.isEmpty()) {
        out << "No report yet: choose units to check the source.";
        return out.join('\n') + '\n';
    }
    const auto outcome = terrain["outcome"].toString();
    out << "Outcome: " + (outcome == "refused"               ? QString("refused. Nothing was changed.")
                          : outcome == "importedWithNotices" ? QString("ready, with notices")
                                                             : QString("ready"));
    out << QString("Format: %1 · Units: %2 (%3 m per unit)")
               .arg(full["format"].toString(), terrain["units"].toString(),
                    terrain["metresPerUnitExact"].toString());
    out << "Conversion: " + terrain["conversion"].toString();
    out << "Source extents: " +
               extentsText(terrain["sourceExtents"].toObject(), terrain["units"].toString());
    out << "Local extents: " + extentsText(terrain["localExtents"].toObject(), "m");
    out << QString("Triangles: %1 · Vertices: %2 · Tiles: %3")
               .arg(count(terrain["triangles"].toDouble()), count(terrain["vertices"].toDouble()),
                    count(terrain["tiles"].toDouble()));
    const auto units = terrain["units"].toString();
    for (const auto &[group, title] :
         {std::pair{Group::Refusals, "Refusals"}, std::pair{Group::Merges, "Merges"},
          std::pair{Group::Notices, "Notices"}}) {
        const auto kinds = groupKinds(terrain, group);
        if (kinds.isEmpty())
            continue;
        out << "" << title;
        for (const auto &value : kinds) {
            const auto kind = value.toObject();
            out << "  " + kindText(kind) + " [" + kind["kind"].toString() + "]";
            for (const auto &location : kind["locations"].toArray())
                out << "    " + locationText(location.toObject(), units);
            if (const auto omitted = kind["locationsOmitted"].toDouble();
                omitted > 0 && !kind["locations"].toArray().isEmpty())
                out << "    … " + count(omitted) + " more not listed";
        }
    }
    if (const auto omitted = terrain["kindsOmitted"].toDouble(); omitted > 0)
        out << "" << QString("%1 further kinds (%2 occurrences) are counted but not listed.")
                         .arg(count(omitted), count(terrain["occurrencesInOmittedKinds"].toDouble()));
    const auto counts = terrain["counts"].toObject();
    if (!counts.isEmpty()) {
        out << "" << "Counts";
        for (auto it = counts.begin(); it != counts.end(); ++it)
            out << "  " + it.key() + ": " + count(it.value().toDouble());
    }
    return out.join('\n') + '\n';
}
void TerrainImportDialog::saveReport(const QString &path) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    const auto bytes = path.endsWith(".json", Qt::CaseInsensitive)
                           ? QJsonDocument(report()).toJson(QJsonDocument::Indented)
                           : reportText().toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error(file.errorString().toStdString());
}
void TerrainImportDialog::refreshReport() {
    const auto terrain = report()["terrain"].toObject();
    // Keep the chosen location across a refresh with the same report.
    const auto chosen = reportList_->currentItem() ? reportList_->currentItem()->text(0) : QString();
    const QSignalBlocker block(reportList_);
    reportList_->clear();
    QString base;
    if (dryRun_ && !dryRun_->error.isEmpty())
        base = "Nothing was changed. " + dryRun_->error;
    else if (terrain.isEmpty())
        base = "The report appears once the source has been checked.";
    else
        base = terrain["outcome"].toString() == "refused"
                   ? "Refused: nothing was changed. Choose a location to show it in the preview."
                   : "Choose a location to show it in the preview.";
    reportSummary_->setProperty("base", base);
    reportSummary_->setText(base);
    copyReport_->setEnabled(!terrain.isEmpty() || (dryRun_ && !dryRun_->error.isEmpty()));
    saveReport_->setEnabled(copyReport_->isEnabled());
    if (terrain.isEmpty())
        return;
    const auto units = terrain["units"].toString();
    const double scale = dryRun_->metresPerUnit;
    const auto origin = dryRun_->result->origin;
    QTreeWidgetItem *restore = nullptr;
    for (const auto &[group, title] :
         {std::pair{Group::Refusals, "Refusals"}, std::pair{Group::Merges, "Merges"},
          std::pair{Group::Notices, "Notices"}}) {
        const auto kinds = groupKinds(terrain, group);
        if (kinds.isEmpty())
            continue;
        double total = 0;
        for (const auto &value : kinds)
            total += value.toObject()["count"].toDouble();
        auto *top = new QTreeWidgetItem(reportList_, {QString("%1 · %2 %3, %4")
                                                          .arg(title)
                                                          .arg(kinds.size())
                                                          .arg(kinds.size() == 1 ? "kind" : "kinds")
                                                          .arg(plural(total, "occurrence",
                                                                      "occurrences"))});
        top->setData(0, Qt::UserRole + 2, QString(title).toLower());
        for (const auto &value : kinds) {
            const auto kind = value.toObject();
            auto *item = new QTreeWidgetItem(top, {kindText(kind)});
            item->setToolTip(0, kindText(kind) + " [" + kind["kind"].toString() + "]");
            item->setData(0, Qt::UserRole + 2, kind["kind"].toString());
            for (const auto &entry : kind["locations"].toArray()) {
                const auto location = entry.toObject();
                auto *child = new QTreeWidgetItem(item, {locationText(location, units)});
                child->setToolTip(0, child->text(0));
                const auto xy = location["xy"].toArray();
                if (xy.size() == 2) {
                    child->setData(0, Qt::UserRole,
                                   QPointF(xy[0].toDouble() * scale - origin.easting,
                                           xy[1].toDouble() * scale - origin.northing));
                    child->setData(0, Qt::UserRole + 1, true);
                }
                if (child->text(0) == chosen)
                    restore = child;
            }
            if (const auto omitted = kind["locationsOmitted"].toDouble();
                omitted > 0 && !kind["locations"].toArray().isEmpty())
                new QTreeWidgetItem(item, {"… " + count(omitted) + " more not listed"});
        }
        top->setExpanded(true);
        if (group == Group::Refusals)
            for (int i = 0; i < top->childCount(); ++i)
                top->child(i)->setExpanded(true);
    }
    if (restore)
        reportList_->setCurrentItem(restore);
    else if (reportList_->topLevelItemCount())
        reportList_->setCurrentItem(reportList_->topLevelItem(0));
}
void TerrainImportDialog::chooseLocation(QTreeWidgetItem *item) {
    if (!item || !item->data(0, Qt::UserRole + 1).toBool()) {
        if (item && item->childCount() == 0)
            preview_->clearMarker();
        return;
    }
    const auto local = item->data(0, Qt::UserRole).toPointF();
    double span = 20;
    if (const auto *data = preview_->data(); data && !data->bounds.isNull())
        span = std::clamp(std::max(data->bounds.width(), data->bounds.height()) / 8, 5.0, 500.0);
    preview_->focusLocation(local, span);
}

// ---- Dry runs ------------------------------------------------------------------------------
void TerrainImportDialog::schedule(int delayMs) {
    rerun_.start(delayMs);
    refreshFooter();
}
void TerrainImportDialog::documentChanged() {
    refreshSite();
    if (!dryRun_ || dryRun_->revision != doc_.revision())
        schedule(150);
}
void TerrainImportDialog::start() {
    if (running_) {
        pending_ = true;
        refreshFooter();
        return;
    }
    running_ = true;
    pending_ = false;
    const auto generation = ++generation_;
    revision_ = doc_.revision();
    const auto unit = units_->currentData().toString();
    const bool chosen = !unit.isEmpty();
    QPointer<TerrainImportDialog> guard(this);
    // The worker owns copies of its inputs: the source bytes (shared, read-only), the request,
    // the site origin and the document's other terrain triangles. It never reads the document.
    pool_.start([bytes = setup_.bytes, command = command(), chosen, site = doc_.site(),
                 other = doc_.materializedTotals().terrainTriangles, revision = revision_,
                 generation, guard] {
        std::shared_ptr<TerrainImportDryRun> result;
        try {
            result = dryRun(bytes, command, chosen, site, other, revision);
        } catch (const std::exception &error) {
            result = std::make_shared<TerrainImportDryRun>();
            result->command = command;
            result->unitsChosen = chosen;
            result->revision = revision;
            result->error = QString::fromUtf8(error.what());
        }
        QMetaObject::invokeMethod(
            qApp,
            [guard, generation, result = std::move(result)]() mutable {
                if (guard)
                    guard->finished(generation, std::move(result));
            },
            Qt::QueuedConnection);
    });
    refreshFooter();
}
void TerrainImportDialog::finished(std::uint64_t generation,
                                   std::shared_ptr<TerrainImportDryRun> result) {
    running_ = false;
    // A stale run (the settings changed while it ran) is dropped; only one result is kept.
    if (generation == generation_)
        dryRun_ = std::move(result);
    if (pending_ || doc_.revision() != revision_) {
        pending_ = false;
        start();
    }
    refreshUnits();
    if (dryRun_ && dryRun_->command != command() && !checking())
        schedule(0); // the units choice followed the file's header
    refresh();
}

void TerrainImportDialog::refresh() {
    const bool wasUpdating = updating_;
    updating_ = true;
    subline_->setText(QFileInfo(setup_.fileName).fileName() + " · " +
                      plural(double(setup_.bytes.size()), "byte", "bytes") +
                      " · adds a terrain to the open model in one undoable edit");
    refreshSample();
    refreshRoles();
    refreshSite();
    refreshTriangulation();
    updating_ = wasUpdating;
    refreshReport();
    // The preview.
    const auto unit = units_->currentData().toString();
    if (unit.isEmpty())
        preview_->setPlaceholder(dryRun_ && dryRun_->unitsMissing
                                     ? "This DXF does not declare its units. Choose them to "
                                       "preview the terrain."
                                     : "Choose units to preview the terrain.");
    else if (dryRun_ && !dryRun_->error.isEmpty())
        preview_->setPlaceholder("The source could not be read.");
    else if (!dryRun_ || !dryRun_->preview)
        preview_->setPlaceholder("Checking the source…");
    else
        preview_->setPlaceholder({});
    if (dryRun_ && dryRun_->preview && dryRun_->preview.get() != preview_->data())
        preview_->setData(dryRun_->preview);
    // The banner: a refusal changes nothing.
    QString banner;
    if (dryRun_ && !dryRun_->error.isEmpty())
        banner = "Nothing was changed. This source cannot be imported: " + dryRun_->error;
    else if (const auto terrain = report()["terrain"].toObject();
             terrain["outcome"].toString() == "refused") {
        const auto refusals = terrain["refusals"].toArray();
        double total = 0;
        for (const auto &value : refusals)
            total += value.toObject()["count"].toDouble();
        auto first = refusals.isEmpty() ? QString()
                                        : refusals.first().toObject()["message"].toString();
        if (first.endsWith('.'))
            first.chop(1);
        banner = "Import refused. Nothing was changed. " + first +
                 (refusals.size() > 2   ? QString(" (and %1 other kinds)").arg(refusals.size() - 1)
                  : refusals.size() == 2 ? QString(" (and 1 other kind)")
                                         : QString()) +
                 QString(". %1 in all; Show report lists the locations.")
                     .arg(plural(total, "problem", "problems"));
    }
    bannerText_->setText(banner);
    bannerText_->setAccessibleName(banner);
    banner_->setVisible(!banner.isEmpty());
    refreshFooter();
}
void TerrainImportDialog::refreshFooter() {
    const auto unit = units_->currentData().toString();
    const auto terrain = report()["terrain"].toObject();
    const bool current = dryRun_ && dryRun_->command == command() && !checking();
    QString text;
    bool refused = false;
    if (current && !dryRun_->error.isEmpty()) {
        text = "Cannot import: nothing was changed.";
        refused = true;
    } else if (unit.isEmpty())
        text = dryRun_ && dryRun_->unitsMissing
                   ? "Choose units: this DXF does not declare them."
                   : "Choose units to preview and import.";
    else if (!current)
        text = "Checking…";
    else if (terrain["outcome"].toString() == "refused") {
        refused = true;
        text = QString("Refused: %1 · nothing was changed")
                   .arg(plural(double(terrain["refusals"].toArray().size()), "kind of problem",
                               "kinds of problem"));
    } else if (const auto problem = settingsProblem(); !problem.isEmpty())
        text = problem;
    else if (!terrain.isEmpty()) {
        const auto notices = terrain["notices"].toArray().size();
        text = QString("Ready: %1 · %2")
                   .arg(plural(terrain["triangles"].toDouble(), "triangle", "triangles"),
                        plural(double(notices), "notice", "notices"));
    }
    status_->setText(text);
    status_->setProperty("refused", refused);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
    showReport_->setText(reportPanel_->isVisible() ? "Hide report" : "Show report");
    showReport_->setAccessibleName(showReport_->text());
    import_->setEnabled(canImport());
    import_->setToolTip(import_->isEnabled() ? "Add the terrain to the open model" : text);
}
void TerrainImportDialog::importNow() {
    if (!canImport())
        return;
    try {
        // The same core path as terrain.import: one labelled "Import terrain" edit.
        auto result = std::move(*dryRun_->result);
        dryRun_->result.reset();
        operation_ = applyTerrainImport(doc_, std::move(result), dryRun_->name);
    } catch (const std::exception &error) {
        dryRun_->error = QString::fromUtf8(error.what());
        refresh();
        return;
    }
    accept();
}
} // namespace sketchy
