#include "io/sheet_export.hpp"
#include "core/drawing_sheets.hpp"
#include "io/drawing_sheet_paint.hpp"
#include "io/new_file.hpp"
#include "sketchyup/version.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPageLayout>
#include <QPageSize>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QPdfWriter>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
namespace sketchy {
namespace {
// A whole drawing set with raster views stays far below this; it bounds memory.
constexpr qsizetype pdfByteLimit = 512 * 1024 * 1024;
constexpr int rasterSideLimit = 8192;
constexpr qint64 rasterPixelLimit = 16000000;

[[noreturn]] void invalid(const std::string &message) {
    throw SheetExportError("INVALID_REQUEST", message);
}
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString id(Id value) { return QString::number(value); }
QJsonArray ids(const std::vector<Id> &values) {
    QJsonArray result;
    for (const auto value : values)
        result.append(id(value));
    return result;
}
template <class T> void addUnique(std::vector<T> &values, const T &value) {
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}
std::string quoted(const std::string &text) { return "“" + text + "”"; }
std::string plural(size_t count, const char *one, const char *many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
}
// "A, B and C".
std::string joined(const std::vector<std::string> &parts) {
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i)
        result += (i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ") + parts[i];
    return result;
}
std::string denominatorText(double denominator) {
    if (denominator == std::round(denominator) && std::abs(denominator) < 1e15)
        return std::to_string(static_cast<long long>(denominator));
    return QString::number(denominator, 'g', 12).toStdString();
}
const char *itemKindName(const DrawingItemData &data) {
    static const char *const names[] = {"text", "label",     "dimension",
                                        "line", "rectangle", "titleBlock"};
    return data.index() < std::size(names) ? names[data.index()] : "item";
}
// What a broken or failed view's reasons mean, for a sentence after the view's name.
std::string reasonPhrase(const std::string &reason) {
    if (reason == "sceneMissing")
        return "its scene was deleted";
    if (reason == "sceneHasNoCamera")
        return "its scene has no camera";
    if (reason == "temporaryClipPlane")
        return "its scene uses a temporary section plane";
    if (reason == "showHidden")
        return "its scene shows hidden geometry";
    if (reason == "captureFailed")
        return "it could not be drawn";
    return reason;
}

// One selected page, prepared once and shared by the findings, the report and painting.
struct PreparedPage {
    SheetExportPage page;
    std::string label; // "A-101", or "page 3" for a sheet without a number.
    PreparedDrawingSheet sheet;
    std::map<Id, DrawingViewEvaluationInput> rasterInputs; // Raster views only.
};
enum class ViewOutcome { Vector, Raster, EmptyFrame };
// Per view: how it is exported, and why.
struct ViewPlan {
    const PreparedPage *page{};
    const PreparedDrawingView *view{};
    bool broken{}, changed{}, raster{}, rasterUnavailable{};
    QSize rasterPixels;
    double rasterDpi{};
};
struct Preparation {
    std::vector<PreparedPage> pages;
    std::vector<ViewPlan> views; // Page order, then the sheet's view order.
    SheetExportPreflight preflight;
};

bool brokenView(const PreparedDrawingView &view) {
    const auto state = view.evaluation.status.state;
    return state == DrawingViewState::Broken || state == DrawingViewState::Failed ||
           !view.pathError.empty();
}
std::string viewLabel(const ViewPlan &plan) {
    return plan.page->label + " view " + std::to_string(plan.view->number) + " " +
           quoted(plan.view->record.name);
}
// The pixel size of a raster view at the requested DPI, within the per-view caps.
std::pair<QSize, double> rasterSize(const DrawingFrame &frame, int dpi) {
    double effective = dpi;
    auto pixels = [&](double d) {
        return QSize(std::max(1, int(std::lround(frame.widthMm / 25.4 * d))),
                     std::max(1, int(std::lround(frame.heightMm / 25.4 * d))));
    };
    auto size = pixels(effective);
    const auto side = std::max(size.width(), size.height());
    if (side > rasterSideLimit)
        effective *= double(rasterSideLimit) / side;
    size = pixels(effective);
    const auto area = qint64(size.width()) * size.height();
    if (area > rasterPixelLimit)
        effective *= std::sqrt(double(rasterPixelLimit) / double(area));
    size = pixels(effective);
    while (size.width() > rasterSideLimit || size.height() > rasterSideLimit ||
           qint64(size.width()) * size.height() > rasterPixelLimit) {
        effective *= .999;
        size = pixels(effective);
    }
    return {size, effective};
}

std::vector<Id> selectedSheets(const Document &doc, const SheetExportOptions &options) {
    const auto ordered = orderedDrawingSheets(doc);
    if (ordered.empty())
        invalid("The document has no drawing sheets to export");
    if (options.sheets.empty())
        return ordered;
    for (const auto sheet : options.sheets)
        if (!doc.drawingSheets().contains(sheet))
            invalid("Drawing sheet " + std::to_string(sheet) + " does not exist");
    std::vector<Id> result;
    for (const auto sheet : ordered)
        if (std::find(options.sheets.begin(), options.sheets.end(), sheet) != options.sheets.end())
            result.push_back(sheet);
    return result;
}

std::vector<SheetExportFinding> findings(const Preparation &prepared,
                                         const SheetExportOptions &options);

Preparation prepare(const Document &doc, const SheetExportOptions &options) {
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        throw SheetExportError("UNAVAILABLE_CONTEXT",
                               "Sheet export requires the graphical font worker");
    if (options.rasterDpi < 72 || options.rasterDpi > 1200)
        invalid("Raster DPI must be 72–1200");
    Preparation result;
    result.preflight.documentId = doc.identity();
    result.preflight.revision = doc.revision();
    const auto ordered = orderedDrawingSheets(doc);
    const auto selected = selectedSheets(doc, options);
    result.pages.reserve(selected.size());
    for (const auto sheet : selected) {
        const auto capture = captureDrawingSheet(doc, sheet);
        PreparedPage page;
        page.sheet = prepareDrawingSheet(capture);
        const auto &record = page.sheet.sheet;
        page.page = {sheet, record.number, record.name, record.paper.widthMm,
                     record.paper.heightMm};
        const auto position = std::find(ordered.begin(), ordered.end(), sheet) - ordered.begin();
        page.label = record.number.empty() ? "page " + std::to_string(position + 1) : record.number;
        for (const auto &view : page.sheet.views)
            if (view.raster)
                for (const auto &input : capture.views)
                    if (input.view == view.record.id)
                        page.rasterInputs.emplace(input.view, input);
        result.preflight.pages.push_back(page.page);
        result.pages.push_back(std::move(page));
    }
    for (const auto &page : result.pages)
        for (const auto &view : page.sheet.views) {
            ViewPlan plan;
            plan.page = &page;
            plan.view = &view;
            plan.broken = brokenView(view);
            plan.changed = !plan.broken && view.evaluation.status.state == DrawingViewState::Stale;
            plan.raster = !plan.broken && view.raster;
            plan.rasterUnavailable = plan.raster && !options.rasterRenderer;
            if (plan.raster)
                std::tie(plan.rasterPixels, plan.rasterDpi) =
                    rasterSize(view.record.frame, options.rasterDpi);
            result.views.push_back(plan);
        }
    result.preflight.findings = findings(result, options);
    return result;
}

std::vector<SheetExportFinding> findings(const Preparation &prepared,
                                         const SheetExportOptions &options) {
    std::vector<SheetExportFinding> out;
    auto optIn = [](SheetExportFinding &finding, bool allowed) {
        finding.severity = allowed ? SheetExportSeverity::Warning : SheetExportSeverity::Blocking;
        finding.optedIn = allowed;
    };
    // Broken and failed views: one finding each, so a dialog can go to each view.
    for (const auto &plan : prepared.views) {
        if (!plan.broken)
            continue;
        const auto &view = *plan.view;
        const auto &status = view.evaluation.status;
        std::vector<std::string> phrases;
        for (const auto &reason : status.reasons)
            phrases.push_back(reasonPhrase(reason));
        if (phrases.empty())
            phrases.push_back("it could not be drawn");
        auto sentence = joined(phrases);
        const auto detail = !view.pathError.empty() ? view.pathError : status.detail;
        if (!detail.empty())
            sentence += " (" + detail + ")";
        SheetExportFinding finding;
        finding.code = "viewBroken";
        finding.sheets = {plan.page->page.sheet};
        finding.views = {view.record.id};
        finding.message = plan.page->label + ", view " + std::to_string(view.number) + " " +
                          quoted(view.record.name) + ": " + sentence + ".";
        QJsonArray reasons;
        for (const auto &reason : status.reasons)
            reasons.append(QString::fromStdString(reason));
        if (!view.pathError.empty())
            reasons.append("pathBudgetExceeded");
        finding.detail = {{"state", drawingViewStateName(status.state)},
                          {"reasons", reasons},
                          {"detail", QString::fromStdString(detail)},
                          {"exportAs", "emptyLabelledFrame"}};
        optIn(finding, options.allowBroken);
        out.push_back(std::move(finding));
    }
    // Changed since accepted: one finding, listing each view.
    {
        SheetExportFinding finding;
        finding.code = "viewChanged";
        std::vector<std::string> names;
        QJsonArray views;
        for (const auto &plan : prepared.views) {
            if (!plan.changed)
                continue;
            const auto &record = plan.view->record;
            const auto &status = plan.view->evaluation.status;
            addUnique(finding.sheets, plan.page->page.sheet);
            finding.views.push_back(record.id);
            names.push_back(viewLabel(plan));
            QJsonArray reasons;
            for (const auto &reason : status.reasons)
                reasons.append(QString::fromStdString(reason));
            views.append(QJsonObject{
                {"view", id(record.id)},
                {"sheet", id(plan.page->page.sheet)},
                {"reasons", reasons},
                {"fingerprint", QString::fromStdString(status.fingerprint)},
                {"accepted",
                 record.accepted
                     ? QJsonValue(QJsonObject{
                           {"revision", QString::number(record.accepted->revision)},
                           {"fingerprint", QString::fromStdString(record.accepted->fingerprint)}})
                     : QJsonValue(QJsonValue::Null)}});
        }
        if (!finding.views.empty()) {
            finding.message =
                plural(finding.views.size(), "view changed", "views changed") +
                " since accepted: " + joined(names) +
                (options.allowChanged ? ". They are exported as they are now and named in the "
                                        "export report."
                                      : ". Accept them, or export them as they are now.");
            finding.detail = {{"views", views}, {"exportAs", "asIs"}};
            optIn(finding, options.allowChanged);
            out.push_back(std::move(finding));
        }
    }
    // Raster views this export cannot render (no viewport): they would print empty.
    {
        SheetExportFinding finding;
        finding.code = "rasterUnavailable";
        std::vector<std::string> names;
        for (const auto &plan : prepared.views)
            if (plan.rasterUnavailable) {
                addUnique(finding.sheets, plan.page->page.sheet);
                finding.views.push_back(plan.view->record.id);
                names.push_back(viewLabel(plan));
            }
        if (!finding.views.empty()) {
            finding.message = plural(finding.views.size(), "raster view", "raster views") +
                              " cannot be rendered without the desktop viewport: " + joined(names) +
                              ". Export from the desktop, or export " +
                              (finding.views.size() == 1 ? "it" : "them") +
                              " as an empty, labelled frame.";
            finding.detail = {{"exportAs", "emptyLabelledFrame"}};
            optIn(finding, options.allowBroken);
            out.push_back(std::move(finding));
        }
    }
    // Font substitutions, merged across pages by requested family.
    struct Substitution {
        std::string used;
        std::vector<Id> sheets, views, items;
        std::vector<std::string> labels;
    };
    std::map<std::string, Substitution> substitutions;
    std::map<std::string, Substitution> outlined;
    qsizetype missingGlyphs{};
    std::vector<Id> missingGlyphSheets;
    for (const auto &page : prepared.pages) {
        // View and item IDs come from separate allocators: the layer says which one a
        // source is.
        for (const auto &substitution : page.sheet.fontSubstitutions) {
            auto &entry = substitutions[substitution.requested];
            entry.used = substitution.used;
            addUnique(entry.sheets, page.page.sheet);
            addUnique(entry.labels, page.label);
            for (const auto source : substitution.items)
                addUnique(entry.items, source);
            for (const auto source : substitution.views)
                addUnique(entry.views, source);
        }
        for (const auto &primitive : page.sheet.primitives)
            for (const auto &run : primitive.glyphs)
                if (!drawingFontEmbeddable(run.rawFont())) {
                    auto &entry = outlined[run.rawFont().familyName().toStdString()];
                    addUnique(entry.sheets, page.page.sheet);
                    addUnique(entry.labels, page.label);
                    if (primitive.source)
                        addUnique(primitive.layer == DrawingSheetLayer::Item ? entry.items
                                                                             : entry.views,
                                  primitive.source);
                }
        if (page.sheet.missingGlyphs > 0) {
            missingGlyphs += page.sheet.missingGlyphs;
            missingGlyphSheets.push_back(page.page.sheet);
        }
    }
    for (const auto &[requested, entry] : substitutions) {
        SheetExportFinding finding;
        finding.severity = SheetExportSeverity::Warning;
        finding.code = "fontSubstituted";
        finding.sheets = entry.sheets;
        finding.views = entry.views;
        finding.items = entry.items;
        std::vector<std::string> uses;
        if (!entry.items.empty())
            uses.push_back(plural(entry.items.size(), "page item", "page items"));
        if (!entry.views.empty())
            uses.push_back(plural(entry.views.size(), "view caption", "view captions"));
        finding.message = "1 font is not installed. " + quoted(requested) + ", used by " +
                          joined(uses) + " on " + joined(entry.labels) + ", will be replaced by " +
                          entry.used + ".";
        finding.detail = {{"requested", QString::fromStdString(requested)},
                          {"used", QString::fromStdString(entry.used)}};
        out.push_back(std::move(finding));
    }
    for (const auto &[family, entry] : outlined) {
        SheetExportFinding finding;
        finding.severity = SheetExportSeverity::Warning;
        finding.code = "fontOutlined";
        finding.sheets = entry.sheets;
        finding.views = entry.views;
        finding.items = entry.items;
        finding.message = "Font " + quoted(family) + " cannot be embedded in a PDF. Its text on " +
                          joined(entry.labels) +
                          " is drawn as outlines, so it cannot be selected or searched.";
        finding.detail = {{"family", QString::fromStdString(family)}};
        out.push_back(std::move(finding));
    }
    if (missingGlyphs > 0) {
        SheetExportFinding finding;
        finding.severity = SheetExportSeverity::Warning;
        finding.code = "missingGlyphs";
        finding.sheets = missingGlyphSheets;
        finding.message = plural(size_t(missingGlyphs), "character has", "characters have") +
                          " no glyph in its font and print as empty boxes.";
        finding.detail = {{"count", double(missingGlyphs)}};
        out.push_back(std::move(finding));
    }
    // Page items whose model reference is missing print in red as "[Missing reference]".
    {
        SheetExportFinding finding;
        finding.severity = SheetExportSeverity::Warning;
        finding.code = "itemBroken";
        std::vector<std::string> names;
        QJsonArray items;
        for (const auto &page : prepared.pages)
            for (const auto &item : page.sheet.items) {
                if (item.evaluation.state != DrawingItemState::Broken)
                    continue;
                addUnique(finding.sheets, page.page.sheet);
                finding.items.push_back(item.record.id);
                names.push_back(page.label + " " + itemKindName(item.record.data) + " " +
                                std::to_string(item.record.id));
                QJsonArray reasons;
                for (const auto &reason : item.evaluation.reasons)
                    reasons.append(QString::fromStdString(reason));
                items.append(QJsonObject{{"item", id(item.record.id)},
                                         {"sheet", id(page.page.sheet)},
                                         {"kind", itemKindName(item.record.data)},
                                         {"reasons", reasons}});
            }
        if (!finding.items.empty()) {
            finding.message = plural(finding.items.size(), "page item has", "page items have") +
                              " lost a model reference and print" +
                              (finding.items.size() == 1 ? "s" : "") +
                              " in red as “[Missing reference]”: " + joined(names) + ". Reattach " +
                              (finding.items.size() == 1 ? "it" : "them") + " to fix this.";
            finding.detail = {{"items", items}};
            out.push_back(std::move(finding));
        }
    }
    // Scenes that name deleted model entities: those entities are simply not drawn.
    for (const auto &plan : prepared.views) {
        const auto missing = plan.view->evaluation.status.warnings;
        if (plan.broken || missing == 0)
            continue;
        SheetExportFinding finding;
        finding.severity = SheetExportSeverity::Warning;
        finding.code = "sceneReferencesMissing";
        finding.sheets = {plan.page->page.sheet};
        finding.views = {plan.view->record.id};
        finding.message = viewLabel(plan) + ": its scene refers to " +
                          plural(missing, "model entity", "model entities") +
                          " that no longer exist" + (missing == 1 ? "s" : "") + ".";
        finding.detail = {{"missingReferences", double(missing)}};
        out.push_back(std::move(finding));
    }
    // Measured scales.
    {
        // Scales in order of first appearance; the summary lists the most used first.
        std::vector<std::pair<double, std::vector<Id>>> scales;
        size_t measured = 0, notToScale = 0;
        SheetExportFinding finding;
        finding.severity = SheetExportSeverity::Info;
        finding.code = "scaleSummary";
        for (const auto &plan : prepared.views) {
            if (plan.broken)
                continue;
            if (plan.view->evaluation.status.notToScale) {
                ++notToScale;
                continue;
            }
            ++measured;
            const auto denominator = plan.view->record.scaleDenominator;
            auto entry = std::find_if(scales.begin(), scales.end(),
                                      [&](const auto &s) { return s.first == denominator; });
            if (entry == scales.end())
                entry = scales.insert(scales.end(), {denominator, {}});
            entry->second.push_back(plan.view->record.id);
            finding.views.push_back(plan.view->record.id);
            addUnique(finding.sheets, plan.page->page.sheet);
        }
        if (measured > 0) {
            auto ordered = scales;
            std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
                return a.second.size() > b.second.size();
            });
            std::vector<std::string> parts;
            QJsonArray list;
            for (const auto &[denominator, views] : ordered) {
                const auto scale = "1:" + denominatorText(denominator);
                parts.push_back(ordered.size() == 1
                                    ? (measured == 1 ? "at " : "all at ") + scale
                                    : std::to_string(views.size()) + " at " + scale);
                list.append(QJsonObject{{"scale", QString::fromStdString(scale)},
                                        {"scaleDenominator", denominator},
                                        {"views", ids(views)}});
            }
            finding.message = plural(measured, "view", "views") +
                              " at measured scale: " + joined(parts) +
                              ". Print at 100% to measure from paper.";
            finding.detail = {{"scales", list},
                              {"measuredViews", double(measured)},
                              {"notToScaleViews", double(notToScale)}};
            out.push_back(std::move(finding));
        }
    }
    // Raster views.
    {
        SheetExportFinding finding;
        finding.severity = SheetExportSeverity::Info;
        finding.code = "rasterViews";
        std::vector<std::string> names;
        QJsonArray views;
        for (const auto &plan : prepared.views) {
            if (!plan.raster)
                continue;
            const auto perspective = plan.view->evaluation.status.notToScale;
            addUnique(finding.sheets, plan.page->page.sheet);
            finding.views.push_back(plan.view->record.id);
            names.push_back(quoted(plan.view->record.name) + " (" +
                            (perspective ? "perspective" : "rendered appearance") + ")");
            views.append(QJsonObject{{"view", id(plan.view->record.id)},
                                     {"reason", perspective ? "notToScale" : "rasterAppearance"},
                                     {"dpi", plan.rasterDpi},
                                     {"widthPx", plan.rasterPixels.width()},
                                     {"heightPx", plan.rasterPixels.height()}});
        }
        if (!finding.views.empty()) {
            finding.message = plural(finding.views.size(), "view", "views") +
                              " will be raster: " + joined(names) + " at " +
                              std::to_string(options.rasterDpi) +
                              " dpi. Everything else is vector.";
            finding.detail = {{"dpi", options.rasterDpi}, {"views", views}};
            out.push_back(std::move(finding));
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const auto &a, const auto &b) { return a.severity < b.severity; });
    return out;
}

