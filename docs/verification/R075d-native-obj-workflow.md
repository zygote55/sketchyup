# R075.d — Native and standalone OBJ interchange

2026-10-07. Workflow `4c79662`, X11 filename entry `06a2673` and export-menu coverage
`f30b0ed`, contract `d5f6aad`; final integration `d8a5198`.
Parent: [PR #174](https://github.com/zygote55/sketchyup/pull/174).
Contract: [0115](../decisions/0115-native-obj-interchange-workflow.md).

The complete Debug build passes **153/153 CTest suites in 172.33 s**, including
actual Blender interchange. Five final native Wayland 2× integration checks pass
in **22.699 s** ([matrix](R075d-native-matrix.json)).
The dedicated [platform matrix](R075d-platform-matrix.json) passes all eight
normal/ASan/UBSan Wayland/X11 checks at 1×/2×. Leak detection and halt-on-error are
enabled. The reviewed import report shows explicit units/axis, editable groups and
conversion notices. Dedicated CLI/interchange checks pass **4/4 in 2.70 s**;
sanitized CLI/import checks pass **2/2 in 5.06 s**.

Native and CLI import require an explicit coordinate unit and up axis. Native
conversion happens privately before the dirty-document replacement prompt;
canceling preserves the current model. Successful import creates an unsaved native
model with editable geometry, selection and undo, and Save requires a native path.
The source OBJ, material libraries and embedded dependency inputs remain unchanged.
Native export options and File-menu publication use the same whole-model package
writer as the CLI, including hidden objects without presentation section clipping.
Existing files/directories and mixed CLI operation modes reject.

The [installed smoke](R075d-installed-smoke.json) verifies eight catalogs,
twenty contracts, actual installed OBJ import/native validation/export, explicit
units, existing/source protection, glTF import, desktop/license bytes and relocated
standalone CLI operation. [Source package](R075d-source-package.json): **166 installed
inputs** match byte for byte; SHA-256 `02af83185ebd1a4db56dc61d25da960e935f3c7568d17dd0a3bafd150a359039`.

Together R075.a–d complete local OBJ acceptance. The supported subset and counted
fidelity losses remain documented in ADRs 0112–0115. Required remote CI and ordered
merges remain delivery gates; M7 acceptance is not claimed while prerequisites
remain open.
