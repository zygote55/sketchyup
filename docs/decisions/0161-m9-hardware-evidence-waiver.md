# M9 hardware evidence waiver

Date: 2026-10-12. Owner decision on the M9 release gate ([M9 acceptance](../M9_ACCEPTANCE.md)).
Follows the partial run of the [M9 hardware session](../M9_HARDWARE_SESSION.md) on
2026-10-11/12.

## Decision

The project owner decided on 2026-10-12: "Let's just skip all this testing. I'm the
only user of this and I'll test as I use it."

The physical and hardware checks of the [M9 hardware session runbook](../M9_HARDWARE_SESSION.md)
that were not run are **waived for the 1.0 gate**, by explicit owner decision. The
waiver narrows what evidence the gate asks for. It does not claim that any waived
check passes, and it does not edit or remove any earlier result. The owner will
exercise the waived behavior in daily use.

## What is waived

By runbook section:

| Section | Check | Status |
| --- | --- | --- |
| 1 (1a-1d) | R082 Intel campaigns: frame, history, file, and the 10,000-placement case | Waived. Never run; no Intel result exists at the session revision. |
| 1d, 2 | The sharing-off control: the by-hand 10,000-placement run with `SKETCHYUP_BENCHMARK_PLACEMENT_SHARING=0` (modeled worst case, 3 GiB ceiling), on Intel and AMD | Waived. Not run on any hardware. Software measured 1.93 GB ([R082.gg](../verification/R082gg-placement-limits.md)). |
| 3 | R085 output hotplug on the Qt X11 fallback (Xcb) | Waived. Not run. |
| 4 | R085 suspend and resume | Waived. Not run. |
| 5 | N04 physical 150% scale | Waived. Not run. |
| 6 | N03 physical pointer capture and focus | Waived. Not run. |
| 7 | R085 input methods (fcitx5 composition, physical keyboard) | Waived. Not run. |
| 8 | D-020 on Hyprland (hidden window during a long open) | Waived. Not run. |
| 9 | R084 interactive screen-reader session with Orca | Waived. Not run. |
| 10 | R085 automatic X11 output placement | Waived. Not run. |
| [Walkthrough](../M9_ACCEPTANCE.md#combined-release-candidate-walkthrough) | The manual pass on the installed RC: every user-guide workflow by hand on Omarchy/Hyprland and under Qt X11 at 1x and 2x, the keyboard-only pass, the save/recover/migrate cycle on real documents, and the assistant with and without a provider | Waived. The owner's statement covers manual testing. The automated walkthrough steps stay required (below). |

## What was measured

- **Section 2, AMD discrete GPU** (RX 6800 XT, X11): the frame, history, file and
  10,000-placement campaigns ran at revision `db68cd14`. Every case exits 0. The 100k
  families, history and file campaigns have no `MISS`. At 10,000 placements, peak RSS is
  616-759 MB against the 1.25 GiB class of [R082.w v2](../verification/R082w-memory-budget-v2.md),
  and complete-frame p95 is 17.3-21.7 ms against 33.3 ms. Edit feedback p95 misses the
  100 ms routine-edit target for deep and textures at 10,000 placements (defect D-024).
  See [R082.ii](../verification/R082ii-amd-hardware-session.md).
- **Section 3, Wayland hotplug**: an owner-attested pass, with a partial automated
  record. See [R085.r](../verification/R085r-hotplug-owner-attested.md).
- **Preflight**: the monitor layout, accessibility and suspend-inhibitor state were
  recorded on the laptop. These are inputs, not results.
- **Finding**: at a 701x418 window the Model breadcrumb overlaps the viewport's
  "PERSPECTIVE / METERS" label (D-025, fix in progress on `fix/viewport-label-overlap`).

## What remains required

The waiver covers only the checks in the table above. Everything else in
[M9 acceptance](../M9_ACCEPTANCE.md) is unchanged:

- The release-candidate (RC) freeze and the RC record.
- All four CI jobs passing at the RC: development build and native tests, Blender
  interoperability, sanitized builds, and Arch package acceptance (with the R087
  package, upgrade and migration checks it runs).
- The complete regression suite and the dedicated ASan/UBSan tests, and the isolated
  X11 and Weston native suites at 1x and 2x (the automated steps of the walkthrough:
  the `dev` and `sanitize` presets, `ctest`, the native workflow invocations and the
  integrated offline studies).
- The frozen provider corpus re-run for the remote OpenAI ChatGPT-plan profile at the RC
  ([ADR 0160](0160-provider-scope-for-1-0.md)).
- Every Core row's RC re-run and the evidence not named above, including the all-dialog
  keyboard audit (N05), the 10,000-placement edit-feedback finding D-024, and the
  triage list showing that no defect is release-blocking.

## How the owner covers the waived behavior

The owner is the project's only user and will test the waived behavior as the
application is used: the user-guide workflows on the installed build, Intel graphics, output hotplug and suspend, scaling, pointer
capture, input methods, the screen reader and X11 placement. Defects found that way
enter the [triage list](../verification/R089a-defect-triage.md) like any other.

## Consequences

- Scope rows whose only open clauses were these checks take the status
  **Evidenced (owner waiver)** in [R088.d](../verification/R088d-core-parity-gaps.md),
  a status distinct from plain Evidenced. Rows that also carry non-waived open clauses stay
  Open.
- [D-003](../verification/R089a-defect-triage.md) is closed: the AMD hardware run
  accepted the v2 budget. D-004, D-005 and D-020 become documented limitations that
  are untested on the waived configurations. The known one-CPU Intel frame miss
  recorded by [R082.an](../verification/R082an-public-memory.md) stays on record.
- The support matrix and release notes must say that Intel graphics, suspend/resume,
  physical 150% scale, input methods, Orca and automatic X11 output placement were not
  tested for 1.0, rather than imply support.
- The waiver rests on the owner being the only user. If the application is given to
  other users, the waived checks are the first to run. The runbook is kept for that.
