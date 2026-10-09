#pragma once
#include <QSurfaceFormat>

namespace sketchy {
// Configure before QApplication so the top-level compositor surface, not just
// the QOpenGLWidget framebuffer, uses full RGB channels. Unspecified sizes can
// select RGB565; odd-width software Wayland buffers then have unaligned strides.
inline void setDefaultViewportFormat(QSurfaceFormat format) {
    format.setRedBufferSize(8);
    format.setGreenBufferSize(8);
    format.setBlueBufferSize(8);
    format.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
}
} // namespace sketchy
