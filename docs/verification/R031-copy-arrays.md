# R031: linear and radial copy arrays

Date: 2026-10-04. Merged in [PR #34](https://github.com/zygote55/sketchyup/pull/34); both CI jobs passed (17m14s and 11m52s). Merge `9fa7852aae2d7cddde0a400cf0ee6e5443940f48`. Requires merged R030, PRs #32–33.
Owner/implementer: coding agent under the owner's full-roadmap authorization.

The public `geometry.array_selection` command copies typed geometry or hierarchies
with linear displacement or axis/angle rotation. `count` means **new copies**:
the source plus `count` copies makes `count + 1` instances. It must be an integer
from 1 through 100. Each copy returns its one-based `instance` and typed identity
maps. Raw geometry stays in its editing context; whole contexts duplicate their
hierarchy. Local/world frames and pivots follow R030's transform contract.

For a linear array, `delta` is the displacement between copies. For a radial
array, `axis` and `angle` give the rotation between copies; angles are radians in
the public command. Optional `divide: true` treats the displacement/angle as the
whole span: it creates `count` equal intervals, including the destination. The
original is always retained. Whole-turn and multi-turn rotations retain this
explicit count, even when placements coincide; no special endpoint is omitted.
Coincident geometry is not welded, and analytic-outline ambiguity retains the
existing atomic rejection policy.

The core validates counts and generates bounded transforms before creating a
private candidate. One private copy determines exact expansion cost, including
hierarchy descendants and deduplicated raw boundaries. Aggregate document and
per-context curve/guide limits are checked before expanding the remaining copies.
Any failed candidate leaves source records, history and allocation floors intact.
The complete array publishes once, with source-to-all-copy topology lineage.

In Move or Rotate copy mode, complete the first copy, then enter `xN` for N new
copies or `/N` for N intervals ending at the original destination. Both prefixes
transfer keyboard focus to Measurements. Changing a distance or angle retains the
current count and division mode; changing xN or /N retains the original step/span.
Full entered angles are retained: dividing 540 degrees uses that sweep rather
than recovering 180 degrees from an equivalent rotation matrix. New copied
geometry is selected after every replacement. Scale and Flip do not seed arrays.

Changing a hierarchy copy's count requires a different number of new contexts.
The new explicit CopyArray amendment policy allows that count change only through
the single-array command path. Default amendment keeps its prior count guard;
both policies retain exact session/revision eligibility, one-operation replacement,
monotonic identities and protection against edits to unrelated existing contexts.
Re-entry shares one undo item, and undo/redo invalidates further amendment.

Validation:

- All 23 development suites and 18 ASan/UBSan suites pass. The command catalog
  has executable coverage for all 31 public commands, including strict count,
  mode, frame, nested entity and boolean validation.
- Core fixtures verify exact spacing, equal divisions, radial pivot placement,
  hierarchy counts, local axes, source-to-all-copy lineage, retired ID floors,
  one-step undo, default amendment restrictions, array-specific count changes,
  unrelated-context protection and aggregate guide limits.
- Native array workflows pass on Xvfb/Mesa at DPR 1 and 2, including xN keyboard
  ownership, selected copied results, count/spacing/division revisions, rejected
  huge counts, a complete 540-degree sweep and stale amendment rejection.
- The full X11 native regression set passes with host fonts mounted: transforms,
  navigation, selection, guides, constraints, inference, curves, drawing, numeric
  entry, lifecycle, interaction, viewport/topology, dialogs and shell.
- Isolated Weston 15.0.1 / Mesa 26.2.3 / Qt 6.11.2 Wayland checks pass: transforms
  at DPR 1 and 2, arrays at DPR 1 and 2, and a DPR-2 rendering smoke test with
  `glError: 0`. These use a headless output and software rendering, not the
  physical Hyprland session. Physical-desktop retesting remains a separate M4
  checkpoint item; the R030.b environment limitation is preserved in its record.
- The transform fixture now sends explicit Qt pointer events instead of relying
  on cursor warping for preview and inference preparation. Both native backends
  exercise the same application handler and keep the activation requirement.
- `scripts/test-wayland.sh` creates an isolated output, limits runtime and returns
  the child's status. Weston itself can exit successfully after a child failure;
  an intentional exit-7 fixture verified that the wrapper propagates failure.
  CI installs Weston and the package's declared DejaVu font, and runs both scales.
- The public recipe executes, previews privately and reopens its saved document.
- A raw-face array with 100 copies completed in 0.87 s in the local debug build.
  The initially proposed 1000-copy limit exceeded a 60 s stress timeout. The
  published limit is therefore 100 new copies per operation; 101 rejects before
  expansion. This is an explicit interaction budget, not a claim of scalable
  performance at the full document geometry limit.

No independent human input or physical-device acceptance is claimed.

```sh
ctest --preset dev
ctest --preset sanitize
build/dev/sketchyup-cli --script examples/copy-arrays.json --output /tmp/arrays.sketchyup
scripts/test-wayland.sh build/dev/array_input_tests
SKETCHYUP_TEST_SCALE=2 scripts/test-wayland.sh build/dev/transform_input_tests
```
