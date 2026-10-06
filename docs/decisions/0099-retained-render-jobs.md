# Retained render inputs, results and job records

R070.a, 2026-10-06. RenderJobStore owns a private directory and one process lock.
Each job has a canonical UUID directory, immutable `scene` package and an atomically
published versioned `job.json`. Its record contains source identity/revision/hash,
captured worker options, timestamps, queue sequence, launch count, state and bounded
diagnostics. No document or history mutation is part of a store operation.

Default limits are eight pending jobs, sixteen total retained records and a 2 GiB
store budget. The initial storage layer reserves 65 MiB per pending result;
[the queue contract](0100-render-queue-lifecycle.md) expands this for worker scratch
and atomic result publication.
Enqueue checks capacity before copying and commits metadata last; any ordinary
failure removes the new job directory. Existing jobs are never silently evicted to
make space. Explicit removal/clear operates only on terminal jobs. Source files and
job directories cannot be symlinks; removal cannot follow a substituted job directory.

Queued, running, canceling, completed, failed, canceled and interrupted are distinct
states. Only verified image publication can enter completed. A monotonic persisted
queue sequence establishes order even within one clock tick; retry appends to that
order while preserving the original capture. Launch count is bounded to 1,000.
Completed work is retained for inspection; retry applies to failed/canceled/interrupted
jobs. New rendering can enqueue another capture.

Inputs reopen only after bounded manifest/GLB/HDR integrity checks and are copied to
an isolated worker snapshot. Retained result loading repeats the worker's image,
source, settings, engine, renderer, lighting, loss and CPU-fallback verification.
Image/result files publish before the completed record. Failure to write or verify
an artifact leaves the prior state intact and never reports successful publication.
Attempt diagnostics are stored separately from the result manifest. Job reports are
bounded to 256 KiB; excess diagnostics retain a marked 64 KiB encoded tail.

Opening the store converts running/canceling jobs to interrupted. Queued sources and
completed results are reverified. Corrupt or incomplete records appear as failed;
their on-disk evidence is retained for explicit cleanup. The store never assumes a
completed flag proves that valid pixels still exist. A live process lock prevents
another writer from opening the same store.

Tests cover original-capture destruction, later model edits, source/result integrity,
reopen after interruption, immutable retry, queue ordering, lock exclusion, queue and
disk limits, failed atomic record writes, invalid/corrupt PNGs, bounded diagnostics,
explicit cleanup and substituted symlinks. Scheduling, process lifetime handling and
the native Jobs list are the next R070 layers.
