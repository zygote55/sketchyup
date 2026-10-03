# R002.c: dialogs and pointer lifecycle

Date: 2026-10-03. Host/toolchain: same as R002.b. This slice covers the desktop
adapter; it does not claim screen-reader, touch, tablet or compositor acceptance.

Save As now chooses the final suffix before our explicit overwrite confirmation.
This prevents an extensionless selection from silently replacing an existing
`.sketchyup` file. Cancel is the replacement prompt's default. Ordinary Save keeps
the previously accepted path. QSaveFile still handles atomic replacement.

`dialog_tests` exercises native-enabled QFileDialog instances, using the host's
GTK3 platform theme on Wayland and X11. It checks save/open cancel, default suffix,
replacement cancel/accept, roundtrip contents, unsaved replacement cancellation,
and rejection of invalid files before asking to discard edits. It drives selection
and acceptance through Qt APIs, not physical clicks. It does not set
DontUseNativeDialog. CI separately exercises the available Xvfb dialog backend.
The temporary current directory is necessary because the GTK backend does not
apply directory changes made by selectFile to an already visible dialog.

Navigation now ends on focus loss, window deactivation, hide, lost pointer grab,
or a move event that no longer contains the initiating button. Releasing another
button does not end the active drag. Focus moving to Measurements preserves a
drawing preview. Interaction tests send out-of-bounds motion and release and
simulate each loss event, checking camera coordinates and unchanged geometry.
These are adapter event tests, not a claim to inject physical cross-window input.

Reproduce:

```sh
cmake --build --preset dev --parallel 4
ctest --preset dev
QT_QPA_PLATFORM=wayland timeout 30s build/dev/dialog_tests
QT_QPA_PLATFORM=xcb timeout 30s build/dev/dialog_tests
QT_QPA_PLATFORM=wayland build/dev/interaction_tests
```

Observed: core/persistence tests pass; dialog tests pass on both native platforms;
Wayland interaction checks pass at effective scale 1.6. Theme tokens and broader
layout contracts are addressed by R006/R010. Physical compositor drag acceptance,
sleep/resume and accessibility remain explicit platform acceptance work.
