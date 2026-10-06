# Native retained render jobs

R070.c, 2026-10-06. The status chip opens a modeless Jobs window; Render setup also
provides Jobs. The list shows all retained captures, queue state/progress, attempt
count, elapsed time, source revision and current-session staleness. Selected-job
controls cancel pending work, retry its immutable capture, reopen verified output
and explicitly remove stored files. Cleanup never removes pending work.

The setup allows a new capture while earlier workers run. Capturing one model at a
time still prevents overlapping asynchronous preparation. Device probes wait for
pending renders, preserving the shared two-worker process bound. Existing verified
device settings can enqueue subsequent captures. The chip counts active and queued
jobs even when the most recent job has finished.

The application data directory holds the locked render store. Existing stores reopen
when the native panel starts; queued captures resume and interrupted work stays
available for explicit retry. First use creates the store lazily. Failure to open or
write it appears in the native status/details; no provider credentials are involved.

Two result tabs remain open at most to bound decoded image memory. Closing a tab
keeps the durable result; Jobs reopens it after full verification. Removing a retained
job also closes its result tab. A fresh application/window labels restored captures
conservatively rather than claiming the current in-memory document session matches.
Current-session results distinguish later edits and document replacement.

Native tests exercise concurrent captures, queue advancement, cancellation, retained
logs, explicit cleanup, reopening a closed tab, a destroyed/recreated window, and
source provenance. Tests isolate both configuration and application data directories.
The renderer and model/history remain independent of Jobs controls.
