#include "io/ifc_export.hpp"
#include "io/ifc_worker_run.hpp"

#include "core/appearance.hpp"
#include "core/component_parameter_records.hpp"
#include "geometry/diagnostics.hpp"
#include "io/new_file.hpp"
#include "sketchyup/version.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTemporaryDir>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <csignal>
#include <cstring>
#include <optional>
#include <set>
#include <sys/prctl.h>
#include <unistd.h>
#include <variant>

namespace sketchy {
namespace {
constexpr char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$";
constexpr std::size_t maxIfcBytes = 256u * 1024 * 1024;
constexpr std::size_t maxManifestBytes = 32u * 1024 * 1024, maxMeshBytes = 128u * 1024 * 1024;
const char *const privatePrefix = "sketchyup-ifc-";
constexpr std::array<float, 3> defaultBodyColor{0.73f, 0.79f, 0.73f};
constexpr std::size_t labelCodePoints = 255;
constexpr std::size_t maxMeshVertices = 300000, maxMeshTriangles = 1000000, maxNodes = 400000, maxStyles = 65536;

using Value = std::variant<bool, double, std::string>;

[[noreturn]] void refuse(const std::string &code, const std::string &message, QJsonObject report = {}) {
    throw IfcExportError(code, message, std::move(report));
}

// ---- Text ---------------------------------------------------------------------------------------------------
// A shortened copy of `text` of at most `codePoints` code points, cut at a code point boundary.
std::string clipCodePoints(const std::string &text, std::size_t codePoints, bool &clipped) {
    std::size_t count = 0, i = 0;
    while (i < text.size()) {
        if (count == codePoints) {
            clipped = true;
            return text.substr(0, i);
        }
        ++i;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
            ++i;
        ++count;
    }
    return text;
}
std::string clipBytes(const std::string &text, std::size_t bytes) {
    if (text.size() <= bytes)
        return text;
    std::size_t cut = bytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
    return text.substr(0, cut);
}
// Text the helper will take: valid UTF-8 (a bad byte becomes '?'), control characters become spaces (a line break and a tab
// stay when `keepLineBreaks`).
std::string cleanText(const std::string &text, bool keepLineBreaks = false) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if ((c < 0x20 && !(keepLineBreaks && (c == '\n' || c == '\t'))) || c == 0x7f)
                out += ' ';
            else
                out += char(c);
            continue;
        }
        const std::size_t length = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 ? 2 : 0;
        bool valid = length != 0 && c < 0xF5 && i + length <= text.size();
        for (std::size_t k = 1; valid && k < length; ++k)
            valid = (static_cast<unsigned char>(text[i + k]) & 0xC0) == 0x80;
        if (valid) {
            const unsigned char second = static_cast<unsigned char>(text[i + 1]);
            valid = !((c == 0xE0 && second < 0xA0) || (c == 0xED && second >= 0xA0) || (c == 0xF0 && second < 0x90) || (c == 0xF4 && second >= 0x90));
        }
        if (!valid) {
            out += '?';
            continue;
        }
        out.append(text, i, length);
        i += length - 1;
    }
    return out;
}
// Control characters become spaces; the message never quotes more than 64 bytes of user text.
std::string userText(const std::string &text) {
    std::string out = clipBytes(text, 64);
    for (auto &c : out)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            c = ' ';
    return out;
}
std::string stringProperty(const Body &body, const char *key) {
    const auto found = body.properties.find(key);
    if (found == body.properties.end())
        return {};
    if (const auto *text = std::get_if<std::string>(&found->second))
        return *text;
    return {};
}
std::optional<double> numberProperty(const Body &body, const char *key) {
    const auto found = body.properties.find(key);
    if (found == body.properties.end())
        return std::nullopt;
    if (const auto *number = std::get_if<double>(&found->second))
        return *number;
    return std::nullopt;
}

// ---- Geometry helpers -------------------------------------------------------------------------------------
Vec3 column(const Transform &t, int c) { return {t.m[std::size_t(c * 4)], t.m[std::size_t(c * 4 + 1)], t.m[std::size_t(c * 4 + 2)]}; }
bool nearIdentity(const Transform &t, double tol = 1e-12) {
    const Transform identity;
    for (std::size_t i = 0; i < 16; ++i)
        if (std::abs(t.m[i] - identity.m[i]) > tol)
            return false;
    return true;
}
double snap(double v) {
    if (std::abs(v) < 1e-15)
        return 0;
    if (std::abs(v - 1) < 1e-15)
        return 1;
    if (std::abs(v + 1) < 1e-15)
        return -1;
    return v;
}
// The rotation of `t` that IfcAxis2Placement3D can carry: x along its first column, z along its third (made
// perpendicular), y completing a right-handed frame. Everything else in `t` (scale, shear, a reflection) is
// the caller's residual.
Transform rigidPart(const Transform &t) {
    auto x = normalized(column(t, 0));
    auto zRaw = column(t, 2);
    auto z = normalized(zRaw - x * dot(zRaw, x));
    auto y = cross(z, x);
    Transform r;
    r.m = {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, t.m[12], t.m[13], t.m[14], 1};
    return r;
}
std::array<double, 9> placementOf(const Transform &r) {
    const auto z = column(r, 2), x = column(r, 0);
    return {snap(r.m[12]), snap(r.m[13]), snap(r.m[14]), snap(z.x), snap(z.y), snap(z.z), snap(x.x), snap(x.y), snap(x.z)};
}

// ---- Looks (materials and colours) ------------------------------------------------------------------------
struct Look {
    Id material{};
    bool colored{};
    std::array<float, 3> rgb{};
    bool operator==(const Look &) const = default;
    bool operator<(const Look &o) const { return std::tie(material, colored, rgb) < std::tie(o.material, o.colored, o.rgb); }
};
Look lookOf(const Body &body, Id face) {
    const auto sides = faceMaterials(body, face);
    if (sides.front)
        return {sides.front, false, {}};
    if (const auto found = body.faceColors.find(face); found != body.faceColors.end())
        return {0, true, found->second};
    if (body.color != defaultBodyColor)
        return {0, true, body.color};
    return {};
}

struct StyleRecord {
    std::string name;
    std::array<float, 3> color{};
    float opacity{1};
    bool material{};
};

// ---- The IFC-side tree -------------------------------------------------------------------------------------
struct Prop {
    std::string set, name;
    Value value;
};
struct Node {
    Id body{};       // the body this node came from; 0 for a node the export made up
    std::string role; // project site building storey space assembly element
    int parent{-1};
    std::string requestedClass, name, longName, objectType, predefinedType, guid;
    std::optional<double> elevation;
    Id geometry{};   // the body whose faces are written (the node's own, or a folded child), 0 none
    std::vector<Prop> props;
    bool hidden{};
    std::vector<int> children;
    // outputs
    int mesh{-1}, style{-1};
    std::array<double, 9> placement{0, 0, 0, 0, 0, 1, 1, 0, 0};
    Transform emitted;
    bool exactFrame{}; // the node's emitted frame is its native world, so a native child's own transform is its placement
    bool hasProvenanceGuid{};
    std::string candidateGuid;
};

enum class Disposition { Unvisited, Written, Terrain, TerrainTile, ReferenceImage, EmptyGroup, NoFaces };
enum class Level { Top, Site, Building, Storey, Space, Part };

struct MeshOut {
    std::vector<double> vertices;
    std::vector<std::uint32_t> triangles, colorIndex;
    std::vector<std::array<float, 3>> colors;
    bool closed{};
};

struct Capture {
    const Document &doc;
    explicit Capture(const Document &d) : doc(d) {}

    std::map<Id, std::vector<Id>> children;
    std::vector<Id> roots, preorder;
    std::map<Id, Disposition> disposition;
    std::set<Id> placementRoots;
    std::map<Id, Id> fold;       // group -> its geometry child that is the group's own geometry
    std::set<Id> folded;         // those children
    std::vector<Node> nodes;
    std::vector<StyleRecord> styles;
    std::map<Look, int> styleOf;
    std::vector<MeshOut> meshes;
    // counters
    std::map<std::string, qint64> count;      // written / notWritten / misc counters
    std::map<std::string, qint64> notices;
    QJsonArray locations;
    std::map<std::string, int> locationCounts;
    Id projectGroup{};

    void location(const std::string &kind, const QJsonObject &fields) {
        if (++locationCounts[kind] > 32)
            return;
        QJsonObject entry = fields;
        entry["kind"] = QString::fromStdString(kind);
        locations.append(entry);
    }

