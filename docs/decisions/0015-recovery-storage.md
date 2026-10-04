# Durable recovery checkpoints and journal

Date: 2026-10-04. R038.a implements storage; scheduling and desktop recovery UI
follow in R038.b. Native model schema and container version are unchanged.

## State and ownership

Recovery is separate from explicit Save. `captureRecovery` captures immutable
native container bytes, document identity/revision, source path, last explicit
save revision/time and capture time. The writer receives no live document and
never calls `markSaved`. A successful write returns only the captured revision;
newer in-memory edits have no implied protection. The caller must keep its last
acknowledged recovery revision/time separately. On write failure it keeps the
previous acknowledgement and reports the error.

Each writer owns an application-data session directory named
`DOCUMENT-1-SESSION`, with random 32-hex session tokens. The middle field is the
current document epoch, 1. Separate sessions for the same document never overwrite
one another. A process lock excludes active sessions from recovery selection and
discard. Dead-process locks can be reclaimed; locks never expire by age alone.
New directories have owner-only access and their entries are synced to disk.

Opening verified recovery returns a dirty document with its original identity,
revision, records and allocator floors, and no invented undo history. Undoing
new edits back to the recovered checkpoint does not mark it saved. Explicit Save
acknowledges the recovered state normally. Source files and their retained
`.sketchyup.bak` copies are untouched by the recovery store.

## On-disk protocol

`CURRENT` is an atomically replaced, checksummed pointer containing a validated
checkpoint record and the name of an append-only journal. Each checkpoint is a
complete native container, including embedded asset bytes. Each journal record
contains document/epoch, sequence, base/result revisions, previous-frame hash,
checkpoint name and hash, and explicit-save/capture metadata. It references a
complete replacement state rather than replaying commands or serializing the
in-memory undo stack. This specializes decision 0005's validated-change frames
for interval-based M4 recovery; per-command durable remote outcomes remain later
work.

Frames have a little-endian 32-bit payload length, 32 raw SHA-256 bytes and UTF-8
JSON. Metadata payloads are bounded to 64 KiB (below the protocol's 32 MiB
ceiling). Checkpoints use the native container bounds; the reader additionally
caps any file at 128 MiB and journals at 512 KiB. All integers use canonical
decimal strings. Filenames must match generated token forms, never relative or
absolute paths from metadata. Linked checkpoint/pointer/journal files reject.

A normal write durably publishes a new immutable checkpoint first, then appends,
flushes and fsyncs its journal frame. Only then does it acknowledge protection.
Every generation contains at most four checkpoints. Before another append would
exceed four checkpoints or 128 MiB retained container bytes, it starts a new
generation: publish and sync the new checkpoint and empty journal, publish and
sync the new `CURRENT`, then retire old generation files. This bounds routine
retention without deleting the last verified generation before replacement.
Peak disk use includes the previous generation plus the replacement checkpoint.
A failed write forces the next retry to publish a new generation, so it cannot
append behind an uncertain or partial tail. Failed writes may leave orphan files;
a successful compaction cleans them up.

The full-checkpoint approach trades extra bounded disk writes for a single,
already validated codec covering every M4 record and binary resource. There is
no claim of delta-journal efficiency. Scheduling/performance is measured in the
native follow-up; serialization currently occurs at snapshot capture.

## Recovery and discard

The reader verifies the pointer, checkpoint checksum/native structure, and every
complete subsequent frame's chain and referenced checkpoint. It stops at the
first invalid frame; it never skips interior corruption. A partial last frame is
ignored and reported separately. The result identifies exactly the last verified
revision, or makes no recovery claim if even the initial checkpoint is invalid.
Corrupt original data remains untouched for diagnosis. Listing verifies one model
at a time and retains only metadata, not all model payloads.

Discard requires the session lock. It first renames the selected session outside
the candidate namespace and fsyncs the parent, then removes retired files. Other
sessions and explicit saves are unaffected. A cleanup failure may leave a hidden
retired directory, which is not offered as recoverable work.