QJsonObject viewReport(const ViewPlan &plan, ViewOutcome outcome, bool asIs) {
    const auto &record = plan.view->record;
    const auto &status = plan.view->evaluation.status;
    QJsonArray reasons;
    for (const auto &reason : status.reasons)
        reasons.append(QString::fromStdString(reason));
    QJsonObject result{
        {"view", id(record.id)},
        {"number", plan.view->number},
        {"name", QString::fromStdString(record.name)},
        {"state", drawingViewStateName(status.state)},
        {"reasons", reasons},
        {"fingerprint", QString::fromStdString(status.fingerprint)},
        {"accepted", record.accepted ? QJsonValue(QJsonObject{
                                           {"revision", QString::number(record.accepted->revision)},
                                           {"fingerprint",
                                            QString::fromStdString(record.accepted->fingerprint)}})
                                     : QJsonValue(QJsonValue::Null)},
        {"scale", status.notToScale
                      ? QString("Not to scale")
                      : "1:" + QString::fromStdString(denominatorText(record.scaleDenominator))},
        {"scaleDenominator", record.scaleDenominator},
        {"notToScale", status.notToScale},
        {"frameMm", QJsonObject{{"x", record.frame.xMm},
                                {"y", record.frame.yMm},
                                {"width", record.frame.widthMm},
                                {"height", record.frame.heightMm}}},
        {"content", outcome == ViewOutcome::Vector   ? "vector"
                    : outcome == ViewOutcome::Raster ? "raster"
                                                     : "emptyFrame"},
        {"exported", outcome == ViewOutcome::EmptyFrame ? "emptyFrame"
                     : asIs                             ? "asIs"
                                                        : "current"}};
    if (outcome == ViewOutcome::Raster) {
        result["rasterDpi"] = plan.rasterDpi;
        result["rasterPixels"] = QJsonArray{plan.rasterPixels.width(), plan.rasterPixels.height()};
    }
    return result;
}

