# R045 local provider verification

Date: 2026-10-05 UTC. Local acceptance complete; CI pending. Live task failures are reported as failures.
[Protocol and bounds](../decisions/0036-local-provider.md).

## Reproducible profile

The isolated trial uses the official Ollama 0.35.1 image pinned to
`sha256:292ee7945dfc3d5840a181f3ab86fedb1e66703e02c8af98b50f4da56b7e278c`.
Container and retained model volume: `sketchyup-ollama-r045`. Only
`127.0.0.1:11435` is published to container port 11434. Eight CPU quota units,
10 GiB memory, no additional swap, no GPU devices. Environment:
`OLLAMA_NO_CLOUD=1`, `OLLAMA_MAX_LOADED_MODELS=1`, `OLLAMA_NUM_PARALLEL=1`,
`OLLAMA_CONTEXT_LENGTH=32768`, `OLLAMA_KEEP_ALIVE=5m`. Logs confirm cloud disabled
and CPU inference. The editor does not provision or start this service.

Host: Linux x86-64, Intel Core Ultra 7, 30 GiB RAM. Other compilation work ran
concurrently during some measurements, so timings describe this trial and are
not an isolated throughput benchmark. The runtime reached about 7.1 GiB memory
usage with this profile. No remote provider credential or user model was used.

Selected model: `qwen3:4b-instruct`, Qwen3 4B Instruct 2507, Q4_K_M, Apache-2.0;
manifest digest
`0edcdef34593eac1aa2be9c7d06c432dcf81945adca5eca2f27662c18f168ba0`,
2,497,293,803 bytes. `/api/show` reports native context 262,144; the configured
window remains 32,768. A small standalone tool smoke returned a valid call in
15.286 s (10.111 s load, 145 prompt tokens, 16 output tokens). This is protocol
compatibility evidence only.

An earlier `qwen3:4b` digest
`359d7dd4bcdab3d86b87d73ac27966f4dbb9f5efdfcc75d34a8764a09474fae7`
was unsuitable: its metadata allowed thinking only, and the smoke produced prose
until its 128-token limit despite `think:false` (22.156 s). It is not the selected
profile and is not counted as a successful tool trial.

A live overflow request supplied 40,009 tokens to a 4,096-token window with
`truncate:false` and `shift:false`. Ollama rejected it with HTTP 400 in 7.264 s,
reporting the actual prompt and context lengths. No generated reply or tool call
was accepted. This replaces the initial byte-based admission experiment, which
incorrectly rejected the real catalog even when its tokenized prompt fitted.

## Corpus

The opt-in `local_provider_trial` executable runs synthetic fixtures through the
real `AssistantTask`, transport and `AutomationSession`. It records activity,
reported usage, provider exchanges and actual execution receipts. Only its host
harness applies a sealed room/resize preview. Saved geometry is then measured
independently; model text is never the geometry oracle. All reports also exercise
manual edit/save/reopen after the provider task. It never sends user files.

```
build/dev/local_provider_trial http://127.0.0.1:11435 qwen3:4b-instruct measure build/evidence/r045/measure.json
build/dev/local_provider_trial http://127.0.0.1:11435 qwen3:4b-instruct room build/evidence/r045/room.json
build/dev/local_provider_trial http://127.0.0.1:11435 qwen3:4b-instruct resize build/evidence/r045/resize.json
build/dev/local_provider_trial http://127.0.0.1:11435 qwen3:4b-instruct unsupported build/evidence/r045/unsupported.json
```

Each task uses 768 output tokens per turn, at most 12 attempts and 300 seconds
wall time. A completed measurement process exits zero even when model quality
fails: inspect `geometryVerified`, `inspectionVerified`, execution receipts and
unverified model text in the report. A report is not a quality-pass assertion.

Initial measurement trial: **failed**, 191.271 s, three provider attempts including
one timeout, one invalid tool call, 7,204 reported tokens. No inspection reached
the backend and no edit was applied. The model then acknowledged it could not
continue. This run predates exchange capture in the trial harness; the invalid
call is identified by task accounting and the model's returned explanation,
not a retained raw-call assertion.

The first room trial also failed (217.927 s, three attempts, two rejected calls,
14,286 reported tokens). Its recorded exchanges identified an adapter issue:
`transaction.begin` had `operation:{}` and `document.describe` had `query:{}`.
Ollama's pinned `ToolProperty` decoder omits JSON Schema `const`, so the native
prompt lost those discriminator values. No call reached the backend and no
geometry was applied. The final adapter adds redundant type/enum and alternative
hints and bound descriptions, retaining the original host schemas. The corrected corpus results follow; these earlier runs remain failures, not
quality results for the corrected adapter.

## Corrected live corpus (retained after the host reboot)

The complete corrected run produced **no successful modeling or measurement task**.
The local CPU profile is experimental and unsuitable for claiming the M5 assistant
quality gate. Compilation ran concurrently, so these are observed latency limits,
not an isolated inference benchmark. The discriminator correction did allow one
valid `document.describe` execution; it did not solve task latency or quality.

| Task | Elapsed | Attempts | Accepted tool calls | Result |
| --- | ---: | ---: | ---: | --- |
| Measure face area | 300.018 s | 3 | 1 | Deadline; document description only, no verified area |
| Build room | 300.022 s | 3 | 0 | Deadline; no proposal |
| Resize one window | 300.054 s | 3 | 0 | Deadline; no proposal |
| Unsupported request | 300.031 s | 3 | 0 | Deadline; no verified explanation |
| Endpoint stopped | 0.800 s | 3 | 0 | Bounded connection failure |

All five left the document revision unchanged, reported `applied:false`, and
passed manual edit/save/reopen afterward. No model output was treated as a
measurement or geometry success. The measurement task reported 6,381 tokens;
other tasks had no completed provider response and therefore no reported usage.
Missing usage is not proof of zero runtime computation. Requests interrupted by
the timeout have transport status zero, not a successful HTTP response.

[Compact machine-readable evidence](R045-corpus-summary.json) retains results,
actual receipts, exchange timing and SHA-256 hashes of the raw reports. Raw
synthetic exchanges remain in gitignored `build/evidence/r045/`; they repeat the
full schemas and runtime metadata. Earlier `/tmp` artifacts were lost in the
host reboot; their recorded observations above are distinguished from these
retained reruns. The owned service is stopped after measurement; its model volume
is retained. No GPU performance or live remote-provider success is claimed.

## Automated verification

All 61 development suites passed in 39.22 s; the real-Blender opt-in suite was
skipped, with its actual native rendering evidence recorded under R049/R050.
Both provider suites passed ASan/UBSan with leak detection in 8.42 s after native
schema hints were added. The local fixture exercises a real staged face,
sealed preview and host Apply, full replay, version/model checks before document
transfer, no proxy/credential/redirect, invalid usage/tool responses, cancellation
and bounded retry. It asserts the discriminator/type/enum hints on actual wire
requests. Existing OpenAI protocol/credential tests pass after transport sharing.
Source-package and install checks also passed. The corrected live corpus above is independent of these fixture passes.
