# Responsive native file operations

R082.d, 2026-10-07. Native open/load validation and explicit save serialization,
backup and durable publication run on owned worker state. Widgets and the active
Document remain on the GUI thread. A nested event loop delivers timers and paints
while the existing synchronous action/can-replace contract waits for completion.
An indeterminate application-modal progress dialog appears after 150 ms; short
operations do not flash a dialog. File operations cannot reenter open/save/close
or replacement while active. An operation that has begun is not cancelled through
the dialog, because durable publication can already have occurred. Application
shutdown joins the owned operation before releasing worker state.

Open validates a separate Document before asking to replace the current model.
Failure preserves the current document and original file. Save takes a cheap
immutable read snapshot and captures its state stamp/revision on the GUI thread.
The worker serializes and saves that snapshot through the existing durable writer.
On success the live document marks only the captured state saved. Newer edits
remain dirty, status explicitly reports them, and their recovery data is retained.
The saved-file recovery context records the captured revision, not a newer live
revision. A different document session cannot be marked saved by an old result.

Save-before-replace checks dirty state again after the save completes. Edits
arriving while the worker runs therefore block replacement instead of being
silently discarded. Existing backup, source-preservation, failed-save banner and
retry behavior remain authoritative. No file-format, size-limit or durability
contract changes. Recovery checkpointing and unrelated import/export work are
not moved to this worker by this change; full UI-latency acceptance remains a
separate measured gate.

The native fixture verifies timer delivery, exact load/save bytes, read failure,
reentrant file-action/close fencing, edits arriving during save, and save-before-
replace preservation. It can additionally receive a large native fixture path.
Normal and sanitizer runs cover native display backends/scales; variable-host
wall-clock timings are not CTest performance gates.
