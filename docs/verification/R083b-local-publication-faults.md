# R083.b local publication fault checks

2026-10-07. Source `8d20257`: the dedicated new-file fault matrix passes **1/1
normal in 0.04 s** and **1/1 ASan/UBSan in 0.09 s**, with leak detection and
halt-on-error. ENOSPC/partial writes, file/directory sync failures, EINTR retry,
link failure, a racing writer, intentionally killed writers and existing/symlink
targets all preserve the specified before/after-publication outcomes.

Faults are confined to the test process; no user file or machine-wide disk state
is modified. The [contract](../decisions/0143-new-file-publication-faults.md)
distinguishes process interruption from power-loss durability. Existing recovery
and transaction suites and subsequent hardening layers remain required. This is
local overlapping fixture work, not R083/M9 acceptance; integration, remote CI,
ordered merges and milestone prerequisites remain open.
