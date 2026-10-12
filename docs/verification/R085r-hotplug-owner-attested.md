# R085.r — Output hotplug on native Wayland: owner-attested

2026-10-12. Source revision `db68cd14bcc1ee93ad9adaa4bb981bc63984b46a`. Section 3 of the
[M9 hardware session runbook](../M9_HARDWARE_SESSION.md), native Wayland run. This is a
manual attestation with a partial automated record. It is not a clean instrumented
pass, it does not accept R085 and it is not release-candidate evidence.

## Result

**Owner-attested pass.** After unplugging and replugging the external display (DP-7)
with SketchyUp running, the owner reported on 2026-10-12: "Everything acted exactly as
I'd expect." The attestation is the owner's observation. Nothing below measures it.

## What the automated record shows

Run `hotplug-wayland-20261012-091242` (`scripts/session-hotplug-check.sh wayland`,
`--demo` model). The environment: Hyprland 0.56.2 (`efb50993`), Qt 6.11.2 with
`qt6-wayland 6.11.2-1`, Mesa 26.2.2, kernel 7.2.5-3-omarchy, Intel Arc (laptop),
binary `93d722b7…` (the session build).

- At the start, `hyprctl monitors` listed eDP-1 (2880×1800, scale 2, at 2400×0) and
  DP-7 (3840×2160, scale 1.6, at 0×0).
- The script recorded an edit before the unplug (a deleted box), then DP-7's removal.
  After it, only eDP-1 was listed, and the application process was alive.
- The window did not change: `hyprctl` reports it on monitor 0 (eDP-1) at
  3127,470 with size 701×418, workspace 2, before and after. The before and after
  screenshots are byte-identical.
- The application log has one line, a Qt portal registration warning
  ("Could not register app ID") that is unrelated to outputs.

## What the automated record does not show

- **The window was never on DP-7.** It was on the laptop display throughout, so the
  run did not exercise a window being moved off a removed output. Hyprland's own
  monitor-removal handling is the only part exercised by what was recorded.
- **DP-7 was already absent when the script prompted for the unplug**, according to the
  session notes, and **the replug wait timed out**; the script did not capture the
  replug. The script's log line "DP-7 removed" therefore records the owner's
  confirmation, not an observed transition.
- No recording was kept of the replug, and no check of picking, scale or rendering
  after it. The Qt X11 fallback (Xcb) was not run; its hotplug check is waived by
  [ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md).

The owner accepted section 3 as passed on this basis. The owner will see any hotplug
problem in daily use.

## Finding: breadcrumb overlaps the viewport label at a small window

The screenshots show the 701×418 window with the "Model" breadcrumb drawn over the
viewport's "PERSPECTIVE / METERS" label, so the label reads "Model ECTIVE / METERS".
This is unrelated to hotplug. It is recorded as D-025 (Minor) in
[R089.a](R089a-defect-triage.md); the fix is in progress on the branch
`fix/viewport-label-overlap`.

Raw evidence (screenshots, monitor dumps, window JSON, `notes.txt`) stays outside the
repository in `~/sketchyup-verification/m9-hardware-session/hotplug-wayland-20261012-091242/`.
