# R041.c durable publication and reconciliation

Date: 2026-10-05 UTC. Local regression and sanitizer verification passed; CI pending. Requires [PR #61](https://github.com/zygote55/sketchyup/pull/61).
[Coordinator contract](../decisions/0024-transaction-coordinator.md).

The actual M4 room fixture prepares, inspects and commits Window A's move and a
unit change while preserving a single labeled assistant undo entry. Retrying a
lost reply and cancelling after commit return the exact original receipt without
another revision or history entry. User undo remains a separate revision; the
original committed outcome is unchanged. Stale manual edits and edit-then-undo
reject publication without overwriting the live model. Pending cancellation,
monotonic expiry, wrong-thread and reentrant operations are checked.

Explicit restart recovery reconstructs the exact committed native container and
original undo metadata, retaining a dirty recovery baseline. Another fixture edits
a shared definition with multiple instances and owned assets; decoded unchanged
records are not mistaken for edits, and the recovered definition undoes correctly.
A 100-face proposal exceeding the conservative mapping budget fails before disk
effects and remains cancellable. Lost volatile staging on restart is aborted.

Fault tests cover confirmed failure before replacement, uncertainty after rename
and after directory sync, and a real rename failure with the original checkpoint
restored for reconciliation. Dependent manual changes and cancellation are blocked
while uncertain. Verified commit evidence publishes once; verified absence records
`VERIFIED_NO_COMMIT` without publication. An uncertain begin cannot recycle its
accepted identity or mutate the model.

A real child actor is killed with SIGKILL after the committed checkpoint's directory
fsync and before live publication. Explicit restart recovery restores the exact
candidate and one original task/undo entry without command replay. The lower-level
outcome suite continues to exercise truncation, checksum/semantic corruption,
retention, storage limits and killed writers around rename.

The full development build and 51/51 CTest suites pass. All six relevant suites
(coordinator, outcome store, staging, both inspection suites and the command
regression suite) pass under ASan/UBSan. The unchanged core matrix passed in
R041.a. Builds report no compiler warnings. The coordinator contract is installed
and included in the source archive. CI includes the new coordinator sanitizer
boundary alongside the existing full core/native/package checks.
