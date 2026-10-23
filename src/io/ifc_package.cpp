#include "io/ifc_package.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
namespace sketchy {
namespace {
const QByteArray magic("SKIFCP01", 8);
constexpr std::uint32_t none = IfcPackageMesh::noMaterial;
[[noreturn]] void fail(const std::string &message) {
    throw std::runtime_error("Invalid IFC conversion package: " + message);
}
void require(bool ok, const char *message) {
    if (!ok)
        fail(message);
}
struct RoleName {
    IfcNodeRole role;
    const char *name;
};
constexpr RoleName roleNames[] = {
    {IfcNodeRole::Project, "project"},   {IfcNodeRole::Site, "site"},
    {IfcNodeRole::Building, "building"}, {IfcNodeRole::Storey, "storey"},
    {IfcNodeRole::Space, "space"},       {IfcNodeRole::Assembly, "assembly"},
    {IfcNodeRole::Element, "element"},   {IfcNodeRole::Opening, "opening"},
    {IfcNodeRole::Spatial, "spatial"}};
double finite(const QJsonValue &value, const char *what) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        fail(std::string(what) + " must be a finite number");
    return value.toDouble();
}
std::int64_t integer(const QJsonValue &value, std::int64_t low, std::int64_t high,
                     const char *what) {
    const double v = finite(value, what);
    if (v != std::floor(v) || v < double(low) || v > double(high))
        fail(std::string(what) + " is out of range");
    return std::int64_t(v);
}
std::string text(const QJsonValue &value, std::size_t limit, const char *what) {
    if (!value.isString())
        fail(std::string(what) + " must be a string");
    const auto bytes = value.toString().toUtf8();
    if (std::size_t(bytes.size()) > limit)
        fail(std::string(what) + " is too long");
    return bytes.toStdString();
}
QJsonObject object(const QJsonValue &value, const char *what) {
    if (!value.isObject())
        fail(std::string(what) + " must be an object");
    return value.toObject();
}
QJsonArray array(const QJsonValue &value, std::size_t limit, const char *what) {
    if (!value.isArray())
        fail(std::string(what) + " must be an array");
    const auto result = value.toArray();
    if (std::size_t(result.size()) > limit)
        fail(std::string(what) + " has too many entries");
    return result;
}
void onlyKeys(const QJsonObject &o, std::initializer_list<const char *> keys, const char *what) {
    std::set<QString> allowed;
    for (const auto *key : keys)
        allowed.insert(QString::fromLatin1(key));
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!allowed.contains(it.key()))
            fail(std::string(what) + " has an unknown member");
    for (const auto *key : keys)
        if (!o.contains(QString::fromLatin1(key)))
            fail(std::string(what) + " is missing a member");
}
// A section the conversion reads field by field: bounded size and depth, finite numbers.
void checkSection(const QJsonValue &value, std::size_t depth, std::size_t &budget) {
    if (depth > 8 || budget == 0)
        fail("a section is too deep or too large");
    --budget;
    if (value.isString()) {
        if (std::size_t(value.toString().toUtf8().size()) > IfcPackageLimits::stringBytes)
            fail("a section string is too long");
    } else if (value.isDouble()) {
        if (!std::isfinite(value.toDouble()))
            fail("a section number is not finite");
    } else if (value.isArray()) {
        for (const auto &entry : value.toArray())
            checkSection(entry, depth + 1, budget);
    } else if (value.isObject()) {
        const auto o = value.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.key().size() > 256)
                fail("a section key is too long");
            checkSection(it.value(), depth + 1, budget);
        }
    } else if (!value.isBool() && !value.isNull()) {
        fail("a section holds an unsupported value");
    }
}
IfcPackageValue decodeValue(const QJsonValue &type, const QJsonValue &value) {
    const auto kind = text(type, 1, "value type");
    if (kind == "s")
        return text(value, IfcPackageLimits::stringBytes, "property text");
    if (kind == "b") {
        if (!value.isBool())
            fail("property flag must be a boolean");
        return value.toBool();
    }
    if (kind == "d")
        return finite(value, "property number");
    fail("unknown value type");
}
QJsonValue encodeValue(const IfcPackageValue &value, QString &type) {
    if (const auto *flag = std::get_if<bool>(&value)) {
        type = QStringLiteral("b");
        return *flag;
    }
    if (const auto *number = std::get_if<double>(&value)) {
        type = QStringLiteral("d");
        return *number;
    }
    type = QStringLiteral("s");
    return QString::fromStdString(std::get<std::string>(value));
}
IfcNodeRole decodeRole(const QJsonValue &value) {
    const auto name = text(value, 16, "node role");
    for (const auto &entry : roleNames)
        if (name == entry.name)
            return entry.role;
    fail("unknown node role");
}
std::size_t meshBlockBytes(std::size_t vertices, std::size_t triangles, std::size_t edges,
                           bool colors) {
    return vertices * 24 + triangles * 12 + triangles * 4 + edges * 8 + (colors ? triangles * 12 : 0);
}
double readF64(const unsigned char *&cursor) {
    const auto bits = qFromLittleEndian<quint64>(cursor);
    cursor += 8;
    double value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}
