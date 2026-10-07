#pragma once
#include "automation/commands.hpp"
#include "core/annotations.hpp"
#include "core/assets.hpp"
#include "core/entity_measure.hpp"
#include "core/face_textures.hpp"
#include "core/materials.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "integrations/animation_capture.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include "io/texture_image.hpp"
#include <QJsonArray>
namespace sketchy::m7 {
inline void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Study {
    Document document;
    Id room{}, wall{}, floor{}, perspective{}, plan{}, section{}, cut{};
};
inline Study study() {
    Study out;
    auto &doc = out.document;
    const auto result =
        executeBatch(doc, {{"apiVersion", 1},
                           {"documentId", QString::fromStdString(doc.identity())},
                           {"expectedRevision", QString::number(doc.revision())},
                           {"commands", QJsonArray{QJsonObject{{"command", "assembly.room"}}}}});
    const auto room = result["recipeOperations"].toArray()[0].toObject();
    out.room = room["room"].toString().toULongLong();
    out.wall = room["wall"].toString().toULongLong();
    for (const auto &[id, body] : doc.bodies())
        if (body->name == "Floor")
            out.floor = id;
    check(out.room && out.wall && out.floor, "Building study has room, wall and floor");
    const auto bounds = measureEntity(doc, {out.room, SelectionKind::Body, 0}).world.bounds;
    check(bounds && length(bounds->dimensions() - Vec3{6, 4, 2.7}) < 1e-6,
          "Study is exactly 6 by 4 by 2.7 metres");
    const auto png = encodeTexturePng(TextureImage(
        2, 2, {182, 145, 98, 255, 218, 189, 145, 255, 218, 189, 145, 255, 182, 145, 98, 255}));
    const auto image = createAsset(
        doc, "Study floor tile", "image/png",
        std::make_shared<AssetPayload>(std::vector<std::uint8_t>(png.begin(), png.end())));
    const auto tile = createMaterial(doc, "One metre floor tile", {1, 1, 1}, 1, image);
    assignMaterial(doc, out.floor, {}, tile);
    const auto floorBody = doc.bodies().at(out.floor);
    for (const auto &[face, record] : floorBody->surface.faces) {
        (void)record;
        assignTextureMapping(doc, out.floor, face, TextureMapping{});
    }
    const auto plaster = createMaterial(doc, "Warm plaster", {.88f, .85f, .78f});
    assignMaterial(doc, out.wall, {}, plaster);
    auto vertex = [&](Vec3 point) {
        for (const auto &[id, p] : doc.bodies().at(out.wall)->surface.vertices)
            if (length(doc.worldTransform(out.wall).point(p) - point) < 1e-6)
                return vertexAnchor(doc, out.wall, id);
        throw std::runtime_error("Study dimension endpoint not found");
    };
    AnnotationRecord width;
    width.name = "Building width";
    width.text = "Width";
    width.textSize = 16;
    width.anchors = {vertex({0, 0, 0}), vertex({6, 0, 0})};
    width.offset = {0, -.7, 0};
    createAnnotation(doc, width);
    AnnotationRecord depth;
    depth.name = "Building depth";
    depth.text = "Depth";
    depth.textSize = 16;
    depth.anchors = {vertex({6, 0, 0}), vertex({6, 4, 0})};
    depth.offset = {.7, 0, 0};
    createAnnotation(doc, depth);
    AnnotationRecord height;
    height.name = "Wall height";
    height.text = "Height";
    height.textSize = 16;
    height.anchors = {vertex({0, 0, 0}), vertex({0, 0, 2.7})};
    height.offset = {-.6, -.25, 0};
    createAnnotation(doc, height);
    AnnotationRecord label;
    label.kind = AnnotationKind::Label;
    label.name = "Presentation study";
    label.text = "M7 · Building study\nTwo shared windows · 200 mm walls";
    label.textSize = 14;
    label.anchors = {pointAnchor({3, 4, 2.7})};
    label.offset = {0, .65, .5};
    createAnnotation(doc, label);
    auto style = doc.style();
    style.axesVisible = style.gridVisible = false;
    style.groundVisible = true;
    style.background = {1, 1, 1};
    style.profiles = true;
    doc.setStyle(style);
    SolarSettings sun;
    sun.enabled = true;
    sun.shadows = true;
    sun.latitude = 40;
    sun.longitude = -105;
    sun.time = {2010, 6, 21, 12, 0, 0, -420};
    doc.setSolar(sun);
    out.cut = createSection(doc, "Plan cut at 1.4 m", 0, {{0, 0, -1}, 1.4});
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{{3, 2, 1.2}, -55, 35, 13, 45, false};
    snapshot.style = style;
    snapshot.solar = sun;
    snapshot.section = SceneSection{};
    out.perspective = createScene(doc, "01 · Textured perspective", snapshot);
    snapshot.camera = SceneCamera{{3, 2, 0}, -90, 90, 10, 45, true};
    snapshot.style->groundVisible = false;
    out.plan = createScene(doc, "02 · Dimensioned plan", snapshot);
    snapshot.section->active = {{0, out.cut}};
    snapshot.camera = SceneCamera{{3, 2, .7}, -55, 45, 12, 45, false};
    out.section = createScene(doc, "03 · Section at 1.4 m", snapshot);
    return out;
}
inline QJsonObject verify(const Study &study) {
    const auto &doc = study.document;
    const auto original = encodeDocument(doc);
    const auto history = doc.history().total;
    QJsonArray dimensions;
    for (const auto &[id, annotation] : doc.annotations()) {
        (void)id;
        const auto measured = measureAnnotation(doc, *annotation);
        check(measured.state == AnchorState::Resolved, "Study annotations remain resolved");
        if (annotation->kind == AnnotationKind::Distance) {
            const double expected = annotation->name == "Building width"   ? 6
                                    : annotation->name == "Building depth" ? 4
                                                                           : 2.7;
            check(measured.distance && std::abs(*measured.distance - expected) < 1e-7,
                  "Study dimension matches independent metre oracle");
            dimensions.append(QJsonObject{{"name", QString::fromStdString(annotation->name)},
                                          {"metres", *measured.distance}});
        }
    }
    RenderOptions options;
    options.settings = {320, 240, 4, 0};
    const auto capture = AnimationCapture::capture(
        doc, {study.perspective, study.plan, study.section}, {1, 1, 0}, options);
    check(capture.frames().size() == 3, "Study has three exact camera keyframes");
    QJsonArray views;
    for (size_t i = 0; i < capture.frames().size(); ++i) {
        const auto snapshot = capture.frame(i);
        const auto exported = exportGlb(snapshot);
        const auto scene = doc.scenes().at(i + 1);
        check(describeRenderCamera(snapshot.camera()) ==
                  describeRenderCamera(renderSceneCamera(*scene->snapshot.camera)),
              "Every frame camera is exactly its saved view");
        check(exported.manifest["losses"].toObject()["annotationsOmitted"].toInt() == 4,
              "Render handoff explicitly omits annotation overlays");
        check(snapshot.document().activeSections() == scene->snapshot.section->active,
              "Frame retains saved named section state");
        check(snapshot.sourceIdentity() == doc.identity() &&
                  snapshot.sourceRevision() == doc.revision(),
              "Frames retain original document provenance");
        views.append(QJsonObject{{"name", QString::fromStdString(scene->name)},
                                 {"camera", describeRenderCamera(snapshot.camera())},
                                 {"losses", exported.manifest["losses"]},
                                 {"triangles", exported.manifest["visibleTriangles"]}});
    }
    check(encodeDocument(doc) == original && doc.history().total == history,
          "Study captures leave model and history unchanged");
    check(encodeDocument(decodeDocument(original)) == original,
          "Complete study round trips exactly");
    return {{"dimensions", dimensions},
            {"views", views},
            {"units", "m"},
            {"annotations", 4},
            {"sharedWindowInstances", qint64(doc.instances().size())}};
}
} // namespace sketchy::m7
