# R082.w — Prospective memory calibration

The promised M0 numeric peak-memory budget was not frozen. This record explicitly
closes that planning omission prospectively; it does not label earlier measurements
as accepted or alter the existing frame, inference, edit or native-file targets.

The [versioned declaration](R082w-memory-budget-v1.json), frozen at `2026-10-08T06:45:07.038486+00:00` with
SHA-256 `5f4c4a11f9f1d8a0a7b11b7e417e5036baa7a64d7e0c5afb15a80368ba8fe8dd`, sets process peak RSS ceilings of 1 GiB for the supported 100k
five-family controls and 3.5 GiB for the experimental million-triangle control.
The latter leaves 512 MiB below the operational 4 GiB job cap. Neither declaration
widens production document limits or establishes that the private 1M fixture is
supported by the public editor.

Own-process DRM resident buffer-object upper bounds must remain at or below 2 GiB.
This leaves margin over the approximately 330 MiB body vertex payload plus the
128 MiB maximum texture fixture, staging and framebuffer allocations. Values must
be available and complete at warmup, final state and all 65 history checkpoints.
Missing, partial or overflowing accounting leaves the gate open. Client/region
resident values may overlap, so the sum is conservative; categories must not be
added together or to RSS. Driver internals outside the kernel interface remain
unaccounted, and this record does not claim complete device allocation accounting.

Across the unchanged five-cycle rendered-history campaign, sampled current RSS
may grow at most 64 MiB from the first warm sample, and charged retained history
may not exceed 64 MiB. All 500 edits and 1,000 rendered undo/redo frames remain.
This finite sample does not prove infinite-session boundedness. The remaining
full application workflows and current/reference hardware checks must also pass.

These ceilings are a documented late calibration informed by retained exploratory
measurements. They apply only to fresh forward runs with frozen artifacts, fixtures
and platform identities. Original failures remain retained. No old result receives
a retrospective pass, and release acceptance remains open.
