# R041.b durable outcome storage

Date: 2026-10-05 UTC. Local regression, sanitizer and both CI runs passed. [PR #61](https://github.com/zygote55/sketchyup/pull/61) merged at 01:51:39 UTC as `6fe484780b889017eebaa5b0efac403826df82f9`; runs `37249850156` and `37249847175` succeeded. Requires [PR #60](https://github.com/zygote55/sketchyup/pull/60).
[Storage contract](../decisions/0023-durable-outcomes.md).

The fixture verifies persisted pending identities, exact co-recorded before/after
native containers and original result, exclusive writer ownership, terminal retry
without rewriting, mismatched payload conflicts, cancellation before/after commit,
expiry, missing identities and restart. Older or divergent same-revision live
models require reconciliation before accepting a new request.

Retention tests fill a lowered outcome cap, reject new work before 30 days,
repeat that rejection after clock rollback, then prune expired terminal entries
without recycling their IDs. A small storage cap rejects admission before a
pending identity is persisted, preserving capacity for terminal results.

Injected failures cover before write, after write/flush, after file fsync, after
rename and after directory fsync. Pre-replacement failures retain the complete
pending checkpoint. Possible replacement freezes lookups, cancellation and new
work; reconciliation resolves the complete committed checkpoint without command
replay. All truncated prefixes and an interior payload corruption reject opening
without rewriting evidence. Removing an existing checkpoint cannot reset IDs.

Real child processes are killed with SIGKILL after temporary-file fsync and after
rename. Restart releases their stale process locks and recovers a complete old
pending state or the complete committed candidate respectively. Verified absence
of a commit can be recorded as aborted. These are process-interruption and fault
injection checks, not a claim to emulate physical power loss on every filesystem.

This storage layer does not mutate a live document, restore its undo entry, expose
transaction tools or implement provider authorization. The coordinator is the
next R041 slice.

The development build and 50/50 CTest suites pass. Final focused normal and
ASan/UBSan runs pass after stricter native-payload validation and clock handling.
Checksummed but semantically invalid metadata/native contents are rejected with
`CORRUPT_OUTCOMES`. A commit whose two snapshots exceed a lowered store budget
fails before disk effects and still permits a durable abort using reserved capacity.

A standalone Debug benchmark records five durable begin/commit samples per
fixture on this Linux/Intel Ultra 7 host (Qt 6.11.2, GCC 16.2.1). Median begin/commit
milliseconds: empty 0.67/0.76; actual M4 room 2.51/4.34; 1000 face records
43.78/88.21; one 8 MiB owned asset 89.53/108.25. Corresponding final checkpoint
sizes are 4,124; 41,234; 1,141,500; and 16,782,380 bytes. Whole-process peak RSS
across the benchmark was 126,888 KiB (about 124 MiB), measured with Linux
`getrusage(RUSAGE_CHILDREN)`. This is evidence for these fixtures, not a strict
RSS bound or release performance/hardware guarantee. The rewrite cost motivates
keeping the initial storage cap explicit and measuring a future append design.
Raw [samples and fixture hashes](R041b-outcome-benchmark.json) and
[resource usage](R041b-outcome-benchmark-usage.json) are retained.
