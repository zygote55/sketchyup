# R087.e — Installed historical document migration

2026-10-07. The retained development Arch package from source
`8f93b4fca51a52a6cfd77e25366dc2c57430b178` passes all 25 historical fixture
migrations through its installed `/usr/bin/sketchyup-cli`. The
[report](R087e-installed-migrations.json) binds the package, installed executable,
fixture and result hashes to harness source `7e70875d0471008baa5f51056daa37b7697f692e`.
This validates that earlier package, not a newly rebuilt current release candidate.

The corpus spans document versions 1–11 and 15–23, with repeated fixtures for
some versions; it does not contain a feature-rich retained file for every schema.
Every migration preserves source bytes, identity/revision, public model inspection
and resource counts. Complete output containers match a separate ordinary load/save
CLI invocation byte for byte, including embedded asset payloads. Every existing
destination survives a rejected repeat. Writable source copies survive rejected
in-place migration, and truncated inputs fail without creating outputs. The native
migration implementation also validates complete model round-trip before publication.

The actual installed desktop application opens the migrated M4 fixture with seven
bodies and revision 36, matching the original identity. Its
[capture report](R087e-migrated-desktop.json) records ready renderer/text and zero
GL errors using Qt 6.11.2, X11 and Mesa 26.2.3 llvmpipe. This is a launch check,
not a visual parity review or performance measurement. Package removal succeeds
and the migrated user document remains.

The check ran alone under `run-container-check.sh`: one CPU, 4 GiB memory, no
additional swap, persistent temporary/capture paths and no host package changes.
Container exit was zero. Complete logs, migrated files, rejection fixtures and the
capture remain in `build/local-checks/r087e-installed-migrations` on persistent disk.
No application rebuild or provider call was required.

The reusable verifier is now called after package upgrade in the lifecycle
harness. It refuses an existing evidence directory and writes an incomplete report
if a later case fails. The [current-package follow-up](R087f-current-package.md) now passes this
integration and a separate prior-application package upgrade. Current/reference
clean-machine validation, final release checks, CI and ordered merges remain open. No acceptance gate or performance budget is waived.