    // ---- 1. The document's scene tree and what becomes of every body ----
    void index() {
        for (const auto &[id, body] : doc.bodies())
            if (body->parent)
                children[body->parent].push_back(id);
            else
                roots.push_back(id);
        for (auto &[parent, list] : children)
            std::sort(list.begin(), list.end());
        std::sort(roots.begin(), roots.end());
        std::vector<Id> stack(roots.rbegin(), roots.rend());
        while (!stack.empty()) {
            const Id id = stack.back();
            stack.pop_back();
            preorder.push_back(id);
            if (const auto found = children.find(id); found != children.end())
                stack.insert(stack.end(), found->second.rbegin(), found->second.rend());
        }
        if (preorder.size() != doc.bodies().size())
            refuse("internal", "The document's body tree is not a forest.");
        for (const auto &[root, instance] : doc.instances())
            placementRoots.insert(root);
    }
    const Body &body(Id id) const { return *doc.bodies().at(id); }

    void classify() {
        // Terrain subtrees and reference images are not written.
        for (const Id id : preorder) {
            const auto &b = body(id);
            if (b.kind == BodyKind::Terrain) {
                disposition[id] = Disposition::Terrain;
            } else if (b.kind == BodyKind::ReferenceImage) {
                disposition[id] = Disposition::ReferenceImage;
            } else if (b.parent && disposition.contains(b.parent) &&
                       (disposition.at(b.parent) == Disposition::Terrain || disposition.at(b.parent) == Disposition::TerrainTile)) {
                disposition[id] = Disposition::TerrainTile;
            }
        }
        // Children before parents.
        for (auto it = preorder.rbegin(); it != preorder.rend(); ++it) {
            const Id id = *it;
            if (disposition.contains(id))
                continue;
            const auto &b = body(id);
            bool anyChild = false;
            if (const auto found = children.find(id); found != children.end())
                for (const Id child : found->second)
                    anyChild = anyChild || disposition.at(child) == Disposition::Written;
            if (b.kind == BodyKind::Group)
                disposition[id] = anyChild ? Disposition::Written : Disposition::EmptyGroup;
            else
                disposition[id] = (!b.surface.faces.empty() || anyChild) ? Disposition::Written : Disposition::NoFaces;
        }
    }

    // A group G whose geometry child F carries the same class, name and step, no GlobalId of its own and an identity
    // placement is how the IFC import represents "an element with parts and a body of its own": it is written as one
    // element.
    void findFolds() {
        for (const Id id : preorder) {
            const auto &g = body(id);
            if (g.kind != BodyKind::Group || disposition.at(id) != Disposition::Written)
                continue;
            const auto cls = stringProperty(g, "ifc.Class");
            const auto step = numberProperty(g, "ifc.Step");
            if (cls.empty() || !step || cls == "IfcProject")
                continue; // (a project has no body)
            const auto found = children.find(id);
            if (found == children.end())
                continue;
            for (const Id childId : found->second) {
                const auto &f = body(childId);
                if (f.kind != BodyKind::Geometry || disposition.at(childId) != Disposition::Written || f.surface.faces.empty() ||
                    children.contains(childId) || folded.contains(childId) || f.transform != Transform{} ||
                    !f.properties.contains("ifc.Class") || stringProperty(f, "ifc.Class") != cls ||
                    numberProperty(f, "ifc.Step") != step || f.properties.contains("ifc.GlobalId") || f.name != g.name)
                    continue;
                fold[id] = childId;
                folded.insert(childId);
                break;
            }
        }
    }

    bool effectivelyHidden(Id id) const {
        for (Id at = id; at; at = body(at).parent) {
            const auto &b = body(at);
            if (b.hidden || (b.tag && !tagVisible(doc.tags(), b.tag)))
                return true;
        }
        return false;
    }

    // ---- 2. Nodes ----
    // `parent` -2 waits for the default building, which is not known until the whole tree has been seen.
    std::vector<int> pendingTop;
    int addNode(Node node, int parent) {
        node.parent = parent;
        nodes.push_back(std::move(node));
        const int index = int(nodes.size()) - 1;
        if (parent >= 0)
            nodes[std::size_t(parent)].children.push_back(index);
        else if (parent == -2)
            pendingTop.push_back(index);
        return index;
    }

