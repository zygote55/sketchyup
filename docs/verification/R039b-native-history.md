# R039.b: native History controls

Date: 2026-10-04. Local and CI verification passed; merged. Builds on
[PR #52](https://github.com/zygote55/sketchyup/pull/52).

View → History opens the tray's fifth tab. The panel exposes a bounded page of
labeled steps, current/applied/redo/saved markers, explicit retention boundaries,
Undo/Redo buttons and plain-text task/request details. Click or Enter moves to
a step through the guarded history API. Deferred callbacks avoid replacing Qt
items while their delegate event is active and reject stale document state.
Successful navigation cancels a tool preview and returns keyboard focus to the
model. Menu labels identify the next undo/redo action, with full tooltips and
escaped ampersands. Saving updates markers without requiring a content revision.

The native fixture checks click, Enter, menu and keyboard agreement; multi-step
rewind to explicitly saved content; monotonic allocator floors; redo branching;
assistant labels and inert request text; viewport focus; stale queued selection;
and read-only pagination over 220 edits. Undo back to saved content reports no
unsaved edits even when a later recovery checkpoint exists. The obsolete sidebar
warning that recovery was unimplemented is replaced with compact camera hints.

X11 and isolated Weston pass at DPR 1 and 2. CI includes all four combinations.
The captured History panel is inspected for readable rows, task details and tray
layout: [History capture](R039-history-x11.png). Development suite and native
regression results follow below.

This is implementation-agent verification, not independent human acceptance.
Document units and the integrated M4 room/component/recovery gate remain pending.

The first full development run passed 42/42. Existing native numeric editing,
shared components, recovery, materials and shell regressions passed on X11; the
updated pointer-scale assertion and History also passed on DPR 2 Weston.

Screenshot review exposed background streaks inside an opaque face in perspective.
A new framebuffer regression sampled 2,301 interior points and reproduced 74
background holes on the original shader. Opacity is constant per face, but smooth
perspective interpolation could round a value of one below the opaque-pass cutoff.
The shader now carries front/back opacity with flat interpolation while preserving
interpolated RGB. The regression requires every interior probe to remain blue;
this is a tolerant color classification, not an exact cross-GPU pixel comparison.

After the shader change, History and material rendering both pass on X11 and
Weston at DPR 1 and 2. The opacity regression has zero background holes. Existing
front/back transparency, mixed-depth composition, reflected instances, CPU/GPU
picking, cache invalidation, undo and reopen assertions remain passing.

The final rebuilt development suite also passes 42/42 after the opacity fix.
The core did not change from R039.a's passing 29/29 sanitizer suites.

[PR #53](https://github.com/zygote55/sketchyup/pull/53) merged on 2026-10-04 as `b740b37` after both Native build runs passed: [37240281729](https://github.com/zygote55/sketchyup/actions/runs/37240281729), [37240278832](https://github.com/zygote55/sketchyup/actions/runs/37240278832).
