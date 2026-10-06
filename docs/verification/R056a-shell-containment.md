# R056.a — Native shell-containment evidence

Date: 2026-10-05 UTC. Depends on R055.c in the review stack; CI/merges pending.
[Contract and bounded numerical method](../decisions/0047-shell-containment.md).

The immutable native analyzer identifies connected shell face IDs, signed volume,
nearest enclosing parent and nesting depth. It classifies only after manifold,
face-winding and geometric intersection validation. It rejects ambiguous contact,
inconsistent nested winding and analysis limits without exposing partial volume
or hierarchy. Existing editing operations retain single-shell acceptance in this
slice; validated cavities are not yet enabled in Boolean output reconstruction.

`solid_shell_tests` independently checks:

- A 4 m cube with a reversed 2 m internal boundary: 56 m³ material, with the
  inner boundary assigned to the outer shell. Reversing the whole hierarchy keeps
  the same interpretation; two outward nested boundaries reject and identify both.
- An outer 6 m cube, 4 m cavity and 2 m material island: 160 m³ and depths 0/1/2,
  with the nearest parent retained. Two separate unit cavities yield 214 m³.
- A disconnected inward-wound unit cube stays a separate root, giving 57 m³ when
  combined with the first cavity fixture.
- A cube inside a through-hole stays outside material despite sharing its outer
  bounding box; a cavity inside the tunnel's actual wall is correctly enclosed.
- Twelve oblique rotations near (800000,-700000,600000), with mirrored/nonuniform
  scaling, retain the analytical volume. Centimeter-scale shells also pass.
- Crossing, touching, near-boundary, open and loose geometry reject with no partial
  hierarchy/volume. A 65-shell input hits the explicit analysis limit. Repeated
  analysis preserves source geometry and deterministic face/parent/volume results.

Nine targeted development suites pass in 1.59 s: shell containment, existing solid
classification, Boolean kernel/command, entity measurement, measurement queries,
GLB export, and sweep kernel/command. The new containment and existing solid/Boolean
kernel suites pass ASan, UBSan and leak detection in 7.13 s. Native Entity Info
passes on isolated X11 at DPR 1, covering frames, units/locale, atomic Undo, errors,
shared scope and diagnostics. Its label explicitly identifies ambiguous shell
containment. No new native solid-operation or live-provider acceptance is claimed.

CI's existing core test discovery includes the new `solid_shells` suite, including
the build without Qt and sanitizer preset. The next R056 layer must group cavity
boundaries with material outputs and preserve source-face provenance before
broadening Boolean acceptance; trim, split and outer-shell tools remain outstanding.