    void collectProperties(Node &node, const Body &b) {
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto &[key, value] : b.properties) {
            if (key.rfind("ifc.", 0) == 0) {
                static const std::set<std::string> driving{"ifc.Class", "ifc.GlobalId", "ifc.PredefinedType", "ifc.ObjectType",
                                                           "ifc.LongName", "ifc.Name", "ifc.Elevation"};
                if (!driving.contains(key))
                    ++count["provenancePropertiesNotWritten"];
                continue;
            }
            std::string set = "SketchyUp", name = key;
            if (const auto dot = key.find('.'); dot != std::string::npos && dot > 0 && dot + 1 < key.size()) {
                set = key.substr(0, dot);
                name = key.substr(dot + 1);
            }
            // Text the helper accepts: no control characters in a set or a name, none but line breaks and tabs in a value, at most 255
            // bytes in a set or a name and 32 KiB in a value, and no empty name.
            set = clipBytes(cleanText(set), 255);
            name = clipBytes(cleanText(name), 255);
            if (set.empty())
                set = "SketchyUp";
            if (name.empty())
                name = "(unnamed)";
            if (!seen.insert({set, name}).second) {
                ++count["propertyKeyCollisions"];
                continue;
            }
            Value cleaned = value;
            if (const auto *text = std::get_if<std::string>(&value))
                cleaned = clipBytes(cleanText(*text, true), 32 * 1024);
            node.props.push_back({set, name, cleaned});
        }
    }

    void label(std::string &target, const std::string &text) {
        bool clipped = false;
        target = clipCodePoints(cleanText(text), labelCodePoints, clipped);
        if (clipped)
            ++notices["nameClipped"];
    }

    Node makeNode(Id id, const std::string &role) {
        const auto &b = body(id);
        Node node;
        node.body = id;
        node.role = role;
        node.geometry = fold.contains(id) ? fold.at(id) : (b.kind == BodyKind::Geometry && !b.surface.faces.empty() ? id : 0);
        const auto fullName = stringProperty(b, "ifc.Name");
        label(node.name, fullName.empty() ? b.name : fullName);
        label(node.longName, stringProperty(b, "ifc.LongName"));
        label(node.objectType, stringProperty(b, "ifc.ObjectType"));
        node.predefinedType = clipBytes(cleanText(stringProperty(b, "ifc.PredefinedType")), 120);
        node.requestedClass = clipBytes(cleanText(stringProperty(b, "ifc.Class")), 120);
        if (role == "storey")
            node.elevation = numberProperty(b, "ifc.Elevation");
        else if (b.properties.contains("ifc.Elevation"))
            ++count["provenancePropertiesNotWritten"]; // only a storey has an elevation
        if (role != "project" && role != "site" && role != "building" && role != "storey" && role != "space" && b.properties.contains("ifc.LongName"))
            ++count["provenancePropertiesNotWritten"]; // an element has no long name
        node.candidateGuid = stringProperty(b, "ifc.GlobalId");
        node.hasProvenanceGuid = b.properties.contains("ifc.GlobalId");
        collectProperties(node, b);
        node.hidden = effectivelyHidden(id) || (node.geometry && effectivelyHidden(node.geometry));
        return node;
    }

    struct Context {
        int parent{};
        int spatial{-1}; // nearest spatial node; -2: the default building
        Level level{Level::Top};
    };

    void buildTree() {
        // The project: the first root group of class IfcProject, or one made up.
        for (const Id id : roots)
            if (body(id).kind == BodyKind::Group && stringProperty(body(id), "ifc.Class") == "IfcProject" &&
                disposition.at(id) == Disposition::Written) {
                projectGroup = id;
                break;
            }
        Node project;
        if (projectGroup) {
            project = makeNode(projectGroup, "project");
            project.requestedClass.clear();
            project.hidden = false;
        } else {
            project.role = "project";
            project.name = "SketchyUp model";
        }
        addNode(std::move(project), -1);

        struct Item {
            Id id;
            Context context;
        };
        std::vector<Item> stack;
        const auto pushChildren = [&](Id id, const Context &context) {
            if (const auto found = children.find(id); found != children.end())
                for (auto it = found->second.rbegin(); it != found->second.rend(); ++it)
                    stack.push_back({*it, context});
        };
        {
            std::vector<Id> top;
            for (const Id id : roots)
                if (id != projectGroup)
                    top.push_back(id);
            for (auto it = top.rbegin(); it != top.rend(); ++it)
                stack.push_back({*it, Context{0, -2, Level::Top}});
            if (projectGroup) {
                // The project group's children come first, in the project's own order.
                std::vector<Item> inner;
                const auto saved = stack;
                stack.clear();
                pushChildren(projectGroup, Context{0, -2, Level::Top});
                inner = stack;
                stack = saved;
                stack.insert(stack.end(), inner.begin(), inner.end());
            }
        }
        while (!stack.empty()) {
            const Item item = stack.back();
            stack.pop_back();
            const Id id = item.id;
            if (disposition.at(id) != Disposition::Written || folded.contains(id))
                continue;
            const auto &b = body(id);
            const std::string cls = stringProperty(b, "ifc.Class");
            const Context &ctx = item.context;
            // A spatial group whose class fits where it is.
            std::string role;
            if (b.kind == BodyKind::Group) {
                // A site may be made of sites, a building of buildings, a storey of storeys (IFC4 composition); a site or a building stands in the project.
                if (cls == "IfcSite" && (ctx.level == Level::Top || ctx.level == Level::Site))
                    role = "site";
                else if (cls == "IfcBuilding" && (ctx.level == Level::Top || ctx.level == Level::Site || ctx.level == Level::Building))
                    role = "building"; // (a building may stand directly in the project, as in the buildingSMART reference files)
                else if (cls == "IfcBuildingStorey" && (ctx.level == Level::Building || ctx.level == Level::Storey))
                    role = "storey";
            }
            if (!role.empty()) {
                const int parent = ctx.level == Level::Top ? 0 : ctx.spatial;
                const int index = addNode(makeNode(id, role), parent);
                const Level level = role == "site" ? Level::Site : role == "building" ? Level::Building : Level::Storey;
                pushChildren(id, Context{index, index, level});
                continue;
            }
            if (cls == "IfcProject" || cls == "IfcSite" || cls == "IfcBuilding" || cls == "IfcBuildingStorey")
                ++notices["spatialClassNotUsed"];
            // An element, an assembly or a space.
            int parent = ctx.level == Level::Part ? ctx.parent : ctx.spatial;
            const bool spatialParent = ctx.level != Level::Part;
            const bool hasChildren = [&] {
                if (const auto found = children.find(id); found != children.end())
                    for (const Id child : found->second)
                        if (disposition.at(child) == Disposition::Written && !folded.contains(child))
                            return true;
                return false;
            }();
            std::string kind = hasChildren ? "assembly" : "element";
            if (cls == "IfcSpace" && spatialParent)
                kind = "space";
            const int index = addNode(makeNode(id, kind), parent);
            if (kind == "space")
                pushChildren(id, Context{index, index, Level::Space}); // a space contains elements and aggregates spaces
            else
                pushChildren(id, Context{index, ctx.spatial, Level::Part});
        }
        // The default building holds what the project group or the document root holds directly: the first
        // building of the document, or one made up (and a site for it, when the document has none).
        int firstBuilding = -1, firstSite = -1;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (firstBuilding < 0 && nodes[i].role == "building")
                firstBuilding = int(i);
            if (firstSite < 0 && nodes[i].role == "site")
                firstSite = int(i);
        }
        if (firstBuilding < 0) {
            if (firstSite < 0) {
                Node site;
                site.role = "site";
                site.name = "Site";
                firstSite = addNode(std::move(site), 0);
                count["synthesizedSite"] = 1;
            }
            Node building;
            building.role = "building";
            building.name = "Building";
            firstBuilding = addNode(std::move(building), firstSite);
            count["synthesizedBuilding"] = 1;
        }
        defaultBuilding = firstBuilding;
        for (const int index : pendingTop) {
            nodes[std::size_t(index)].parent = defaultBuilding;
            nodes[std::size_t(defaultBuilding)].children.push_back(index);
        }
        pendingTop.clear();
    }
    int defaultBuilding{-1};

    // ---- 3. Order, placements, meshes ----
    std::vector<int> order;
    void linearize() {
        order.clear();
        std::vector<int> stack{0};
        while (!stack.empty()) {
            const int at = stack.back();
            stack.pop_back();
            order.push_back(at);
            const auto &list = nodes[std::size_t(at)].children;
            stack.insert(stack.end(), list.rbegin(), list.rend());
        }
    }

    MeshOut makeMesh(Id geometryId, const Transform &residual, bool bake, bool flip, int &styleIndex, bool &hasTriangles) {
        const auto &b = body(geometryId);
        MeshOut out;
        std::map<std::array<double, 3>, std::uint32_t> index;
        // The looks of the faces, and the body's own (the most used one).
        std::map<Id, Look> looks;
        std::map<Look, std::size_t> usage;
        for (const auto &[faceId, face] : b.surface.faces) {
            (void)face;
            const auto look = lookOf(b, faceId);
            looks[faceId] = look;
            ++usage[look];
        }
        Look bodyLook;
        std::size_t best = 0;
        for (const auto &[look, used] : usage)
            if (used > best) {
                best = used;
                bodyLook = look;
            }
        const bool mixed = usage.size() > 1;
        std::map<std::array<float, 3>, std::uint32_t> colorIndex;
        const auto rgbOf = [&](const Look &look) {
            if (look.material)
                return doc.materials().at(look.material)->color;
            return look.colored ? look.rgb : b.color;
        };
        styleIndex = -1;
        if (bodyLook.material || bodyLook.colored) {
            auto found = styleOf.find(bodyLook);
            if (found == styleOf.end()) {
                StyleRecord style;
                if (bodyLook.material) {
                    const auto &material = *doc.materials().at(bodyLook.material);
                    style = {cleanText(material.name), material.color, material.opacity, true};
                } else {
                    char name[64];
                    std::snprintf(name, sizeof name, "SketchyUp colour #%02X%02X%02X", int(std::lround(bodyLook.rgb[0] * 255)),
                                  int(std::lround(bodyLook.rgb[1] * 255)), int(std::lround(bodyLook.rgb[2] * 255)));
                    style = {name, bodyLook.rgb, 1, false};
                }
                bool clipped = false;
                style.name = clipCodePoints(style.name, labelCodePoints, clipped);
                styles.push_back(style);
                found = styleOf.emplace(bodyLook, int(styles.size()) - 1).first;
            }
            styleIndex = found->second;
        }
        const float styleOpacity = styleIndex >= 0 ? styles[std::size_t(styleIndex)].opacity : 1.0f;
        for (const auto &[faceId, face] : b.surface.faces) {
            (void)face;
            std::vector<Triangle> triangles;
            try {
                triangles = b.surface.triangulateExact(faceId);
            } catch (const std::exception &) {
                ++count["facesNotTriangulated"];
                continue;
            }
            // The tessellator leaves out a loop vertex that lies on a straight edge (an edge split in the model, say), so the
            // face would no longer meet its neighbour's end to end. Such a vertex is put back by splitting the triangle whose
            // edge it lies on; the triangles keep their orientation and the mesh keeps every vertex of the face.
            {
                std::vector<Vec3> missing;
                std::set<std::array<double, 3>> corners;
                for (const auto &t : triangles)
                    for (const Vec3 &p : {t.a, t.b, t.c})
                        corners.insert({p.x, p.y, p.z});
                for (const auto &loop : b.surface.faces.at(faceId).loops)
                    for (const Id vertexId : loop) {
                        const Vec3 p = b.surface.vertices.at(vertexId);
                        if (!corners.contains({p.x, p.y, p.z}))
                            missing.push_back(p);
                    }
                for (const Vec3 p : missing) {
                    bool placed = false;
                    for (std::size_t k = 0; k < triangles.size() && !placed; ++k) {
                        const Triangle t = triangles[k];
                        const Vec3 corners[3] = {t.a, t.b, t.c};
                        for (int e = 0; e < 3 && !placed; ++e) {
                            const Vec3 u = corners[e], w = corners[(e + 1) % 3], other = corners[(e + 2) % 3];
                            const Vec3 edge = w - u;
                            const double edgeLength = length(edge);
                            if (edgeLength == 0)
                                continue;
                            const double along = dot(p - u, edge) / (edgeLength * edgeLength);
                            if (along <= 0 || along >= 1 || length(cross(p - u, edge)) / edgeLength > 1e-9)
                                continue;
                            // (u, w, other) becomes (u, p, other) and (p, w, other), whichever edge u-w is.
                            Triangle first = t, second = t;
                            const Vec3 firstCorners[3] = {u, p, other}, secondCorners[3] = {p, w, other};
                            // Restore the rotation of the corners so the winding is the original's.
                            auto assign = [&](Triangle &target, const Vec3 *c) {
                                const Vec3 rotated[3] = {c[0], c[1], c[2]};
                                // Triangle corners are (a, b, c) = corners[0..2]; the edge e started at corner e.
                                Vec3 out[3];
                                for (int i = 0; i < 3; ++i)
                                    out[(e + i) % 3] = rotated[i];
                                target.a = out[0];
                                target.b = out[1];
                                target.c = out[2];
                            };
                            assign(first, firstCorners);
                            assign(second, secondCorners);
                            triangles[k] = first;
                            triangles.push_back(second);
                            placed = true;
                        }
                    }
                    if (!placed)
                        ++count["faceVerticesNotInMesh"];
                }
            }
            const auto &look = looks.at(faceId);
            if ((look.material ? doc.materials().at(look.material)->opacity : 1.0f) != styleOpacity)
                ++count["faceOpacityNotWritten"];
            std::uint32_t colour = 0;
            if (mixed) {
                const auto rgb = rgbOf(look);
                auto found = colorIndex.find(rgb);
                if (found == colorIndex.end()) {
                    found = colorIndex.emplace(rgb, std::uint32_t(out.colors.size())).first;
                    out.colors.push_back(rgb);
                }
                colour = found->second;
            }
            const auto vertex = [&](Vec3 corner) {
                if (bake)
                    corner = residual.point(corner);
                const std::array<double, 3> key{corner.x == 0 ? 0.0 : corner.x, corner.y == 0 ? 0.0 : corner.y, corner.z == 0 ? 0.0 : corner.z};
                auto found = index.find(key);
                if (found == index.end()) {
                    found = index.emplace(key, std::uint32_t(out.vertices.size() / 3)).first;
                    out.vertices.insert(out.vertices.end(), key.begin(), key.end());
                }
                return found->second;
            };
            for (const auto &t : triangles) {
                auto a = vertex(t.a), c1 = vertex(t.b), c2 = vertex(t.c);
                if (flip)
                    std::swap(c1, c2);
                if (a == c1 || c1 == c2 || a == c2) {
                    ++count["degenerateTriangles"];
                    continue;
                }
                out.triangles.insert(out.triangles.end(), {a, c1, c2});
                if (mixed)
                    out.colorIndex.push_back(colour);
            }
        }
        hasTriangles = !out.triangles.empty();
        // Closed only when the native solid check passes.
        try {
            const auto status = diagnoseGeometry(b.surface, b.topology).solidStatus;
            out.closed = status == "solid" || status == "multiple_shells";
        } catch (const std::exception &) {
            out.closed = false;
        }
        return out;
    }

    void placeAndMesh() {
        nodes[0].emitted = Transform{};
        std::size_t totalTriangles = 0;
        for (const int at : order) {
            Node &node = nodes[std::size_t(at)];
            if (node.role == "project") {
                // No placement: the project is the world.
                continue;
            }
            Transform world;
            if (node.body)
                world = doc.worldTransform(node.body);
            const Node &parent = nodes[std::size_t(node.parent)];
            Transform relative;
            if (node.body) {
                const bool nativeChild = parent.body && parent.role != "project" && parent.exactFrame && body(node.body).parent == parent.body;
                relative = nativeChild ? body(node.body).transform : parent.emitted.inverse() * world;
            }
            const Transform rigid = rigidPart(relative);
            node.emitted = parent.emitted * rigid;
            node.placement = placementOf(rigid);
            node.exactFrame = nearIdentity(node.emitted.inverse() * world);
            if (node.geometry) {
                Transform residual = node.emitted.inverse() * (node.geometry == node.body ? world : doc.worldTransform(node.geometry));
                const bool bake = !nearIdentity(residual);
                const bool flip = bake && residual.determinant() < 0;
                int style = -1;
                bool hasTriangles = false;
                auto mesh = makeMesh(node.geometry, residual, bake, flip, style, hasTriangles);
                if (hasTriangles) {
                    if (mesh.vertices.size() / 3 > maxMeshVertices || mesh.triangles.size() / 3 > maxMeshTriangles)
                        refuse("meshTooLarge", "The body \"" + userText(body(node.geometry).name) + "\" has more vertices or triangles than the IFC package takes (" +
                                                   std::to_string(maxMeshVertices) + " and " + std::to_string(maxMeshTriangles) + ").");
                    totalTriangles += mesh.triangles.size() / 3;
                    node.mesh = int(meshes.size());
                    node.style = style;
                    meshes.push_back(std::move(mesh));
                } else {
                    ++count["bodiesWithoutTriangles"];
                }
            }
        }
        (void)totalTriangles;
    }

    // ---- 4. GlobalIds ----
    qint64 kept = 0, derivedNew = 0, replacedDuplicate = 0, replacedInvalid = 0;
    void assignGlobalIds() {
        std::set<std::string> used;
        std::vector<std::size_t> claimants;
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].body && nodes[i].hasProvenanceGuid)
                claimants.push_back(i);
        std::stable_sort(claimants.begin(), claimants.end(), [&](std::size_t a, std::size_t b) { return nodes[a].body < nodes[b].body; });
        std::set<std::size_t> replaced;
        for (const auto i : claimants) {
            Node &node = nodes[i];
            if (!validIfcGlobalId(node.candidateGuid)) {
                replaced.insert(i);
                ++replacedInvalid;
                location("globalIdReplaced", QJsonObject{{"bodyId", QString::number(node.body)},
                                              {"name", QString::fromStdString(userText(node.name))},
                                              {"reason", "invalid"},
                                              {"message", "The GlobalId of this body is not a valid IFC identifier; it was given a derived one."}});
            } else if (!used.insert(node.candidateGuid).second) {
                replaced.insert(i);
                ++replacedDuplicate;
                location("globalIdReplaced", QJsonObject{{"bodyId", QString::number(node.body)},
                                              {"name", QString::fromStdString(userText(node.name))},
                                              {"reason", "duplicate"},
                                              {"message", "Another body already carries this GlobalId (a copy of an imported body does); this one was given a derived one."}});
            } else {
                node.guid = node.candidateGuid;
                ++kept;
            }
        }
        for (auto &node : nodes) {
            if (!node.guid.empty())
                continue;
            if (!replaced.contains(std::size_t(&node - nodes.data())))
                ++derivedNew;
            const std::string kind = node.body ? "body" : node.role;
            for (int attempt = 0;; ++attempt) {
                auto guid = derivedIfcGlobalId(doc.identity(), kind, node.body, attempt);
                if (used.insert(guid).second) {
                    node.guid = guid;
                    break;
                }
            }
        }
    }

    // ---- 5. The numbers ----
    std::map<std::string, qint64> notWrittenCounts;
    qint64 items = 0, writtenGroups = 0, writtenBodies = 0, writtenPlacements = 0, notWrittenTotal = 0;

    void account() {
        for (const Id id : preorder) {
            const auto &b = body(id);
            const bool placement = placementRoots.contains(id);
            switch (disposition.at(id)) {
            case Disposition::Written:
                if (placement)
                    ++writtenPlacements;
                else if (b.kind == BodyKind::Group)
                    ++writtenGroups;
                else
                    ++writtenBodies;
                break;
            case Disposition::Terrain: ++notWrittenCounts["terrain"]; break;
            case Disposition::TerrainTile: ++notWrittenCounts["terrainTiles"]; break;
            case Disposition::ReferenceImage: ++notWrittenCounts["referenceImages"]; break;
            case Disposition::EmptyGroup: ++notWrittenCounts["emptyGroups"]; break;
            case Disposition::NoFaces: ++notWrittenCounts["bodiesWithoutFaces"]; break;
            case Disposition::Unvisited: refuse("internal", "A body was not classified.");
            }
            if (disposition.at(id) != Disposition::Terrain && disposition.at(id) != Disposition::TerrainTile &&
                disposition.at(id) != Disposition::ReferenceImage) {
                notWrittenCounts["guides"] += qint64(b.guides.size());
                notWrittenCounts["looseEdges"] += qint64(b.surface.wires.size());
                notWrittenCounts["curves"] += qint64(b.curves.size());
            }
        }
        notWrittenCounts["annotations"] += qint64(doc.annotations().size());
        notWrittenCounts["sections"] += qint64(doc.sections().size());
        notWrittenCounts["scenes"] += qint64(doc.scenes().size());
        notWrittenCounts["sheets"] += qint64(doc.drawingSheets().size());
        for (const auto &[id, definition] : doc.definitions())
            if (definition->parameters)
                ++notWrittenCounts["componentParameters"];
        for (const auto &[id, material] : doc.materials())
            if (material->asset)
                ++notWrittenCounts["textures"];
        for (auto it = notWrittenCounts.begin(); it != notWrittenCounts.end();)
            it = it->second == 0 ? notWrittenCounts.erase(it) : std::next(it);
        for (const auto &[kind, number] : notWrittenCounts)
            notWrittenTotal += number;
        // The universe is counted again, independently, from the document's own tables.
        items = qint64(doc.bodies().size());
        for (const auto &[id, body] : doc.bodies()) {
            const auto d = disposition.at(id);
            if (d != Disposition::Terrain && d != Disposition::TerrainTile && d != Disposition::ReferenceImage)
                items += qint64(body->guides.size() + body->surface.wires.size() + body->curves.size());
        }
        items += qint64(doc.annotations().size() + doc.sections().size() + doc.scenes().size() + doc.drawingSheets().size());
        for (const auto &[id, definition] : doc.definitions())
            items += definition->parameters ? 1 : 0;
        for (const auto &[id, material] : doc.materials())
            items += material->asset ? 1 : 0;
        if (items != writtenGroups + writtenBodies + writtenPlacements + notWrittenTotal)
            refuse("internal", "The export's accounting does not balance (" + std::to_string(items) + " items against " +
                                   std::to_string(writtenGroups + writtenBodies + writtenPlacements + notWrittenTotal) + ").");
    }

    QJsonObject georeference(std::optional<QJsonObject> &packageBlock) {
        QJsonObject block;
        const auto &site = doc.site();
        if (!site) {
            block = {{"result", "notApplicable"}, {"reason", "noSiteOrigin"}};
            return block;
        }
        block["siteOrigin"] = QJsonObject{{"eastings", site->easting}, {"northings", site->northing}, {"elevation", site->elevation}};
        if (site->crs.empty()) {
            block["result"] = "notWritten";
            block["reason"] = "noCrsLabel";
        } else if (!isAuthorityCode(site->crs)) {
            block["result"] = "notWritten";
            block["reason"] = "crsNotAuthorityCode";
            block["crs"] = QString::fromStdString(userText(site->crs));
        } else {
            block["result"] = "written";
            block["crs"] = QString::fromStdString(site->crs);
            packageBlock = QJsonObject{{"crs", QString::fromStdString(site->crs)},
                                       {"eastings", site->easting},
                                       {"northings", site->northing},
                                       {"height", site->elevation}};
        }
        return block;
    }
};

