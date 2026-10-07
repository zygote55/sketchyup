# R087.f — Current development package and prior-application upgrade

2026-10-07. The full Release package built from source
`29fb31bba53e0b6dd2c0d5a73ffbc7fa1b44903e` completes its lifecycle with exit zero
and no container OOM. [Artifact and execution record](R087f-current-package.json).
The source archive contains the previously verified 1,051 byte-exact inputs.

Of 181 registered tests, **170 pass and 11 optional external-consumer checks are
skipped**, in 109.20 seconds. Skips are not passes. The retained
[CTest output](R087f-package-ctest.txt) identifies each skipped Blender/exchange
consumer. Earlier all-consumer evidence remains separate; this package run does
not claim to repeat it.

Package installation, owned-file checks, installed API contracts, desktop/MIME
launch of a filename containing spaces, extensionless MIME recognition, installed
recipes, GLB export, pkgrel 1→2 upgrade, reopen and uninstall all pass. The
[actual desktop launch](R087f-installed-launcher.json) reports ready renderer/text
and zero GL errors on X11/llvmpipe. The integrated installed CLI also passes all
[25 retained historical migration fixtures](R087f-installed-migrations.json),
including byte-exact ordinary-save comparisons, unchanged sources and rejected
overwrite/in-place/truncated-input cases. These fixtures cover versions 1–11 and
15–23, not every feature combination or every schema version.

A second disposable check upgrades between **different application implementations**:
the retained package from `8f93b4fca51a52a6cfd77e25366dc2c57430b178` (0.1.0-1) and
the new package (0.1.0-2). Both installed GUI/CLI executable hashes change. The
earlier installed CLI creates a document from its installed example; both installed
desktop versions open that document with identical identity, revision and body
count. Complete public CLI inspection and document bytes remain unchanged.
Theme, text size, reduced motion, trackpad navigation, recovery interval, field
of view and synthetic unknown settings survive. Uninstall removes package-owned
files and preserves the document, preferences, data and cache sentinels.
[Upgrade report](R087f-prior-application-upgrade.json).

The reusable `scripts/verify-package-upgrade.py` harness is source
`35f4168d5d73c8833537c709de9ebf9d50c788c9`. It requires a disposable package
container, distinct packages, an increasing package version, changed application
executables and a new evidence directory. Its checks use the actual installed
programs, with bounded subprocess deadlines and retained failure logs. No host
installation or provider call occurs. This is a development-to-development
upgrade, not an upgrade from a previously published 1.0 release.

The successful build reuses objects from interrupted earlier attempts. The final
retry follows the owner's authorization for up to eight CPUs: eight-CPU ceiling,
two outer build jobs, one LTO worker per link, 4 GiB memory, no additional swap,
256 processes and persistent temporary storage. Completed objects were preserved
when the serial retry was stopped for that concurrency change. This is **not an
uninterrupted clean-from-empty build**. The host-specific eight-CPU runner is
retained privately; the checked-in runner still defaults to one CPU.

The package container finished at 18:06 UTC. Its waiting runner/follow-up processes
were subsequently found absent, so the successful Docker state and logs were
recovered at 18:41 UTC. Their disappearance's cause is not established. The
prior-application check and current GPU-fixture build then completed under
systemd user services. These orchestration facts are not package test failures
and are retained rather than attributed to an unverified crash cause.

Current/reference clean-machine validation, remaining platform/performance
acceptance, exact-head CI, ordered merges and final release acceptance remain
open. No release tag or gate waiver is implied.
