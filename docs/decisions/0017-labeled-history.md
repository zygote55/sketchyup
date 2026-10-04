# Labeled history and guarded navigation

Date: 2026-10-04. R039.a. History remains session-local; native model schema 11
and container v2 are unchanged by this layer.

## Entries and retention

Each retained Edit has a nonempty label (up to 512 UTF-8 bytes), optional task ID
(up to 128 bytes), optional request text (up to 4,096 bytes), and an assistant flag.
Assistant entries require both task ID and request text. Text rejects embedded
NUL; the JSON adapter additionally rejects lossy Unicode conversion. These are
inert display/provenance values, never executable instructions or authorization.
Task labels do not imply that an AI provider exists yet.

The existing change-set stack remains authoritative. Metadata counts against the
64 MiB history budget, and retained entries are additionally capped at 10,000.
Pruning exposes a marked retained baseline; it never claims that earlier steps
can still be undone. Pages contain at most 1,000 entries (200 by default), ordered
chronologically across both applied and redo portions. Each row has the cursor
position after that step, label/task data, applied state and explicit-save marker.
The baseline has its own save marker. A new edit removes the redo branch.

Numeric amendment preserves the original action label and task/request identity.
It replaces the original operation's data as one history item and accounts for
metadata memory before publishing the revised state. Save/reopen and crash
recovery restore model records, not the old undo stack or task prompts. A recovered
baseline is Edited until explicitly saved, even after undoing new changes back to
that baseline.

## Navigation and public API

`Document::navigateHistory(position, expectedRevision)` stages movement privately
using existing undo/redo change sets. It validates the target, stale revision and
revision-space capacity before publication. Every traversed step advances the
revision, and all requested steps publish together. It adds no new history item;
navigating to the current position is a no-op. Saved-state and monotonic allocator
floors retain ordinary undo/redo semantics.

`history.describe` is a read-only public query with optional canonical decimal
string `offset` and `limit`. Results include identity/revision, total/current
position, pagination, memory usage, pruning, save markers and row metadata.
`executeHistory` is a separate control operation accepting `apiVersion: 1`,
`documentId`, `expectedRevision` and canonical string `position`. It cannot be
composed into a geometry batch: traversing existing history is not a new modeling
Edit. Capabilities expose this history control contract separately from the
modeling command catalog.

Modeling batches accept optional `history` metadata with `label`, `taskId`,
`request` and `assistant`. A single command defaults to its human-facing catalog
label; multi-command batches use a counted batch label. Validation rejects unknown
metadata fields or invalid values before publishing anything. Preview keeps labels
and task metadata private with the candidate model.

The CLI supports `--query history.describe`. `--history-position N` moves the
retained cursor after opening/importing or running a local recipe, before writing
an optional output file. It returns the resulting document plus navigation outcome,
not stale creation results for geometry just undone. It cannot be combined with a
read-only query or preview. Reopened native files start with an empty history,
so meaningful CLI navigation applies to operations in that invocation.

## Native controls (R039.b)

View → History reveals the model tray and selects its History tab. Rows name the
starting/retained baseline and up to 200 steps per page, with current, applied,
redo and saved markers. Assistant entries display an AI prefix and read-only
plain-text task/request details. They do not claim provider integration.

Clicking or pressing Enter navigates to that row. Navigation defers until Qt has
finished dispatching the item event, then checks the captured document session,
state and revision before canceling any tool preview and executing the shared
history control. A stale event refreshes the list with an inline explanation and
preserves newer edits. Successful navigation returns focus to the viewport.

Menu labels name the next undo/redo operation; tooltips preserve full labels and
ampersands are escaped for Qt mnemonics. Menu, keyboard and panel actions share
the existing stack. Saving refreshes markers even without a content revision.
Undoing back to explicitly saved content reports no unsaved edits, even if a
recovery checkpoint exists for a later state. Paging changes no model state.

Document unit preferences and the integrated M4 room fixture remain within R039.
