# R012: bounded containers and explicit durable saves

Date: 2026-10-03. Native Linux foundation scope. Recovery journals remain absent.

The writer emits the ADR 0005 binary envelope with one SHA-256 checked JSON v2
document chunk. Readers enforce header, manifest and payload size bounds, canonical
integer strings, exact chunk coverage and metadata agreement; unsupported chunks,
features and fields are rejected. Raw v1/v2 JSON is migrated in memory, without
rewriting its source. The first explicit save retains the old valid file as `.bak`.
The reserved epoch is currently `1`; assets and epoch rotation are not implemented.

A save captures immutable bytes plus a content-state stamp. State stamps distinguish
branches of copied documents even when revision numbers coincide. A completion
acknowledges only captured content in the same document session; later edits stay
Edited, and undo to the saved content becomes clean. Reopening invalidates pending
stamps. The API is ready to separate capture/write, but the current UI saves
synchronously; it makes no background-save responsiveness claim.

The Linux writer disables direct-write fallback, writes a sibling temporary file,
flushes/fsyncs it, separately writes/fsyncs/replaces a verified prior-file backup,
syncs the directory, replaces the target and syncs its parent again. Only then does
it mark the captured state saved. An invalid existing target cannot overwrite a
known-good backup. Unreadable targets and obstructed backups fail before replacement.
Existing symlinks resolve to their actual target and parent for replacement/sync.
Post-rename sync failures explicitly report uncertain durability and leave Edited.

`persistence_tests` uses process-local syscall interposition, with no production
fault switches. It checks all truncation byte boundaries, corrupt hashes, future
versions, overflow/range metadata, duplicate chunks, unknown data, migration/source
preservation, snapshot completion after edits/branching/reopen, backups and symlinks.
Forked children are killed during the file-sync, backup-directory-sync and final
replacement-sync stages. The target is always the complete expected old/new file;
the previous valid backup survives replacement. Injected ENOSPC writes, EIO syncs,
directory-sync failures, permission denial and obstructed backups exercise dirty
state and prior-file preservation. Existing short-write RLIMIT_FSIZE tests pass.
These are syscall/process-failure checks, not a claim of physical power-loss testing
or guarantees from storage hardware that lies about fsync.

Observed checks:

```sh
cmake --build --preset dev --parallel 4
ctest --preset dev                         # six targets pass
QT_QPA_PLATFORM=wayland timeout 40s build/dev/dialog_tests
cmake --build --preset sanitize --parallel 4
ctest --preset sanitize                    # three non-Qt core targets pass
build/dev/persistence_benchmark            # R012-codecs.json
```

The codec report repeats the original 1/100/1000 wall-ring fixtures for JSON, CBOR,
compressed JSON and the new envelope. Container decode timing includes hash checks,
full geometry validation and canonical JSON re-encoding for comparison; the older
codec decode timings exclude geometry validation (shown separately). It is not a
large-asset benchmark. Native Save/Save As/Open cancellation and replacement dialogs
continue to pass with the new envelope. Save errors remain visible through the
existing native error dialog, without claiming recovery protection.
