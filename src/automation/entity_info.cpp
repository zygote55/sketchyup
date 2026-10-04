#include "automation/entity_info.hpp"
#include <QJsonArray>
namespace sketchy {
namespace {
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
QJsonArray ids(const std::vector<Id> &values) {
    QJsonArray result;
    for (auto id : values)
        result.append(QString::number(id));
    return result;
}
QJsonObject frame(const FrameMeasures &measured) {
    QJsonObject result{
        {"length", measured.infiniteLength ? QJsonValue{} : QJsonValue(measured.length)},
        {"infiniteLength", measured.infiniteLength},
        {"area", measured.area},
        {"volume", measured.volume ? QJsonValue(*measured.volume) : QJsonValue{}},
        {"bounds", QJsonValue{}}};
    if (measured.bounds)
        result["bounds"] = QJsonObject{{"minimum", point(measured.bounds->low)},
                                       {"maximum", point(measured.bounds->high)},
                                       {"dimensions", point(measured.bounds->dimensions())}};
    return result;
}
} // namespace
QJsonObject entityDescription(const Document &doc, SelectedEntity entity) {
    const auto measured = measureEntity(doc, entity);
    const auto &body = *doc.bodies().at(entity.body);
    QJsonObject properties;
    for (const auto &[key, value] : body.properties)
        std::visit(
            [&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>)
                    properties[QString::fromStdString(key)] = QString::fromStdString(v);
                else
                    properties[QString::fromStdString(key)] = v;
            },
            value);
    const auto kind = entity.kind == SelectionKind::Body   ? "context"
                      : entity.kind == SelectionKind::Face ? "face"
                      : entity.kind == SelectionKind::Edge ? "edge"
                                                           : "guide";
    auto world = frame(measured.world), local = frame(measured.local),
         parent = frame(measured.parent);
    world["origin"] = point(measured.worldOrigin);
    parent["origin"] = point(measured.parentOrigin);
    return {{"documentId", QString::fromStdString(doc.identity())},
            {"revision", QString::number(doc.revision())},
            {"body", QString::number(entity.body)},
            {"kind", kind},
            {"entity", QString::number(entity.entity)},
            {"name", QString::fromStdString(body.name)},
            {"tag", QString::number(body.tag)},
            {"ownerKind", doc.instances().contains(entity.body) ? "component"
                          : body.kind == BodyKind::Group        ? "group"
                                                                : "geometry"},
            {"color", QJsonArray{body.color[0], body.color[1], body.color[2]}},
            {"properties", properties},
            {"units", QJsonObject{{"length", "m"}, {"area", "m2"}, {"volume", "m3"}}},
            {"includesHidden", true},
            {"world", world},
            {"local", local},
            {"parent", parent},
            {"parentOrigin", point(measured.parentOrigin)},
            {"counts", QJsonObject{{"records", int(measured.records)},
                                   {"vertices", int(measured.vertices)},
                                   {"edges", int(measured.edges)},
                                   {"faces", int(measured.faces)},
                                   {"guides", int(measured.guides)}}},
            {"solid", QJsonObject{{"status", QString::fromStdString(measured.solid.status)},
                                  {"body", QString::number(measured.solidBody)},
                                  {"faces", ids(measured.solid.faces)},
                                  {"edges", ids(measured.solid.edges)},
                                  {"vertices", ids(measured.solid.vertices)}}}};
}
} // namespace sketchy
