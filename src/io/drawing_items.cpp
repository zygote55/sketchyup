#include "io/drawing_items.hpp"
#include "core/drawing_sheets.hpp"
#include "core/model.hpp"
#include "presentation/unit_display.hpp"
#include <QString>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
namespace sketchy {
namespace {
using Point = DrawingPointMm;
Point point(Vec3 sheet) { return {sheet.x, sheet.y}; }
Point add(Point a, Point b) { return {a[0] + b[0], a[1] + b[1]}; }
Point sub(Point a, Point b) { return {a[0] - b[0], a[1] - b[1]}; }
Point scale(Point a, double s) { return {a[0] * s, a[1] * s}; }
double dot(Point a, Point b) { return a[0] * b[0] + a[1] * b[1]; }
double distance(Point a, Point b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }
std::string formatDenominator(double denominator) {
    if (denominator == std::round(denominator) && std::abs(denominator) < 1e15)
        return std::to_string(static_cast<long long>(denominator));
    return QString::number(denominator, 'g', 12).toStdString();
}
void addReason(std::vector<std::string> &reasons, const std::string &reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end())
        reasons.push_back(reason);
}
// Resolves the anchors and projects them when the view projects.
void resolveAnchors(const Document &doc, Id view, const std::vector<AnnotationAnchor> &anchors,
                    DrawingItemEvaluation &result) {
    const auto mapping = drawingViewMapping(doc, view);
    if (!mapping.projection)
        addReason(result.reasons, mapping.reason);
    for (const auto &anchor : anchors) {
        const auto resolved = resolveAnnotationAnchor(doc, anchor);
        DrawingItemAnchorEvaluation entry{resolved.state, resolved.point, std::nullopt};
        if (resolved.state != AnchorState::Resolved)
            addReason(result.reasons, "referenceMissing");
        else if (mapping.projection)
            entry.paper = point(mapping.projection->worldToSheet(resolved.point));
        result.anchors.push_back(entry);
    }
    if (!result.reasons.empty())
        result.state = DrawingItemState::Broken;
}
void placeDimension(const Document &doc, const DrawingDimensionItem &item,
                    DrawingItemEvaluation &result) {
    resolveAnchors(doc, item.view, {item.anchors[0], item.anchors[1]}, result);
    if (result.state == DrawingItemState::Broken) {
        result.text = "[Missing reference]" + (item.prefix.empty() ? "" : " " + item.prefix);
        return;
    }
    const auto p0 = *result.anchors[0].paper, p1 = *result.anchors[1].paper;
    const auto offset = item.offsetMm;
    Point a, b;
    double length = 0;
    switch (item.orientation) {
    case DrawingDimensionOrientation::Aligned: {
        // The normal is the measured direction turned toward the top of the paper
        // for a left-to-right pair: n = (d.y, -d.x) with paper y down.
        length = distance(p0, p1);
        const Point direction = length > 0 ? scale(sub(p1, p0), 1 / length) : Point{1, 0};
        const Point normal{direction[1], -direction[0]};
        a = add(p0, scale(normal, offset));
        b = add(p1, scale(normal, offset));
        break;
    }
    case DrawingDimensionOrientation::Horizontal: {
        // A positive offset places the line above both points, a negative one below.
        length = std::abs(p1[0] - p0[0]);
        const auto y = offset >= 0 ? std::min(p0[1], p1[1]) - offset
                                   : std::max(p0[1], p1[1]) - offset;
        a = {p0[0], y};
        b = {p1[0], y};
        break;
    }
    case DrawingDimensionOrientation::Vertical: {
        // A positive offset places the line right of both points, a negative one left.
        length = std::abs(p1[1] - p0[1]);
        const auto x = offset >= 0 ? std::max(p0[0], p1[0]) + offset
                                   : std::min(p0[0], p1[0]) + offset;
        a = {x, p0[1]};
        b = {x, p1[1]};
        break;
    }
    }
    const auto &view = *doc.drawingViews().at(item.view);
    result.measuredMm = std::array<Point, 2>{p0, p1};
    result.lineMm = std::array<Point, 2>{a, b};
    result.textMm = scale(add(a, b), .5);
    result.paperLengthMm = length;
    // The view-plane distance, which is what an orthographic drawing measures.
    result.valueMetres = length * view.scaleDenominator / 1000;
    const auto value =
        displayLength(*result.valueMetres, doc.displayUnits(), doc.displayPrecision())
            .toStdString();
    result.text = item.prefix.empty() ? value : item.prefix + " " + value;
}
struct Evaluator {
    const Document &doc;
    const DrawingItemRecord &record;
    DrawingItemEvaluation &result;
    void operator()(const DrawingTextItem &item) const { result.text = item.text; }
    void operator()(const DrawingLabelItem &item) const {
        result.text = item.text;
        if (item.target.kind == DrawingLabelTargetKind::Paper) {
            result.tipMm = item.target.pointMm;
            return;
        }
        // The text stays put; only the leader tip follows the model or breaks.
        resolveAnchors(doc, item.target.view, {item.target.anchor}, result);
        if (result.state == DrawingItemState::Current)
            result.tipMm = result.anchors[0].paper;
    }
    void operator()(const DrawingDimensionItem &item) const { placeDimension(doc, item, result); }
    void operator()(const DrawingLineItem &) const {}
    void operator()(const DrawingRectangleItem &) const {}
    void operator()(const DrawingTitleBlockItem &) const {
        result.titleBlock = drawingTitleBlockFields(doc, record.sheet);
    }
};
// The nearest point to q on segment ab, as a fraction of ab.
double fractionOn(Point a, Point b, Point q) {
    const auto v = sub(b, a);
    const auto squared = dot(v, v);
    return squared > 0 ? std::clamp(dot(sub(q, a), v) / squared, 0., 1.) : 0.;
}
} // namespace
const char *drawingItemStateName(DrawingItemState state) {
    switch (state) {
    case DrawingItemState::Current:
        return "current";
    case DrawingItemState::Broken:
        return "broken";
    }
    return "?";
}
DrawingViewMapping drawingViewMapping(const Document &doc, Id id) {
    const auto view = doc.drawingViews().find(id);
    if (view == doc.drawingViews().end())
        throw std::runtime_error("Drawing view does not exist");
    const auto scene = doc.scenes().find(view->second->source.scene);
    if (scene == doc.scenes().end())
        return {std::nullopt, "sceneMissing"};
    if (!scene->second->snapshot.camera)
        return {std::nullopt, "sceneHasNoCamera"};
    const auto camera = renderSceneCamera(*scene->second->snapshot.camera);
    if (!camera.orthographic)
        return {std::nullopt, "notToScale"};
    return {drawingViewProjection(*view->second, camera), {}};
}
std::string drawingPaperName(const DrawingPaper &paper) {
    static const std::vector<std::pair<const char *, std::array<double, 2>>> sizes{
        {"A0", {841, 1189}}, {"A1", {594, 841}},       {"A2", {420, 594}},
        {"A3", {297, 420}},  {"A4", {210, 297}},       {"A5", {148, 210}},
        {"Letter", {215.9, 279.4}}, {"Legal", {215.9, 355.6}}, {"Tabloid", {279.4, 431.8}}};
    const auto shorter = std::min(paper.widthMm, paper.heightMm),
               longer = std::max(paper.widthMm, paper.heightMm);
    for (const auto &[name, size] : sizes)
        if (std::abs(shorter - size[0]) <= .5 && std::abs(longer - size[1]) <= .5)
            return name;
    return {};
}
DrawingTitleBlockDerived drawingTitleBlockFields(const Document &doc, Id id) {
    const auto sheet = doc.drawingSheets().find(id);
    if (sheet == doc.drawingSheets().end())
        throw std::runtime_error("Drawing sheet does not exist");
    DrawingTitleBlockDerived result;
    result.sheetNumber = sheet->second->number;
    result.page = std::to_string(sheet->second->position + 1) + " of " +
                  std::to_string(doc.drawingSheets().size());
    std::set<double> scales;
    bool perspective = false;
    for (const auto view : drawingViewsOnSheet(doc, id)) {
        const auto mapping = drawingViewMapping(doc, view);
        if (mapping.projection)
            scales.insert(doc.drawingViews().at(view)->scaleDenominator);
        else if (mapping.reason == "notToScale")
            perspective = true;
    }
    if (scales.size() > 1)
        result.scale = "As indicated";
    else if (scales.size() == 1) {
        result.scale = "1:" + formatDenominator(*scales.begin());
        if (const auto name = drawingPaperName(sheet->second->paper); !name.empty())
            result.scale += " @ " + name;
    } else
        result.scale = perspective ? "Not to scale" : "—";
    return result;
}
DrawingItemEvaluation evaluateDrawingItem(const Document &doc, Id id) {
    const auto found = doc.drawingItems().find(id);
    if (found == doc.drawingItems().end())
        throw std::runtime_error("Drawing item does not exist");
    DrawingItemEvaluation result;
    result.item = id;
    result.revision = doc.revision();
    std::visit(Evaluator{doc, *found->second, result}, found->second->data);
    return result;
}
DrawingReattachProposal drawingReattachCandidates(const Document &doc, Id id) {
    const auto found = doc.drawingItems().find(id);
    if (found == doc.drawingItems().end())
        throw std::runtime_error("Drawing item does not exist");
    const auto &record = *found->second;
    const auto view = drawingItemView(record);
    std::vector<std::pair<AnnotationAnchor, Point>> stored;
    if (const auto *dimension = std::get_if<DrawingDimensionItem>(&record.data))
        for (size_t i = 0; i < 2; ++i)
            stored.emplace_back(dimension->anchors[i], dimension->lastMm[i]);
    else if (const auto *label = std::get_if<DrawingLabelItem>(&record.data);
             label && label->target.kind == DrawingLabelTargetKind::View)
        stored.emplace_back(label->target.anchor, label->target.lastMm);
    else
        throw std::runtime_error("Only dimensions and view-target labels can be reattached");
    DrawingReattachProposal result;
    result.evaluation = evaluateDrawingItem(doc, id);
    for (size_t i = 0; i < stored.size(); ++i)
        result.anchors.push_back(
            {i, stored[i].second, result.evaluation.anchors.at(i).state, {}, {}, {}});
    const bool missing = std::any_of(result.anchors.begin(), result.anchors.end(),
                                     [](const auto &a) { return a.state != AnchorState::Resolved; });
    if (!missing)
        return result;
    const auto evaluation = evaluateDrawingViewDrawing(doc, view);
    if (evaluation.status.state == DrawingViewState::Broken ||
        evaluation.status.state == DrawingViewState::Failed) {
        result.unavailable = evaluation.status.reasons;
        return result;
    }
    if (evaluation.status.notToScale || !evaluation.projection || !evaluation.drawing) {
        result.unavailable = {"notToScale"};
        return result;
    }
    const auto &projection = *evaluation.projection;
    const auto &drawing = *evaluation.drawing;
    const Point origin{projection.frame.xMm, projection.frame.yMm};
    for (auto &candidate : result.anchors) {
        if (candidate.state == AnchorState::Resolved)
            continue;
        const auto q = candidate.lastMm;
        double bestEdge = std::numeric_limits<double>::infinity(),
               bestVertex = std::numeric_limits<double>::infinity();
        std::optional<std::pair<AnnotationAnchor, Point>> edge, vertex;
        for (const auto &line : drawing.geometry.lines) {
            if (line.source >= drawing.sources.size())
                continue;
            const auto &source = drawing.sources[line.source];
            // Section-cut lines and lines not drawn from a model edge are refused.
            if (source.section || !source.body || !source.edge)
                continue;
            const auto body = doc.bodies().find(source.body);
            if (body == doc.bodies().end())
                continue;
            const auto topology = body->second->topology.edges.find(source.edge);
            if (topology == body->second->topology.edges.end())
                continue;
            const auto &vertices = body->second->surface.vertices;
            const auto transform = doc.worldTransform(source.body);
            const Point a = point(projection.worldToSheet(
                            transform.point(vertices.at(topology->second.a)))),
                        b = point(projection.worldToSheet(
                            transform.point(vertices.at(topology->second.b))));
            const Point la = add(point(line.a), origin), lb = add(point(line.b), origin);
            // A drawn endpoint that matches a projected edge endpoint is a vertex.
            for (const auto &end : {la, lb})
                for (const auto &[corner, model] :
                     {std::pair{a, topology->second.a}, std::pair{b, topology->second.b}})
                    if (distance(end, corner) <= .001 && distance(q, corner) < bestVertex) {
                        bestVertex = distance(q, corner);
                        vertex = {vertexAnchor(doc, source.body, model), corner};
                    }
            // Any other point on the drawn line is an edge anchor at the projected
            // parameter; an edge seen end-on has only its vertices.
            if (distance(a, b) <= .001)
                continue;
            const auto on = add(la, scale(sub(lb, la), fractionOn(la, lb, q)));
            if (distance(q, on) < bestEdge) {
                bestEdge = distance(q, on);
                edge = {edgeAnchor(doc, source.body, source.edge, fractionOn(a, b, on)), on};
            }
        }
        const bool preferVertex =
            vertex && (bestVertex <= drawingReattachVertexSnapMm || bestVertex <= bestEdge);
        const auto &chosen = preferVertex ? vertex : edge;
        if (!chosen)
            continue;
        candidate.anchor = chosen->first;
        candidate.paperMm = chosen->second;
        candidate.distanceMm = distance(q, chosen->second);
    }
    return result;
}
} // namespace sketchy
