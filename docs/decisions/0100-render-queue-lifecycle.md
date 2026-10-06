# Render queue scheduling and process lifetime

R070.b, 2026-10-06. RenderQueue schedules captured jobs in persisted queue order,
with one or two active Blender workers (default two). It never recaptures the model
when retrying. Queued cancellation prevents launch; running cancellation records the
intent before signaling the worker and wins over a concurrently completed image.
Only successful storage and repeated result verification emit resultReady.

Defaults remain eight pending and sixteen retained jobs, with a 2 GiB admission
budget. Each pending job reserves 195 MiB for two attempt images, logs/metadata and
atomic retained-image publication, plus one temporary imported GLB. Existing files
count separately; admission is deliberately conservative. This is an application
storage budget, not an operating-system filesystem quota. External disk exhaustion
can still occur; write failures remain failed jobs, never successful renders.

Each job owns canonical scratch directories containing bounded per-attempt logs.
The last 64 KiB of worker output is atomically persisted as it arrives. Reports retain
up to eight run histories, subject to the store's 256 KiB diagnostic limit. Graceful
owner destruction stops children and marks unfinished work interrupted. On Linux,
PR_SET_PDEATHSIG kills the direct Blender process if its application terminates;
a parent-PID check closes the setup race. Supported Blender runs directly as the
child; arbitrary launcher descendants are not a managed process tree.

Startup marks running/canceling jobs interrupted, harvests bounded worker logs, then
removes only owned canonical scratch directories. Invalid records retain their
artifacts for explicit cleanup. A failed metadata write is exposed as a runtime
failed state; later reopening reconciles the last durable state. No UI success is
reported solely because a worker exited successfully.

Tests cover simultaneous workers, queued/running cancellation, retained immutable
retry, explicit CPU fallback, failed image/record publication, storage repair,
graceful shutdown, forced application termination, child death, retained crash logs
and scratch cleanup. Native Jobs controls are the next integration layer.
