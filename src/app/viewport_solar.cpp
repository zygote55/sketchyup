#include "app/viewport.hpp"
#include "io/solar_io.hpp"
#include <algorithm>
#include <limits>
namespace sketchy {
void Viewport::applyModelSolar(const SolarSettings &solar) {
    solar.validate();
    if (solar == doc_.solar())
        return;
    cancel();
    commitCommands(
        {QJsonObject{{"command", "document.solar"}, {"solar", encodeSolarSettings(solar)}}}, false);
    refresh();
    emit changed();
}
void Viewport::syncSolar() {
    if (displayedSolar_ && *displayedSolar_ == doc_.solar())
        return;
    displayedSolar_ = doc_.solar();
    displayedSun_ = solarPosition(doc_.solar());
    ++presentationRevision_;
    cacheDirty_ = transparentDirty_ = true;
    groundMatrix_ = {};
}
float Viewport::solarLight(Vec3 normal) const {
    return .3f + (displayedSun_.aboveHorizon
                      ? .7f * float(std::max(0., dot(normal, displayedSun_.direction)))
                      : 0.f);
}
void Viewport::cleanupSolarShadowMap() {
    if (solarShadowFramebuffer_)
        gl_->glDeleteFramebuffers(1, &solarShadowFramebuffer_);
    if (solarShadowTexture_)
        gl_->glDeleteTextures(1, &solarShadowTexture_);
    solarShadowFramebuffer_ = solarShadowTexture_ = 0;
}
void Viewport::drawSolarShadowMap(const QMatrix4x4 &viewMatrix) {
    shader_->setUniformValue("solarShadowEnabled", 0);
    if (!doc_.solar().enabled || !doc_.solar().shadows || !displayedSun_.aboveHorizon ||
        doc_.style().mode == ModelStyleMode::Wireframe || doc_.style().mode == ModelStyleMode::XRay)
        return;
    constexpr int resolution = 2048;
    const auto sun = displayedSun_.direction;
    auto right = cross({0, 0, 1}, sun);
    right = length(right) > 1e-8 ? right * (1 / length(right)) : Vec3{1, 0, 0};
    const auto up = cross(sun, right);
    const std::array<Vec3, 3> axes{right, up, sun};
    const double infinity = std::numeric_limits<double>::infinity();
    std::array<double, 3> low{infinity, infinity, infinity}, high{-infinity, -infinity, -infinity};
    size_t count{};
    auto include = [&](const Vertex &v) {
        const Vec3 relative{v.x - renderOrigin_.x, v.y - renderOrigin_.y, v.z - renderOrigin_.z};
        for (size_t i = 0; i < 3; ++i) {
            const auto p = dot(axes[i], relative);
            low[i] = std::min(low[i], p);
            high[i] = std::max(high[i], p);
        }
        ++count;
    };
    for (const auto &[id, cache] : bodyCaches_) {
        for (const auto &v : cache->opaque)
            include(v);
        for (const auto &triangle : cache->transparent)
            for (const auto &v : triangle)
                include(v);
    }
    if (!count)
        return;
    if (doc_.style().groundVisible) {
        // Include the receiver plane's depth range without expanding the caster XY footprint.
        const double extent = std::max(100., double(distance_) * 8);
        for (double dx : {-extent, extent})
            for (double dy : {-extent, extent}) {
                const auto z =
                    dot(sun, Vec3{target_.x + dx, target_.y + dy, doc_.style().groundHeight} -
                                 renderOrigin_);
                low[2] = std::min(low[2], z);
                high[2] = std::max(high[2], z);
            }
    }
    for (size_t i = 0; i < 3; ++i) {
        const double margin = std::max(.001, (high[i] - low[i]) * .01);
        low[i] -= margin;
        high[i] += margin;
    }
    QMatrix4x4 light;
    for (size_t row = 0; row < 3; ++row) {
        const double scale = (row == 2 ? -2. : 2.) / (high[row] - low[row]);
        light(int(row), 0) = float(axes[row].x * scale);
        light(int(row), 1) = float(axes[row].y * scale);
        light(int(row), 2) = float(axes[row].z * scale);
        light(int(row), 3) = float(-(high[row] + low[row]) * .5 * scale);
    }
    const auto bias = float(
        std::clamp(std::max(high[0] - low[0], high[1] - low[1]) / (resolution * (high[2] - low[2])),
                   1e-7, .005));
    GLint drawFramebuffer{}, readFramebuffer{}, viewport[4];
    gl_->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    gl_->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    gl_->glGetIntegerv(GL_VIEWPORT, viewport);
    gl_->glActiveTexture(GL_TEXTURE2);
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
    if (!solarShadowTexture_) {
        gl_->glGenTextures(1, &solarShadowTexture_);
        gl_->glBindTexture(GL_TEXTURE_2D, solarShadowTexture_);
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, resolution, resolution, 0,
                          GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const GLfloat white[4]{1, 1, 1, 1};
        gl_->glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, white);
        gl_->glGenFramebuffers(1, &solarShadowFramebuffer_);
        gl_->glBindFramebuffer(GL_FRAMEBUFFER, solarShadowFramebuffer_);
        gl_->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                    solarShadowTexture_, 0);
        gl_->glDrawBuffer(GL_NONE);
        gl_->glReadBuffer(GL_NONE);
        if (gl_->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            gl_->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(drawFramebuffer));
            gl_->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(readFramebuffer));
            throw std::runtime_error("Could not allocate the sun shadow depth buffer");
        }
        gl_->glBindTexture(GL_TEXTURE_2D, 0);
    } else
        gl_->glBindFramebuffer(GL_FRAMEBUFFER, solarShadowFramebuffer_);
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glViewport(0, 0, resolution, resolution);
    gl_->glClearDepth(1);
    gl_->glClear(GL_DEPTH_BUFFER_BIT);
    gl_->glEnable(GL_POLYGON_OFFSET_FILL);
    gl_->glPolygonOffset(2, 4);
    shader_->setUniformValue("mvp", light);
    shader_->setUniformValue("surfacePass", 4);
    for (auto &[id, cache] : bodyCaches_)
        draw(cache->opaqueGpu, GL_TRIANGLES);
    draw(transparentGpu_, GL_TRIANGLES);
    gl_->glDisable(GL_POLYGON_OFFSET_FILL);
    gl_->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(drawFramebuffer));
    gl_->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(readFramebuffer));
    gl_->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    shader_->setUniformValue("mvp", viewMatrix);
    shader_->setUniformValue("surfacePass", 0);
    shader_->setUniformValue("solarShadowMatrix", light);
    shader_->setUniformValue("solarShadowBias", bias);
    shader_->setUniformValue("solarShadowMap", 2);
    shader_->setUniformValue("solarShadowEnabled", 1);
    gl_->glActiveTexture(GL_TEXTURE2);
    gl_->glBindTexture(GL_TEXTURE_2D, solarShadowTexture_);
    gl_->glActiveTexture(GL_TEXTURE0);
}

} // namespace sketchy
