# R083.b — Integrated new-file publication fault checks

2026-10-07. Integration `2078249`; fixture `8d20257`.
Parent: [PR #204](https://github.com/zygote55/sketchyup/pull/204).

The final fault matrix builds and passes **1/1 in 0.04 s**. Dedicated
[ASan/UBSan evidence](R083b-local-publication-faults.md) remains **1/1 in 0.09 s**
with leak detection and halt-on-error. The actual publication path sees injected
ENOSPC/partial writes, file and directory sync failures, EINTR retry, link failure,
a racing writer, killed writers and preexisting/symlink destinations. Assertions
distinguish failures before publication from an already published file whose
directory synchronization failed. Handled failures clean temporary outputs.

Interposition and process termination are confined to test-owned processes and
files. Process kill is not a power-loss durability simulation; this fixture does
not claim physical filesystem crash recovery. Application code is unchanged.

[Installed checks](R083b-final-installed-smoke.json) confirm the desktop/CLI/helpers
remain byte-identical to the prior verified build, all installed catalogs/contracts
match, and ADR 0143 is installed. The [source package](R083b-final-source-package.json)
contains **202 byte-exact installed inputs**, SHA-256
`f4f2d9f9b6044b435cb9e37c9773bc3b51d9aa9f1a2828f6d149d477b0cf1755`.
Transaction-sequence and complete hardening verification follow. Remote CI, ordered
prerequisite merges and R083/M9 acceptance remain open.
