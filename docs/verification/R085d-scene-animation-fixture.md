# R085.d — Scene transition correctness under delayed events

2026-10-07. [CI run 37607518121](https://github.com/zygote55/sketchyup/actions/runs/37607518121)
at source `886a401ce8e337a347cc14c3f38a550e0440156b` failed the native Wayland
scene fixture while waiting 1.5 seconds for the exact saved camera pose. The
160 ms product animation relies on event-loop delivery. The retained CI log also
reports a compositor timestamp jump during teardown; that does not establish a
causal explanation. The original fixture passes isolated local Wayland at both
1× and 2×, so the original CI failure was not reproduced locally.

The fixture now permits five seconds for eventual correctness and prints actual
versus expected yaw, pitch and distance on failure. It still requires the full
camera record to equal the saved pose, runs a real animation, and does not force
its time or substitute a tolerance. Product animation timing and behavior are
unchanged. This is an event-driven correctness assertion, not a reference
hardware frame-time acceptance budget. Delayed event delivery is a possible
explanation for the CI result, not a confirmed root cause.

[All eight native cases pass](R085d-scene-matrix.json): normal and ASan/UBSan,
Wayland/X11, 1×/2×. Sanitizers use leak detection and halt-on-error and the existing
private Wayland client lifetime correction. The tests retain selective scene
capture/recall, exact final pose, manual-interruption cancellation, reduced motion,
rename/update, undo, persistence and missing-reference assertions. Exact tested
source hashes are in the matrix; the subsequent parent merge is covered by its
own CI, not by relabeling these prior binary runs.

The narrow fixture change is also backported to PR #206. Exact-head CI, ordered
merges and all physical-platform/release gates remain required.
