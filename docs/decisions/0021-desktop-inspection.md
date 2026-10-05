# Desktop inspection and current-view capture

R040.c binds `Window::inspect` to that window's actual document, viewport,
selection and retained inspection session. Dispatch runs on the GUI thread;
other threads receive `WRONG_THREAD` before model or widget access. The existing
[bounded inspection](0019-bounded-inspection.md) and
[snapshot](0020-inspection-snapshots.md) contracts apply to document queries.
The service does not open files, alter selection, cancel gestures, move the
camera, or write captures to disk.

`desktopInspectionCapabilities()` generates the executable registry published as
[`inspection-desktop-v1.json`](../api/inspection-desktop-v1.json). The installed
application prints it with `sketchyup --inspection-capabilities`, without opening
a window or model. Discovery cannot be combined with model/rendering options.
The schema and this contract ship with desktop packages. Persistent transports
and providers remain later roadmap work; this is in-process dispatch.

`view.describe` and `view.capture` require API version 1, the current document ID
and exact current revision string. Unknown fields are rejected. They report the
current view only: snapshot IDs are unsupported, so an old retained model read
cannot be mistaken for the current rendered scene. Requests have the common
16 KiB limit. Revision and document validation precede capture.

View metadata includes projection, field of view in radians, logical viewport
size, device pixel ratio, selection count, active context, hidden/guide display
flags, visibility, renderer readiness and gesture/override state. Camera matrices
are column-major 4×4 `clipFromWorld` and `worldFromClip`. OpenGL clip coordinates
use [-1,1] on each axis; image coordinates start at the top left with inverted Y.
The common envelope specifies meters, Z-up and the document tolerance.

Capture reads the viewport framebuffer synchronously. It includes rendered
selection outlines, guides and viewport overlays, but not surrounding application
chrome. It does not reframe the model. A hidden or uninitialized viewport returns
`VIEW_UNAVAILABLE`. A pending gesture returns `VIEW_BUSY` without cancelling it.
Benchmark buffers, clipping planes and temporary opacity overrides return
`UNSUPPORTED_VIEW`. Native material opacity is part of the ordinary model render.
The document save stamp/revision and camera/editor-state digest must remain
unchanged across readback; otherwise capture returns `VIEW_CHANGED`.

Source images are limited to 8192 pixels per edge and 16,777,216 total pixels,
checked from physical viewport dimensions before readback and actual image size
afterward. `maxWidth` and `maxHeight` each accept integers 64–1024, default 768.
The output shrinks to fit with preserved aspect ratio and never enlarges the
source. It is RGB PNG, at most 3 MiB, embedded as base64. The complete compact
response is capped at 4 MiB + 64 KiB. Excess size returns `CAPTURE_LIMIT`; callers
may request smaller dimensions. Failed readback or encoding returns
`VIEW_UNAVAILABLE`.

Successful capture includes encoded image, MIME type, encoding, SHA-256, output
and source dimensions, UTC capture time and the matching camera metadata. All
model bytes, history, saved markers, selection and camera remain unchanged.
Readback and PNG encoding execute synchronously on the GUI thread within these
size bounds; asynchronous transport dispatch must marshal requests to that thread.
