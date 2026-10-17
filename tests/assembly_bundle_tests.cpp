// R094.f (ADR 0166 §6.1): the six shipped assemblies. Every source builds twice to the same
// bytes (and to the bytes the CLI wrote at build time, when it did), decodes as a component
// bundle with its parameters and assembly identity, and places at its defaults and at its
// bounds' extremes without a regeneration error; the door and the window cut their opening
// in a fixture wall.
#include "automation/component_parameter_commands.hpp"
#include "geometry/solid.hpp"
#include "io/assembly_source.hpp"
#include "io/component_library.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QVariant>
#include <cmath>
#include <iostream>
using namespace sketchy;
namespace {
std::size_t checks = 0;
void check(bool value, const std::string &message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read " + path.toStdString());
    return file.readAll();
}
struct Expected {
    const char *slug, *name;
    QStringList parameters; // display order, inputs and derived
    bool cuts;
};
const std::vector<Expected> assemblies{
    {"interior-door", "Interior door", {"width", "height", "hinge", "leaf_open"}, true},
    {"casement-window",
     "Casement window",
     {"width", "height", "panes", "frame_depth", "opening"},
     true},
    {"dining-table", "Dining table", {"width", "depth", "height", "legs", "apron"}, false},
    {"bookshelf", "Bookshelf", {"width", "height", "depth", "shelves"}, false},
    {"base-cabinet",
     "Base cabinet",
     {"width", "height", "depth", "doors", "shelves", "toe_kick"},
     false},
    {"straight-stair",
     "Straight stair",
     {"rise", "going", "width", "risers", "nosing"},
     false},
};
// A 4 m long, 200 mm thick, 3 m high wall whose front face (normal -y) is at y = 0.
struct Wall {
    Document doc{DisplayUnit::Millimeters};
    Id body{}, face{};
    Wall() {
        body = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, .2, 0}, {0, .2, 0}}}, "Wall");
        doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 3);
        for (const auto &[id, record] : doc.bodies().at(body)->surface.faces) {
            (void)record;
            if (doc.bodies().at(body)->surface.normal(id).y < -.99)
                face = id;
        }
        check(face != 0, "The wall has a front face");
    }
    double volume() const {
        const auto &b = *doc.bodies().at(body);
        const auto analysis = analyzeSolidShells(b.surface, b.topology);
        check(analysis.report.volume.has_value(), "The wall is a closed solid");
        return *analysis.report.volume;
    }
};
QJsonObject describe(const ComponentBundle &bundle) {
    return componentParametersDescription(bundle.document, bundle.definition);
}
// Each input at its minimum and its maximum (others at their defaults), every choice
// option and both booleans, then every input at its minimum and every input at its maximum.
std::vector<QJsonObject> extremes(const QJsonArray &parameters) {
    std::vector<QJsonObject> result{QJsonObject{}};
    QJsonObject lows, highs;
    for (const auto &entry : parameters) {
        const auto parameter = entry.toObject();
        if (parameter["mode"] != "input")
            continue;
        const auto name = parameter["name"].toString();
        const auto type = parameter["type"].toString();
        if (type == "boolean") {
            result.push_back({{name, true}});
            result.push_back({{name, false}});
        } else if (type == "choice") {
            for (const auto &option : parameter["options"].toArray())
                result.push_back({{name, option.toObject()["value"]}});
        } else {
            check(parameter.contains("min") && parameter.contains("max"),
                  "Every quantity input of a shipped assembly is bounded: " + name.toStdString());
            result.push_back({{name, parameter["min"]}});
            result.push_back({{name, parameter["max"]}});
            lows[name] = parameter["min"];
            highs[name] = parameter["max"];
        }
    }
    result.push_back(lows);
    result.push_back(highs);
    return result;
}
AssemblyPlacement onWall(const Wall &wall, double x, double z) {
    AssemblyPlacement placement;
    placement.host = AssemblyHostFace{wall.body, wall.face, {x, 0, z}, {1, 0, 0}, 0};
    return placement;
}
void bundle(const Expected &expected) {
    const auto source =
        read(QString(SKETCHYUP_ASSEMBLY_SOURCES) + "/" + expected.slug + ".json");
    const auto first = buildAssemblyBundle(source), second = buildAssemblyBundle(source);
    check(first == second, std::string(expected.slug) + " builds byte-identically twice");
    const QString generated =
        QString(SKETCHYUP_ASSEMBLY_BUNDLES) + "/" + expected.slug + ".sketchylib";
    if (QFile::exists(generated))
        check(read(generated) == first,
              std::string(expected.slug) + " matches the bundle sketchyup-cli built");
    const auto decoded = decodeComponentBundle(first);
    check(decoded.metadata.name == expected.name, "Bundle name");
    const auto identity = libraryAssemblyIdentity(decoded);
    check(identity && identity->id == std::string("sketchyup/") + expected.slug &&
              identity->version == 1,
          "Bundle assembly identity");
    // A 320 x 240 PNG thumbnail drawn at build time.
    AssetRecord thumbnail;
    thumbnail.mediaType = "image/png";
    thumbnail.payload = std::make_shared<AssetPayload>(
        std::vector<std::uint8_t>(decoded.thumbnailPng.begin(), decoded.thumbnailPng.end()));
    const auto image = decodeTextureImage(thumbnail);
    check(image.status == TextureImageStatus::Ready && image.image->width() == 320 &&
              image.image->height() == 240,
          "The thumbnail is a 320 x 240 PNG");
    const auto description = describe(decoded);
    auto ordered = description["parameters"].toArray().toVariantList();
    std::stable_sort(ordered.begin(), ordered.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["position"].toInt() < b.toMap()["position"].toInt();
    });
    const auto parameters = QJsonArray::fromVariantList(ordered);
    QStringList names;
    for (const auto &parameter : parameters)
        names.append(parameter.toObject()["name"].toString());
    check(names == expected.parameters,
          std::string(expected.slug) + " has its ADR 0166 §6.1 parameters, got " +
              names.join(",").toStdString());
    const auto definition = decoded.document.definitions().at(decoded.definition);
    check(bool(definition->glue) == expected.cuts &&
              (!definition->glue || definition->glue->cutsOpening),
          "Only the door and the window glue to a face and cut an opening");
    // Defaults and extremes place without a regeneration error. One model holds every
    // placement, which reuses the definition the first one inserted.
    Wall wall;
    const auto before = wall.volume();
    int placed = 0;
    Id reused = 0;
    for (const auto &values : extremes(parameters)) {
        // Openings may not overlap: each cutting placement goes in a fresh wall.
        Wall fresh;
        auto &doc = expected.cuts ? fresh.doc : wall.doc;
        AssemblyPlacement placement;
        if (expected.cuts)
            placement = onWall(fresh, 2, 1.3);
        else
            placement.transform = Transform::translation({5.0 + 3 * placed, 0, 0});
        const auto history = doc.history().total;
        AssemblyPlacementResult result;
        try {
            result = placeLibraryAssembly(doc, decoded, placement, values);
        } catch (const std::exception &error) {
            throw std::runtime_error(std::string(expected.slug) + " places with " +
                                     QJsonDocument(values).toJson(QJsonDocument::Compact)
                                         .toStdString() +
                                     ": " + error.what());
        }
        check(doc.history().total == history + 1, "One placement is one History entry");
        const auto valuesOf = componentParameterValues(doc, result.placement);
        for (const auto &entry : valuesOf["values"].toArray()) {
            const auto value = entry.toObject();
            if (values.contains(value["name"].toString()))
                check(value["source"] == "override" || value["source"] == "default",
                      "Placed values are overrides");
        }
        if (!expected.cuts) {
            if (!reused)
                reused = result.definition;
            check(result.definition == reused && (placed == 0 || result.reused),
                  "Later placements reuse the definition by assembly identity");
        } else {
            check(fresh.doc.hostedComponents().attachments.contains(result.placement),
                  "A cutting assembly is attached to its wall");
            check(fresh.volume() < before - .05,
                  std::string(expected.slug) + " cuts its opening through the wall (" +
                      std::to_string(before) + " -> " + std::to_string(fresh.volume()) + ")");
        }
        ++placed;
    }
    std::cout << expected.slug << ": " << first.size() << " bytes, " << placed
              << " placements\n";
}
void cuts() {
    // The door at its defaults cuts a 900 x 2100 mm opening through the 200 mm wall.
    const auto door = decodeComponentBundle(buildAssemblyBundle(
        read(QString(SKETCHYUP_ASSEMBLY_SOURCES) + "/interior-door.json")));
    Wall wall;
    const auto before = wall.volume();
    placeLibraryAssembly(wall.doc, door, onWall(wall, 2, 1.2));
    check(std::abs(before - wall.volume() - .9 * 2.1 * .2) < 1e-6,
          "The default door cuts 900 x 2100 mm through the wall");
    const auto window = decodeComponentBundle(buildAssemblyBundle(
        read(QString(SKETCHYUP_ASSEMBLY_SOURCES) + "/casement-window.json")));
    Wall second;
    const auto full = second.volume();
    placeLibraryAssembly(second.doc, window, onWall(second, 2, 1.5),
                         {{"width", "1500 mm"}, {"height", .9}});
    check(std::abs(full - second.volume() - 1.5 * .9 * .2) < 1e-6,
          "A 1500 x 900 mm window cuts its opening through the wall");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        for (const auto &expected : assemblies)
            bundle(expected);
        cuts();
        std::cout << "assembly_bundle_tests passed " << checks << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "assembly_bundle_tests failed: " << error.what() << '\n';
        return 1;
    }
}