QString hashHex(const QByteArray &bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }

void putU32(QByteArray &out, quint32 v) {
    for (int i = 0; i < 4; ++i)
        out.append(char((v >> (8 * i)) & 0xFF));
}
void putU64(QByteArray &out, quint64 v) {
    for (int i = 0; i < 8; ++i)
        out.append(char((v >> (8 * i)) & 0xFF));
}
void putF64(QByteArray &out, double v) {
    quint64 bits;
    std::memcpy(&bits, &v, 8);
    putU64(out, bits);
}
void putF32(QByteArray &out, float v) {
    quint32 bits;
    std::memcpy(&bits, &v, 4);
    putU32(out, bits);
}

// A float as the double nearest to its shortest decimal form, so a colour of 0.73f is written 0.73.
double shortDouble(float v) {
    char text[40];
    const auto end = std::to_chars(text, text + sizeof text, v).ptr;
    return std::strtod(std::string(text, end).c_str(), nullptr);
}
QJsonArray propertyJson(const Prop &p) {
    QJsonValue type, value;
    if (const auto *flag = std::get_if<bool>(&p.value)) {
        type = "b";
        value = *flag;
    } else if (const auto *number = std::get_if<double>(&p.value)) {
        type = "d";
        value = *number;
    } else {
        type = "s";
        value = QString::fromStdString(std::get<std::string>(p.value));
    }
    return QJsonArray{QString::fromStdString(p.set), QString::fromStdString(p.name), type, value};
}

