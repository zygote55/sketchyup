# R042.a persistent headless sessions

Date: 2026-10-05 UTC. Local verification passed; CI acceptance pending. Requires
[PR #63](https://github.com/zygote55/sketchyup/pull/63).
[Session contract](../decisions/0026-headless-session.md).

The process fixture launches the actual CLI with DISPLAY, WAYLAND_DISPLAY and
QT_QPA_PLATFORM removed. It creates an explicitly named native baseline, opens a
private draft, creates a 2 × 3 m face, seals and commits, then exits without saving
the later revision. Reopening that older input fails with a structured error.
Explicit transaction recovery returns the original receipt without replay, saves
a separately selected copy and measures its world area as exactly 6 m². The
original model stays at revision zero; the recovered copy opens at revision one.

In-process fixtures verify the shared command/inspection dispatch, original
identity persistence, confirmed save digest and clean state, pending cancellation
on graceful close, idempotent close, unknown capabilities, document/revision
preconditions and unavailable save without an explicit output. An output lock
rejects a competing session. Externally changed destination bytes reject save and
remain untouched. A conflicting native backup injects a persistence failure;
save uncertainty is explicit and dependent operations remain blocked.

Wire tests cover malformed JSON, oversized lines, excessive nesting, invalid
correlation IDs/envelopes, stale revisions and continuation to later inspection
following request failure. Invalid mode combinations fail before creating a
model. Request errors produce structured failures and a nonzero final process
exit. Recovery preserves the original input; requests cannot supply arbitrary
paths. Source, output and private outcome storage remain explicitly scoped.

All 53 development CTest suites pass. Six targeted ASan/UBSan suites pass:
headless session, transaction dispatcher, retained and bounded inspection, legacy
CLI history and CLI recovery. The full transaction/core sanitizer matrices passed
in R041; no core geometry changed here. Builds report no compiler warnings.

Installed `--session-capabilities` exactly matches the installed JSON contract.
The source archive includes the session schema, contract and process fixture.
Disposable package acceptance checks installed schema equality and artifact
removal. The versioned recipe runner remains R042.b; no provider or MCP integration
is claimed by this slice.
