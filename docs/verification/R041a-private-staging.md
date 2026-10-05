# R041.a private proposals and staged inspection

Date: 2026-10-05 UTC. Local regression, sanitizer and both CI runs passed. [PR #60](https://github.com/zygote55/sketchyup/pull/60) merged at 01:30:26 UTC as `70bf4d695753b663ecfc14a6f6ea43304def4580`; CI runs `37248907134` and `37248903593` succeeded.
[Preparation contract](../decisions/0022-private-staging.md).

The core fixture verifies immutable preparation, unchanged live model/history and
saved marker, exactly one composed edit, original history preservation and strict
session/state/revision compare-and-swap. Commit does not rerun the callback and
cannot apply twice. Undo restores the saved state while invalidating the original
proposal. Empty, multi-edit, replacement and throwing callbacks cannot publish.
Metadata-only proposals work and a save during staging preserves the correct
saved marker at publication.

The automation fixture opens the actual M4 room and stages a Window A move plus
unit change as one assistant task. Its proposed document is inspected through the
bounded measurement API and direct changes are paged. The publication candidate
matches the proposed native container byte for byte and adds exactly one undo
entry carrying the original task ID, request and label. Live bytes/history remain
untouched during preparation. Intervening edits, undo back to the baseline and a
reopened session with the same document ID/revision reject access.

Capacity checks cover four retained proposals, admission with owned resource
payloads, a 100-face batch exceeding its final retained charge, explicit release,
clear, expiry before access, expiry during inspection and expiry during preparation.
The 100-face proposal also verifies complete created-ID results and 60/40-row diff
pagination. Failure after an earlier successful private command leaves live bytes
and retained counts unchanged. Invalid request sizes, TTLs, pages, documents and
revisions fail before publication.

This slice provides preparation and core application primitives only. It does not
claim durable commit outcomes, provider authorization, remote retry reconciliation
or persistent transaction transport. Those remain the following R041 slices.

The complete development build and all 49/49 CTest suites pass. All 32 core
suites plus staging and both inspection suites pass under ASan/UBSan (35/35).
The existing command suite passes with unchanged default full responses. Native
X11 M4 room/components/history/save/recovery and desktop inspection/capture
regressions pass after the shared batch change. Builds report no compiler warnings.
