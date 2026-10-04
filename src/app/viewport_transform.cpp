#include "app/viewport.hpp"
#include "automation/measurements.hpp"
#include <QLocale>
#include <algorithm>
#include <numbers>
namespace sketchy {
namespace {
QJsonArray jsonPoint(Vec3 p) { return {p.x, p.y, p.z}; }
QString kindName(SelectionKind kind) {
    switch (kind) {
    case SelectionKind::Body:
        return "context";
    case SelectionKind::Face:
        return "face";
    case SelectionKind::Edge:
        return "edge";
    case SelectionKind::Guide:
        return "guide";
    }
    throw std::runtime_error("Unknown selection kind");
}
} // namespace
void Viewport::setTransformCopy(bool enabled) {
    transformCopy_ = enabled;
    if (transformTool() && session_.active() && !transformPreview_.isEmpty()) {
        transformPreview_["copy"] = enabled;
        previewCommand(transformPreview_);
    }
    emit transformOptionsChanged();
    update();
}
void Viewport::setTransformLocal(bool enabled) {
    if (transformLocal_ == enabled)
        return;
    cancel();
    transformLocal_ = enabled;
    emit transformOptionsChanged();
    update();
}
void Viewport::captureTransformTargets() {
    syncSelection();
    transformTargets_ = {};
    transformSelection_ = selection_.entities();
    for (auto target : transformSelection_) {
        if (!selectable(target))
            throw std::runtime_error("Selection contains hidden, locked or inactive geometry");
        transformTargets_.append(QJsonObject{{"body", QString::number(target.body)},
                                             {"kind", kindName(target.kind)},
                                             {"entity", QString::number(target.entity)}});
    }
    if (transformTargets_.isEmpty() && tool_ == Tool::Move) {
        const auto candidate = acquiredInference();
        if (candidate && candidate->entityType == InferenceEntity::Vertex &&
            selection_.inContext(candidate->body) && !selection_.locked(doc_, candidate->body))
            transformTargets_.append(QJsonObject{{"body", QString::number(candidate->body)},
                                                 {"kind", "vertex"},
                                                 {"entity", QString::number(candidate->entity)}});
    }
    if (transformTargets_.isEmpty())
        throw std::runtime_error("Select geometry before choosing a transform pivot");
    transformFrame_ = {};
    if (transformLocal_) {
        const auto body = transformTargets_.first().toObject()["body"].toString().toULongLong();
        for (const auto &value : transformTargets_)
            if (value.toObject()["body"].toString().toULongLong() != body)
                throw std::runtime_error(
                    "Local axes require one context; use world axes for multiple contexts");
        transformFrame_ = doc_.worldTransform(body);
    }
}
void Viewport::beginTransform(Vec3 pivot) {
    checkPoint(pivot);
    session_.cancel();
    captureTransformTargets();
    clearPreview();
    clearConstraints();
    session_.begin();
    transformPivot_ = pivot;
    transformBase_.reset();
    transformEnd_.reset();
    transformPreview_ = {};
    anchor_ = cursor_ = pivot;
    transformAxis_ = transformLocal_ ? Vec3{0, 0, 1} : plane_.normal;
    if (transformLocal_) {
        const auto axis = transformFrame_.vector({1, 0, 0});
        plane_ = DrawingPlane::make(
            pivot, normalized(cross(axis, transformFrame_.vector({0, 1, 0}))), axis);
    } else
        plane_ = DrawingPlane::make(pivot, plane_.normal, plane_.xAxis);
    emit message(tool_ == Tool::Move ? "Choose destination or enter a displacement · Ctrl: copy"
                 : tool_ == Tool::Rotate
                     ? "Choose baseline, or enter an angle around the plane normal"
                     : "Choose a reference point, or enter one or three scale factors");
    update();
}
QJsonObject Viewport::transformCommand(const Transform &operation) const {
    if (transformTargets_.isEmpty())
        throw std::runtime_error("Choose a transform pivot first");
    QJsonArray matrix;
    for (auto value : operation.m)
        matrix.append(value);
    const auto pivot =
        transformLocal_ ? transformFrame_.inverse().point(transformPivot_) : transformPivot_;
    return {{"command", "geometry.transform_selection"},
            {"entities", transformTargets_},
            {"matrix", matrix},
            {"pivot", jsonPoint(pivot)},
            {"space", transformLocal_ ? "local" : "world"},
            {"copy", transformCopy_}};
}
QJsonObject Viewport::transformAt(Vec3 point) const {
    const auto inverse = transformFrame_.inverse();
    const auto vector = [&](Vec3 value) { return transformLocal_ ? inverse.vector(value) : value; };
    if (tool_ == Tool::Move)
        return transformCommand(Transform::translation(vector(point - transformPivot_)));
    if (!transformBase_)
        throw std::runtime_error("Choose a distinct reference point first");
    auto start = vector(*transformBase_ - transformPivot_);
    auto end = vector(point - transformPivot_);
    if (tool_ == Tool::Rotate) {
        const auto axis = normalized(transformAxis_);
        start = start - axis * dot(start, axis);
        end = end - axis * dot(end, axis);
        start = normalized(start);
        end = normalized(end);
        const auto angle = std::atan2(dot(axis, cross(start, end)), dot(start, end));
        return transformCommand(Transform::rotation(axis, angle));
    }
    const auto denominator = dot(start, start);
    if (denominator <= tolerance * tolerance)
        throw std::runtime_error("Scale reference must differ from pivot");
    const auto factor = dot(end, start) / denominator;
    return transformCommand(Transform::scaling({factor, factor, factor}));
}
void Viewport::transformClick(QPointF position) {
    try {
        if (!session_.active()) {
            choosePlane(position);
            const auto point = ground(position);
            if (!point)
                throw std::runtime_error("Choose a visible point or enter pivot coordinates");
            const auto inferred = acquiredInference();
            const auto vertex =
                tool_ == Tool::Move && inferred && inferred->entityType == InferenceEntity::Vertex;
            if (selection_.entities().empty() && !vertex) {
                if (auto target = selectionAt(position))
                    selectEntities({*target});
            }
            // Selecting a fallback face/edge refreshes inference; only vertex targets
            // consume the freshly acquired endpoint above.
            beginTransform(*point);
            toolPressed_ = tool_ == Tool::Move;
            toolPressPosition_ = position;
        } else if (const auto point = ground(position)) {
            if (tool_ != Tool::Move && !transformBase_) {
                if (length(*point - transformPivot_) <= tolerance)
                    throw std::runtime_error("Reference must differ from pivot");
                transformBase_ = point;
                toolPressed_ = true;
                toolPressPosition_ = position;
                emit message("Choose destination, drag, or enter the angle or scale factors");
            } else {
                transformEnd_ = point;
                finishTransform(transformAt(*point));
            }
        }
    } catch (const std::exception &error) {
        toolPressed_ = false;
        emit message(error.what());
    }
    update();
}
void Viewport::updateTransformPreview(QPointF point) {
    try {
        const auto end = ground(point);
        if (!end)
            throw std::runtime_error("Orbit to see the manipulation plane or enter measurements");
        cursor_ = end;
        transformEnd_ = end;
        if (tool_ != Tool::Move && !transformBase_) {
            previewEdges_ = {{{transformPivot_, *end}}};
            previewValid_ = false;
            previewError_.clear();
            update();
            return;
        }
        transformPreview_ = transformAt(*end);
        previewCommand(transformPreview_);
        if (tool_ == Tool::Move)
            emit measurementPreview(QLocale().toString(length(*end - transformPivot_), 'g', 8));
    } catch (const std::exception &error) {
        previewValid_ = false;
        previewEdges_.clear();
        previewGuides_.clear();
        previewError_ = error.what();
        update();
    }
}
void Viewport::finishTransform(const QJsonObject &command) {
    for (auto target : transformSelection_)
        if (!selectable(target))
            throw std::runtime_error("Transform source is no longer editable");
    const auto revision = doc_.revision();
    const auto result = session_.commit(command);
    if (revision == doc_.revision()) {
        session_.cancel();
        clearPreview();
        emit message("Transform made no change");
        return;
    }
    auto selected = transformSelection_;
    if (command["copy"].toBool()) {
        selected.clear();
        for (const auto &value : result["copies"].toArray()) {
            const auto mapping = value.toObject();
            const auto source = mapping["sourceBody"].toString().toULongLong();
            const auto body = mapping["body"].toString().toULongLong();
            for (auto entity : transformSelection_) {
                if (entity.body != source)
                    continue;
                if (entity.kind == SelectionKind::Body)
                    selected.insert({body, entity.kind, 0});
                else {
                    const auto field = entity.kind == SelectionKind::Face   ? "faces"
                                       : entity.kind == SelectionKind::Edge ? "edges"
                                                                            : "guides";
                    const auto id = mapping[field]
                                        .toObject()[QString::number(entity.entity)]
                                        .toString()
                                        .toULongLong();
                    if (id)
                        selected.insert({body, entity.kind, id});
                }
            }
        }
    }
    clearPreview();
    refresh();
    selectEntities(selected);
    emit changed();
    emit message("Transform complete · Type measurements to revise · Ctrl+Z undoes the operation");
}
bool Viewport::transformMeasurements(const QString &text) {
    if (session_.phase() == ToolSession::Phase::Committed && !session_.canRevise())
        throw std::runtime_error("Another edit changed the document; choose a new pivot");
    if (text.startsWith('[')) {
        const auto input = parseMeasurements(text, "m", QLocale());
        if (input.kind != MeasurementKind::AbsolutePoint || input.values.size() != 3)
            throw std::runtime_error("Enter world coordinates [x,y,z]");
        const Vec3 point{input.values[0], input.values[1], input.values[2]};
        if (!session_.active() && !session_.canRevise()) {
            plane_ = configuredPlane_.value_or(DrawingPlane{});
            beginTransform(point);
        } else if (tool_ != Tool::Move && !transformBase_) {
            if (length(point - transformPivot_) <= tolerance)
                throw std::runtime_error("Reference must differ from pivot");
            transformBase_ = point;
        } else {
            transformEnd_ = point;
            finishTransform(transformAt(point));
        }
        return true;
    }
    if (!session_.active() && !session_.canRevise())
        throw std::runtime_error("Choose a pivot or enter world coordinates [x,y,z] first");
    Transform matrix;
    if (tool_ == Tool::Rotate) {
        matrix = Transform::rotation(transformAxis_, parseAngle(text, "deg", QLocale()));
    } else if (tool_ == Tool::Scale) {
        const auto parts = text.split(QLocale().decimalPoint() == "," ? ';' : ',');
        if (parts.size() != 1 && parts.size() != 3)
            throw std::runtime_error("Enter one uniform factor or x, y, z scale factors");
        Vec3 scale;
        double *fields[] = {&scale.x, &scale.y, &scale.z};
        for (int i = 0; i < 3; ++i) {
            bool valid;
            *fields[i] = QLocale().toDouble(parts[parts.size() == 1 ? 0 : i].trimmed(), &valid);
            if (!valid || !std::isfinite(*fields[i]))
                throw std::runtime_error("Scale factors must be finite numbers without units");
        }
        matrix = Transform::scaling(scale);
    } else {
        const auto input = parseMeasurements(text, "m", QLocale());
        Vec3 delta;
        if ((input.kind == MeasurementKind::Values ||
             input.kind == MeasurementKind::RelativePoint) &&
            input.values.size() == 3) {
            delta = {input.values[0], input.values[1], input.values[2]};
        } else if (input.kind == MeasurementKind::Values && input.values.size() == 1) {
            auto direction = directionLocks_.current() ? directionLocks_.current()->direction
                             : transformEnd_           ? *transformEnd_ - transformPivot_
                                                       : plane_.xAxis;
            if (transformLocal_)
                direction = transformFrame_.inverse().vector(direction);
            delta = normalized(direction) * input.values[0];
        } else
            throw std::runtime_error(
                "Move expects a distance, x,y,z displacement or [world destination]");
        matrix = Transform::translation(delta);
    }
    finishTransform(transformCommand(matrix));
    return true;
}
void Viewport::flipSelection(int axis) {
    if (axis < 0 || axis > 2)
        throw std::runtime_error("Choose X, Y or Z flip axis");
    cancel();
    captureTransformTargets();
    Bounds bounds;
    auto include = [&](Vec3 point) {
        if (!bounds.valid) {
            bounds.minimum = bounds.maximum = point;
            bounds.valid = true;
        } else {
            bounds.minimum = {std::min(bounds.minimum.x, point.x),
                              std::min(bounds.minimum.y, point.y),
                              std::min(bounds.minimum.z, point.z)};
            bounds.maximum = {std::max(bounds.maximum.x, point.x),
                              std::max(bounds.maximum.y, point.y),
                              std::max(bounds.maximum.z, point.z)};
        }
    };
    for (auto target : transformSelection_) {
        const auto &body = *doc_.bodies().at(target.body);
        const auto world = doc_.worldTransform(target.body);
        if (target.kind == SelectionKind::Body) {
            for (const auto &[id, record] : doc_.bodies()) {
                auto ancestor = id;
                while (ancestor && ancestor != target.body)
                    ancestor = doc_.bodies().at(ancestor)->parent;
                if (!ancestor)
                    continue;
                for (const auto &[vertex, point] : record->surface.vertices)
                    include(doc_.worldTransform(id).point(point));
                for (const auto &[guide, value] : record->guides)
                    include(doc_.worldTransform(id).point(value.origin));
            }
        } else if (target.kind == SelectionKind::Face) {
            for (const auto &loop : body.surface.faces.at(target.entity).loops)
                for (auto vertex : loop)
                    include(world.point(body.surface.vertices.at(vertex)));
        } else if (target.kind == SelectionKind::Edge) {
            const auto &edge = body.topology.edges.at(target.entity);
            include(world.point(body.surface.vertices.at(edge.a)));
            include(world.point(body.surface.vertices.at(edge.b)));
        } else
            include(world.point(body.guides.at(target.entity).origin));
    }
    transformPivot_ = bounds.valid ? (bounds.minimum + bounds.maximum) * .5 : Vec3{};
    session_.begin();
    Vec3 factors{1, 1, 1};
    (axis == 0 ? factors.x : axis == 1 ? factors.y : factors.z) = -1;
    finishTransform(transformCommand(Transform::scaling(factors)));
}
} // namespace sketchy
