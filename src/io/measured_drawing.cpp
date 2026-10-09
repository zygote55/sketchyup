#include "io/measured_drawing.hpp"
#include "core/edge_appearance.hpp"
#include "core/section_records.hpp"
#include <QJsonArray>
#include <set>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
MeasuredDrawing captureMeasuredDrawing(const RenderSnapshot &snapshot, MeasuredPage page) {
    const auto &doc = snapshot.document();
    require(snapshot.camera().orthographic, "Measured drawing requires an orthographic camera");
    const auto frame = renderCameraTransform(snapshot.camera());
    page.origin = snapshot.camera().target;
    page.right = frame.vector({1, 0, 0});
    page.up = frame.vector({0, 1, 0});
    page.towardEye = frame.vector({0, 0, 1});
    page.validate();
    MeasuredDrawing result;
    result.page = page;
    result.units = doc.displayUnits();
    std::vector<MeasuredEdge> edges;
    std::vector<MeasuredTriangle> occluders;
    QJsonObject losses;
    std::set<QString> rasterReasons;
    size_t faceCount{}, edgeCount{}, triangleCount{}, bodyCount{}, annotationCount{}, vertexCount{},
        referenceCount{};
    auto loss = [&](const QString &name) { losses[name] = losses[name].toInt() + 1; };
    auto appendEdge = [&](Vec3 a, Vec3 b, MeasuredSource source) {
        require(edges.size() < 20000, "Measured drawing exceeds 20000 visible edges");
        edges.push_back({a, b, result.sources.size()});
        result.sources.push_back(source);
    };
    auto appendTriangle = [&](Vec3 a, Vec3 b, Vec3 c) {
        require(occluders.size() < 20000, "Measured drawing exceeds 20000 opaque triangles");
        occluders.push_back({{a, b, c}});
    };
    require(doc.bodies().size() <= 10000, "Measured drawing exceeds 10000 body records");
    for (const auto &[id, ptr] : doc.bodies()) {
        const auto &body = *ptr;
        if (!snapshot.visible(id)) {
            loss("hiddenBodiesOmitted");
            continue;
        }
        if (body.kind == BodyKind::Group)
            continue;
        ++bodyCount;
        if (body.kind == BodyKind::ReferenceImage) {
            loss("referenceImagesOmitted");
            rasterReasons.insert("referenceImages");
            continue;
        }
        require(body.topology.edges.size() <= 100000 - edgeCount,
                "Measured drawing source edge budget exceeded");
        edgeCount += body.topology.edges.size();
        require(body.surface.faces.size() <= 20000 - faceCount,
                "Measured drawing source face budget exceeded");
        faceCount += body.surface.faces.size();
        require(body.surface.vertices.size() <= 100000 - vertexCount,
                "Measured drawing source vertex budget exceeded");
        vertexCount += body.surface.vertices.size();
        for (const auto &[face, record] : body.surface.faces) {
            (void)face;
            for (const auto &loop : record.loops) {
                require(loop.size() <= 200000 - referenceCount,
                        "Measured drawing source face-reference budget exceeded");
                referenceCount += loop.size();
            }
        }
        const auto world = doc.worldTransform(id);
        const auto cuts = effectiveSectionCuts(doc, id);
        const auto adjacency = body.topology.adjacency(body.surface);
        std::vector<Triangle> triangles;
        std::map<Id, bool> opaque;
        std::map<Id, double> facing;
        for (const auto &[face, record] : body.surface.faces) {
            (void)record;
            if (!snapshot.visible(id, face)) {
                loss("hiddenFacesOmitted");
                continue;
            }
            const auto front = surfaceAppearance(doc.materials(), body, face);
            const auto back = surfaceAppearance(doc.materials(), body, face, true);
            opaque[face] = front.opacity == 1 && back.opacity == 1;
            if (!opaque[face]) {
                loss("transparentFacesUnfilled");
                rasterReasons.insert("transparency");
            }
            if ((front.material && doc.materials().at(front.material)->asset) ||
                (back.material && doc.materials().at(back.material)->asset)) {
                loss("texturedFacesUnfilled");
                rasterReasons.insert("textures");
                // Texture alpha cannot safely be assumed opaque.
                opaque[face] = false;
            }
            const auto faceTriangles = body.surface.triangulate(face);
            require(faceTriangles.size() <= 20000 - triangleCount,
                    "Measured drawing source triangle budget exceeded");
            triangleCount += faceTriangles.size();
            Vec3 normal{};
            for (const auto &t : faceTriangles) {
                const auto a = world.point(t.a), b = world.point(t.b), c = world.point(t.c);
                normal = normal + cross(b - a, c - a);
                triangles.push_back({a, b, c, face});
            }
            facing[face] = dot(normal, page.towardEye);
        }
        for (const auto &[edge, record] : body.topology.edges) {
            const auto appearance = edgeAppearance(body, edge);
            if (appearance.hidden || !snapshot.edgeVisible(id, edge)) {
                loss("hiddenEdgesOmitted");
                continue;
            }
            size_t visibleFaces{};
            bool positive = false, negative = false;
            if (const auto it = adjacency.edgeFaces.find(edge); it != adjacency.edgeFaces.end())
                for (const auto &incidence : it->second)
                    if (snapshot.visible(id, incidence.face)) {
                        ++visibleFaces;
                        const auto direction = facing.at(incidence.face);
                        positive = positive || direction >= 0;
                        negative = negative || direction <= 0;
                    }
            if (!record.wire && !visibleFaces)
                continue;
            if (appearance.soft && visibleFaces > 1 && !(positive && negative)) {
                loss("softInteriorEdgesOmitted");
                continue;
            }
            const auto segment =
                sectionSegment(world.point(body.surface.vertices.at(record.a)),
                               world.point(body.surface.vertices.at(record.b)), cuts);
            if (segment)
                appendEdge((*segment)[0], (*segment)[1], {id, edge, 0});
        }
        if (cuts.empty()) {
            for (const auto &t : triangles)
                if (opaque.at(t.face))
                    appendTriangle(t.a, t.b, t.c);
        } else {
            const auto section = sectionMesh(triangles, cuts);
            for (const auto &t : section.triangles) {
                const bool solid =
                    t.section ? doc.sections().at(t.section)->fill : opaque.at(t.face);
                if (solid)
                    appendTriangle(t.vertices[0].point, t.vertices[1].point, t.vertices[2].point);
            }
            for (const auto &e : section.edges)
                if (doc.sections().at(e.section)->edges)
                    appendEdge(e.a, e.b, {id, 0, e.section});
            for (auto unused : section.unfilledSections) {
                (void)unused;
                loss("unfilledSectionContours");
            }
        }
    }
    result.geometry = measuredHiddenLines(page, edges, occluders);
    for (const auto &[id, ptr] : doc.annotations()) {
        (void)id;
        require(++annotationCount <= annotationRecordLimit,
                "Measured drawing annotation budget exceeded");
        const auto &record = *ptr;
        MeasuredAnnotation annotation;
        annotation.record = record;
        annotation.measurement = measureAnnotation(doc, record);
        bool visible = true;
        for (size_t i = 0; i < record.anchors.size(); ++i) {
            const auto &anchor = record.anchors[i];
            const auto &resolved = annotation.measurement.anchors[i];
            if (anchor.kind == AnchorKind::Point || resolved.state != AnchorState::Resolved)
                continue;
            if (!snapshot.visible(anchor.body,
                                  anchor.kind == AnchorKind::Face ? anchor.entity : 0) ||
                !sectionContains(resolved.point, effectiveSectionCuts(doc, anchor.body)))
                visible = false;
            if (anchor.kind == AnchorKind::Edge &&
                (!snapshot.edgeVisible(anchor.body, anchor.entity) ||
                 edgeAppearance(*doc.bodies().at(anchor.body), anchor.entity).hidden))
                visible = false;
        }
        if (!visible) {
            loss("hiddenOrClippedAnnotationsOmitted");
            continue;
        }
        annotation.textPoint = page.project(annotation.measurement.textPoint);
        for (const auto &anchor : annotation.measurement.anchors)
            annotation.anchors.push_back(page.project(anchor.point));
        if (annotation.measurement.state != AnchorState::Resolved)
            loss("brokenAnnotationReferences");
        result.annotations.push_back(std::move(annotation));
    }
    QJsonArray reasons;
    for (const auto &reason : rasterReasons)
        reasons.append(reason);
    result.report = {{"mode", "orthographicTechnicalLines"},
                     {"units", "mm"},
                     {"scaleDenominator", page.scaleDenominator},
                     {"pageWidthMm", page.widthMm},
                     {"pageHeightMm", page.heightMm},
                     {"marginMm", page.marginMm},
                     {"includeHidden", page.includeHidden},
                     {"bodies", double(bodyCount)},
                     {"sourceEdges", double(edgeCount)},
                     {"sourceTriangles", double(triangleCount)},
                     {"lineSegments", double(result.geometry.lines.size())},
                     {"annotations", double(result.annotations.size())},
                     {"losses", losses},
                     {"rasterAppearanceReasons", reasons},
                     {"cameraDepthClipping", false},
                     {"surfaceColorAndLighting", "unfilled technical line drawing"},
                     {"annotationTextScale", "96 logical pixels per inch; fixed physical size"}};
    return result;
}
} // namespace sketchy
