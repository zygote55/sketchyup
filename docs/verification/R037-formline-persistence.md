# R037: Formline import and complete M4 persistence

Date: 2026-10-04. Local implementation-agent validation; CI pending.
Requires [PR #48](https://github.com/zygote55/sketchyup/pull/48).

The [format decision](../decisions/0014-formline-import.md) records source semantics,
conversion notices, lifecycle and limits. No prototype runtime was restored.

The development suite passes 37/37. The core sanitizer suite passes 28/28,
including ordinary, small and tall faceted-cylinder solid regressions and the
existing invalid-shell checks. Formline tests cover axes, meter units, yaw signs,
base height, outward winding, colors, visibility, persistent source mapping,
editable geometry, one-step undo/redo, relocation and source preservation.
Malformed/truncated/oversized input, duplicate identities, invalid Unicode,
unsupported versions and out-of-range numeric values reject. Valid Unicode names
retain their text. A separate maximum-size experiment imported and saved 1,000
48-sided cylinders into an 11,035,091-byte native container.

The complete M4 golden fixture verifies nested hierarchy, canonical definitions
and their shared members, reflected nonuniform placements, unplaced definitions,
tags, locks, hidden state, curves, guides, properties, side assignments,
present/missing resources and allocation floors. The relocated container compares
all serialized records; canonical-member editing still updates both placements.
A captured save snapshot preserves the earlier asset graph while later edits
remain dirty. No undo history is fabricated on load.

Native input tests pass on X11 and isolated Weston at DPR 1 and 2. They verify
corrupt-source rejection, cancellation with dirty edits, discard/import, a readable
report, first-save path selection, untouched source bytes, native reopen, face
selection and undoable editing in the imported group. The [captured report](R037-formline-report.png)
was visually inspected. Existing native X11 Info, Materials and shell regression
suites also pass. This is synthetic input/visual evidence, not independent
human or physical-monitor acceptance.

The CLI imported/saved/reopened the fixture and measured the rotated box as a
closed 48 m³ solid. Direct, relative and symbolic-link output aliases of the source
reject with its SHA-256 unchanged. CI adds native checks on both display backends
and scales plus headless import/reopen. Recovery is the next roadmap step.

PR #49 merged after both Native build runs passed on 2026-10-04 at
`0c53fd13e3a8f80f77793e8bbbaea532f8838c4a`; merge commit `61fc155`.
