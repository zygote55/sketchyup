# R050 native rendering verification

Date: 2026-10-05 UTC. Local acceptance passed; CI/merge pending.
[Interaction and provenance contract](../decisions/0035-native-render-ui.md).

The first actual Wayland run on the Intel Arc desktop passed with Blender 5.2.1
LTS: setup with a missing executable, keyboard dismissal, perspective/top/bottom
camera capture, asynchronous preparation cancellation, unsupported version,
invalid PNG, worker failure, running-job cancellation, immutable revision capture
while editing, verified in-window result, exact-byte PNG save, narrow/wide layout,
replacement-document provenance and result close. The room is authored using the
real R047 assembly recipe. Fake executables cover adverse outcomes; the successful
CPU image is produced by installed Blender. Rendering/cancellation leave native
model bytes and undo history unchanged until the test explicitly edits the model.

The final native test also checks CPU probe readiness, invalidation on backend
change, rejected mismatched devices, primary controls in a 340×350 setup sheet and
the two-result retention bound. It passes on Xvfb/X11 and isolated Weston/Wayland
at DPR 1 and DPR 2. The actual Wayland/Intel run rendered the room at 512×512 and
32 samples; the resulting PNG and native screenshot were inspected. The native
ASan/UBSan run passed with leak detection, generic platform theme, Fusion style
and compose input method; no sanitizer checks were disabled.

The full development CTest run passed in 35.61 s: 60 passed, with the explicit
real-Blender opt-in test skipped. Native real rendering was tested separately.
Native MCP and desktop inspection regressions pass on isolated X11 and Wayland. The shell regression passes on X11 across 640/900/1200/1600 logical widths and both themes. Live-desktop attempts could not acquire focus and are not counted as passes; isolation resolved that environment issue.

Source archive and installation passed, including render controller, tests, embedded worker and contract. Screenshots and logs are local acceptance artifacts, not source files.
