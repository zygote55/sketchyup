# Native recovery scheduling and selection

Date: 2026-10-04. R038.b. Builds on the durable store in decision 0015.

## Scheduling and acknowledgement

Normal application launches enable recovery every 30 seconds. File → Recovery
settings accepts 5–3,600 seconds or Off, stored in application preferences. File →
Save recovery now provides an immediate checkpoint/retry. Explicit automated
smoke/capture/benchmark launches do not start recovery; native tests use isolated
temporary roots. The normal root is `recovery` beneath Qt's application data path.

Snapshot capture/serialization happens on the document thread. Disk publication,
sync and generation cleanup run on a worker with immutable snapshot bytes. At
most one write is in flight. Completion acknowledges the captured revision even
if editing has since advanced; it never marks the document explicitly saved.
A document-session stamp prevents a completion for a replaced document from
claiming protection for the new model. There is no general claim of asynchronous
explicit Save or constant-time snapshot capture for large models.

The persistent recovery line shows the last verified copy's capture time and the
number of newer edit revisions still in memory. Its tooltip gives exact revision
numbers and any storage error. A failure pauses automatic attempts until manual
retry, so persistent I/O faults do not accumulate repeated uncertain writes. A
successful retry uses the store's fresh-generation path. Off stops scheduling
but retains already protected data. Returning to the explicitly saved content
retires stale recovery on the next timer tick.

## Opening recovered work

Startup offers inactive recovery sessions; File → Recover work reopens the list.
Rows show the model name, last explicit-save time, recovery capture time and the
revision difference. For a reopened native file, its modification time supplies
the initial saved-file timestamp; saves in the current session record their actual
completion time. These are edit-revision counts, not reconstructed undo history.
Unreadable data is labeled Unverified and cannot be opened as recovered work.
Permission/lock errors remain visible when they are not a live-session conflict.

The details area reports the verified revision, incomplete tail/interior errors,
missing resource names and source path. It scrolls rather than expanding without
bound. Corrupt data is preserved unless the user explicitly discards it or opens
a verified prefix and a replacement recovery copy later becomes durable.

Open recovered version opens the verified model as Edited, retains the identity,
and clears the native save destination. Its first explicit Save chooses a new
`.sketchyup` path. The source recovery's verified revision is shown immediately;
the source recovery directory remains until a replacement checkpoint is durable
or the user explicitly saves/discards. An intervening crash therefore retains
recoverable work. Open last saved validates the current saved file and its document
identity; it retains the recovery alternative. Neither choice implicitly writes
the original saved file. Missing or replaced saved files produce an inline error.

## Save, close and replacement

Save/Discard/Cancel remains the gate for replacing or closing dirty work. Cancel
leaves the current document and its recovery intact. Successful explicit Save and
chosen Discard retire that document's recovery sessions, after any in-flight write
finishes. Cleanup failure leaves a visible recovery error and may leave an old
candidate; it never converts a failed explicit Save into success. Destruction
without an accepted close retains recovery instead of treating it as Discard.

Explicit save failures have a persistent banner with Retry Save and Save as, and
a Not saved title. The recovery line continues to state only its independently
verified revision; it does not claim that a save failure destroyed or protected
newer edits. Successful Save clears the failure banner and retires old recovery.

The recovery workflow does not restore the previous undo stack, viewport camera,
tool preview or selection. R039 adds labeled history and the integrated M4 gate.

## Headless access

`sketchyup-cli --recovery-list ROOT` returns inactive candidates with verified
revision/time, errors, incomplete-tail status and missing resources.
`sketchyup-cli --recover ROOT/SESSION --output copy.sketchyup` opens the same
verified prefix and can run the normal queries or local editing script. Source
selection is exclusive with `--input` and `--import-formline`. Output must be
outside the recovery session and cannot resolve to the original saved file;
linked source/output-parent aliases are checked. Neither command discards
recovery evidence. The JSON result includes a structured `recoveryReport`.