QString timestampText(qint64 epoch) { return QDateTime::fromSecsSinceEpoch(epoch, QTimeZone::utc()).toString("yyyy-MM-ddTHH:mm:ss"); }

qint64 resolveTimestamp(const IfcExportOptions &options, QString &source) {
    if (options.timestamp) {
        source = "option";
        if (*options.timestamp < 0 || *options.timestamp > 253402300799LL)
            refuse("badTimestamp", "The time stamped into the file must lie between 1970 and 9999.");
        return *options.timestamp;
    }
    const auto fromEnvironment = qgetenv("SOURCE_DATE_EPOCH");
    if (!fromEnvironment.isEmpty()) {
        bool ok{};
        const auto value = fromEnvironment.toLongLong(&ok);
        if (!ok || value < 0 || value > 253402300799LL)
            refuse("badTimestamp", "SOURCE_DATE_EPOCH is not a number of seconds between 1970 and 9999.");
        source = "SOURCE_DATE_EPOCH";
        return value;
    }
    source = "clock";
    return QDateTime::currentSecsSinceEpoch();
}

QJsonObject countsObject(const std::map<std::string, qint64> &counts) {
    QJsonObject object;
    for (const auto &[key, value] : counts)
        object[QString::fromStdString(key)] = value;
    return object;
}

} // namespace

bool isAuthorityCode(const std::string &label) {
    static const QRegularExpression pattern(R"(^[A-Za-z]{2,16}:[A-Za-z0-9][A-Za-z0-9_.\-]{0,31}\z)");
    return pattern.match(QString::fromStdString(label)).hasMatch();
}

bool validIfcGlobalId(const std::string &guid) {
    if (guid.size() != 22)
        return false;
    for (const char c : guid)
        if (c == '\0' || std::strchr(alphabet, c) == nullptr)
            return false;
    return guid[0] >= '0' && guid[0] <= '3';
}

std::string derivedIfcGlobalId(const std::string &identity, const std::string &kind, Id id, int attempt) {
    std::string seed = "SketchyUp IFC GlobalId v1";
    seed.push_back('\0');
    seed += identity;
    seed.push_back('\0');
    seed += kind;
    seed.push_back('\0');
    seed += std::to_string(id);
    if (attempt > 0) {
        seed.push_back('\0');
        seed += std::to_string(attempt);
    }
    const auto digest = QCryptographicHash::hash(QByteArray(seed.data(), qsizetype(seed.size())), QCryptographicHash::Sha256);
    std::string out;
    int bit = 0;
    for (int group = 0; group < 22; ++group) {
        const int width = group == 0 ? 2 : 6;
        unsigned value = 0;
        for (int k = 0; k < width; ++k, ++bit)
            value = (value << 1) | ((static_cast<unsigned char>(digest[bit / 8]) >> (7 - bit % 8)) & 1u);
        out.push_back(alphabet[value]);
    }
    return out;
}

