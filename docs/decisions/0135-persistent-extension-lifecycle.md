# Persistent extension lifecycle

R080.b, 2026-10-07. Installation copies a validated declarative `.sketchyext` package
into an application-owned registry and starts it disabled. The original package
path is not a dependency. Enabling requires compatibility with the current manifest
and command API; disabling blocks subsequent actions. A reported worker/action
failure disables the package and persists its bounded error until explicit enable
clears it. Removing an entry affects the installed copy only. Duplicate identities
reject; installing another version requires explicit removal first.

The registry is one version-1 JSON file containing at most 64 entries and 24 MiB.
Each entry records its identity, canonical Base64 source bytes, source SHA-256,
enabled flag and bounded error. Manifest limits still apply independently. Source
hashes, identity agreement, exact record fields and state types are checked on load.
An installed manifest that is incompatible with the current application remains
inspectable with its source retained, but is disabled in memory and cannot run.
Corrupt registry framing, hashes or identity records fail instead of resetting or
overwriting user state.

Updates build a replacement state privately. A cooperating-process lock and the
previous registry digest reject concurrent stale writes. QSaveFile writes an atomic
replacement with direct-write fallback disabled and owner-only permissions. Links
and non-regular registry destinations reject. In-memory state changes only after a
successful commit. This guards ordinary failed/stale updates; it is not an isolation
boundary against another process already running as the same operating-system user.

Tests cover default-disabled installation, exact source retention, duplicate/invalid
package rejection, enable/disable/error/remove across reloads, explicit failure
recovery, stale instances, incompatible source retention, failed persistence,
malformed registry preservation and linked destinations. Native management and the
bounded process runner follow in subsequent R080 layers. No extension runs merely
because the registry was loaded or a document was opened.
