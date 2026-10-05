# R043.a scoped headless MCP

Date: 2026-10-05 UTC. Local verification and both CI runs passed; merged. Requires
[PR #65](https://github.com/zygote55/sketchyup/pull/65).
[Transport contract](../decisions/0028-local-mcp.md).

The actual CLI process runs the MCP 2026-07-28 stdio binding without display
variables. The client discovers the server and shared tools, identifies the bound
document, subscribes to its state, creates/seals/commits a private task, retries its
original outcome, saves to the explicitly selected destination and reconnects in a
fresh process. The new process resolves the original committed receipt. Exiting
with another sealed pending task durably aborts it while preserving the prior
committed model.

Tests cover per-request metadata, unsupported versions, rejection of the legacy
handshake, wrong document identity, mismatched outer/inner tool names, missing shell
and arbitrary filesystem capabilities, malformed IDs, invalid JSON, batches,
oversized messages and excessive nesting. Shared model errors remain actionable
tool errors; protocol errors remain JSON-RPC errors. The headless selection query
truthfully returns `UNAVAILABLE_CONTEXT`; native selection acceptance remains the
following R043 slice.

Subscription checks cover first-message acknowledgment, omission of unsupported
filters, one update after commit, no duplicate event on a committed retry,
cancellation without a reply, late cancellation without undoing a commit, eight
active subscriptions, rejection of a ninth and duplicate in-flight IDs, and all
uncancelled subscriptions ending gracefully on EOF. Owner-thread checks and the
tool token bucket are exercised. Stdout contains only protocol objects.

All 55 development suites pass. The MCP, recipe, headless session and CLI inspection
suites pass under ASan/UBSan. The first MCP sanitizer attempt encountered the old
generated discovery artifact while the parallel development build was still
regenerating it; the targeted rerun with the final artifact passes. This was a
schema-equality fixture failure, not a sanitizer memory report. Builds report no
compiler warnings.

An independent `jsonschema` 4.26.0 check validates all 199 synthetic messages against
their concrete official response/notification definitions. The maximum message is
73,457 bytes. [Machine-readable evidence](R043a-protocol-validation.json) records
the immutable upstream commit, schema SHA-256 and ten exercised message types.
CI downloads that pinned schema, checks its digest and repeats the independent
validation on the native test transcript.

Installed `--mcp-capabilities` equals the installed JSON artifact. The source
archive includes the protocol implementation, schema, contract and process fixture.
Disposable package acceptance verifies installed schema equality and removal.
No native window mutation or legacy MCP compatibility is claimed. Parent R043
remains open for the actual native selection binding and CI/merge acceptance.

PR [#66](https://github.com/zygote55/sketchyup/pull/66) merged at
2026-10-05 03:35:46 UTC as `c3fa0fa4450a4ecf78e15bb7b447b49f25975a21`.
Both CI runs, `37256366073` and `37256412105`, passed before merge.