class BoundedOutput final : public QIODevice {
  public:
    QByteArray bytes;
    bool exceeded{};
    BoundedOutput() { open(QIODevice::WriteOnly); }
    bool isSequential() const override { return true; }

  protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *data, qint64 size) override {
        if (size < 0 || size > pdfByteLimit - bytes.size()) {
            exceeded = true;
            return -1;
        }
        bytes.append(data, size);
        return size;
    }
};
} // namespace

const char *sheetExportSeverityName(SheetExportSeverity severity) {
    switch (severity) {
    case SheetExportSeverity::Blocking:
        return "blocking";
    case SheetExportSeverity::Warning:
        return "warning";
    case SheetExportSeverity::Info:
        return "info";
    }
    return "info";
}
bool SheetExportPreflight::blocked() const {
    return std::any_of(findings.begin(), findings.end(), [](const auto &finding) {
        return finding.severity == SheetExportSeverity::Blocking;
    });
}
SheetExportError::SheetExportError(std::string code, const std::string &message,
                                   std::optional<SheetExportPreflight> preflight)
    : std::runtime_error(message), code_(std::move(code)), preflight_(std::move(preflight)) {}

std::vector<Id> resolveSheetPageRange(const Document &doc, const std::string &range) {
    const auto ordered = orderedDrawingSheets(doc);
    if (ordered.empty())
        invalid("The document has no drawing sheets to export");
    auto text = QString::fromStdString(range).trimmed();
    if (text.isEmpty() || text.compare("all", Qt::CaseInsensitive) == 0)
        return ordered;
    if (text.size() > 1024)
        invalid("Page range exceeds 1024 characters");
    // The position (0-based) of one page named by sheet number or 1-based position.
    auto position = [&](const QString &name) -> size_t {
        const auto wanted = name.trimmed().toStdString();
        if (wanted.empty())
            invalid("Page range has an empty page");
        for (size_t i = 0; i < ordered.size(); ++i)
            if (doc.drawingSheets().at(ordered[i])->number == wanted)
                return i;
        bool ok{};
        const auto index = name.trimmed().toULongLong(&ok);
        if (ok && index >= 1 && index <= ordered.size() && QString::number(index) == name.trimmed())
            return size_t(index - 1);
        invalid("Page range names no sheet number or page " + quoted(wanted));
    };
    std::set<size_t> chosen;
    for (const auto &part : text.split(',')) {
        auto separator = part.indexOf("..");
        auto length = 2;
        if (separator < 0) {
            separator = part.indexOf(QChar(0x2013));
            length = 1;
        }
        if (separator < 0) {
            chosen.insert(position(part));
            continue;
        }
        const auto from = position(part.left(separator));
        const auto to = position(part.mid(separator + length));
        if (from > to)
            invalid("Page range " + quoted(part.trimmed().toStdString()) +
                    " ends before it starts");
        for (auto i = from; i <= to; ++i)
            chosen.insert(i);
    }
    std::vector<Id> result;
    for (const auto i : chosen)
        result.push_back(ordered[i]);
    return result;
}

