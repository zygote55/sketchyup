# R084.d — Keyboard-only measured construction

2026-10-07. Push/pull fix `822d3e7`; palette activation fix `5ddd1ec`.
Contract: [0147](../decisions/0147-keyboard-measured-modeling.md).

The new native workflow initially reached the selected face through command search,
then rejected `4m`: the ready push/pull tool had never initialized an extrusion session
without a pointer click. Numeric entry now starts the existing validated session from
an editable selected face. The workflow then exposed lost parent activation after
palette acceptance under X11; explicit parent activation restores keyboard focus.

All eight [normal/sanitized Wayland/X11 1×/2× checks](R084d-keyboard-platform-matrix.json)
pass: **12.788 s normal**, **16.814 s ASan/UBSan**, with leak detection and
halt-on-error. Each creates a measured 2×3×4 m solid through keyboard events and
verifies one-step undo/redo. The fixture was corrected to compare model content
rather than shared-record identity/allocation floors, which intentionally advance.
Existing native units and tool-lifecycle suites also pass on Wayland at 2×, including
pointer push/pull, camera interleaving, cancellation, numeric amendment and stale guards.

CI runs all four normal combinations and sanitized Wayland 2×. Fixture config/data
are private; no user documents, preferences, provider settings or desktop configuration
are used. Broader keyboard/assistive-technology coverage, shortcut preferences,
contrast/text scale and R084/M9 acceptance remain open.
