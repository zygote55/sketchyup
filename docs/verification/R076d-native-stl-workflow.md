# R076.d — Native and standalone STL interchange

2026-10-07. Workflow `53ebf6e`, CLI/native acceptance `fd4c461`, native event-loop
fixture `ae63f5e`, contract `b28eea0`; final integration `dca2d4c`.
Parent: [PR #178](https://github.com/zygote55/sketchyup/pull/178).
Contract: [0119](../decisions/0119-native-stl-interchange-workflow.md).

The complete Debug build passes **158/158 CTest suites in 245.29 s**, including
actual Blender interchange. Five final native Wayland 2× integration checks pass
in **22.994 s** ([matrix](R076d-native-matrix.json)).
The dedicated [platform matrix](R076d-platform-matrix.json) passes all eight
normal/ASan/UBSan Wayland/X11 checks at 1×/2×. Leak detection and halt-on-error are
enabled. The reviewed import report shows explicit units/axis, repair choices and
bounded topology findings. Dedicated CLI/interchange checks pass **4/4 in 2.72 s**;
sanitized CLI/import checks pass **1/1 in 4.21 s**.

Native and CLI import require explicit units, axis and repair choices. Native
conversion happens privately before the dirty-document replacement prompt;
canceling preserves the current model. Successful import creates an unsaved native
document with source-safe Save behavior. Converted geometry supports ordinary
native face editing and undo. Native and CLI export require explicit encoding and
a new destination, preserve source/history, and disclose omitted STL appearance
and structure. Existing paths, mixed CLI modes and missing units reject.

The [installed smoke](R076d-installed-smoke.json) performs STL import and both
binary/ASCII exports, reimports each, rejects overwrites and protects source/native
bytes. It also verifies eight catalogs, twenty-four contracts, previous OBJ/glTF
workflows and desktop/license bytes. [Source package](R076d-source-package.json):
**170 installed inputs** match byte for byte; SHA-256 `bb5d4f403e5ff44fdb4f503461ca8849140f0641ec8c810d6f00ac80bd2778df`.

R076 local layers are complete. Remote CI and ordered merges remain delivery gates;
M7/M8 milestone acceptance is not claimed while prerequisites remain open.
