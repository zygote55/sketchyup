// ADR 0166 §6.3 (R094.f): placing a configurable assembly from the Components tab.
#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/measurements.hpp"
#include "core/component_glue.hpp"
#include "core/component_placement.hpp"
#include "core/hosted_components.hpp"
#include "io/component_library.hpp"
#include "io/library_bundle.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <cmath>
namespace sketchy {
namespace {
bool hiddenInScene(const Document &doc, Id body) {
    for (auto id = body; id && doc.bodies().contains(id); id = doc.bodies().at(id)->parent)
        if (doc.bodies().at(id)->hidden)
            return true;
    return false;
}
// The surface a host face is placed on: the uncut surface of a host that already has
// openings, as component.attach uses.
const Surface &hostSurface(const Document &doc, Id host) {
    const auto &records = doc.hostedComponents();
    return records.hosts.contains(host) ? records.hosts.at(host)->uncut
                                        : doc.bodies().at(host)->surface;
}
// The area-weighted centre of a face, on its plane.
Vec3 faceCentre(const Surface &surface, Id face) {
    Vec3 sum{};
    double area = 0;
    for (const auto &t : surface.triangulate(face)) {
        const auto a = length(cross(t.b - t.a, t.c - t.a)) / 2;
        sum = sum + (t.a + t.b + t.c) * (a / 3);
        area += a;
    }
    if (area <= 0)
        throw std::runtime_error("Choose a nondegenerate face");
    return sum * (1 / area);
}
// A tangent that keeps an assembly upright on a wall: horizontal along the face. On a face
// that is not vertical the first boundary edge is the tangent, as Shift+H uses.
Vec3 uprightTangent(const Document &doc, Id host, const Surface &surface, Id face) {
    const auto normal = surface.normal(face);
    const auto up = doc.worldTransform(host).inverse().vector({0, 0, 1});
    const auto along = cross(up, normal);
    if (length(along) > 1e-6 * std::max(1., length(up)))
        return normalized(along);
    const auto &loop = surface.faces.at(face).loops.at(0);
    return normalized(surface.vertices.at(loop.at(1)) - surface.vertices.at(loop.at(0)));
}
} // namespace
QJsonObject Viewport::beginAssemblyPlacement(std::shared_ptr<const ComponentBundle> bundle,
                                             const QJsonObject &values) {
    if (!bundle)
        throw std::runtime_error("Choose an assembly to place");
    if (componentScope())
        throw std::runtime_error("Close component editing before placing an assembly");
    setTool(Tool::Select);
    AssemblyPlacementState state;
    state.bundle = std::move(bundle);
    const auto &definition = *state.bundle->document.definitions().at(state.bundle->definition);
    state.gluing = definition.glue && definition.glue->cutsOpening;
    if (definition.glue)
        state.glue = resolveComponentGlue(definition).frame;
    assembly_ = std::move(state);
    assemblyAmend_.reset();
    if (!assemblyDryRun_) {
        assemblyDryRun_ = new QTimer(this);
        assemblyDryRun_->setSingleShot(true);
        assemblyDryRun_->setInterval(150);
        connect(assemblyDryRun_, &QTimer::timeout, this, [this] { assemblyFullDryRun(); });
    }
    const auto refusal = setAssemblyValues(values);
    emit message(assemblyPlacementStatus());
    // The Measurements box is the placement's now.
    emit toolChanged(int(tool_));
    return refusal;
}
QJsonObject Viewport::setAssemblyValues(const QJsonObject &values) {
    if (!assembly_)
        return {};
    auto &state = *assembly_;
    // The ghost is the assembly at these values: a dry run of component_parameter.override on
    // a private copy of the bundle's document, once per value change.
    auto copy = state.bundle->document.readSnapshot();
    Id root = 0;
    for (const auto &[id, body] : copy.bodies())
        if (!body->parent)
            root = id;
    if (!values.isEmpty()) {
        try {
            executeBatch(copy, {{"apiVersion", 1},
                                {"documentId", QString::fromStdString(copy.identity())},
                                {"expectedRevision", QString::number(copy.revision())},
                                {"commands", QJsonArray{QJsonObject{
                                                 {"command", "component_parameter.override"},
                                                 {"body", QString::number(root)},
                                                 {"values", values}}}}});
        } catch (const InspectionError &error) {
            return {{"code", QString::fromStdString(error.code())},
                    {"message", QString::fromUtf8(error.what())},
                    {"details", error.details()}};
        } catch (const std::exception &error) {
            return {{"message", QString::fromUtf8(error.what())}};
        }
    }
    state.values = values;
    state.edges.clear();
    state.faces.clear();
    state.outline.clear();
    for (const auto &[id, body] : copy.bodies()) {
        if (body->kind != BodyKind::Geometry || hiddenInScene(copy, id))
            continue;
        const auto world = copy.worldTransform(id);
        for (const auto &[edge, record] : body->topology.edges) {
            (void)edge;
            state.edges.push_back({world.point(body->surface.vertices.at(record.a)),
                                   world.point(body->surface.vertices.at(record.b))});
        }
        for (const auto &[face, record] : body->surface.faces) {
            (void)face;
            std::vector<Vec3> polygon;
            for (const auto vertex : record.loops.front())
                polygon.push_back(world.point(body->surface.vertices.at(vertex)));
            state.faces.push_back(std::move(polygon));
        }
    }
    const auto &definition = *copy.definitions().at(state.bundle->definition);
    if (state.gluing) {
        // The opening outline: the glue face's outer loop at these values.
        const auto member = copy.instances().at(root)->members.at(definition.glue->member);
        const auto &surface = copy.bodies().at(member)->surface;
        const auto world = copy.worldTransform(member);
        for (const auto vertex : surface.faces.at(definition.glue->face).loops.front())
            state.outline.push_back(world.point(surface.vertices.at(vertex)));
    }
    state.cut.clear();
    if (state.target && assemblyDryRun_)
        assemblyDryRun_->start();
    update();
    return {};
}
void Viewport::endAssemblyPlacement() {
    if (!assembly_)
        return;
    assembly_.reset();
    if (assemblyDryRun_)
        assemblyDryRun_->stop();
    update();
    emit assemblyPlacementEnded(false, 0);
}
bool Viewport::assemblyPlacing() const { return assembly_.has_value(); }
bool Viewport::assemblyMeasurementsActive() const {
    return assembly_ || (assemblyAmend_ && doc_.canAmend(assemblyAmend_->stamp));
}
QString Viewport::assemblyPlacementStatus() const {
    if (!assembly_)
        return {};
    const auto name = assembly_->bundle->metadata.name;
    return assembly_->gluing
               ? "Placing " + name +
                     " · Click a wall face to place · the opening is cut in the same step · Esc "
                     "cancels"
               : "Placing " + name + " · Click to place · Esc cancels";
}
std::size_t Viewport::assemblyGhostEdges() const {
    return assembly_ && assembly_->world ? assembly_->edges.size() : 0;
}
std::optional<Transform> Viewport::assemblyGhostTransform() const {
    return assembly_ ? assembly_->world : std::nullopt;
}
QPolygonF Viewport::assemblyOpeningOutline() const {
    QPolygonF result;
    if (!assembly_ || !assembly_->world)
        return result;
    for (const auto &point : assembly_->outline)
        result << project(assembly_->world->point(point));
    return result;
}
std::optional<AssemblyPlacement>
Viewport::assemblyOnFace(Id host, Id face, std::optional<Vec3> anchor, Transform &world) const {
    if (!doc_.bodies().contains(host) || doc_.bodies().at(host)->kind != BodyKind::Geometry ||
        !selectable({host, SelectionKind::Face, face}))
        return std::nullopt;
    for (auto id = doc_.bodies().at(host)->parent; id; id = doc_.bodies().at(id)->parent)
        if (doc_.instances().contains(id))
            return std::nullopt; // a host is ordinary geometry, not a component's member
    const auto &surface = hostSurface(doc_, host);
    if (!surface.faces.contains(face))
        return std::nullopt;
    AssemblyHostFace target{host, face, anchor ? *anchor : faceCentre(surface, face),
                            uprightTangent(doc_, host, surface, face), 0};
    // The anchor moves in whole millimetres across the face, measured from its first corner
    // along the tangent. Openings cut from anchors with arbitrary low-order bits (pointer rays
    // are computed in single precision) are refused by the host's closed-solid check about
    // one time in seven; millimetre anchors are not (R094.f implementation notes).
    const auto frame = DrawingPlane::make(
        surface.vertices.at(surface.faces.at(face).loops.at(0).at(0)), surface.normal(face),
        target.tangent);
    const auto local = frame.coordinates(target.anchor);
    target.anchor =
        frame.point(std::round(local.x * 1000) / 1000, std::round(local.y * 1000) / 1000);
    FacePlacementOptions options;
    options.anchor = target.anchor;
    options.tangent = target.tangent;
    world = componentPlacementOnFace(surface, face, doc_.worldTransform(host), assembly_->glue,
                                     options)
                .world;
    AssemblyPlacement placement;
    placement.host = target;
    return placement;
}
void Viewport::updateAssemblyTarget(QPointF position) {
    auto &state = *assembly_;
    state.target.reset();
    state.world.reset();
    state.cut.clear();
    state.error.clear();
    const auto [rayOrigin, rayDirection] = ray(position);
    const auto [body, face] = pick(position);
    try {
        if (state.gluing) {
            if (!body || !face || !doc_.bodies().contains(body))
                throw std::runtime_error("Point at a wall face to place " +
                                         state.bundle->metadata.name.toStdString());
            const auto &surface = hostSurface(doc_, body);
            if (!surface.faces.contains(face))
                throw std::runtime_error("Point at a wall face");
            const auto inverse = doc_.worldTransform(body).inverse();
            const auto origin = inverse.point(rayOrigin), direction = inverse.vector(rayDirection);
            const auto normal = surface.normal(face);
            const auto point = surface.vertices.at(surface.faces.at(face).loops.at(0).at(0));
            const auto denominator = dot(normal, direction);
            if (std::abs(denominator) < 1e-9 * length(direction))
                throw std::runtime_error("Orbit to view the wall face");
            const auto anchor = origin + direction * (dot(normal, point - origin) / denominator);
            Transform world;
            const auto target = assemblyOnFace(body, face, anchor, world);
            if (!target)
                throw std::runtime_error("Choose a face of ordinary geometry, such as a wall");
            state.target = target;
            state.world = world;
        } else {
            // On the hovered face's plane, else on the ground.
            Vec3 normal{0, 0, 1}, point{};
            if (body && face && doc_.bodies().contains(body) &&
                doc_.bodies().at(body)->surface.faces.contains(face)) {
                const auto &surface = doc_.bodies().at(body)->surface;
                const auto world = doc_.worldTransform(body);
                const auto triangles = surface.triangulate(face);
                if (!triangles.empty()) {
                    const auto a = world.point(triangles.front().a),
                               b = world.point(triangles.front().b),
                               c = world.point(triangles.front().c);
                    if (length(cross(b - a, c - a)) > 0) {
                        point = a;
                        normal = normalized(cross(b - a, c - a));
                    }
                }
            }
            const auto denominator = dot(normal, rayDirection);
            if (std::abs(denominator) < 1e-9 * length(rayDirection))
                throw std::runtime_error("Orbit to view the ground");
            const auto distance = dot(normal, point - rayOrigin) / denominator;
            if (distance <= 0)
                throw std::runtime_error("Point below the horizon to place");
            AssemblyPlacement placement;
            placement.transform = Transform::translation(rayOrigin + rayDirection * distance);
            state.target = placement;
            state.world = placement.transform;
        }
    } catch (const std::exception &error) {
        state.error = QString::fromUtf8(error.what());
    }
    if (state.gluing && state.target && assemblyDryRun_)
        assemblyDryRun_->start();
    update();
}
void Viewport::assemblyFullDryRun() {
    if (!assembly_ || !assembly_->target)
        return;
    auto &state = *assembly_;
    // A dry run of the full placement on a private copy shows the real cut, or the refusal.
    auto draft = doc_.readSnapshot();
    try {
        const auto result = placeLibraryAssembly(draft, *state.bundle, *state.target, state.values);
        state.cut.clear();
        if (state.target->host) {
            const auto host = state.target->host->host;
            const auto &opening =
                draft.hostedComponents().hosts.at(host)->openings.at(result.placement);
            const auto world = draft.worldTransform(host);
            for (const auto &corner : opening.profile.corners)
                state.cut.push_back(world.point(corner.point));
        }
        state.error.clear();
        emit message(assemblyPlacementStatus());
    } catch (const std::exception &error) {
        state.error = QString::fromUtf8(error.what());
        emit message(state.error);
    }
    update();
}
void Viewport::placeAssembly(const AssemblyPlacement &placement) {
    if (!assembly_)
        return;
    auto state = *assembly_;
    const auto result = placeLibraryAssembly(doc_, *state.bundle, placement, state.values);
    assemblyAmend_ = AssemblyAmend{state.bundle, state.values, placement, doc_.amendmentStamp(),
                                   result.placement};
    assembly_.reset();
    if (assemblyDryRun_)
        assemblyDryRun_->stop();
    refresh();
    selectEntities({{result.placement, SelectionKind::Body, 0}});
    emit changed();
    emit message("Placed " + state.bundle->metadata.name + " · " +
                 (placement.host ? QString("Type anchor coordinates or an inset to revise")
                                 : QString("Type [x,y,z] to move it")) +
                 " · Ctrl+Z undoes");
    emit assemblyPlacementEnded(true, result.placement);
}
bool Viewport::assemblyPress(QMouseEvent *event) {
    if (!assembly_ || event->button() != Qt::LeftButton ||
        event->modifiers().testFlag(Qt::AltModifier))
        return false;
    event->accept();
    updateAssemblyTarget(event->position());
    if (!assembly_->target) {
        emit message(assembly_->error.isEmpty() ? assemblyPlacementStatus() : assembly_->error);
        return true;
    }
    try {
        placeAssembly(*assembly_->target);
    } catch (const std::exception &error) {
        emit message(QString::fromUtf8(error.what()));
    }
    return true;
}
void Viewport::assemblyMove(QMouseEvent *event) {
    if (assembly_ && !dragging_)
        updateAssemblyTarget(event->position());
}
bool Viewport::assemblyKey(QKeyEvent *event) {
    if (!assembly_)
        return false;
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        endAssemblyPlacement();
        emit message("Placement canceled");
        return true;
    }
    if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter)
        return false;
    event->accept();
    try {
        // Enter places a gluing assembly on the selected face at its centre.
        if (!assembly_->gluing)
            throw std::runtime_error("Type [x,y,z] in the Measurements box, or click to place");
        const auto &entities = selection_.entities();
        if (entities.size() != 1 || entities.begin()->kind != SelectionKind::Face)
            throw std::runtime_error("Select one wall face, then Enter places on it");
        Transform world;
        const auto target =
            assemblyOnFace(entities.begin()->body, entities.begin()->entity, std::nullopt, world);
        if (!target)
            throw std::runtime_error("Choose a face of ordinary geometry, such as a wall");
        placeAssembly(*target);
    } catch (const std::exception &error) {
        emit message(QString::fromUtf8(error.what()));
    }
    return true;
}
bool Viewport::assemblyMeasurements(const QString &text) {
    const auto input = parseMeasurements(text, inputUnit(doc_.displayUnits()), QLocale());
    const bool amend = !assembly_;
    if (amend && (!assemblyAmend_ || !doc_.canAmend(assemblyAmend_->stamp)))
        throw std::runtime_error("Place an assembly before typing its position");
    auto placement = amend ? assemblyAmend_->placement : AssemblyPlacement{};
    const bool gluing = amend ? bool(placement.host) : assembly_->gluing;
    const bool point = (input.kind == MeasurementKind::Values ||
                        input.kind == MeasurementKind::AbsolutePoint) &&
                       input.values.size() == 3;
    const Vec3 typed = point ? Vec3{input.values[0], input.values[1], input.values[2]} : Vec3{};
    if (gluing) {
        if (!amend) {
            // On the selected face, else the face the ghost is on.
            const auto &entities = selection_.entities();
            if (entities.size() == 1 && entities.begin()->kind == SelectionKind::Face) {
                Transform world;
                const auto target = assemblyOnFace(entities.begin()->body,
                                                   entities.begin()->entity, std::nullopt, world);
                if (target)
                    placement = *target;
            } else if (assembly_->target)
                placement = *assembly_->target;
            if (!placement.host)
                throw std::runtime_error("Select a wall face, then type anchor coordinates or "
                                         "an inset");
        }
        if (input.kind == MeasurementKind::Values && input.values.size() == 1)
            placement.host->inset = input.values[0];
        else if (point)
            placement.host->anchor = typed; // host-local, as Shift+H takes it
        else
            throw std::runtime_error("Enter one inset length or three host-local anchor "
                                     "coordinates");
    } else {
        if (!point)
            throw std::runtime_error("Enter the position as [x,y,z]");
        placement.transform = Transform::translation(typed);
    }
    if (!amend) {
        placeAssembly(placement);
        return true;
    }
    // Re-entry amends the placement's entry while nothing else changed the model.
    const auto previous = *assemblyAmend_;
    Id placed = 0;
    doc_.amendLast(previous.stamp, [&](Document &draft) {
        placed = placeLibraryAssembly(draft, *previous.bundle, placement, previous.values)
                     .placement;
    });
    assemblyAmend_ = AssemblyAmend{previous.bundle, previous.values, placement,
                                   doc_.amendmentStamp(), placed};
    refresh();
    selectEntities({{placed, SelectionKind::Body, 0}});
    emit changed();
    emit message("Placement revised · Ctrl+Z undoes");
    return true;
}
void Viewport::paintAssemblyOverlay(QPainter &painter) {
    if (!assembly_ || !assembly_->world)
        return;
    const auto &state = *assembly_;
    const auto &world = *state.world;
    // The preview overlay's dashed edges and hatch (UX_DESIGN §10), as the parameter ghost.
    auto hatch = colors_.selectionStroke;
    hatch.setAlphaF(.35);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(hatch, Qt::BDiagPattern));
    for (const auto &face : state.faces) {
        QPolygonF polygon;
        for (const auto &point : face)
            polygon << project(world.point(point));
        painter.drawPolygon(polygon);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(colors_.selectionStroke, 1.6, Qt::DashLine));
    for (const auto &edge : state.edges)
        painter.drawLine(project(world.point(edge[0])), project(world.point(edge[1])));
    if (state.gluing) {
        // The opening outline on the wall, from the dry run once the pointer rests.
        QPolygonF outline;
        if (!state.cut.empty())
            for (const auto &point : state.cut)
                outline << project(point);
        else
            for (const auto &point : state.outline)
                outline << project(world.point(point));
        painter.setPen(QPen(colors_.selectionStroke.darker(130), 1.2));
        painter.drawPolygon(outline);
        painter.setBrush(colors_.selectionStroke.darker(130));
        painter.drawEllipse(project(world.point(state.glue.origin)), 3, 3);
        // The opening's width under it, in the information colour.
        if (state.outline.size() >= 2) {
            double low = 1e18, high = -1e18;
            for (const auto &point : state.outline) {
                const auto x = state.glue.coordinates(point).x;
                low = std::min(low, x);
                high = std::max(high, x);
            }
            const auto text = displayLength(high - low, doc_.displayUnits(),
                                            doc_.displayPrecision());
            QPointF bottom = outline.boundingRect().center();
            bottom.setY(outline.boundingRect().bottom() + 18);
            const auto metrics = painter.fontMetrics();
            QRectF box(0, 0, metrics.horizontalAdvance(text) + 16, metrics.height() + 8);
            box.moveCenter(bottom);
            painter.setPen(QPen(colors_.information, 1));
            painter.setBrush(colors_.input);
            painter.drawRoundedRect(box, 4, 4);
            painter.drawText(box, Qt::AlignCenter, text);
        }
    }
    painter.restore();
}
} // namespace sketchy