SheetExportPreflight preflightSheetExport(const Document &doc, const SheetExportOptions &options) {
    return prepare(doc, options).preflight;
}

namespace {
void requireUnblocked(const SheetExportPreflight &preflight) {
    if (!preflight.blocked())
        return;
    size_t blocking = 0;
    std::string first;
    for (const auto &finding : preflight.findings)
        if (finding.severity == SheetExportSeverity::Blocking && blocking++ == 0)
            first = finding.message;
    throw SheetExportError(
        "EXPORT_BLOCKED",
        "Export blocked by " + plural(blocking, "finding", "findings") + ": " + first, preflight);
}
// Raster content, rendered before any page is painted. A view whose renderer fails is a
// failure unless broken views may print as empty frames.
struct RenderedRasters {
    std::map<Id, QImage> images;
    std::vector<Id> failures;
};
RenderedRasters renderRasters(const Preparation &prepared, const SheetExportOptions &options) {
    RenderedRasters result;
    for (const auto &plan : prepared.views) {
        if (!plan.raster || plan.rasterUnavailable)
            continue;
        const auto &input = plan.page->rasterInputs.at(plan.view->record.id);
        QImage image;
        std::string error;
        try {
            image = options.rasterRenderer(input, plan.rasterPixels);
            if (image.isNull() || std::abs(image.width() - plan.rasterPixels.width()) > 1 ||
                std::abs(image.height() - plan.rasterPixels.height()) > 1)
                error = "the renderer returned no image of the requested size";
        } catch (const std::exception &e) {
            error = e.what();
        }
        if (!error.empty()) {
            if (!options.allowBroken)
                throw SheetExportError("RASTER_FAILED",
                                       "Could not render " + viewLabel(plan) + ": " + error);
            result.failures.push_back(plan.view->record.id);
            continue;
        }
        result.images.emplace(plan.view->record.id, std::move(image));
    }
    return result;
}
DrawingSheetPaintOptions pagePaint(const PreparedPage &page, const RenderedRasters &rasters) {
    DrawingSheetPaintOptions paint;
    paint.text = DrawingTextMode::Text;
    for (const auto &view : page.sheet.views)
        if (const auto it = rasters.images.find(view.record.id); it != rasters.images.end())
            paint.rasters.emplace(it->first, it->second);
    return paint;
}
bool emptyFrame(const ViewPlan &plan, const RenderedRasters &rasters) {
    return plan.broken || plan.rasterUnavailable ||
           std::find(rasters.failures.begin(), rasters.failures.end(), plan.view->record.id) !=
               rasters.failures.end();
}
} // namespace

