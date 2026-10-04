# R038.a: durable recovery storage

Date: 2026-10-04. Storage implementation; background scheduling and native UI
are R038.b. Requires [PR #49](https://github.com/zygote55/sketchyup/pull/49).

The [storage decision](../decisions/0015-recovery-storage.md) describes the format,
ordering, limits, generation replacement and explicit-save separation.

`recovery_tests` passes immutable snapshot completion after newer edits; exact
native record/identity comparison; dirty recovered state without invented history;
last explicit-save metadata; active session exclusion; independent sessions for
one document; path/checksum/chain validation; complete M4 resource preservation;
and selected-session discard without source mutation.

Every truncation boundary of a journal frame recovers the previous verified
checkpoint. A corrupt complete frame, broken previous hash, invalid filename,
linked/truncated checkpoint and damaged CURRENT pointer stop recovery without
rewriting the evidence or claiming unverified revisions.

Process-local syscall interposition injects ENOSPC and partial writes. Tests fail
every sync boundary of append and compaction (3 and 6), then verify an intact
old or new generation. Forked writers are killed at each of those boundaries;
the reader reclaims the dead-process lock and verifies the last complete revision.
A retry after uncertain append publishes a fresh generation and only then retires
old files. These are process/syscall fault checks, not physical power-loss tests.

Full development checks pass 38/38; core sanitizer checks pass 28/28.
The desktop remains
explicit-save only until the scheduling/UI layer is enabled; this layer alone
does not claim automatic recovery protection.
