# R059.b — Bounded native through-wall openings

Date: 2026-10-06 UTC. Depends on R059.a in the review stack.
[Hosted component contract](../decisions/0054-hosted-components.md).

`hosted_opening_tests` verifies:

- A rectangular wall opening removes exactly the expected prism volume and leaves
  a validated closed solid. All original face/vertex IDs, unrelated face records
  and original edge IDs survive. Generated jambs and appearance lineage are explicit.
- A second cut retains the first, using the same entry/exit face identities. Both
  profile winding orders work. Overlapping holes, a profile enclosing an existing
  hole, outside profiles, edge contact, insufficient clearance and off-plane input
  reject; within-tolerance plane coordinates snap safely.
- A room with 1,000 cubic units outside and a 729-unit interior cavity loses only
  two units for a four-square-unit opening through its half-unit near wall. The
  opposite wall remains byte-for-byte unchanged. A wholly enclosed intervening
  cavity rejects instead of being bridged by an oversized tunnel.
- A U-shaped host rejects a profile whose corners are inside but whose edges bridge
  the void. Concave simple cut profiles preserve their analytical volume.
- Reflected/nonuniform rotated hosts around a large origin, sheared hosts and
  feature scales from 1e-3 to 100 preserve independently expected material volumes.
  An independent native-loop signed-volume oracle checks the sheared result before
  comparing the analyzer's 1e-7-quantized triangulation within its precision.
- Missing faces, self-crossing profiles, open/inverted hosts, sloped exits and
  over-budget loops/profile counts fail without source mutation.

Four targeted CTest suites pass (hosted openings, placement, solid shells and
push/pull; 0.26 seconds). Hosted openings also pass AddressSanitizer,
UndefinedBehaviorSanitizer and leak detection. Normal, Qt-free and sanitizer CI
discover the new core test. Persistent attachment records, automatic move/delete
updates, material publication, native UI and live-provider acceptance remain
separate work; this evidence covers immutable geometry only.
