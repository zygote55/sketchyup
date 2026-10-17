// ADR 0166 §2 and §3.4 (R094.d): the Preview ghost and the stretch plane.
#include "app/viewport.hpp"
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <set>
namespace sketchy {
namespace {
double coordinate(Vec3 point, int axis) {
    return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
}
void setCoordinate(Vec3 &point, int axis, double value) {
    (axis == 0 ? point.x : axis == 1 ? point.y : point.z) = value;
}
double segmentDistance(QPointF p, QPointF a, QPointF b) {
    const auto ab = b - a;
    const double length = QPointF::dotProduct(ab, ab);
    const double t =
        length > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / length, 0.0, 1.0) : 0.0;
    const auto nearest = a + ab * t;
    return std::hypot(p.x() - nearest.x(), p.y() - nearest.y());
}
// Snap radius in logical pixels (ADR 0166 §3.4).
constexpr double snapRadius = 8;
} // namespace

bool Viewport::stretchFrame(Transform &world, Vec3 &low, Vec3 &high) const {
    if (!stretchPlane_ || !doc_.bodies().contains(stretchPlane_->body))
        return false;
    const auto &body = *doc_.bodies().at(stretchPlane_->body);
    if (body.surface.vertices.empty())
        return false;
    low = {INFINITY, INFINITY, INFINITY};
    high = {-INFINITY, -INFINITY, -INFINITY};
    for (const auto &[id, point] : body.surface.vertices) {
        low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
        high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
    }
    world = doc_.worldTransform(stretchPlane_->body);
    return true;
}
void Viewport::setStretchPlane(std::optional<StretchPlane> plane) {
    if (!plane)
        stretchDragging_ = false;
    stretchPlane_ = std::move(plane);
    update();
}
std::vector<double> Viewport::stretchSnaps() const {
    std::vector<double> result;
    if (!stretchPlane_ || !doc_.bodies().contains(stretchPlane_->body))
        return result;
    std::set<double> values;
    for (const auto &[id, point] : doc_.bodies().at(stretchPlane_->body)->surface.vertices)
        values.insert(coordinate(point, stretchPlane_->axis));
    // The distinct coordinates and the midpoints between neighbours: the places where the set
    // of moving points changes.
    std::optional<double> previous;
    for (const auto value : values) {
        if (previous)
            result.push_back((*previous + value) / 2);
        result.push_back(value);
        previous = value;
    }
    return result;
}
std::pair<std::size_t, std::size_t> Viewport::stretchMoving() const {
    if (!stretchPlane_ || !doc_.bodies().contains(stretchPlane_->body))
        return {};
    std::set<std::tuple<double, double, double>> all, moving;
    for (const auto &[id, point] : doc_.bodies().at(stretchPlane_->body)->surface.vertices) {
        all.insert({point.x, point.y, point.z});
        // The core moves the points past the plane (ADR 0165 §1).
        if (coordinate(point, stretchPlane_->axis) > stretchPlane_->at)
            moving.insert({point.x, point.y, point.z});
    }
    return {moving.size(), all.size()};
}
QPointF Viewport::stretchPlanePoint(double at) const {
    Transform world;
    Vec3 low, high;
    if (!stretchFrame(world, low, high))
        return {};
    Vec3 center{(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
    setCoordinate(center, stretchPlane_->axis, at);
    return project(world.point(center));
}
QPolygonF Viewport::stretchPlaneOutline() const {
    Transform world;
    Vec3 low, high;
    if (!stretchFrame(world, low, high))
        return {};
    const int axis = stretchPlane_->axis;
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    // A margin around the member's bounds keeps the quad visible past thin members.
    const double margin =
        .08 * std::max({high.x - low.x, high.y - low.y, high.z - low.z, 1e-3});
    QPolygonF outline;
    for (const auto [a, b] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{1, 1}, std::pair{0, 1}}) {
        Vec3 corner{};
        setCoordinate(corner, axis, stretchPlane_->at);
        setCoordinate(corner, u, a ? coordinate(high, u) + margin : coordinate(low, u) - margin);
        setCoordinate(corner, v, b ? coordinate(high, v) + margin : coordinate(low, v) - margin);
        outline << project(world.point(corner));
    }
    return outline;
}
double Viewport::stretchAtPointer(QPointF point, bool snap) const {
    Transform world;
    Vec3 low, high;
    if (!stretchFrame(world, low, high))
        return 0;
    const int axis = stretchPlane_->axis;
    Vec3 base{(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
    setCoordinate(base, axis, 0);
    Vec3 unit{};
    setCoordinate(unit, axis, 1);
    const auto origin = world.point(base);
    const auto direction = world.vector(unit);
    const auto [eye, ray] = this->ray(point);
    // The closest point between the pointer's ray and the axis line, in member-local units.
    const auto w0 = origin - eye;
    const double a = dot(direction, direction), b = dot(direction, ray), c = dot(ray, ray);
    const double d = dot(direction, w0), e = dot(ray, w0);
    const double denominator = a * c - b * b;
    double at = std::abs(denominator) < 1e-12 ? stretchPlane_->at : (b * e - c * d) / denominator;
    if (!std::isfinite(at))
        at = stretchPlane_->at;
    if (snap) {
        const auto pointer = stretchPlanePoint(at);
        double best = snapRadius;
        for (const auto candidate : stretchSnaps()) {
            const auto shown = stretchPlanePoint(candidate);
            const double distance = std::hypot(shown.x() - pointer.x(), shown.y() - pointer.y());
            if (distance <= best) {
                best = distance;
                at = candidate;
            }
        }
    }
    return at;
}
bool Viewport::stretchPress(QMouseEvent *event) {
    if (!stretchPlane_ || event->button() != Qt::LeftButton || tool_ != Tool::Select)
        return false;
    const auto outline = stretchPlaneOutline();
    if (outline.size() != 4)
        return false;
    const auto point = event->position();
    bool hit = outline.containsPoint(point, Qt::OddEvenFill);
    for (int i = 0; i < 4 && !hit; ++i)
        hit = segmentDistance(point, outline[i], outline[(i + 1) % 4]) <= snapRadius;
    if (!hit)
        return false;
    stretchDragging_ = true;
    event->accept();
    return true;
}
bool Viewport::stretchMove(QMouseEvent *event) {
    if (!stretchDragging_ || !stretchPlane_)
        return false;
    // Alt drags without snapping (ADR 0166 §3.4).
    stretchPlane_->at =
        stretchAtPointer(event->position(), !event->modifiers().testFlag(Qt::AltModifier));
    update();
    emit stretchPlaneMoved(stretchPlane_->at, false);
    event->accept();
    return true;
}
bool Viewport::stretchRelease(QMouseEvent *event) {
    if (!stretchDragging_ || event->button() != Qt::LeftButton)
        return false;
    stretchDragging_ = false;
    if (stretchPlane_)
        emit stretchPlaneMoved(stretchPlane_->at, true);
    event->accept();
    return true;
}
QJsonObject Viewport::previewParameters(Id placement, const QJsonObject &values) {
    clearParameterGhost();
    const QJsonObject request{
        {"apiVersion", 1},
        {"documentId", QString::fromStdString(doc_.identity())},
        {"expectedRevision", QString::number(doc_.revision())},
        {"commands", QJsonArray{QJsonObject{{"command", "component_parameter.override"},
                                            {"body", QString::number(placement)},
                                            {"values", values}}}}};
    QJsonObject result;
    try {
        result = previewBatch(doc_, request);
    } catch (const InspectionError &error) {
        return {{"code", QString::fromStdString(error.code())},
                {"message", QString::fromUtf8(error.what())},
                {"details", error.details()}};
    } catch (const std::exception &error) {
        // Values equal to the stored ones change nothing: no ghost, no error.
        if (QString::fromUtf8(error.what()) == "Batch has no committed changes")
            return {};
        return {{"message", QString::fromUtf8(error.what())}};
    }
    const auto geometry = result["geometry"].toObject();
    for (auto it = geometry.begin(); it != geometry.end(); ++it) {
        const auto body = it.value().toObject();
        const auto values = body["worldTransform"].toArray();
        if (values.size() != 16)
            continue;
        Transform world;
        for (int i = 0; i < 16; ++i)
            world.m[i] = values[i].toDouble();
        std::map<QString, Vec3> vertices;
        for (const auto &value : body["vertices"].toArray()) {
            const auto vertex = value.toObject();
            const auto p = vertex["point"].toArray();
            vertices[vertex["id"].toString()] =
                world.point({p[0].toDouble(), p[1].toDouble(), p[2].toDouble()});
        }
        std::map<QString, std::array<QString, 2>> edges;
        for (const auto &value : body["edges"].toArray()) {
            const auto edge = value.toObject();
            const auto ends = edge["vertices"].toArray();
            edges[edge["id"].toString()] = {ends[0].toString(), ends[1].toString()};
            if (vertices.contains(ends[0].toString()) && vertices.contains(ends[1].toString()))
                ghostEdges_.push_back(
                    {vertices.at(ends[0].toString()), vertices.at(ends[1].toString())});
        }
        for (const auto &value : body["faces"].toArray()) {
            const auto loops = value.toObject()["loops"].toArray();
            if (loops.isEmpty())
                continue;
            std::vector<Vec3> polygon;
            for (const auto &entry : loops[0].toArray()) {
                const auto oriented = entry.toObject();
                const auto found = edges.find(oriented["edge"].toString());
                if (found == edges.end())
                    continue;
                const auto &start = found->second[oriented["reversed"].toBool() ? 1 : 0];
                if (vertices.contains(start))
                    polygon.push_back(vertices.at(start));
            }
            if (polygon.size() >= 3)
                ghostFaces_.push_back(std::move(polygon));
        }
    }
    ghostDocument_ = doc_.identity();
    ghostRevision_ = doc_.revision();
    update();
    return {};
}
void Viewport::clearParameterGhost() {
    ghostEdges_.clear();
    ghostFaces_.clear();
    ghostDocument_.clear();
    update();
}
bool Viewport::parameterGhostShown() const {
    return !ghostEdges_.empty() && ghostDocument_ == doc_.identity() &&
           ghostRevision_ == doc_.revision();
}
void Viewport::paintParameterOverlay(QPainter &painter) {
    if (parameterGhostShown()) {
        // The preview overlay's dashed edges and hatch (UX_DESIGN §10), over the geometry.
        auto hatch = colors_.selectionStroke;
        hatch.setAlphaF(.55);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(hatch, Qt::BDiagPattern));
        for (const auto &face : ghostFaces_) {
            QPolygonF polygon;
            for (const auto &point : face)
                polygon << project(point);
            painter.drawPolygon(polygon);
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(colors_.selectionStroke, 1.6, Qt::DashLine));
        for (const auto &edge : ghostEdges_)
            painter.drawLine(project(edge[0]), project(edge[1]));
    }
    if (!stretchPlane_)
        return;
    const auto outline = stretchPlaneOutline();
    if (outline.size() != 4)
        return;
    auto fill = colors_.selectionStroke;
    fill.setAlphaF(.08);
    painter.setBrush(fill);
    painter.setPen(QPen(colors_.selectionStroke, 1.4, Qt::DashLine));
    painter.drawPolygon(outline);
    // The points past the plane move with the stretch: dots in the selection colour with an
    // arrow along the axis, and the driver form states their count.
    Transform world;
    Vec3 low, high;
    if (stretchFrame(world, low, high)) {
        const int axis = stretchPlane_->axis;
        Vec3 unit{};
        setCoordinate(unit, axis, std::max({high.x - low.x, high.y - low.y, high.z - low.z}) * .12);
        painter.setPen(QPen(colors_.selectionStroke, 1.6));
        painter.setBrush(colors_.selectionStroke);
        std::set<std::tuple<double, double, double>> drawn;
        for (const auto &[id, point] : doc_.bodies().at(stretchPlane_->body)->surface.vertices) {
            if (coordinate(point, axis) <= stretchPlane_->at ||
                !drawn.insert({point.x, point.y, point.z}).second)
                continue;
            const auto from = project(world.point(point));
            const auto to = project(world.point({point.x + unit.x, point.y + unit.y,
                                                  point.z + unit.z}));
            painter.drawEllipse(from, 3, 3);
            const auto delta = to - from;
            const double size = std::hypot(delta.x(), delta.y());
            if (size < 6)
                continue;
            const auto along = delta / size;
            const QPointF side(-along.y(), along.x());
            painter.drawLine(from, to);
            painter.drawPolygon(QPolygonF{to, to - along * 6 + side * 3, to - along * 6 - side * 3});
        }
    }
    if (!stretchPlane_->label.isEmpty()) {
        painter.setPen(colors_.selectionStroke);
        const auto top = *std::min_element(outline.begin(), outline.end(),
                                           [](QPointF a, QPointF b) { return a.y() < b.y(); });
        painter.drawText(top + QPointF(6, -6), stretchPlane_->label);
    }
}
} // namespace sketchy
