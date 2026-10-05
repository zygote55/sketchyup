# R046.a native assistant transaction verification

Date: 2026-10-05 UTC. Development and focused sanitizer acceptance passed.
[Binding and lifecycle contract](../decisions/0037-native-assistant-transactions.md).

The new suite drives real `AssistantTask` tool replies through a native session
bound to an existing `Document` and actual `Selection`. It verifies that initial
selection context is real, staging leaves the live object unchanged, the pinned
proposal contains the measured half-metre move, and host Apply preserves earlier
manual history while adding exactly one labeled assistant entry. Native undo and
redo restore measured positions; the immutable proposal stays unchanged.

Further cases cover human edits invalidating a sealed preview, discard without
mutation, active modeling gestures, wrong-thread calls, file-operation denial,
reopening the same document ID under a new session, and temporary locks added
after sealing. Malformed/wrong-document commits leave the valid sealed request
pending even when native policy would reject its eventual Apply.

An injected post-rename outcome-store failure leaves live memory unchanged but
reports unknown (`applied:null`), refuses session close and reconciles the durable
candidate once. Repeating host Apply afterward does not add another undo entry.
Existing owning-coordinator, transaction-dispatch and assistant suites also pass.
The final targeted development run passed all four suites in 1.52 s.
The full development run passed all 62 enabled suites in 59.63 s; the separate
real-Blender opt-in test was skipped (existing live evidence is under R049/R050).
All four focused suites passed ASan/UBSan with leak detection in 21.25 s.

Source packaging and installation also pass. CI is pending.

These are in-process transaction/selection tests. They do not stand in for the
forthcoming native panel, provider setup, hatched overlay, keyboard/layout or
live-provider acceptance.

Both CI runs passed (37328455986, 1h5m37s; 37328605854, 1h6m36s). PR #75
merged as `9be2d34f136f02cc378d27a7f3ef9c695ac3c66a` at 16:04:24 UTC.
