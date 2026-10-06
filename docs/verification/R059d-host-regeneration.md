# R059.d — Immutable host regeneration with stable opening identities

Date: 2026-10-06 UTC. Depends on R059.c in the review stack.
[Hosted component contract](../decisions/0054-hosted-components.md).

`host_regeneration_tests` verifies:

- Two independently keyed cuts produce the expected closed material volume. Moving
  one retains its corner, reveal and edge IDs without consuming allocator space,
  and leaves its peer's opening/reveal records exact. Repeating the request is
  byte-for-byte idempotent.
- Resizing retains matching source-corner identities. Reversing profile winding
  changes no host geometry. Adding a collinear source corner retains matching
  features, allocates new ones above current floors, and retires replaced edges and
  reveal faces without copying their flags onto new edges.
- Painted reveal colors/material sides and styled opening edges survive movement.
  New reveals inherit current entry appearance. Cleared original edge flags remain
  cleared because the uncut baseline contains no stale appearance state. Reflected/
  nonuniform host placement and unrelated properties remain unchanged.
- Removing one opening restores its region and preserves its neighbor. Removing
  the final opening restores original geometry with current original-face metadata
  and retained allocator floors. A later new opening receives fresh native IDs.
- Dense temporary construction allows movement and restoration even when both live
  allocators are exhausted; adding geometry then rejects safely.
- Independent host geometry drift, colliding cached identities, duplicate profile
  keys, overlapping cuts and too many openings reject without source mutation.
- The generated body stages through the actual Document PreparedEdit path, remains
  private until Apply, and supports one Undo and exact Redo with resolved appearance.

The regeneration, hosted opening, component glue and prepared-edit CTest suites pass
(4/4, 0.26 seconds). Regeneration also passes AddressSanitizer,
UndefinedBehaviorSanitizer and leak detection. Normal, Qt-free and sanitizer CI
discover the new core test. The previous integration layer passed all 88 development
suites. This layer adds immutable regeneration only: persistent attachment storage
and automatic instance-event publication remain the next integration step.
