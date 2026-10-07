# ADR 0157: Request full RGB channels before native application initialization

Status: Accepted for implementation; platform/release acceptance remains gated.

The desktop and native test entry points use `setDefaultViewportFormat` before
constructing QApplication. It requests at least eight bits for each RGB and alpha
channel while retaining each caller's OpenGL version, profile, depth and sample
settings. The viewport regression records negotiated channel sizes and requires
at least eight RGB bits. This also keeps application and fixture initialization
consistent.

Leaving channel sizes unspecified allowed EGL to choose RGB565 for the top-level
Wayland compositor surface. At fractional client scaling, odd pixel widths led
to two-byte-aligned buffer strides and exposed a Weston/Mesa software-rendering
crash. Changing only QOpenGLWidget's format did not change that compositor
surface. The application-wide default removes the observed low-depth selection;
it does not patch the compositor or claim that all upstream defects are fixed.

Qt documents the role of the application default in
[QSurfaceFormat](https://doc.qt.io/qt-6/qsurfaceformat.html#setDefaultFormat) and
the initialization requirements of
[QOpenGLWidget](https://doc.qt.io/qt-6/qopenglwidget.html).
Physical output transitions and hardware support remain separate acceptance work.