namespace {
IfcExportCapture captureChecked(const Document &document, const IfcExportOptions &options, const QString &fileName) {
    if (document.readSnapshotBytes() > 256u * 1024 * 1024)
        refuse("documentTooLarge", "The document is too large to export (over the 256 MiB snapshot budget).");
    Capture c(document);
    c.index();
    c.classify();
    c.findFolds();
    c.buildTree();
    c.linearize();
    if (c.nodes.size() > maxNodes)
        refuse("packageTooLarge", "The model has more groups and bodies than the IFC package takes (" + std::to_string(maxNodes) + ").");
    c.placeAndMesh();
    if (c.styles.size() > maxStyles)
        refuse("packageTooLarge", "The model has more distinct colours and materials than the IFC package takes (" + std::to_string(maxStyles) + ").");
    c.assignGlobalIds();
    c.account();

    // Renumber to the linear order.
    std::vector<int> newIndex(c.nodes.size(), -1);
    for (std::size_t i = 0; i < c.order.size(); ++i)
        newIndex[std::size_t(c.order[i])] = int(i);

    std::optional<QJsonObject> georeferenceBlock;
    QJsonObject georeference = c.georeference(georeferenceBlock);

    QString timestampSource;
    const qint64 epoch = resolveTimestamp(options, timestampSource);

    // The mesh chunk and the manifest.
    QByteArray meshChunk;
    QJsonArray meshes;
    qint64 vertexTotal = 0, triangleTotal = 0, closedMeshes = 0, colourMaps = 0;
    for (const auto &mesh : c.meshes) {
        for (const double v : mesh.vertices)
            putF64(meshChunk, v);
        for (const auto i : mesh.triangles)
            putU32(meshChunk, i);
        if (!mesh.colors.empty()) {
            for (const auto i : mesh.colorIndex)
                putU32(meshChunk, i);
            for (const auto &rgb : mesh.colors)
                for (const float v : rgb)
                    putF32(meshChunk, v);
        }
        meshes.append(QJsonObject{{"vertices", qint64(mesh.vertices.size() / 3)},
                                  {"triangles", qint64(mesh.triangles.size() / 3)},
                                  {"colors", qint64(mesh.colors.size())},
                                  {"closed", mesh.closed}});
        vertexTotal += qint64(mesh.vertices.size() / 3);
        triangleTotal += qint64(mesh.triangles.size() / 3);
        closedMeshes += mesh.closed ? 1 : 0;
        colourMaps += mesh.colors.empty() ? 0 : 1;
        if (std::size_t(meshChunk.size()) > maxMeshBytes)
            refuse("packageTooLarge", "The export's geometry is larger than the package bound (128 MiB).");
    }
    QJsonArray styles;
    qint64 materialStyles = 0;
    for (const auto &style : c.styles) {
        styles.append(QJsonObject{{"name", QString::fromStdString(style.name)},
                                  {"color", QJsonArray{shortDouble(style.color[0]), shortDouble(style.color[1]), shortDouble(style.color[2])}},
                                  {"opacity", shortDouble(style.opacity)},
                                  {"material", style.material}});
    }
    // A material is written (an IfcMaterial with its definition) only for a style that an element or an assembly uses.
    {
        std::set<int> used;
        for (const auto &node : c.nodes)
            if ((node.role == "assembly" || node.role == "element") && node.mesh >= 0 && node.style >= 0 && c.styles[std::size_t(node.style)].material)
                used.insert(node.style);
        materialStyles = qint64(used.size());
    }

    QJsonArray nodes;
    qint64 hiddenNodes = 0, propertiesTotal = 0, propertySetsTotal = 0;
    std::map<std::string, qint64> roles;
    for (const int at : c.order) {
        const auto &node = c.nodes[std::size_t(at)];
        QJsonArray props;
        for (const auto &p : node.props)
            props.append(propertyJson(p));
        propertiesTotal += qint64(node.props.size());
        {
            std::set<std::string> sets;
            for (const auto &p : node.props)
                sets.insert(p.set);
            propertySetsTotal += qint64(sets.size());
        }
        const bool hidden = node.hidden && node.mesh >= 0;
        hiddenNodes += hidden ? 1 : 0;
        ++roles[node.role];
        QJsonObject entry{{"id", double(node.body)},
                          {"role", QString::fromStdString(node.role)},
                          {"class", QString::fromStdString(node.requestedClass)},
                          {"guid", QString::fromStdString(node.guid)},
                          {"name", QString::fromStdString(node.name)},
                          {"longName", QString::fromStdString(node.longName)},
                          {"objectType", QString::fromStdString(node.objectType)},
                          {"predefinedType", QString::fromStdString(node.predefinedType)},
                          {"parent", node.parent < 0 ? -1 : newIndex[std::size_t(node.parent)]},
                          {"placement", QJsonArray{node.placement[0], node.placement[1], node.placement[2], node.placement[3], node.placement[4],
                                                   node.placement[5], node.placement[6], node.placement[7], node.placement[8]}},
                          {"mesh", node.mesh},
                          {"style", node.style},
                          {"hidden", hidden},
                          {"props", props}};
        if (node.elevation)
            entry["elevation"] = *node.elevation;
        nodes.append(entry);
    }

    QJsonObject manifest{{"format", "sketchyup-ifc-export-package"},
                         {"version", 1},
                         {"meshSha256", hashHex(meshChunk)},
                         {"documentId", QString::fromStdString(document.identity())},
                         {"application", QJsonObject{{"name", "SketchyUp"}, {"version", QStringLiteral(SKETCHYUP_VERSION)}}},
                         {"timestamp", timestampText(epoch)},
                         {"fileName", fileName},
                         {"georeference", georeferenceBlock ? QJsonValue(*georeferenceBlock) : QJsonValue()},
                         {"styles", styles},
                         {"meshes", meshes},
                         {"nodes", nodes}};
    const auto manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (std::size_t(manifestBytes.size()) > maxManifestBytes)
        refuse("packageTooLarge", "The export's description is larger than the package bound (32 MiB).");
    QByteArray package("SKIFCX01", 8);
    putU32(package, quint32(manifestBytes.size()));
    putU32(package, 0);
    putU64(package, quint64(meshChunk.size()));
    package += manifestBytes;
    package += meshChunk;

    // ---- The plan (and the start of the report) ----
    QJsonObject written{{"groups", c.writtenGroups},
                        {"bodies", c.writtenBodies},
                        {"placements", c.writtenPlacements},
                        {"nodes", qint64(c.nodes.size())},
                        {"geometryNodes", qint64(c.meshes.size())},
                        {"faces", [&] {
                             qint64 faces = 0;
                             for (const auto &node : c.nodes)
                                 if (node.mesh >= 0)
                                     faces += qint64(document.bodies().at(node.geometry)->surface.faces.size());
                             return faces;
                         }()},
                        {"triangles", triangleTotal},
                        {"vertices", vertexTotal},
                        {"closedFaceSets", closedMeshes},
                        {"colourMaps", colourMaps},
                        {"styles", qint64(c.styles.size())},
                        {"materials", materialStyles},
                        {"properties", propertiesTotal},
                        {"propertySets", propertySetsTotal},
                        {"hidden", hiddenNodes},
                        {"foldedBodies", qint64(c.folded.size())}};
    QJsonObject spatial{{"projects", 1},
                        {"sites", roles["site"]},
                        {"buildings", roles["building"]},
                        {"storeys", roles["storey"]},
                        {"spaces", roles["space"]},
                        {"assemblies", roles["assembly"]},
                        {"elements", roles["element"]},
                        {"synthesizedSite", c.count["synthesizedSite"] != 0},
                        {"synthesizedBuilding", c.count["synthesizedBuilding"] != 0},
                        {"storeysInvented", 0}};
    QJsonObject globalIds{{"kept", c.kept},
                          {"derived", c.derivedNew},
                          {"replaced", c.replacedDuplicate + c.replacedInvalid},
                          {"replacedDuplicate", c.replacedDuplicate},
                          {"replacedInvalid", c.replacedInvalid}};
    QJsonObject accounting{{"items", c.items},
                           {"written", c.writtenGroups + c.writtenBodies},
                           {"writtenAsCopies", c.writtenPlacements},
                           {"notWritten", c.notWrittenTotal},
                           {"balanced", true}};

    QJsonArray noticeList;
    const auto notice = [&](const char *code, qint64 number, const QString &message) {
        if (number > 0)
            noticeList.append(QJsonObject{{"code", code}, {"count", number}, {"message", message}});
    };
    if (georeference["result"] == "notWritten")
        notice("georeferenceNotWritten", 1,
               georeference["reason"] == "noCrsLabel"
                   ? "The site origin is set but has no coordinate system label, so neither the coordinate system nor the map conversion was written and the origin is lost in the file. Set the label to an authority code such as EPSG:32760 in File, Site settings."
                   : "The site origin's label is not an authority code such as EPSG:32760, so neither the coordinate system nor the map conversion was written and the origin is lost in the file. Change the label in File, Site settings.");
    notice("globalIdReplaced", c.replacedDuplicate + c.replacedInvalid,
           "Identifiers that were not valid, or were already used by another body, were replaced by identifiers derived from the model.");
    notice("realsWithoutUnits", 1, "Numbers are written as plain reals; no unit meaning survives.");
    notice("componentsExpanded", c.writtenPlacements, "Component placements were written as separate geometry; instancing is not written.");
    notice("hiddenWritten", hiddenNodes, "Hidden bodies are written, on a layer that is switched off.");
    notice("nameClipped", c.notices["nameClipped"], "Names longer than 255 characters were shortened.");
    notice("provenancePropertiesNotWritten", c.count["provenancePropertiesNotWritten"],
           "Properties that record where a body came from in an IFC file (ifc.*) are not written; they are not IFC properties.");
    notice("propertyKeyCollisions", c.count["propertyKeyCollisions"], "Two properties mapped to the same set and name; the first was written.");
    notice("spatialClassNotUsed", c.notices["spatialClassNotUsed"],
           "A group with a spatial IFC class (site, building, storey) was not where that class belongs, so it was written as an assembly.");
    notice("facesNotTriangulated", c.count["facesNotTriangulated"], "Faces that could not be triangulated were left out.");
    notice("faceVerticesNotInMesh", c.count["faceVerticesNotInMesh"], "A vertex of a face could not be kept in the triangles of its face (it lies on no edge of them).");
    notice("degenerateTriangles", c.count["degenerateTriangles"], "Triangles that collapsed to a line were left out.");
    notice("faceOpacityNotWritten", c.count["faceOpacityNotWritten"],
           "A face with an opacity different from its body's was written with the body's opacity; per-face opacity is not carried.");
    notice("bodiesWithoutTriangles", c.count["bodiesWithoutTriangles"], "Bodies without usable triangles were written without a shape.");
    notice("foldedGeometry", qint64(c.folded.size()),
           "A body that only carried the geometry of its parent group (as an IFC import makes them) was written as that group's own shape.");

    // Appearance and detail the file cannot carry, counted next to the identity (ADR 0171 section 3.8: "not exported, counted").
    QJsonObject losses;
    {
        qint64 editableText = 0, edgeAppearance = 0, textureMappings = 0, backMaterials = 0;
        for (const auto &[id, b] : document.bodies()) {
            const auto d = c.disposition.at(id);
            if (d != Disposition::Written)
                continue;
            editableText += b->textSource ? 1 : 0;
            edgeAppearance += qint64(b->edgeAppearances.size());
            textureMappings += qint64(b->faceTextureMappings.size());
            for (const auto &[faceId, face] : b->surface.faces) {
                (void)face;
                const auto sides = faceMaterials(*b, faceId);
                backMaterials += sides.back && sides.back != sides.front ? 1 : 0;
            }
        }
        std::set<Id> used;
        for (const auto &[root, instance] : document.instances())
            used.insert(instance->definition);
        qint64 unplaced = 0;
        for (const auto &[id, definition] : document.definitions())
            unplaced += used.contains(id) ? 0 : 1;
        for (const auto &[key, number] : std::initializer_list<std::pair<const char *, qint64>>{{"editableText", editableText}, {"edgeAppearance", edgeAppearance},
                                                                                              {"faceTextureMappings", textureMappings}, {"backFaceMaterials", backMaterials},
                                                                                              {"unplacedDefinitions", unplaced}})
            if (number > 0)
                losses[key] = number;
    }
    // What the report asks the person to look at: a georeference that was not written, and each identifier that was replaced.
    const qint64 attention = (georeference["result"] == "notWritten" ? 1 : 0) + c.replacedDuplicate + c.replacedInvalid;
    QJsonObject plan{{"apiVersion", 1},
                     {"attention", attention},
                     {"format", "IFC"},
                     {"direction", "export"},
                     {"spatial", spatial},
                     {"written", written},
                     {"notWritten", countsObject(c.notWrittenCounts)},
                     {"accounting", accounting},
                     {"losses", losses},
                     {"georeference", georeference},
                     {"globalIds", globalIds},
                     {"notices", noticeList},
                     {"locations", c.locations}};
    QJsonObject totals;
    for (const auto &[kind, number] : c.locationCounts)
        totals[QString::fromStdString(kind)] = number;
    plan["locationTotals"] = totals;
    // Nothing to write: no group, no body with faces. The refusal carries the counts of what was left out.
    if (c.writtenGroups + c.writtenBodies + c.writtenPlacements == 0)
        refuse("emptyModel", "Nothing to export: the model has no group or body with faces.", plan);
    return {package, plan};
}
} // namespace