SheetExport renderSheetsPdf(const Document &doc, const SheetExportOptions &options) {
    auto prepared = prepare(doc, options);
    auto &preflight = prepared.preflight;
    requireUnblocked(preflight);
    const auto rendered = renderRasters(prepared, options);
    QJsonArray rasterReport;

    // The reconciliation report, attached to the PDF and returned.
    QJsonObject report{{"format", "pdf"},
                       {"generator", "SketchyUp " SKETCHYUP_VERSION},
                       {"documentId", QString::fromStdString(preflight.documentId)},
                       {"revision", QString::number(preflight.revision)},
                       {"pageCount", int(prepared.pages.size())},
                       {"printScale", "actual size: each page is its own paper size in mm"},
                       {"text", "real text with embedded font subsets"},
                       {"geometry", "vector"}};
    QJsonArray pages, exportedAsIs, exportedEmpty, substitutions;
    size_t viewIndex = 0;
    for (size_t index = 0; index < prepared.pages.size(); ++index) {
        const auto &page = prepared.pages[index];
        QJsonArray views, brokenItems;
        for (; viewIndex < prepared.views.size() && prepared.views[viewIndex].page == &page;
             ++viewIndex) {
            const auto &plan = prepared.views[viewIndex];
            const auto id = plan.view->record.id;
            const bool empty = emptyFrame(plan, rendered);
            const auto outcome = empty         ? ViewOutcome::EmptyFrame
                                 : plan.raster ? ViewOutcome::Raster
                                               : ViewOutcome::Vector;
            views.append(viewReport(plan, outcome, plan.changed));
            if (empty)
                exportedEmpty.append(QString::number(id));
            else if (plan.changed)
                exportedAsIs.append(QString::number(id));
            if (outcome == ViewOutcome::Raster)
                rasterReport.append(QJsonObject{{"view", QString::number(id)},
                                                {"sheet", QString::number(page.page.sheet)},
                                                {"dpi", plan.rasterDpi},
                                                {"widthPx", plan.rasterPixels.width()},
                                                {"heightPx", plan.rasterPixels.height()}});
        }
        for (const auto &item : page.sheet.items)
            if (item.evaluation.state == DrawingItemState::Broken)
                brokenItems.append(QString::number(item.record.id));
        pages.append(QJsonObject{
            {"page", int(index + 1)},
            {"sheet", QString::number(page.page.sheet)},
            {"number", QString::fromStdString(page.page.number)},
            {"name", QString::fromStdString(page.page.name)},
            {"paperMm", QJsonObject{{"width", page.page.widthMm}, {"height", page.page.heightMm}}},
            {"views", views},
            {"items", int(page.sheet.items.size())},
            {"brokenItems", brokenItems}});
    }
    QJsonArray outlinedFonts;
    for (const auto &finding : preflight.findings) {
        if (finding.code == "fontSubstituted")
            substitutions.append(QJsonObject{{"requested", finding.detail["requested"]},
                                             {"used", finding.detail["used"]},
                                             {"sheets", ids(finding.sheets)},
                                             {"views", ids(finding.views)},
                                             {"items", ids(finding.items)}});
        if (finding.code == "fontOutlined")
            outlinedFonts.append(finding.detail["family"]);
    }
    qsizetype missingGlyphs = 0;
    for (const auto &page : prepared.pages)
        missingGlyphs += page.sheet.missingGlyphs;
    report["pages"] = pages;
    report["fontSubstitutions"] = substitutions;
    report["outlinedFonts"] = outlinedFonts;
    report["missingFontGlyphs"] = double(missingGlyphs);
    report["rasters"] = rasterReport;
    report["exportedAsIs"] = exportedAsIs;
    report["exportedEmpty"] = exportedEmpty;
    report["options"] = QJsonObject{{"sheets", ids([&] {
                                         std::vector<Id> values;
                                         for (const auto &page : prepared.pages)
                                             values.push_back(page.page.sheet);
                                         return values;
                                     }())},
                                    {"allowBroken", options.allowBroken},
                                    {"allowChanged", options.allowChanged},
                                    {"rasterDpi", options.rasterDpi},
                                    {"rasterCapture", bool(options.rasterRenderer)}};
    QJsonArray findingList;
    for (const auto &finding : preflight.findings)
        findingList.append(sheetExportFindingJson(finding));
    report["findings"] = findingList;

    BoundedOutput output;
    {
        QPdfWriter pdf(&output);
        pdf.setResolution(2540); // 100 device units per paper millimetre.
        pdf.setCreator("SketchyUp");
        pdf.setTitle("SketchyUp drawing sheets");
        pdf.addFileAttachment("sheet-export-report.json", QJsonDocument(report).toJson(),
                              "application/json");
        auto layout = [](const SheetExportPage &page) {
            const QPageSize size(QSizeF(page.widthMm, page.heightMm), QPageSize::Millimeter,
                                 QString::fromStdString(page.number), QPageSize::ExactMatch);
            return QPageLayout(size, QPageLayout::Portrait, QMarginsF{}, QPageLayout::Millimeter);
        };
        require(pdf.setPageLayout(layout(prepared.pages.front().page)), "PDF page layout rejected");
        QPainter painter;
        require(painter.begin(&pdf), "PDF painter could not start");
        for (size_t index = 0; index < prepared.pages.size(); ++index) {
            const auto &page = prepared.pages[index];
            if (index > 0) {
                require(pdf.setPageLayout(layout(page.page)), "PDF page layout rejected");
                require(pdf.newPage(), "PDF page could not start");
            }
            const auto painted = paintDrawingSheet(painter, page.sheet, pagePaint(page, rendered));
            // Placeholders appear only where the plan already said "empty frame".
            require(painted.rasterPlaceholders.empty() || options.allowBroken,
                    "A raster view would print as a placeholder");
        }
        require(painter.end(), "PDF painter could not finish");
    }
    require(!output.exceeded && !output.bytes.isEmpty(), "Sheet PDF exceeds 512 MiB or is empty");
    SheetExport result;
    result.bytes = std::move(output.bytes);
    report["sha256"] = QString::fromLatin1(
        QCryptographicHash::hash(result.bytes, QCryptographicHash::Sha256).toHex());
    report["bytes"] = double(result.bytes.size());
    result.report = std::move(report);
    result.preflight = std::move(preflight);
    return result;
}

