# R040.c native inspection and view feedback

Date: 2026-10-04 (local), 2026-10-05 UTC. Local and CI verification passed; merged. Builds on merged [PR #58](https://github.com/zygote55/sketchyup/pull/58).
[Contract and generated registry](../decisions/0021-desktop-inspection.md).

The native fixture opens the M4 room in a real Window, enters Room study and
selects Window A. Inspection reads that actual desktop selection and measures
its 1.2 m width. Current-view capture returns a PNG bounded by 640 × 480 pixels
with the matching document revision, camera matrices, projection, radians field
of view and device pixel ratio. The decoded image dimensions, content diversity,
SHA-256 and byte budget are checked. A native standard-view/fit operation changes
the image and projection without changing the model revision. Native model bytes,
save stamp, dirty state and selection remain unchanged by inspection.

The same fixture passes serially on X11 DPR 1 and 2, nested Weston DPR 1 and 2,
and the actual Hyprland session at DPR 2. The X11 and Hyprland images were visually
inspected: the room, both windows and selected Window A are visible, along with
viewport overlays. Application panels and window chrome are absent, as specified.

- [X11 viewport image](R040-inspection-x11.png)
- [Hyprland viewport image](R040-inspection-hyprland.png)

Failure checks cover wrong documents, stale revisions, excessive dimensions,
unsupported path/snapshot fields, wrong-thread dispatch, clipping overrides and
hidden windows. A real pending rectangle gesture returns `VIEW_BUSY` and remains
active and unchanged afterward. A retained snapshot still measures the old
window after a live move, while a capture using the old revision is rejected.
Published desktop schemas are compared with the executable registry.

The test uses Qt 6.11.2. Docker uses software rendering under Xvfb/Weston; actual
Hyprland uses the host Intel Arc/Mesa renderer. These checks cover the current
machine and CI compositor matrix, not a complete release hardware matrix.

The complete development build and all 47/47 CTest suites pass. After final
formatting and discovery changes, native X11 inspection, the integrated M4
workflow and native selection regressions pass again. Installed native discovery
matches the installed desktop schema exactly; the contract is present. Conflicting
model/render options fail before window construction or model access. The source
archive includes the desktop schema, contract and native fixture. CI adds the new
capture fixture to all four X11/Weston DPR configurations and compares installed
native discovery during disposable package acceptance.

Final CI runs 37247831301 and 37247828686 passed, including native and disposable
package acceptance. Merged on 2026-10-05 UTC as `d880136cfcd99762a6359bb53ea20a2dbbab499e`.
R040 is complete across the three accepted slices.