IfcExportCapture captureIfcExport(const Document &document, const IfcExportOptions &options, const QString &fileName) {
    try {
        return captureChecked(document, options, fileName);
    } catch (const IfcExportError &) {
        throw;
    } catch (const std::exception &error) {
        // A transform or a face the writer cannot take is a refusal with a reason, not a crash.
        refuse("captureFailed", std::string("The model could not be captured for export: ") + error.what());
    }
}

QJsonObject planIfcExport(const Document &document) {
    IfcExportOptions options;
    options.timestamp = 0;
    return captureIfcExport(document, options, "plan.ifc").plan;
}

namespace {
// The report's bounds (ADR 0171 section 4): 64 KiB, 64 kinds, 32 locations a kind, 256-byte messages; counts stay
// complete.
QString clipMessage(QString message) {
    while (message.toUtf8().size() > 256)
        message.chop(1);
    return message;
}
void boundExportReport(QJsonObject &report) {
    auto notices = report["notices"].toArray();
    QJsonArray keptNotices;
    for (const auto &entry : notices) {
        if (keptNotices.size() >= 64)
            break;
        auto notice = entry.toObject();
        notice["message"] = clipMessage(notice["message"].toString());
        keptNotices.append(notice);
    }
    report["notices"] = keptNotices;
    std::map<QString, int> perKind;
    QJsonArray locations;
    for (const auto &entry : report["locations"].toArray()) {
        auto location = entry.toObject();
        const auto kind = location["kind"].toString();
        if ((perKind.size() >= 64 && !perKind.contains(kind)) || ++perKind[kind] > 32)
            continue;
        location["message"] = clipMessage(location["message"].toString());
        locations.append(location);
    }
    report["locations"] = locations;
    {
        QJsonArray fallbacks;
        for (const auto &entry : report["classFallbacks"].toArray()) {
            if (fallbacks.size() >= 64)
                break;
            auto fallback = entry.toObject();
            for (const char *key : {"requested", "written", "reason"})
                fallback[key] = fallback[key].toString().left(64);
            fallbacks.append(fallback);
        }
        if (report.contains("classFallbacks"))
            report["classFallbacks"] = fallbacks;
    }
    if (report["file"].isObject()) {
        auto file = report["file"].toObject();
        auto classes = file["classes"].toObject();
        if (classes.size() > 128) {
            QJsonObject kept;
            qint64 other = 0;
            int number = 0;
            for (auto it = classes.begin(); it != classes.end(); ++it)
                if (++number <= 128)
                    kept[it.key()] = it.value();
                else
                    other += qint64(it.value().toDouble());
            kept["other"] = other;
            file["classes"] = kept;
        }
        report["file"] = file;
    }
    const auto size = [&] { return QJsonDocument(report).toJson(QJsonDocument::Compact).size(); };
    for (const int perKindLimit : {8, 2, 0}) {
        if (size() <= 64 * 1024)
            break;
        std::map<QString, int> seen;
        QJsonArray fewer;
        for (const auto &entry : report["locations"].toArray())
            if (++seen[entry.toObject()["kind"].toString()] <= perKindLimit)
                fewer.append(entry);
        report["locations"] = fewer;
    }
}

// The report holds no host paths: any absolute path the helper's log names (its own, a library's) becomes <path>.
QString scrubPaths(QString text) {
    static const QRegularExpression absolute(R"((?<![\w<])/[\w.\-+@%~/]{2,})");
    return text.replace(absolute, "<path>");
}

qint64 countOf(const QJsonObject &object, const char *key) { return qint64(object[key].toDouble(-1)); }
} // namespace

