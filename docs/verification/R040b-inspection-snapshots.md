# R040.b retained inspection snapshots

Date: 2026-10-04. Local verification passed; CI pending.
Requires [PR #57](https://github.com/zygote55/sketchyup/pull/57). The M4 gate is complete.
[Snapshot contract](../decisions/0020-inspection-snapshots.md).

The integrated room fixture captures Window A's measurements and editor context,
then moves and renames the live window, changes live units and clears live
selection/hiding/locks. Captured geometry, names, units, saved status, resource
counts and temporary view state remain exact. Live revision differences are
reported, continuation pages remain valid, and live document bytes/save stamp
are untouched by reads. A capture made without an editor does not accept a
later editor as a substitute for missing captured selection.

Core snapshot checks cover shared immutable records, exact identity/revision,
saved and dirty markers, editor session membership, absent undo/redo storage,
live edits/deletion and safe allocator floors for a private copied document.
Session checks cover count/byte admission failure, explicit release, fake-clock
expiry before and during a query, reclaim on dispatch, document replacement
with the same ID/revision, session clear, bad TTLs and stale/foreign context.

An initial reconstruction test caught temporary hiding being filtered out by
ordinary UI selection operations. Captured editor state is now retained directly
alongside an immutable document snapshot. A subsequent test crash was reproduced
under GDB: the test retained `QJsonValueRef` from a temporary response object.
It now holds an owning `QJsonValue`; the same assertion pattern was corrected in
PR #57's reflected-normal fixture. The three targeted suites pass afterward.
No system core dump was available; the local test backtrace identified the exact
assertion, and no OOM event or user-model operation was involved.

This is an in-process session boundary. Persistent CLI/MCP dispatch, desktop
view capture, remote providers and editing transactions are not claimed here.

Admission preflight counts model/resource records, semantic strings, allocator
bookkeeping and editor state before allocating the capture. The core read-copy
operation omits history at construction rather than copying and clearing it.
The complete M4 native encoding of that copy matches the original byte for byte.

The full development build and all 47/47 CTest suites pass. The complete 31-suite
core matrix plus both inspection suites pass under ASan/UBSan (33/33). Focused
normal and sanitizer runs also pass after the final discovery-text/schema update.
The resource fixture rejects a 2 MiB owned payload before capture under a 1 MiB
session budget. Both installed schema artifacts match the staged CLI registries;
the source package includes the schemas, contracts and test fixtures. CI checks
both published registries during disposable Arch installation and removal.
