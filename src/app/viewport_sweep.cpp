#include "app/viewport.hpp"
#include <limits>
namespace sketchy {
void Viewport::beginSweep() {
    syncSelection();
    Id source{}, face{}, pathBody{};
    std::set<Id> edges;
    for (auto entity : selection_.entities()) {
        if (!selectable(entity))
            throw std::runtime_error("Follow Me requires editable profile and path geometry");
        if (entity.kind == SelectionKind::Face && !face) {
            source = entity.body;
            face = entity.entity;
        } else if (entity.kind == SelectionKind::Edge && (!pathBody || pathBody == entity.body)) {
            pathBody = entity.body;
            edges.insert(entity.entity);
        } else {
            throw std::runtime_error("Select one profile face and one connected edge path in a "
                                     "single path body, then press Shift+F");
        }
    }
    if (!face || edges.empty() || edges.size() > 128)
        throw std::runtime_error("Select one profile face and 1–128 connected path edges, then "
                                 "press Shift+F · Ctrl adds to selection");
    const auto &path = *doc_.bodies().at(pathBody);
    std::map<Id, std::vector<std::pair<Id, Id>>> graph;
    for (auto id : edges) {
        const auto edge = path.topology.edges.at(id);
        graph[edge.a].push_back({edge.b, id});
        graph[edge.b].push_back({edge.a, id});
    }
    std::vector<Id> ends;
    for (const auto &[vertex, neighbors] : graph) {
        if (neighbors.size() > 2)
            throw std::runtime_error("Follow Me path branches; select a single chain or loop");
        if (neighbors.size() == 1)
            ends.push_back(vertex);
    }
    const bool closed = ends.empty();
    if (!closed && ends.size() != 2)
        throw std::runtime_error("Follow Me path must be one connected chain or closed loop");
    const auto profileWorld = doc_.worldTransform(source);
    const auto &profile = doc_.bodies().at(source)->surface;
    const auto &outer = profile.faces.at(face).loops[0];
    std::vector<Vec3> points;
    Vec3 center{}, normal{};
    for (auto vertex : outer) {
        points.push_back(profileWorld.point(profile.vertices.at(vertex)));
        center = center + points.back();
    }
    center = center * (1. / points.size());
    for (size_t i = 0; i < points.size(); ++i)
        normal = normal + cross(points[i] - points[0], points[(i + 1) % points.size()] - points[0]);
    const auto magnitude = length(normal);
    if (magnitude <= tolerance * tolerance)
        throw std::runtime_error("Follow Me profile is too small in world space");
    normal = normal * (1 / magnitude);
    const auto world = doc_.worldTransform(pathBody);
    Id first{};
    double nearest = std::numeric_limits<double>::infinity();
    // Open paths begin at an endpoint in the profile plane. Closed paths begin
    // at the eligible station nearest its center, with stable ID tie breaking.
    for (const auto &[vertex, neighbors] : graph) {
        if (!closed && neighbors.size() != 1)
            continue;
        const auto p = world.point(path.surface.vertices.at(vertex));
        const auto distance = length(p - center);
        if (std::abs(dot(p - points[0], normal)) <= tolerance && distance < nearest) {
            first = vertex;
            nearest = distance;
        }
    }
    if (!first)
        throw std::runtime_error("The path must start in the profile plane; move its endpoint "
                                 "onto that plane (or a station for a closed path)");
    QJsonArray stations;
    std::set<Id> used;
    auto current = first;
    for (;;) {
        const auto p = world.point(path.surface.vertices.at(current));
        stations.append(QJsonArray{p.x, p.y, p.z});
        Id next{};
        for (auto [vertex, edge] : graph.at(current))
            if (!used.contains(edge)) {
                used.insert(edge);
                next = vertex;
                break;
            }
        if (!next || (closed && next == first))
            break;
        current = next;
    }
    if (used.size() != edges.size())
        throw std::runtime_error("Follow Me path is disconnected; select one connected path");
    sweepCommand_ = QJsonObject{{"command", "geometry.sweep"},
                                {"body", QString::number(source)},
                                {"face", QString::number(face)},
                                {"path", stations},
                                {"closed", closed},
                                {"space", "world"}};
    session_.begin();
    previewCommand(*sweepCommand_);
    if (previewValid_)
        emit message(QString("Follow Me · %1 path segments · Enter or click to apply · Esc cancels "
                             "· Profile and path retained")
                         .arg(edges.size()));
}
void Viewport::finishSweep() {
    if (!sweepCommand_ || !session_.active() || !previewValid_)
        throw std::runtime_error(
            "Select a profile and connected path, then press Shift+F to preview");
    session_.commit(*sweepCommand_);
    cancel();
    refresh();
    emit changed();
    emit message("Sweep created · Profile and path selection retained · Ctrl+Z undoes");
}
} // namespace sketchy
