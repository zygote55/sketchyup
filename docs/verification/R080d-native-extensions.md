# R080.d — Native extension management

2026-10-07. Native contract `d909356`; integration `7bc52ec`.
Parent: [PR #196](https://github.com/zygote55/sketchyup/pull/196).
Contract: [0137](../decisions/0137-native-extension-management.md).

Desktop/CLI/helper and affected native targets build. Final extension checks pass
**3/3 in 0.49 s**. Fresh dedicated ASan/UBSan core checks pass **3/3 in 4.42 s**.
Four final native regressions pass on Wayland at 2×: extension management, library,
history and native MCP (**15.638 s** total). The dedicated extension [platform
matrix](R080d-platform-matrix.json) passes all eight normal/sanitized Wayland/X11
1×/2× combinations: **14.053 s normal**, **32.041 s ASan/UBSan**, with leak detection
and halt-on-error.

The native manager installs, enables, disables and removes declarative packages,
shows capabilities/errors and edits typed action parameters. Resolution runs off the
UI thread, supports cancellation, and joins safely on close. Before applying one
public atomic batch, the parent checks document identity/revision and unchanged,
still-enabled package bytes. Errors persist a disabled state; cancellation preserves
the enabled state. Tests cover successful geometry/undo, failed actions, changed
models/packages, lifecycle reloads and bounded worker failure. The dialog's labels,
numeric formatting and scrolling were visually reviewed in the dedicated matrix.

The [installed smoke](R080d-installed-smoke.json) verifies actual extension discovery
against the installed catalog, mixed-mode rejection, the installed sample/helper,
nine catalogs, forty-two contracts and existing exchange workflows.
[Source package](R080d-source-package.json): **191 installed inputs** match byte for
byte; SHA-256 `986c2b9ad30fddad3360962f372764230fc2b92276702a18455cf6eaf1ec7fa0`.
[Final native regressions](R080d-native-matrix.json) retain individual timings.

R080.a–d are locally implemented and verified. Complete remote CI, ordered merges
and milestone acceptance remain required. No OS sandbox or external plugin runtime
compatibility is claimed.
