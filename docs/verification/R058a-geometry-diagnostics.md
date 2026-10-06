# R058.a — Bounded geometry diagnostics

Date: 2026-10-06 UTC. Depends on R057.h in the review stack. This slice is the
read-only kernel; shared inspection and native repair presentation follow.
[Diagnostics contract](../decisions/0053-geometry-diagnostics.md).

`diagnostics_tests` verifies:

- Open cube boundaries, reversed-face winding and a mixed non-manifold/open/winding
  fixture report all scanned categories with exact counts and valid typed references.
- Disconnected vertex fans and orphan vertices identify source geometry. Deep
  analysis counts are lower bounds because the underlying check stops at a defect.
- An outward cube is clean; a globally inverted cube supplies all six repair-eligible
  faces. Correct inward cavity boundaries remain clean with independently expected
  56 cubic units of material, while an inverted hierarchy identifies all twelve faces.
- Incorrect cavity winding, coplanar self-intersection and near-degenerate thin faces
  remain explicit findings. Separate valid solids are informational material parts.
- 50,000 wire edges retain their exact count with only 64 typed references. Seventy-two
  inverted faces also truncate at 64 and disable whole-shell repair eligibility.
- Deep shell limits and oversized input loops produce incomplete `analysis_limit`
  reports with no clean-volume claim. References are bounded, sorted and valid, and
  diagnosis leaves source geometry unchanged.

All four targeted CTest suites (diagnostics, orientation, solid shells and solids)
pass in 2.65 seconds. The new diagnostic suite also passes AddressSanitizer,
UndefinedBehaviorSanitizer and leak detection. CI core discovery includes it in
normal, Qt-free and sanitizer builds. Native repair UI and live-provider acceptance
are not claimed by this kernel slice.