SheetExport exportSheetsPdf(const Document &doc, const SheetExportOptions &options,
                            const QString &path) {
    // Fail before the work when the destination can never be published.
    const QFileInfo target(path);
    if (path.isEmpty() || target.exists() || target.isSymLink())
        throw SheetExportError("INVALID_PATH",
                               "A new output path is required; existing files are never replaced");
    if (target.dir().canonicalPath().isEmpty())
        throw SheetExportError("INVALID_PATH", "Output parent must already exist");
    auto result = renderSheetsPdf(doc, options);
    publishNewFile(path, result.bytes);
    return result;
}

SheetPrint printSheets(const Document &doc, const SheetExportOptions &options,
                       QPagedPaintDevice &device) {
    auto prepared = prepare(doc, options);
    requireUnblocked(prepared.preflight);
    const auto rendered = renderRasters(prepared, options);
    SheetPrint result;
    for (const auto &plan : prepared.views) {
        const auto id = plan.view->record.id;
        if (emptyFrame(plan, rendered))
            result.exportedEmpty.push_back(id);
        else if (plan.changed)
            result.exportedAsIs.push_back(id);
        if (rendered.images.contains(id))
            result.rasterViews.push_back(id);
    }
    QPainter painter;
    require(painter.begin(&device), "The printer could not start");
    for (size_t index = 0; index < prepared.pages.size(); ++index) {
        if (index > 0)
            require(device.newPage(), "The printer could not start a new page");
        // drawingSheetDeviceTransform: one paper millimetre is one device millimetre.
        const auto painted = paintDrawingSheet(painter, prepared.pages[index].sheet,
                                               pagePaint(prepared.pages[index], rendered));
        require(painted.rasterPlaceholders.empty() || options.allowBroken,
                "A raster view would print as a placeholder");
    }
    require(painter.end(), "The printer could not finish");
    result.pages = prepared.pages.size();
    result.preflight = std::move(prepared.preflight);
    return result;
}

