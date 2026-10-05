# R039.d: native document units

Date: 2026-10-04. Local and CI validation passed; merged. Builds on
[PR #54](https://github.com/zygote55/sketchyup/pull/54).

The native application asks for default units on first run and exposes File →
Document units for later changes. A default initializes only a pristine new
model; an already opened model keeps its stored preference. The optional new-
document default is separate from the current model's undoable units edit.
Cancel preserves state; stale dialogs retain choices with an inline explanation.

Measurement entry, explicit suffix overrides, model-first keyboard focus, Info
readouts/editing, tape measurements, construction-plane/component origins and
component dialog guards share the per-document preference. Scale, angles,
directions and counts keep their distinct interpretation. Readouts and accessible
names update after unit changes and undo/redo. Feet/inches lengths format as
compound values; area and volume use square/cubic feet. Decimal formatting omits
grouping and roundtrips through localized parsing within model tolerance.

The native fixture passes on X11 and isolated Weston at DPR 1 and 2. It checks
first-run/default/current units, typed millimeter rectangles, feet displacements,
explicit unit amendment, dimensionless scale/degrees, Info editing, component
placement, tape readout, cancellation, stale guards and save/reopen. It also checks
that a first-run default choice does not replace an opened model. CI includes all
four platform/scale combinations.

Physical Hyprland and full regression results are recorded below.
This is implementation-agent verification; no independent human acceptance is
claimed. The integrated M4 room/component/recovery gate remains pending.

The native units fixture also passes on the actual Hyprland Wayland desktop at
reported DPR 2. The compositor tiled the window below the automatic tray width;
the test now explicitly opens the tray before checking Info, as a user would.
This verifies the application on the real compositor with synthetic Qt input;
it does not claim a physical mouse/keyboard or independent human review.

The full development suite passes 44/44. Native X11 units, History, numeric entry,
components, Entity info, guides, transforms, recovery and shell regressions pass.
The core is unchanged from R039.c's passing 30/30 sanitizer suites. The inspected
[capture](R039-units-x11.png) shows imperial Info values and the Measurements label.

[PR #55](https://github.com/zygote55/sketchyup/pull/55) merged on 2026-10-04 as `b0bcb88` after both Native build runs passed: [37241710599](https://github.com/zygote55/sketchyup/actions/runs/37241710599) and [37241707249](https://github.com/zygote55/sketchyup/actions/runs/37241707249).
