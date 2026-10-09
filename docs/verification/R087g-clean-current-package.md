# R087.g — Clean current-source development package

2026-10-08. A fresh Release build from source
`64e6e5edbd8d76c8c535fcd6ff76930ff1f3c90c` completes the installed package lifecycle with exit zero
and no OOM. The disposable build begins without source/build or package output
directories; no earlier objects are reused.
[Complete artifact and execution manifest](R087g-clean-current-package.json).

The frozen source archive contains 1,058 regular files
and 1,090 total entries. Every extracted source file matches
the archived bytes; the complete source whitelist excludes Git and build output.
The archive SHA-256 is `65c687323e6b39b4643ce8929eac106e4ab7359fa7a45425674c7bbdb667f907`. The pinned Arch image is
`sha256:a4f0465cd5526d080bc6686368f27a38dd09dd666dbc6f0a356e8138b409b71c`. Environment package manifests are retained privately and
their hashes are included in the public manifest.

The archive retains the support-matrix snapshot from before this qualification.
This evidence record and the revised matrix are documentation added afterward;
the source and artifact hashes identify the exact tested inputs.

Of 181 registered tests, **170 pass and 11 optional external-consumer checks are
skipped**, in 108.62 seconds. These skips remain separate
from passes; this run does not repeat optional Blender/exchange-consumer coverage.
[Full clean build and lifecycle output](R087g-clean-current-package.txt).

Installation, package-owned-file checks, installed API contracts, desktop/MIME
launch of a filename containing spaces, extensionless MIME recognition, installed
recipes, GLB export, pkgrel 2→3 upgrade, reopen and removal all pass. The actual
installed [desktop launch](R087g-installed-launcher.json) checks the document and
renderer. All [25 historical migration fixtures](R087g-installed-migrations.json)
pass through the installed CLI. Their retained versions and negative cases are
the same as the [earlier migration record](R087e-installed-migrations.md).

The pkgrel 2→3 lifecycle uses one application source revision. A separate
[prior-application upgrade](R087g-prior-application-upgrade.json) compares the
retained `0.1.0-2` package from
`29fb31bba53e0b6dd2c0d5a73ffbc7fa1b44903e` with the new `0.1.0-3` package.
Both installed GUI and CLI executable hashes change. The earlier installed CLI
authors a document; both desktop implementations reopen it with the same
identity, revision and body count. Public CLI inspection and document bytes
remain unchanged. Synthetic preferences and unknown settings survive the upgrade;
removal preserves the document, preferences, data and cache sentinels. Full
execution commands and seeded preference values remain private.

The reusable acceptance helper now reads the checked-in integer package release,
builds its immediate predecessor and validates the upgrade to the current release.
It restores the original PKGBUILD on failure. This is a development package
upgrade, not an upgrade from an already published 1.0 release.

Resources remain bounded: eight-CPU ceiling, two outer compiler jobs, one LTO
worker per link, 4 GiB memory, no extra swap, 256 processes, one heavy local job
and disk-backed temporary files. The orchestration envelope was extended while
the same build process continued; compilation and acceptance were not restarted.
The [pre-acceptance resource observation](R087g-build-resource-observation.json)
records an actual cgroup peak of 3,731,030,016 bytes,
including charged file cache, with zero swap and OOM-kill events. This observation
precedes package acceptance and is not the complete lifecycle peak or process RSS.
No host package installation or live provider call occurs.

This qualifies the pinned disposable clean build. Current/reference Arch clean
machines, the final release candidate, remaining platform/performance/provider
gates, exact-head CI and ordered merges remain open. R087 and M9 are incomplete.
