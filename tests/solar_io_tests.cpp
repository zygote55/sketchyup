#include "core/components.hpp"
#include "core/scenes.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include "io/recovery.hpp"
#include "io/scenes_io.hpp"
#include "io/solar_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid sun study accepted");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        SolarSettings settings;
        settings.enabled = true;
        settings.latitude = 40;
        settings.longitude = -105;
        settings.northDegrees = 23;
        settings.time = {2010, 6, 21, 12, 0, 0, -420};
        const auto baseline = doc.readSnapshot();
        doc.setSolar(settings);
        const auto revision = doc.revision();
        doc.setSolar(settings);
        check(doc.revision() == revision, "No-op sun study has no history");
        check(baseline.solar() == SolarSettings{}, "Immutable snapshot retains earlier sun study");
        doc.undo();
        check(doc.solar() == SolarSettings{}, "Undo solar settings");
        doc.redo();
        check(doc.solar() == settings, "Redo exact solar settings");
        auto encoded = encodeContainer(doc);
        auto loaded = decodeContainer(encoded);
        check(loaded.solar() == settings && encodeContainer(loaded) == encoded,
              "Exact solar native roundtrip");
        auto render = RenderSnapshot::capture(doc);
        auto changed = settings;
        changed.time.hour = 19;
        doc.setSolar(changed);
        check(render.document().solar() == settings, "Render snapshot freezes sun study");
        const auto exported = exportGlb(render);
        check(exported.manifest["solar"] == encodeSolarSettings(settings) &&
                  exported.manifest["solarPosition"] == describeSolarPosition(settings),
              "Render manifest carries frozen input and direction");
        SceneSnapshot snapshot;
        snapshot.solar = settings;
        const auto scene = createScene(doc, "Noon", snapshot);
        recallSceneModel(doc, scene);
        check(doc.solar() == settings, "Solar-only scene recalls model settings");
        doc.undo();
        check(doc.solar() == changed, "Undo scene recall restores prior sun");
        check(decodeSceneSnapshot(encodeSceneSnapshot(snapshot)) == snapshot,
              "Solar scene strict codec");
        rejects([&] { decodeSceneSnapshot(encodeSceneSnapshot(snapshot), true, false); });
        encoded = encodeContainer(doc);
        loaded = decodeContainer(encoded);
        check(loaded.scenes().at(scene)->snapshot.solar == settings, "Scene solar persists");
        const auto prepared =
            doc.prepareEdit([&](Document &candidate) { candidate.setSolar(settings); });
        doc.applyPrepared(prepared);
        check(doc.solar() == settings, "Prepared solar edit publishes");
        rejects([&] { doc.applyPrepared(prepared); });
        QTemporaryDir directory;
        check(directory.isValid(), "Recovery directory available");
        QString key;
        {
            RecoveryWriter writer(directory.path(), QString::fromStdString(doc.identity()));
            writer.write(captureRecovery(doc));
            key = writer.key();
        }
        const auto recovered = readRecovery(directory.path(), key);
        check(recovered.verified && recovered.document &&
                  encodeContainer(*recovered.document) == encodeContainer(doc),
              "Recovery retains settings and scene solar exactly");
        doc.setDisplayUnits(DisplayUnit::Millimeters);
        check(doc.solar() == settings, "Display units preserve sun study exactly");
        const auto component = createComponent(doc, 1, "Solar scope");
        editComponentDefinition(doc, component.definition, [&](Document &candidate) {
            check(candidate.solar() == settings, "Component draft retains document sun study");
            return ChangeReport{};
        });
        const auto stable = encodeContainer(doc);
        rejects([&] {
            editComponentDefinition(doc, component.definition, [&](Document &candidate) {
                candidate.setSolar(changed);
                return ChangeReport{};
            });
        });
        check(encodeContainer(doc) == stable, "Shared component solar edit rejects atomically");
        auto invalid = settings;
        invalid.time.day = 32;
        rejects([&] { doc.setSolar(invalid); });
        check(encodeContainer(doc) == stable, "Invalid solar edit leaves live document unchanged");
        auto object = encodeSolarSettings(settings);
        for (auto it = object.begin(); it != object.end(); ++it) {
            auto bad = object;
            bad.remove(it.key());
            rejects([&] { decodeSolarSettings(bad); });
            bad = object;
            bad[it.key()] = QJsonValue::Null;
            rejects([&] { decodeSolarSettings(bad); });
        }
        for (auto pair :
             std::initializer_list<std::pair<QString, QJsonValue>>{{"algorithm", "future"},
                                                                   {"year", 2024.5},
                                                                   {"day", 32},
                                                                   {"utcOffsetMinutes", 841},
                                                                   {"enabled", 1},
                                                                   {"latitude", 91},
                                                                   {"extra", true}}) {
            auto bad = object;
            bad[pair.first] = pair.second;
            rejects([&] { decodeSolarSettings(bad); });
        }
        auto old =
            loadDocument(QStringLiteral(SOURCE_DIR "/tests/fixtures/editable-text-v22.sketchyup"));
        check(old.solar() == SolarSettings{} && old.bodies().at(1)->textSource &&
                  old.bodies().size() == 2,
              "Actual v22 text fixture migrates without losing cached geometry or source");
        auto tree = QJsonDocument::fromJson(encodeDocument(doc)).object();
        check(tree["version"] == 24, "Explicit solar schema 24");
        tree["version"] = 22;
        rejects([&] { decodeDocument(QJsonDocument(tree).toJson()); });
        std::cout << "Sun study persistence, immutable rendering, scenes, prepared edits and "
                     "strict migration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
