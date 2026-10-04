#include "app/viewport.hpp"
#include <QKeyEvent>
#include <algorithm>
namespace sketchy {
namespace {
bool sameSource(const InferenceCandidate &a, const InferenceCandidate &b) {
    return a.kind == b.kind && a.body == b.body && a.entity == b.entity &&
           a.entityType == b.entityType && a.otherBody == b.otherBody &&
           a.otherEntity == b.otherEntity;
}
} // namespace
std::optional<InferenceCandidate> Viewport::acquiredInference() const {
    if (heldPoint_)
        return heldPoint_;
    if (directionLocks_.current() || inferenceChoice_ >= inference_.candidates.size())
        return {};
    return inference_.candidates[inferenceChoice_];
}
std::optional<DirectionCandidate> Viewport::acquiredDirection() const {
    if (const auto lock = directionLocks_.current()) {
        const auto projected = projectDirection(*lock, inferenceCamera(), inferencePointer_.x(),
                                                inferencePointer_.y());
        if (!projected)
            return {};
        for (const auto &candidate : inference_.candidates)
            if (length(cross(candidate.point - lock->origin, lock->direction)) <= tolerance)
                return DirectionCandidate{*lock, candidate.point, candidate.pixels};
        return projected;
    }
    if (inferenceChoice_ < inference_.candidates.size())
        return {};
    const auto index = inferenceChoice_ - inference_.candidates.size();
    return index < directions_.size() ? std::optional<DirectionCandidate>{directions_[index]}
                                      : std::nullopt;
}
void Viewport::acquireInference(QPointF point, bool constrainPlane) {
    if (referenceStamp_ && !doc_.isCurrentSnapshot(*referenceStamp_))
        clearConstraints();
    const auto previous = acquiredInference();
    const auto previousDirection = acquiredDirection();
    if ((point - inferencePointer_).manhattanLength() >= 2)
        inferenceCycled_ = false;
    const auto preserve = inferenceCycled_;
    inferenceWorker_.request(doc_);
    const auto index = inferenceWorker_.ready(doc_);
    inferencePending_ = !index;
    const auto camera = inferenceCamera();
    const auto lock = directionLocks_.current();
    auto queryPoint = point;
    auto queryPlane = constrainPlane ? std::optional<DrawingPlane>{plane_} : configuredPlane_;
    if (lock) {
        if (const auto projected = projectDirection(*lock, camera, point.x(), point.y()))
            if (const auto screen = camera.project(projected->point))
                queryPoint = QPointF(screen->x, screen->y);
        if (tool_ == Tool::Line && std::abs(dot(lock->direction, plane_.normal)) > 1e-10)
            queryPlane.reset();
    }
    inference_ = index ? index->query({camera, queryPoint.x(), queryPoint.y(), 8, queryPlane, 0})
                       : InferenceResult{};
    std::erase_if(inference_.candidates, [&](const auto &candidate) {
        return clipped(candidate.point) ||
               (lock && length(cross(candidate.point - lock->origin, lock->direction)) > tolerance);
    });
    directions_.clear();
    if (constrainPlane && anchor_) {
        if (reference_ && referenceAnchor_ != anchor_) {
            referenceDirections_ = edgeDirections(doc_, *reference_, *anchor_, plane_);
            referenceAnchor_ = anchor_;
        }
        directions_ = directionCandidates(
            inferenceCamera(), point.x(), point.y(), *anchor_, plane_, referenceDirections_,
            reference_ ? std::optional<Vec3>{reference_->point} : std::nullopt);
        std::erase_if(directions_, [&](const auto &candidate) { return clipped(candidate.point); });
    }
    inferenceChoice_ = 0;
    if (!directions_.empty() && (inference_.candidates.empty() ||
                                 inference_.candidates.front().kind == InferenceKind::OnFace))
        inferenceChoice_ = inference_.candidates.size();
    if (preserve && previous)
        for (size_t i = 0; i < inference_.candidates.size(); ++i)
            if (sameSource(inference_.candidates[i], *previous)) {
                inferenceChoice_ = i;
                break;
            }
    if (preserve && previousDirection)
        for (size_t i = 0; i < directions_.size(); ++i)
            if (directions_[i].constraint == previousDirection->constraint) {
                inferenceChoice_ = inference_.candidates.size() + i;
                break;
            }
    inferencePointer_ = point;
    const auto candidate = acquiredInference();
    if (!candidate || candidate->kind == InferenceKind::OnFace || shiftHeld_ || dragging_) {
        hoverReference_.reset();
    } else if (!hoverReference_ || !sameSource(*candidate, *hoverReference_) ||
               (point - referencePointer_).manhattanLength() > 4) {
        hoverReference_ = candidate;
        referencePointer_ = point;
        referenceTimer_.restart();
    }
}
void Viewport::armReference() {
    if (!hoverReference_ || !referenceTimer_.isValid() || referenceTimer_.elapsed() < 450 ||
        !hasFocus() || dragging_ || shiftHeld_ || !drawingTool() ||
        (reference_ && sameSource(*reference_, *hoverReference_) &&
         length(reference_->point - hoverReference_->point) <= tolerance))
        return;
    reference_ = hoverReference_;
    referenceStamp_ = doc_.saveStamp();
    referenceAnchor_.reset();
    referenceDirections_.clear();
    emit message("Reference point armed · Move away to align with it");
    if (session_.active())
        updateToolPreview(inferencePointer_);
    update();
}
void Viewport::releaseInferenceHold() {
    shiftHeld_ = false;
    directionLocks_.release();
    heldPoint_.reset();
    heldPlane_.reset();
    heldContext_ = 0;
}
void Viewport::clearConstraints() {
    releaseInferenceHold();
    directionLocks_.clear();
    directions_.clear();
    inferenceCycled_ = false;
    reference_.reset();
    hoverReference_.reset();
    referenceStamp_.reset();
    referenceAnchor_.reset();
    referenceDirections_.clear();
}
void Viewport::validateLockedPoint(Vec3 point) const {
    if (const auto lock = directionLocks_.current();
        lock && length(cross(point - lock->origin, lock->direction)) > tolerance)
        throw std::runtime_error("Coordinate does not lie on the locked direction");
    if (heldPoint_ && length(point - heldPoint_->point) > tolerance)
        throw std::runtime_error("Coordinate differs from the held inference point");
}
bool Viewport::constraintKey(QKeyEvent *event) {
    if (!drawingTool() || tool_ == Tool::Freehand ||
        (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
        return false;
    const auto key = event->key();
    if (key != Qt::Key_Shift && key != Qt::Key_Left && key != Qt::Key_Right && key != Qt::Key_Up &&
        key != Qt::Key_Down)
        return false;
    if (event->isAutoRepeat())
        return true;
    try {
        if (key == Qt::Key_Shift) {
            if (shiftHeld_)
                return true;
            if (const auto direction = acquiredDirection())
                directionLocks_.hold(direction->constraint);
            else if (const auto candidate = acquiredInference()) {
                if (candidate->kind == InferenceKind::OnFace) {
                    if (!anchor_)
                        choosePlane(inferencePointer_);
                    heldPlane_ = plane_;
                    heldContext_ = drawingContext_;
                } else
                    heldPoint_ = candidate;
            } else if (anchor_ && cursor_ && length(*cursor_ - *anchor_) > tolerance)
                directionLocks_.hold({DirectionKind::FromPoint, *anchor_, *cursor_ - *anchor_});
            else if (anchor_) {
                heldPlane_ = plane_;
                heldContext_ = drawingContext_;
            }
            shiftHeld_ = true;
        } else {
            if (!anchor_) {
                emit message("Choose the first point before locking a direction");
                return true;
            }
            DirectionConstraint constraint{DirectionKind::RedAxis, *anchor_, {1, 0, 0}};
            if (key == Qt::Key_Left)
                constraint = {DirectionKind::GreenAxis, *anchor_, {0, 1, 0}};
            else if (key == Qt::Key_Up)
                constraint = {DirectionKind::BlueAxis, *anchor_, {0, 0, 1}};
            else if (key == Qt::Key_Down) {
                const auto source = reference_ ? reference_ : acquiredInference();
                if (!source)
                    throw std::runtime_error("Hover an edge to use parallel or perpendicular");
                reference_ = source;
                referenceStamp_ = doc_.saveStamp();
                referenceAnchor_.reset();
                auto references = edgeDirections(doc_, *source, *anchor_, plane_);
                const auto current = directionLocks_.current();
                if (current && current->kind == DirectionKind::Perpendicular) {
                    directionLocks_.clear();
                    updateToolPreview(inferencePointer_);
                    return true;
                }
                const auto kind = current && current->kind == DirectionKind::Parallel
                                      ? DirectionKind::Perpendicular
                                      : DirectionKind::Parallel;
                auto found = std::find_if(references.begin(), references.end(),
                                          [&](const auto &value) { return value.kind == kind; });
                if (found == references.end())
                    throw std::runtime_error("Hover an edge to use parallel or perpendicular");
                constraint = *found;
            }
            if (tool_ != Tool::Line && std::abs(dot(constraint.direction, plane_.normal)) > 1e-10)
                throw std::runtime_error("This axis is outside the shape's drawing plane");
            releaseInferenceHold();
            directionLocks_.toggle(constraint);
        }
        referenceStamp_ = doc_.saveStamp();
        if (session_.active())
            updateToolPreview(inferencePointer_);
        emit message(directionLocks_.current()
                         ? QString("Locked · ") + directionLabel(directionLocks_.current()->kind)
                     : heldPoint_ ? "Locked inference point"
                     : heldPlane_ ? "Locked drawing plane"
                                  : "Inference lock released");
        update();
    } catch (const std::exception &error) {
        emit message(error.what());
    }
    return true;
}
void Viewport::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Shift && !event->isAutoRepeat()) {
        releaseInferenceHold();
        if (session_.active())
            updateToolPreview(inferencePointer_);
        update();
        event->accept();
    } else
        QOpenGLWidget::keyReleaseEvent(event);
}
} // namespace sketchy