IfcExportResult exportIfcFile(const Document &document, const QString &path, const IfcExportOptions &options,
                              const IfcWorkerOptions &worker) {
    const auto progress = [&](const char *phase) {
        if (worker.progress)
            worker.progress(phase, 0);
    };
    const auto checkCancelled = [&] {
        if (worker.cancelled && worker.cancelled())
            refuse("cancelled", "The export was cancelled; nothing was written.");
    };
    // The destination first: nothing is built for a path that cannot be used.
    {
        const QFileInfo info(path);
        if (path.isEmpty() || info.fileName().isEmpty())
            refuse("destination", "Choose a new file for the IFC export.");
        if (info.exists() || info.isSymLink())
            refuse("destinationExists", "That file already exists. An existing file is never replaced; choose a new name.");
        if (info.dir().canonicalPath().isEmpty())
            refuse("destination", "The folder for the IFC export does not exist.");
    }
    const QString workerPath = findIfcWorker(worker.workerPath);
    if (workerPath.isEmpty())
        refuse("workerMissing",
               "The IFC helper (sketchyup-ifc-worker) is not installed or cannot be run; install the sketchyup-ifc package. "
               "Nothing else is affected.");
    progress("capture");
    // One time for the header, the owner history and the report.
    QString timestampSource;
    IfcExportOptions resolved = options;
    resolved.timestamp = resolveTimestamp(options, timestampSource);
    auto capture = captureIfcExport(document, resolved, QString::fromStdString(clipBytes(cleanText(QFileInfo(path).fileName().toStdString()), 255)));
    checkCancelled();

    const QString root = worker.scratchRoot.isEmpty() ? QDir::tempPath() : worker.scratchRoot;
    QTemporaryDir directory(QDir(root).filePath(QString(privatePrefix) + "XXXXXX"));
    if (!directory.isValid())
        refuse("scratch", "A private working directory could not be created.");
    QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString packagePath = QDir(directory.path()).filePath("export.skifcx");
    const QString outputPath = QDir(directory.path()).filePath("export.ifc");
    {
        QFile file(packagePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(capture.package) != capture.package.size() || !file.flush())
            refuse("scratch", "The export package could not be written to the private working directory.");
        file.close();
        QFile::setPermissions(packagePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    progress("build");
    QStringList exportArguments{"--export", packagePath, "--output", outputPath};
    QStringList limits;
    if (worker.addressSpaceBytes > 0)
        limits << "--rlimit-as" << QString::number(worker.addressSpaceBytes);
    limits << "--rlimit-cpu" << QString::number(qint64(worker.deadlineSeconds) * 4);
    exportArguments += limits;
    exportArguments << "--rlimit-fsize" << QString::number(qint64(maxIfcBytes) + 1024 * 1024);
    const auto run = runIfcWorker(workerPath, directory.path(), exportArguments, worker);
    auto report = capture.plan;
    if (run.kind == IfcWorkerRun::Cancelled)
        refuse("cancelled", "The export was cancelled; nothing was written.");
    if (run.kind == IfcWorkerRun::Refused) {
        static const QRegularExpression identifier("^[A-Za-z][A-Za-z0-9]{0,39}\\z");
        static const QRegularExpression control("[\\x00-\\x1f\\x7f]");
        QString code = run.result["code"].toString();
        if (!identifier.match(code).hasMatch())
            code = "workerRefused";
        QString message = run.result["message"].toString().left(240);
        message.replace(control, " ");
        while (message.toUtf8().size() > 240)
            message.chop(1);
        refuse(code.toStdString(), message.toStdString(), report);
    }
    if (run.kind != IfcWorkerRun::Finished) {
        const QFileInfo produced(outputPath);
        if (produced.exists() && produced.size() >= qint64(maxIfcBytes))
            refuse("outputTooLarge", "The IFC file would be larger than 256 MiB; nothing was written.", report);
        // (The report holds no host paths: the private directory is named for what it is.)
        report["logTail"] = scrubPaths(QString(run.logTail).replace(directory.path(), "<private directory>").replace(root, "<temporary directory>"));
        refuse("workerFailed", "The IFC helper stopped (" + run.reason.toStdString() + "); nothing was written.", report);
    }
    progress("check");
    checkCancelled();
    const QFileInfo produced(outputPath);
    if (!produced.isFile() || produced.isSymLink() || produced.size() <= 0)
        refuse("workerOutput", "The IFC helper produced no file.", report);
    if (produced.size() > qint64(maxIfcBytes))
        refuse("outputTooLarge", "The IFC file would be larger than 256 MiB; nothing was written.", report);
    QByteArray bytes;
    {
        QFile file(outputPath);
        if (!file.open(QIODevice::ReadOnly))
            refuse("workerOutput", "The IFC helper's file cannot be read.", report);
        bytes = file.read(qint64(maxIfcBytes) + 1);
    }
    if (bytes.size() != produced.size() || !bytes.startsWith("ISO-10303-21;") || !bytes.right(64).trimmed().endsWith("END-ISO-10303-21;"))
        refuse("workerOutput", "The IFC helper's file is not a complete STEP file.", report);

    // The file that is about to be published is read again by the helper's IfcParse and every attribute is checked against the
    // schema (not an independent validator: the same library wrote it).
    {
        const auto checked = runIfcWorker(workerPath, directory.path(), QStringList{"--check", outputPath} + limits, worker);
        if (checked.kind == IfcWorkerRun::Cancelled)
            refuse("cancelled", "The export was cancelled; nothing was written.");
        if (checked.kind != IfcWorkerRun::Finished && checked.result["status"] != "invalid") {
            // The check itself stopped (a crash, the deadline, memory): that is the helper failing, not the file.
            auto failed = report;
            failed["logTail"] = scrubPaths(QString(checked.logTail).replace(directory.path(), "<private directory>").replace(root, "<temporary directory>"));
            refuse("workerFailed", "The IFC helper stopped while checking its file (" + checked.reason.toStdString() + "); nothing was written.", failed);
        }
        if (checked.kind != IfcWorkerRun::Finished) {
            QStringList issues;
            for (const auto &issue : checked.result["issues"].toArray())
                if (issues.size() < 8)
                    issues << issue.toString().left(200);
            auto failed = report;
            failed["checkIssues"] = QJsonArray::fromStringList(issues);
            refuse("workerOutput", "The IFC file does not pass the helper's own schema check; nothing was written. " + issues.value(0).toStdString(), failed);
        }
        if (checked.result["entities"] != run.result["entities"])
            refuse("workerMismatch", "The IFC file read back has a different number of entities than the helper wrote.", report);
    }
    // What the helper wrote must equal what the document said it would.
    const auto counts = run.result["counts"].toObject();
    const auto planned = capture.plan["written"].toObject();
    const auto spatial = capture.plan["spatial"].toObject();
    QStringList mismatches;
    const auto expect = [&](const char *name, qint64 got, qint64 want) {
        if (got != want)
            mismatches << QString("%1: file %2, document %3").arg(name).arg(got).arg(want);
    };
    expect("nodes", countOf(counts, "projects") + countOf(counts, "sites") + countOf(counts, "buildings") + countOf(counts, "storeys") +
                        countOf(counts, "spaces") + countOf(counts, "assemblies") + countOf(counts, "elements"),
           countOf(planned, "nodes"));
    expect("sites", countOf(counts, "sites"), qint64(spatial["sites"].toDouble()));
    expect("buildings", countOf(counts, "buildings"), qint64(spatial["buildings"].toDouble()));
    expect("storeys", countOf(counts, "storeys"), qint64(spatial["storeys"].toDouble()));
    expect("spaces", countOf(counts, "spaces"), qint64(spatial["spaces"].toDouble()));
    expect("representations", countOf(counts, "representations"), countOf(planned, "geometryNodes"));
    expect("triangles", countOf(counts, "triangles"), countOf(planned, "triangles"));
    expect("vertices", countOf(counts, "vertices"), countOf(planned, "vertices"));
    expect("styles", countOf(counts, "styles"), countOf(planned, "styles"));
    expect("colourMaps", countOf(counts, "colourMaps"), countOf(planned, "colourMaps"));
    expect("closedFaceSets", countOf(counts, "closedFaceSets"), countOf(planned, "closedFaceSets"));
    expect("materials", countOf(counts, "materials"), countOf(planned, "materials"));
    expect("propertySets", countOf(counts, "propertySets"), countOf(planned, "propertySets"));
    expect("properties", countOf(counts, "properties"), countOf(planned, "properties"));
    expect("hidden", countOf(counts, "layerAssigned"), countOf(planned, "hidden"));
    expect("georeference", run.result["georeference"].toString() == "written" ? 1 : 0,
           capture.plan["georeference"].toObject()["result"].toString() == "written" ? 1 : 0);
    if (!mismatches.isEmpty())
        refuse("workerMismatch", ("The IFC file does not match the model: " + mismatches.join("; ")).toStdString(), report);

    // The report: the plan, confirmed, plus what only the run knows.
    const auto library = run.result["library"].toObject();
    report["engine"] = QJsonObject{{"library", library["name"]},
                                   {"version", library["version"]},
                                   {"commit", library["commit"]},
                                   {"kernel", "none"},
                                   {"completed", true},
                                   {"deadlineSeconds", worker.deadlineSeconds},
                                   {"addressSpaceBytes", double(worker.addressSpaceBytes)}};
    const qint64 epoch = *resolved.timestamp;
    report["target"] = QJsonObject{{"schema", "IFC4"},
                                   {"geometry", "tessellated"},
                                   {"viewDefinition", "none"},
                                   {"fileDescription", "SketchyUp export, tessellated geometry"},
                                   {"fileName", QFileInfo(path).fileName()},
                                   {"bytes", double(bytes.size())},
                                   {"sha256", hashHex(bytes)},
                                   {"timestamp", timestampText(epoch)},
                                   {"timestampSource", timestampSource},
                                   {"documentId", QString::fromStdString(document.identity())},
                                   {"units", QJsonObject{{"length", "METRE"}, {"area", "SQUARE_METRE"}, {"volume", "CUBIC_METRE"}, {"planeAngle", "RADIAN"}}}};
    report["file"] = QJsonObject{{"entities", run.result["entities"]},
                                 {"classes", run.result["classes"]},
                                 {"counts", counts}};
    auto notices = report["notices"].toArray();
    qint64 fallbackTotal = 0;
    for (const auto &entry : run.result["fallbacks"].toArray())
        fallbackTotal += qint64(entry.toObject()["count"].toDouble());
    if (fallbackTotal > 0) {
        notices.append(QJsonObject{{"code", "classFallback"},
                                   {"count", fallbackTotal},
                                   {"message", "A body asked for an IFC class that is not written as asked (unknown to IFC4, not a building element, abstract, or a deprecated standard-case class, which is written as the class it refines); see the class list."}});
        report["classFallbacks"] = run.result["fallbacks"];
    }
    if (const auto dropped = qint64(run.result["predefinedTypeDropped"].toDouble()); dropped > 0)
        notices.append(QJsonObject{{"code", "predefinedTypeDropped"},
                                   {"count", dropped},
                                   {"message", "A predefined type that the class does not have was left out."}});
    report["notices"] = notices;
    boundExportReport(report);
    progress("publish");
    checkCancelled();
    try {
        publishNewFile(path, bytes);
    } catch (const std::exception &error) {
        refuse("destination", error.what(), report);
    }
    return {report, qint64(bytes.size())};
}
} // namespace sketchy
