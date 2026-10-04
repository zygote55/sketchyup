# R024.b: native circle, arc and pie tools

Date: 2026-10-03. Local verification passed; CI/merge pending. Depends on R024.a.
The parent R024 becomes accepted when both child PRs merge with passing checks.

Circle now commits `geometry.circle` and stores analytic provenance. The Draw
menu adds center arc, two-point arc, three-point arc and pie. All use the shared
command preview, atomic commit, cancellation and guarded amendment lifecycle.
A first drag can establish the first two points without prematurely committing;
the third point finishes the operation. The current drawing plane and context
remain fixed through navigation and Measurements focus changes.

Center arcs and pies use center, radius/start point and end direction. Pointer
sweeps are counterclockwise around the plane normal, including major arcs. Typed
`radius, angle` accepts degrees by default or explicit `deg`/`rad`, and negative
sweeps. A single length sets an unset radius; after the radius point exists a single
value specifies the angle. Two-point arcs use endpoints and signed midpoint bulge.
Three-point arcs use start, through and end coordinates. Each point accepts the
existing absolute/relative coordinate syntax. Comma-decimal locales use semicolons.

`24s` sets the segment count before drawing, during preview, or as guarded re-entry.
Counts respect circle 3–256, arc 1–256 and pie 2–256 limits; sub-tolerance geometry
still rejects. Re-entry reuses the committed command so changing segmentation
cannot lose a negative sweep or alter the construction variant. Intervening edits,
undo or document replacement invalidate amendment. Invalid input leaves the model
unchanged and stays selected in Measurements with its error indication.

Validation:

- All 17 development CTest suites pass. The unchanged core was sanitizer-tested
  in R024.a; this slice changes native interaction only.
- Native curve tests pass on Wayland and pinned Arch/Xvfb: all curve variants,
  private preview, phase-by-phase pointer/typed input, first drag, radius/bulge/
  angle constraints, negative sweeps, radian entry, segmentation, collinear and
  invalid input, one-item amendment, cancellation, locale input, tilted-plane
  pointer equivalence and exact container roundtrip.
- Window-level numeric tests exercise the Center arc menu action, keyboard input
  transfer, signed angle commit, segmentation amendment and retained invalid
  angle feedback. They pass on Wayland and pinned Arch/Xvfb.
- Drawing regression passes on both platforms; lifecycle and responsive shell
  tests pass on Wayland.
- The CLI curve recipe reopens in the native app; [capture](R024-curves.png) and
  [renderer metadata](R024-curves.json) record the result.

```sh
QT_QPA_PLATFORM=wayland build/dev/curve_input_tests
QT_QPA_PLATFORM=wayland build/dev/numeric_input_tests
ctest --preset dev
build/dev/sketchyup-cli --script examples/curves.json --output /tmp/curves.sketchyup
build/dev/sketchyup /tmp/curves.sketchyup
```

Curves remain editable segmented topology with analytic construction provenance;
this does not add spline editing, automatic tangent inference or a curve-editing
inspector. Point/center/tangent inference continues in R025/R026. Default pointer
snapping remains the 0.1 m plane grid until that work lands.
