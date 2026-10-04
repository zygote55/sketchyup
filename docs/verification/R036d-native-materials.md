# R036.d: native material controls

Date: 2026-10-04. Local validation passed; CI pending.
Requires [PR #47](https://github.com/zygote55/sketchyup/pull/47).

The Materials tab exposes document swatches, a local preset library, color/opacity editing, independent
front/back assignment, resource attachment/replacement, explicit missing files,
and unused resource cleanup. B activates Paint; Alt-click samples the visible
physical side. Opened-component assignment publishes shared geometry ownership;
global swatch/resource changes use document scope. Info displays both sides.
See the [interaction decision](../decisions/0013-native-material-controls.md).

Final native input runs passed on X11 and isolated Weston at scale 1 and 2:
creation, color/opacity, retained validation errors, precision-preserving rename,
assignment/undo, reflected sampling, shared component painting, resource import
and resolution, source deletion/reopen, cleanup and stale-editor rejection.
The filename chooser test uses actual text entry after programmatic selectFile
proved insufficient to submit the file selection in this Qt environment.

Screenshot inspection caught button compression in the narrow panel. A scrollable
content area and readable button heights corrected it; the updated X11 run passed.
The final development suite passed 35/35. Existing native X11 material-rendering,
Info, component, selection and shell suites passed. Core-only sanitizer code is
unchanged from R036.b’s 28/28 run. Extra checks cover local presets, mixed face/edge
selection, rejection of painting closed components and canceled file selection.
[Scrolled resource controls](R036d-material-controls-x11.png) show the corrected
button layout during the shared-component fixture.

These are implementation-agent checks, not independent human acceptance. Image
UV mapping/rendering remains later work. Intersecting transparency retains the
limitation described in the renderer decision.

PR #48 merged on 2026-10-04 after both Native build runs passed at
`ef8faed03bfc170b92a07cc1bbeb461588588adf`; merge commit `884594b`.
All four R036 implementation layers are now merged.
