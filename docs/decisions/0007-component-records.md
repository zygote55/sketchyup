# ADR 0007: component definitions and placed instances

Date: 2026-10-04. R033 implementation decision; not a delivered capability.

Definitions own reusable local geometry and a rooted hierarchy. A definition has
its own stable ID, a root member, a member allocator floor, and optional references
to other definitions at leaf group nodes. Reference cycles and expansion beyond
the existing document geometry/hierarchy budgets reject before publication.
The definition root uses an identity frame; placed instance roots carry the
instance's transform and parent. Local axes changes must compensate both local
geometry and instance transforms so existing world placement remains unchanged.

A placed instance maps canonical definition member IDs to stable scene record
IDs. Nested component roots have their own instance binding. The current viewport
and geometry tools continue consuming immutable resolved Body records. Those
records are a checked projection of the definitions, not an independently
editable second source of geometry. Persistent instance bindings retain picking,
selection and command identities across regeneration, save and undo. A future
storage/cache optimization may avoid repeated resolved mesh bytes without
changing these identities or definition ownership.

Instance root placement, label, visibility/lock and instance properties remain
local to that placement. Shared geometry, face colors, curves, guides and member
hierarchy belong to the definition. A definition edit updates every matching
resolved instance atomically, including nested references; an affected locked
instance rejects the shared edit. Moving one instance does not change its peers.
Make unique isolates the selected placed instance. If a shared ancestor would
otherwise propagate that reference change to peers, the necessary ownership path
must also become unique. Other nested references stay shared. Replacement retains the selected root's
placement and identity and allocates fresh member identities as needed.

Definition changes and instance bindings participate in the same document edit,
revision, history budget and persistence transaction as scene changes. Definition,
member and geometry allocator floors survive undo. Malformed projections,
dangling references, stale revisions and cyclic definitions cannot enter the
committed document through either commands or restored files.

The public command path requires an explicit definition scope for shared edits.
The native editor supplies that scope from the open component context and shows
a persistent instance-count banner with Make unique. Root instance transforms and
state changes remain instance-scoped. No component milestone is claimed until
core mutation, persistence, native editing and scope feedback all pass.

Implementation split: R033.a records/transactions and persistence; R033.b shared
mutation, instance operations and public commands; R033.c native context workflow
and scope feedback. Each layer depends on the preceding layer and R032.c.