QJsonObject sheetExportFindingJson(const SheetExportFinding &finding) {
    return {{"severity", sheetExportSeverityName(finding.severity)},
            {"code", QString::fromStdString(finding.code)},
            {"sheets", ids(finding.sheets)},
            {"views", ids(finding.views)},
            {"items", ids(finding.items)},
            {"message", QString::fromStdString(finding.message)},
            {"detail", finding.detail},
            {"optedIn", finding.optedIn}};
}
QJsonObject sheetExportPreflightJson(const SheetExportPreflight &preflight) {
    QJsonArray pages, findings;
    for (size_t i = 0; i < preflight.pages.size(); ++i) {
        const auto &page = preflight.pages[i];
        pages.append(QJsonObject{
            {"page", int(i + 1)},
            {"sheet", QString::number(page.sheet)},
            {"number", QString::fromStdString(page.number)},
            {"name", QString::fromStdString(page.name)},
            {"paperMm", QJsonObject{{"width", page.widthMm}, {"height", page.heightMm}}}});
    }
    size_t blocking = 0, warnings = 0;
    for (const auto &finding : preflight.findings) {
        findings.append(sheetExportFindingJson(finding));
        blocking += finding.severity == SheetExportSeverity::Blocking;
        warnings += finding.severity == SheetExportSeverity::Warning;
    }
    return {{"documentId", QString::fromStdString(preflight.documentId)},
            {"revision", QString::number(preflight.revision)},
            {"blocked", preflight.blocked()},
            {"blockingCount", int(blocking)},
            {"warningCount", int(warnings)},
            {"pageCount", int(preflight.pages.size())},
            {"pages", pages},
            {"findings", findings}};
}
} // namespace sketchy
