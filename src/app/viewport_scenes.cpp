#include "app/viewport.hpp"
#include "core/scenes.hpp"
#include "io/scenes_io.hpp"
#include <QApplication>
#include <QSignalBlocker>
#include <QTabBar>
#include <QVariantAnimation>
namespace sketchy {
void Viewport::initializeSceneViews() {
    sceneTabs_ = new QTabBar(this);
    sceneTabs_->setObjectName("viewportSceneTabs");
    sceneTabs_->setAccessibleName("Saved scene views");
    sceneTabs_->setExpanding(false);
    sceneTabs_->setUsesScrollButtons(true);
    sceneTabs_->setElideMode(Qt::ElideRight);
    sceneTabs_->setFocusPolicy(Qt::StrongFocus);
    sceneTabs_->hide();
    auto recallTab = [this](int index) {
        if (index < 0)
            return;
        try {
            recallSavedScene(sceneTabs_->tabData(index).toULongLong());
        } catch (const std::exception &error) {
            emit message(error.what());
        }
    };
    connect(sceneTabs_, &QTabBar::tabBarClicked, this, recallTab);
    connect(sceneTabs_, &QTabBar::currentChanged, this, [recallTab](int index) {
        if (QApplication::mouseButtons() == Qt::NoButton)
            recallTab(index);
    });
    sceneAnimation_ = new QVariantAnimation(this);
    syncSceneTabs();
    sceneAnimation_->setDuration(160);
    sceneAnimation_->setEasingCurve(QEasingCurve::InOutCubic);
    sceneAnimation_->setStartValue(0.);
    sceneAnimation_->setEndValue(1.);
    connect(sceneAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        if (!doc_.isCurrentSnapshot(sceneAnimationStamp_)) {
            stopSceneTransition();
            return;
        }
        const auto t = value.toDouble();
        auto camera = sceneCameraTo_;
        camera.target = sceneCameraFrom_.target * (1 - t) + sceneCameraTo_.target * t;
        camera.yaw =
            std::remainder(sceneCameraFrom_.yaw +
                               std::remainder(sceneCameraTo_.yaw - sceneCameraFrom_.yaw, 360.) * t,
                           360.);
        camera.pitch = sceneCameraFrom_.pitch * (1 - t) + sceneCameraTo_.pitch * t;
        camera.distance = std::exp(std::log(sceneCameraFrom_.distance) * (1 - t) +
                                   std::log(sceneCameraTo_.distance) * t);
        camera.fieldOfView =
            sceneCameraFrom_.fieldOfView * (1 - t) + sceneCameraTo_.fieldOfView * t;
        if (t >= 1)
            camera = sceneCameraTo_;
        sceneCameraStep_ = true;
        applySceneCamera(camera);
        sceneCameraStep_ = false;
    });
}
void Viewport::stopSceneTransition() {
    if (sceneAnimation_ && !sceneCameraStep_)
        sceneAnimation_->stop();
}
void Viewport::setReducedMotion(bool enabled) {
    reducedMotion_ = enabled;
    if (enabled && sceneAnimation_ && sceneAnimation_->state() == QAbstractAnimation::Running) {
        stopSceneTransition();
        if (doc_.isCurrentSnapshot(sceneAnimationStamp_))
            applySceneCamera(sceneCameraTo_);
    }
}
void Viewport::layoutSceneTabs() {
    if (!sceneTabs_)
        return;
    const auto height = sceneTabs_->sizeHint().height();
    sceneTabs_->setGeometry(8, std::max(0, this->height() - height - 8), std::max(1, width() - 16),
                            height);
    sceneTabs_->raise();
}
void Viewport::syncSceneTabs() {
    const auto order = orderedScenes(doc_);
    bool same = sceneTabs_->count() == int(order.size());
    for (int i = 0; same && i < sceneTabs_->count(); ++i)
        same = sceneTabs_->tabData(i).toULongLong() == order[size_t(i)] &&
               sceneTabs_->tabText(i) ==
                   QString::fromStdString(doc_.scenes().at(order[size_t(i)])->name);
    if (same) {
        layoutSceneTabs();
        return;
    }
    const QSignalBlocker blocked(sceneTabs_);
    const auto previous = sceneTabs_->currentIndex() >= 0
                              ? sceneTabs_->tabData(sceneTabs_->currentIndex()).toULongLong()
                              : 0;
    while (sceneTabs_->count())
        sceneTabs_->removeTab(0);
    for (const auto id : orderedScenes(doc_)) {
        const auto &scene = *doc_.scenes().at(id);
        const auto index = sceneTabs_->addTab(QString::fromStdString(scene.name));
        sceneTabs_->setTabData(index, QVariant::fromValue<qulonglong>(id));
        sceneTabs_->setTabToolTip(index, "Recall " + QString::fromStdString(scene.name));
        if (id == previous)
            sceneTabs_->setCurrentIndex(index);
    }
    sceneTabs_->setVisible(sceneTabs_->count() > 0);
    layoutSceneTabs();
}
SceneSnapshot Viewport::captureSceneSnapshot(bool camera, bool visibility, bool style,
                                             bool section) const {
    SceneSnapshot snapshot;
    if (camera)
        snapshot.camera = SceneCamera{target_, yaw_, pitch_, distance_, fov_, ortho_};
    if (style)
        snapshot.style = doc_.style();
    if (section)
        snapshot.section = SceneSection{clipPlane_};
    if (visibility) {
        SceneVisibility view;
        for (const auto &[id, body] : doc_.bodies())
            view.bodyVisible[id] = !body->hidden;
        for (const auto &[id, tag] : doc_.tags())
            view.tagVisible[id] = tag->visible;
        for (const auto &entity : selection_.hiddenEntities()) {
            if (!selection_.belongsTo(doc_) || !selection_.exists(doc_, entity))
                continue;
            const auto kind = entity.kind == SelectionKind::Body   ? SceneEntityKind::Body
                              : entity.kind == SelectionKind::Face ? SceneEntityKind::Face
                              : entity.kind == SelectionKind::Edge ? SceneEntityKind::Edge
                                                                   : SceneEntityKind::Guide;
            view.hiddenEntities.insert({entity.body, kind, entity.entity});
        }
        view.showHidden = selection_.belongsTo(doc_) && selection_.showingHidden();
        snapshot.visibility = std::move(view);
    }
    validateSceneCapture(doc_, snapshot);
    return snapshot;
}
QJsonObject Viewport::editSavedScenes(const QJsonArray &commands) {
    for (const auto &command : commands)
        if (!command.toObject()["command"].toString().startsWith("saved_scene."))
            throw std::runtime_error("Expected saved scene edit");
    cancel();
    auto result = commitCommands(commands, false);
    refresh();
    emit changed();
    return result;
}
void Viewport::applySceneCamera(const SceneCamera &camera) {
    target_ = camera.target;
    yaw_ = float(camera.yaw);
    pitch_ = float(camera.pitch);
    distance_ = float(camera.distance);
    fov_ = float(camera.fieldOfView);
    ortho_ = camera.orthographic;
    cameraChanged();
}
void Viewport::recallSavedScene(Id id) {
    if (!doc_.scenes().contains(id))
        throw std::runtime_error("Saved scene no longer exists");
    const auto snapshot = doc_.scenes().at(id)->snapshot;
    const auto missing = missingSceneReferences(doc_, snapshot);
    const auto modelEdit = sceneRecallEdit(doc_, id);
    cancel();
    const auto from = SceneCamera{target_, yaw_, pitch_, distance_, fov_, ortho_};
    if (sceneRecallChangesModel(modelEdit))
        commitCommands(
            {QJsonObject{{"command", "saved_scene.recall"}, {"scene", QString::number(id)}}},
            false);
    if (snapshot.visibility)
        selection_.restoreSceneVisibility(doc_, *snapshot.visibility);
    if (snapshot.section)
        clipPlane_ = snapshot.section->plane;
    if (snapshot.camera) {
        if (reducedMotion_ || from == *snapshot.camera)
            applySceneCamera(*snapshot.camera);
        else {
            sceneCameraFrom_ = from;
            sceneCameraTo_ = *snapshot.camera;
            sceneAnimationStamp_ = doc_.saveStamp();
            sceneAnimation_->start();
        }
    }
    refresh();
    {
        const QSignalBlocker blocked(sceneTabs_);
        for (int i = 0; i < sceneTabs_->count(); ++i)
            if (sceneTabs_->tabData(i).toULongLong() == id)
                sceneTabs_->setCurrentIndex(i);
    }
    emit changed();
    emit sceneRecalled(id);
    if (!missing.empty())
        emit message(
            QString("Scene recalled; %1 missing references were skipped").arg(missing.size()));
}
} // namespace sketchy
