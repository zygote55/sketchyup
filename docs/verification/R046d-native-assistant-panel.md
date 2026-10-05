# R046.d native assistant panel verification

Date: 2026-10-05 UTC. [Lifecycle and privacy contract](../decisions/0040-native-assistant-panel.md).
Live OpenAI/R051 acceptance remains unclaimed. Provider replies and credentials in
these native tests are disposable fixtures; no real keyring or provider is used.

The native integration exercises Preferences, storing a credential through an
isolated helper, field clearing, absence of credentials in settings/transcripts,
actual consent before any network request, and a real staged transaction over the
window's existing document. Preview keeps prior manual edits intact; keyboard
Apply adds one undo entry, Undo preserves earlier work, and direct mode uses the
same publication path. Removed context attachments are absent from the actual
outgoing provider request. Clarification sends actual viewport entity references.
A human edit makes the preview stale and removes its overlay. Discard publishes
nothing. Injected post-rename durability uncertainty fences editing, save and
close; Reconcile publishes exactly once and restores native controls.

The full development suite passes: 62 enabled suites, 37.28 seconds; the opt-in
real Blender case is tested separately in its existing acceptance. Native panel
ASan/UBSan/LeakSanitizer passes on isolated Wayland. The existing Qt Wayland
client-decoration workaround applies only to that sanitizer process; no sanitizer
is disabled. Existing shell, dialog, history, recovery and render native
regressions also pass.

Final panel checks pass on X11 and Wayland at DPR 1 and 2. The isolated sessions
verify actual window sizes 640×480, 900×850, 1200×850 and 1600×850, Escape, reopening
the sheet and Apply remaining on screen without scrolling. Visual review led to
hiding the disabled composer during a task, compacting the changed-object list,
keeping publication controls outside the history scroll, and disclosing validated
steps/raw activity on request. Measured values remain visible at the smallest
sheet size. Wide and standard layouts retain usable model and assistant regions.

Screenshots and logs are retained locally under `build/evidence/r046d-*` (ignored
build evidence). Both whole-window captures and direct viewport framebuffer
captures are recorded: Qt's whole-window grab omits the viewport's QPainter
annotations, while the framebuffer includes its measured bounds label. The
native Intel framebuffer was visually checked for the correct 2 m × 3 m × 0 m
label and hatched private geometry. A tiling compositor can override requested
window sizes; the native test records actual dimensions, while isolated sessions
provide exact breakpoint acceptance.

Source packaging, installation and installed capability inspection pass. The final
native Intel run passes at reported DPR 1.6 (actual compositor-selected sizes are
recorded in its log). Final X11/Wayland DPR 1/2 and sanitizer runs also pass,
including replacement of an already-open informational disclosure by the actual
first-request permission card. CI acceptance is pending. No model-quality result is inferred from these fixtures.
