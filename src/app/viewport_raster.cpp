#include "app/viewport.hpp"
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QSaveFile>
#include <QVariantAnimation>
namespace sketchy {
QImage Viewport::renderRaster(QSize pixels) {
    constexpr qint64 pixelLimit = 16 * 1024 * 1024;
    if (pixels.width() < 1 || pixels.height() < 1 || pixels.width() > 8192 ||
        pixels.height() > 8192 || qint64(pixels.width()) * pixels.height() > pixelLimit)
        throw std::runtime_error("Export needs 1–8192 pixels per side and at most 16 megapixels");
    if (!ready_ || !context() || !isVisible())
        throw std::runtime_error("Open a visible, initialized model view before exporting");
    if (inspectionBusy() || benchmarkTriangles_ || !rasterSize_.isEmpty() ||
        (sceneAnimation_ && sceneAnimation_->state() == QAbstractAnimation::Running))
        throw std::runtime_error(
            "Finish the active interaction or camera transition before exporting");
    auto *previousContext = QOpenGLContext::currentContext();
    auto *previousSurface = previousContext ? previousContext->surface() : nullptr;
    makeCurrent();
    if (QOpenGLContext::currentContext() != context())
        throw std::runtime_error("Could not activate the model graphics context");
    GLint drawFramebuffer{}, readFramebuffer{}, viewport[4]{};
    gl_->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    gl_->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    gl_->glGetIntegerv(GL_VIEWPORT, viewport);
    auto restore = [&] {
        rasterSize_ = {};
        // Profile thickness is specified in output pixels, even for an identical aspect ratio.
        profilesDirty_ = true;
        if (shader_)
            shader_->release();
        gl_->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(drawFramebuffer));
        gl_->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(readFramebuffer));
        gl_->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        if (previousContext && previousSurface)
            previousContext->makeCurrent(previousSurface);
        else
            doneCurrent();
        update();
    };
    try {
        GLint textureLimit{}, bufferLimit{};
        gl_->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
        gl_->glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &bufferLimit);
        const auto limit = std::min(textureLimit, bufferLimit);
        if (pixels.width() > limit || pixels.height() > limit)
            throw std::runtime_error("Requested image exceeds this graphics device's size limit");
        syncTextures();
        if (textureCache_.pending())
            throw std::runtime_error(
                "Images are still loading. Wait for the preview and export again");
        if (textureFallbacks_)
            throw std::runtime_error(
                "Some image pixels are unavailable. Restore them before exporting");
        QImage result;
        {
            QOpenGLFramebufferObjectFormat format;
            format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
            format.setInternalTextureFormat(GL_RGBA8);
            format.setSamples(0);
            QOpenGLFramebufferObject target(pixels, format);
            if (!target.isValid() || !target.bind())
                throw std::runtime_error("Could not allocate the requested export image");
            rasterSize_ = pixels;
            profilesDirty_ = true;
            gl_->glViewport(0, 0, pixels.width(), pixels.height());
            QOpenGLPaintDevice device(pixels);
            device.setDevicePixelRatio(1);
            paintScene(&device);
            if (gl_->glGetError() != GL_NO_ERROR)
                throw std::runtime_error("The graphics device could not render the export image");
            result = target.toImage();
            if (result.isNull() || result.size() != pixels)
                throw std::runtime_error("Could not read the complete export image");
            result.setDevicePixelRatio(1);
        }
        restore();
        return result;
    } catch (...) {
        restore();
        throw;
    }
}
void Viewport::exportRaster(const QString &path, QSize pixels) {
    const auto image = renderRaster(pixels);
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG") || !output.commit())
        throw std::runtime_error("Could not save the PNG; the previous destination was retained");
}
} // namespace sketchy