float readF32(const unsigned char *&cursor) {
    const auto bits = qFromLittleEndian<quint32>(cursor);
    cursor += 4;
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}
std::uint32_t readU32(const unsigned char *&cursor) {
    const auto value = qFromLittleEndian<quint32>(cursor);
    cursor += 4;
    return value;
}
void appendU32(QByteArray &out, std::uint32_t value) {
    char word[4];
    qToLittleEndian<quint32>(value, word);
    out.append(word, 4);
}
void appendF64(QByteArray &out, double value) {
    quint64 bits;
    std::memcpy(&bits, &value, 8);
    char word[8];
    qToLittleEndian<quint64>(bits, word);
    out.append(word, 8);
}
void appendF32(QByteArray &out, float value) {
    quint32 bits;
    std::memcpy(&bits, &value, 4);
    appendU32(out, bits);
}
} // namespace
const char *ifcNodeRoleName(IfcNodeRole role) {
    for (const auto &entry : roleNames)
        if (entry.role == role)
            return entry.name;
    return "element";
}
IfcPackage decodeIfcPackage(const QByteArray &bytes, IfcPackageDecodeOptions options) {
    const std::uint64_t size = std::uint64_t(bytes.size());
    if (size < IfcPackageLimits::headerBytes || !bytes.startsWith(magic))
        fail("bad header");
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData());
    const std::uint64_t manifestBytes = qFromLittleEndian<quint32>(data + 8);
    const std::uint64_t reserved = qFromLittleEndian<quint32>(data + 12);
    const std::uint64_t meshBytes = qFromLittleEndian<quint64>(data + 16);
    require(reserved == 0, "reserved header word is not zero");
    require(manifestBytes <= IfcPackageLimits::manifestBytes, "manifest exceeds 32 MiB");
    require(meshBytes <= IfcPackageLimits::meshBytes, "mesh chunk exceeds 128 MiB");
    require(size == IfcPackageLimits::headerBytes + manifestBytes + meshBytes,
            "length disagrees with the header");
    const auto manifestData = bytes.mid(IfcPackageLimits::headerBytes, qsizetype(manifestBytes));
    const auto *meshData = data + IfcPackageLimits::headerBytes + manifestBytes;
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(manifestData, &parse);
    require(parse.error == QJsonParseError::NoError && json.isObject(), "manifest is not a JSON object");
    const auto manifest = json.object();
    onlyKeys(manifest,
             {"format", "version", "meshSha256", "engine", "source", "units", "report", "meshes",
              "materials", "nodes"},
             "manifest");
    require(text(manifest["format"], 64, "format") == "sketchyup-ifc-package", "unknown format");
    require(integer(manifest["version"], 0, 1000, "version") == 1, "unsupported version");
    IfcPackage package;
    std::size_t budget = IfcPackageLimits::sectionNodes;
    package.engine = object(manifest["engine"], "engine");
    package.source = object(manifest["source"], "source");
    package.units = object(manifest["units"], "units");
    package.report = object(manifest["report"], "report");
    for (const auto *section : {&package.engine, &package.source, &package.units, &package.report})
        checkSection(*section, 0, budget);

    // Materials.
    for (const auto &entry : array(manifest["materials"], IfcPackageLimits::materials, "materials")) {
        const auto m = object(entry, "material");
        onlyKeys(m, {"name", "color", "opacity"}, "material");
        IfcPackageMaterial material;
        material.name = text(m["name"], IfcPackageLimits::nameBytes, "material name");
        const auto color = array(m["color"], 3, "material colour");
        require(color.size() == 3, "material colour needs three components");
        for (int i = 0; i < 3; ++i) {
            const double v = finite(color[i], "material colour");
            require(v >= 0 && v <= 1, "material colour is outside 0..1");
            material.color[std::size_t(i)] = float(v);
        }
        const double opacity = finite(m["opacity"], "material opacity");
        require(opacity >= 0 && opacity <= 1, "material opacity is outside 0..1");
        material.opacity = float(opacity);
        package.materials.push_back(std::move(material));
    }

    // Mesh descriptors: every count is checked, and the lengths summed, before anything is read.
    struct Descriptor {
        std::size_t vertices, triangles, edges;
        bool colors;
    };
    std::vector<Descriptor> descriptors;
    std::uint64_t total = 0, triangleTotal = 0;
    for (const auto &entry : array(manifest["meshes"], IfcPackageLimits::meshes, "meshes")) {
        const auto m = object(entry, "mesh");
        onlyKeys(m, {"vertices", "triangles", "edges", "colors"}, "mesh");
        Descriptor d{};
        d.vertices = std::size_t(integer(m["vertices"], 3, IfcPackageLimits::meshVertices, "mesh vertices"));
        d.triangles = std::size_t(integer(m["triangles"], 1, IfcPackageLimits::meshTriangles, "mesh triangles"));
        d.edges = std::size_t(integer(m["edges"], 0, 3 * IfcPackageLimits::meshTriangles, "mesh edges"));
        require(m["colors"].isBool(), "mesh colour flag must be a boolean");
        d.colors = m["colors"].toBool();
        triangleTotal += d.triangles;
        require(triangleTotal <= IfcPackageLimits::totalTriangles, "meshes exceed 1,000,000 triangles");
        total += meshBlockBytes(d.vertices, d.triangles, d.edges, d.colors);
        require(total <= meshBytes, "mesh chunk is shorter than its meshes");
        descriptors.push_back(d);
    }
    require(total == meshBytes, "mesh chunk length disagrees with its meshes");
    const auto digest = text(manifest["meshSha256"], 64, "meshSha256");
    if (options.checkMeshDigest) {
        const auto actual = QCryptographicHash::hash(
            QByteArray::fromRawData(reinterpret_cast<const char *>(meshData), qsizetype(meshBytes)),
            QCryptographicHash::Sha256).toHex();
        require(QByteArray::fromStdString(digest) == actual, "mesh chunk SHA-256 does not match");
    }

    // Mesh data.
    const auto *cursor = meshData;
    package.meshes.reserve(descriptors.size());
    for (const auto &d : descriptors) {
        IfcPackageMesh mesh;
        mesh.vertices.reserve(d.vertices);
        for (std::size_t i = 0; i < d.vertices; ++i) {
            const double x = readF64(cursor), y = readF64(cursor), z = readF64(cursor);
            require(std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
                        std::abs(x) <= coordinateLimit && std::abs(y) <= coordinateLimit &&
                        std::abs(z) <= coordinateLimit,
                    "mesh vertex is not finite or beyond 1e6 m");
            mesh.vertices.push_back({x, y, z});
        }
        mesh.triangles.reserve(d.triangles);
        for (std::size_t i = 0; i < d.triangles; ++i) {
            std::array<std::uint32_t, 3> t{readU32(cursor), readU32(cursor), readU32(cursor)};
            for (const auto index : t)
                require(index < d.vertices, "mesh triangle index is out of range");
            mesh.triangles.push_back(t);
        }
        mesh.materials.reserve(d.triangles);
        for (std::size_t i = 0; i < d.triangles; ++i) {
            const auto material = readU32(cursor);
            require(material == none || material < package.materials.size(),
                    "mesh material index is out of range");
            mesh.materials.push_back(material);
        }
        mesh.featureEdges.reserve(d.edges);
        for (std::size_t i = 0; i < d.edges; ++i) {
            std::array<std::uint32_t, 2> e{readU32(cursor), readU32(cursor)};
            require(e[0] < d.vertices && e[1] < d.vertices, "mesh edge index is out of range");
            mesh.featureEdges.push_back(e);
        }
        if (d.colors) {
            mesh.colors.reserve(d.triangles);
            for (std::size_t i = 0; i < d.triangles; ++i) {
                const std::array<float, 3> c{readF32(cursor), readF32(cursor), readF32(cursor)};
                const int missing = int(std::isnan(c[0])) + int(std::isnan(c[1])) + int(std::isnan(c[2]));
                require(missing == 0 || missing == 3, "mesh colour is partly missing");
                if (missing == 3) {
                    mesh.colors.push_back(std::nullopt);
                    continue;
                }
                for (const auto v : c)
                    require(std::isfinite(v) && v >= 0 && v <= 1, "mesh colour is outside 0..1");
                mesh.colors.push_back(c);
            }
        }
        package.meshes.push_back(std::move(mesh));
    }

    // Nodes.
    const auto nodes = array(manifest["nodes"], IfcPackageLimits::nodes, "nodes");
    std::vector<std::uint32_t> depth;
    depth.reserve(std::size_t(nodes.size()));
    package.nodes.reserve(std::size_t(nodes.size()));
    for (qsizetype index = 0; index < nodes.size(); ++index) {
        const auto n = object(nodes[index], "node");
        onlyKeys(n,
                 {"step", "class", "guid", "name", "parent", "role", "hidden", "transform", "mesh",
                  "ifc", "props"},
                 "node");
        IfcPackageNode node;
        node.step = std::uint32_t(integer(n["step"], 0, 0xFFFFFFFFll, "node step"));
        node.className = text(n["class"], 128, "node class");
        require(node.className.size() >= 4 && node.className.compare(0, 3, "Ifc") == 0,
                "node class is not an IFC class");
        for (const char c : node.className)
            require(std::isalnum(static_cast<unsigned char>(c)), "node class has an unexpected character");
        node.guid = text(n["guid"], 256, "node GlobalId");
        node.name = text(n["name"], IfcPackageLimits::nameBytes, "node name");
        node.parent = integer(n["parent"], -1, index - 1, "node parent");
        node.role = decodeRole(n["role"]);
        require(n["hidden"].isBool(), "node hidden flag must be a boolean");
        node.hidden = n["hidden"].toBool();
        const auto transform = array(n["transform"], 16, "node transform");
        require(transform.size() == 16, "node transform needs 16 numbers");
        for (int i = 0; i < 16; ++i)
            node.transform[std::size_t(i)] = finite(transform[i], "node transform");
        require(node.transform[12] == 0 && node.transform[13] == 0 && node.transform[14] == 0 &&
                    node.transform[15] == 1,
                "node transform is not affine");
        node.mesh = integer(n["mesh"], -1, std::int64_t(package.meshes.size()) - 1, "node mesh");
        for (const auto &entry : array(n["ifc"], IfcPackageLimits::nodeProvenance, "node provenance")) {
            const auto triple = array(entry, 3, "provenance entry");
            require(triple.size() == 3, "provenance entry needs three members");
            auto key = text(triple[0], 128, "provenance key");
            node.provenance.emplace_back(std::move(key), decodeValue(triple[1], triple[2]));
        }
        for (const auto &entry : array(n["props"], IfcPackageLimits::nodeProperties, "node properties")) {
            const auto quintuple = array(entry, 5, "property entry");
            require(quintuple.size() == 5, "property entry needs five members");
            IfcPackageProperty property;
            property.set = text(quintuple[0], IfcPackageLimits::nameBytes, "property set");
            property.name = text(quintuple[1], IfcPackageLimits::nameBytes, "property name");
            property.origin = int(integer(quintuple[2], 0, 3, "property origin"));
            property.value = decodeValue(quintuple[3], quintuple[4]);
            node.properties.push_back(std::move(property));
        }
        const std::uint32_t level = node.parent < 0 ? 0 : depth[std::size_t(node.parent)] + 1;
        require(level <= IfcPackageLimits::depth, "node tree is too deep");
        depth.push_back(level);
        package.nodes.push_back(std::move(node));
    }
    return package;
}
QByteArray encodeIfcPackage(const IfcPackage &package) {
    QByteArray mesh;
    QJsonArray meshes;
    for (const auto &m : package.meshes) {
        const bool colors = !m.colors.empty();
        for (const auto v : m.vertices) {
            appendF64(mesh, v.x);
            appendF64(mesh, v.y);
            appendF64(mesh, v.z);
        }
        for (const auto &t : m.triangles)
            for (const auto i : t)
                appendU32(mesh, i);
        for (const auto material : m.materials)
            appendU32(mesh, material);
        for (const auto &e : m.featureEdges) {
            appendU32(mesh, e[0]);
            appendU32(mesh, e[1]);
        }
        for (const auto &color : m.colors)
            for (std::size_t k = 0; k < 3; ++k)
                appendF32(mesh, color ? (*color)[k] : std::nanf(""));
        meshes.append(QJsonObject{{"vertices", qint64(m.vertices.size())},
                                  {"triangles", qint64(m.triangles.size())},
                                  {"edges", qint64(m.featureEdges.size())},
                                  {"colors", colors}});
    }
    QJsonArray materials;
    for (const auto &m : package.materials)
        materials.append(QJsonObject{{"name", QString::fromStdString(m.name)},
                                     {"color", QJsonArray{double(m.color[0]), double(m.color[1]), double(m.color[2])}},
                                     {"opacity", double(m.opacity)}});
    QJsonArray nodes;
    for (const auto &n : package.nodes) {
        QJsonArray transform, provenance, props;
        for (const auto v : n.transform)
            transform.append(v);
        for (const auto &[key, value] : n.provenance) {
            QString type;
            const auto encoded = encodeValue(value, type);
            provenance.append(QJsonArray{QString::fromStdString(key), type, encoded});
        }
        for (const auto &p : n.properties) {
            QString type;
            const auto encoded = encodeValue(p.value, type);
            props.append(QJsonArray{QString::fromStdString(p.set), QString::fromStdString(p.name),
                                    p.origin, type, encoded});
        }
        nodes.append(QJsonObject{{"step", qint64(n.step)},
                                 {"class", QString::fromStdString(n.className)},
                                 {"guid", QString::fromStdString(n.guid)},
                                 {"name", QString::fromStdString(n.name)},
                                 {"parent", qint64(n.parent)},
                                 {"role", QString::fromLatin1(ifcNodeRoleName(n.role))},
                                 {"hidden", n.hidden},
                                 {"transform", transform},
                                 {"mesh", qint64(n.mesh)},
                                 {"ifc", provenance},
                                 {"props", props}});
    }
    const QJsonObject manifest{
        {"format", "sketchyup-ifc-package"},
        {"version", 1},
        {"meshSha256", QString::fromLatin1(QCryptographicHash::hash(mesh, QCryptographicHash::Sha256).toHex())},
        {"engine", package.engine},
        {"source", package.source},
        {"units", package.units},
        {"report", package.report},
        {"meshes", meshes},
        {"materials", materials},
        {"nodes", nodes}};
    const auto json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QByteArray out = magic;
    char word[8];
    qToLittleEndian<quint32>(quint32(json.size()), word);
    out.append(word, 4);
    qToLittleEndian<quint32>(0, word);
    out.append(word, 4);
    qToLittleEndian<quint64>(quint64(mesh.size()), word);
    out.append(word, 8);
    return out + json + mesh;
}
} // namespace sketchy
