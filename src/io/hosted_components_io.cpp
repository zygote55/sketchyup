#include "io/hosted_components_io.hpp"
#include <QJsonArray>
#include <set>
namespace sketchy {
namespace {
QString sid(Id id) { return QString::number(id); }
[[noreturn]] void invalid() { throw std::runtime_error("Invalid hosted component storage record"); }
Id identity(const QJsonValue &value) {
    bool ok = false;
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    if (!value.isString() || !ok || !result || QString::number(result) != text)
        invalid();
    return result;
}
double number(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        invalid();
    return value.toDouble();
}
QJsonArray array(const QJsonValue &value, qsizetype limit) {
    if (!value.isArray() || value.toArray().size() > limit)
        invalid();
    return value.toArray();
}
QJsonArray tuple(const QJsonValue &value, qsizetype size) {
    const auto result = array(value, size);
    if (result.size() != size)
        invalid();
    return result;
}
QJsonObject object(const QJsonValue &value, const QStringList &fields) {
    if (!value.isObject())
        invalid();
    const auto result = value.toObject();
    if (result.size() != fields.size())
        invalid();
    for (const auto &field : fields)
        if (!result.contains(field))
            invalid();
    return result;
}
QJsonObject surface(const Surface &value) {
    QJsonArray vertices, faces, wires;
    for (const auto &[id, p] : value.vertices)
        vertices.append(QJsonArray{sid(id), p.x, p.y, p.z});
    for (const auto &[id, face] : value.faces) {
        QJsonArray loops;
        for (const auto &loop : face.loops) {
            QJsonArray ids;
            for (auto vertex : loop)
                ids.append(sid(vertex));
            loops.append(ids);
        }
        faces.append(QJsonObject{{"id", sid(id)}, {"loops", loops}});
    }
    for (const auto &wire : value.wires)
        wires.append(QJsonArray{sid(wire[0]), sid(wire[1])});
    return {
        {"nextId", sid(value.nextId)}, {"vertices", vertices}, {"faces", faces}, {"wires", wires}};
}
struct Budget {
    size_t vertices{}, faces{}, wires{}, corners{}, profiles{};
    void add(size_t &value, size_t amount, size_t limit) {
        if (amount > limit - value)
            invalid();
        value += amount;
    }
};
Surface surface(const QJsonValue &value, Budget &budget) {
    const auto record = object(value, {"nextId", "vertices", "faces", "wires"});
    Surface result;
    result.nextId = identity(record["nextId"]);
    const auto vertices = array(record["vertices"], 10000), faces = array(record["faces"], 1000),
               wires = array(record["wires"], 10000);
    budget.add(budget.vertices, vertices.size(), 20000);
    budget.add(budget.faces, faces.size(), 2000);
    budget.add(budget.wires, wires.size(), 20000);
    for (const auto &value : vertices) {
        const auto v = tuple(value, 4);
        if (!result.vertices.emplace(identity(v[0]), Vec3{number(v[1]), number(v[2]), number(v[3])})
                 .second)
            invalid();
    }
    for (const auto &value : faces) {
        const auto f = object(value, {"id", "loops"});
        Face face;
        face.id = identity(f["id"]);
        for (const auto &value : array(f["loops"], 64)) {
            const auto loop = array(value, 4096);
            budget.add(budget.corners, loop.size(), 64000);
            auto &out = face.loops.emplace_back();
            for (const auto &id : loop)
                out.push_back(identity(id));
        }
        if (!result.faces.emplace(face.id, std::move(face)).second)
            invalid();
    }
    for (const auto &value : wires) {
        const auto wire = tuple(value, 2);
        result.wires.push_back({identity(wire[0]), identity(wire[1])});
    }
    return result;
}
} // namespace
QJsonObject encodeHostedComponents(const HostedComponents &records) {
    QJsonArray hosts, attachments;
    for (const auto &[id, attachment] : records.attachments) {
        QJsonArray frame;
        for (auto value : attachment->frame.m)
            frame.append(value);
        attachments.append(QJsonObject{{"instance", sid(id)},
                                       {"host", sid(attachment->host)},
                                       {"face", sid(attachment->face)},
                                       {"frame", frame},
                                       {"inset", attachment->inset}});
    }
    for (const auto &[id, host] : records.hosts) {
        QJsonArray openings;
        for (const auto &[owner, opening] : host->openings) {
            QJsonArray corners, vertices, jambs;
            for (const auto &corner : opening.profile.corners)
                corners.append(
                    QJsonArray{sid(corner.key), corner.point.x, corner.point.y, corner.point.z});
            for (const auto &[key, pair] : opening.vertices)
                vertices.append(QJsonArray{sid(key), sid(pair[0]), sid(pair[1])});
            for (const auto &[edge, face] : opening.jambs)
                jambs.append(QJsonArray{sid(edge[0]), sid(edge[1]), sid(face)});
            openings.append(QJsonObject{{"instance", sid(owner)},
                                        {"face", sid(opening.profile.face)},
                                        {"exit", sid(opening.exit)},
                                        {"corners", corners},
                                        {"vertices", vertices},
                                        {"jambs", jambs}});
        }
        hosts.append(QJsonObject{
            {"body", sid(id)}, {"uncut", surface(host->uncut)}, {"openings", openings}});
    }
    return {{"hosts", hosts}, {"attachments", attachments}};
}
HostedPtr decodeHostedComponents(const QJsonValue &value) {
    const auto root = object(value, {"hosts", "attachments"});
    auto result = std::make_shared<HostedComponents>();
    Budget budget;
    for (const auto &value : array(root["hosts"], 16)) {
        const auto record = object(value, {"body", "uncut", "openings"});
        auto host = std::make_shared<HostedSurface>();
        host->uncut = surface(record["uncut"], budget);
        for (const auto &value : array(record["openings"], 16)) {
            const auto record =
                object(value, {"instance", "face", "exit", "corners", "vertices", "jambs"});
            HostOpening opening;
            opening.profile.face = identity(record["face"]);
            opening.exit = identity(record["exit"]);
            const auto corners = array(record["corners"], 256);
            budget.add(budget.profiles, corners.size(), 4096);
            std::set<Id> keys;
            for (const auto &value : corners) {
                const auto corner = tuple(value, 4);
                const auto key = identity(corner[0]);
                if (!keys.insert(key).second)
                    invalid();
                opening.profile.corners.push_back(
                    {key, {number(corner[1]), number(corner[2]), number(corner[3])}});
            }
            for (const auto &value : array(record["vertices"], 256)) {
                const auto pair = tuple(value, 3);
                if (!opening.vertices
                         .emplace(identity(pair[0]),
                                  std::array<Id, 2>{identity(pair[1]), identity(pair[2])})
                         .second)
                    invalid();
            }
            for (const auto &value : array(record["jambs"], 256)) {
                const auto edge = tuple(value, 3);
                const auto a = identity(edge[0]), b = identity(edge[1]);
                if (a >= b ||
                    !opening.jambs.emplace(std::array<Id, 2>{a, b}, identity(edge[2])).second)
                    invalid();
            }
            if (!host->openings.emplace(identity(record["instance"]), std::move(opening)).second)
                invalid();
        }
        if (!result->hosts.emplace(identity(record["body"]), std::move(host)).second)
            invalid();
    }
    for (const auto &value : array(root["attachments"], 64)) {
        const auto record = object(value, {"instance", "host", "face", "frame", "inset"});
        auto attachment = std::make_shared<ComponentAttachment>();
        attachment->host = identity(record["host"]);
        attachment->face = identity(record["face"]);
        attachment->inset = number(record["inset"]);
        const auto frame = tuple(record["frame"], 16);
        for (size_t i = 0; i < 16; ++i)
            attachment->frame.m[i] = number(frame[qsizetype(i)]);
        if (!result->attachments.emplace(identity(record["instance"]), std::move(attachment))
                 .second)
            invalid();
    }
    return result;
}
} // namespace sketchy
