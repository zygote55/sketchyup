# R086.d — Provider release corpus, version 3: native ChatGPT-plan limits

2026-10-16. This freezes corpus **version 3** for the SketchyUp 1.2.0 release
(F02) before any live run. **No live provider request, provider network call or
credential was used for this layer.** [R086.a](R086a-frozen-provider-corpus.md)
and [M10 acceptance](../M10_ACCEPTANCE.md) require a new corpus version and a
complete new evaluation after a provider product or prompt change. PR #338
(merge `997386d8`, "Fix native Assistant budget exhaustion and timeouts") is
such a change, so the [version 2](R086by-corpus-v2.md) results, including the
1.1.0 release-candidate run [R086.cd](R086cd-rc110b-remote-evaluation.md), do
not carry over to 1.2.0.

## Why version 2 cannot be reused

PR #338 changed the supported profile in four ways
([ADR 0041](../decisions/0041-chatgpt-plan.md)):

- **Budget.** Native ChatGPT-plan tasks share one limit profile
  (`chatGptAssistantLimits()`): 16 turns and **524,288** reported tokens per
  task, up from 262,144. The native panel allows 15 minutes.
- **No per-reply output cap.** ChatGPT-plan replies are no longer rejected at
  8,192 output tokens. All reported usage still counts toward the task budget,
  and the response-byte and tool-call limits still apply. API-key and local
  connections keep their per-reply caps.
- **Receipts.** Face-creation receipts now return surviving face references
  (`createdIds.createdFaces`).
- **Prompt.** The assistant instructions now guide batched modeling and
  inspection, the `geometry.array_selection` command, and reserving turns for
  final measurements. The per-turn budget message now includes the remaining
  wall-clock seconds.

Since #338, `provider_trial --chatgpt` reports `outputTokensPerTurn: null`,
`outputTokenLimitEnforced: false` and `taskReportedTokens: 524288`. The v2
`chatgpt-astra` profile freezes 8,192 and 262,144, so the scorer would reject
every new ChatGPT-plan report against v2 as configuration drift. The advanced
task is also the one most affected by the old budget. At R086.cd it used
231,072, 255,325 and 247,858 of the 262,144 tokens.

## What changes, and what does not

| | Version 2 | Version 3 |
| --- | --- | --- |
| Manifest | [`tests/provider-release-corpus-v2.json`](../../tests/provider-release-corpus-v2.json), kept byte-for-byte | [`tests/provider-release-corpus-v3.json`](../../tests/provider-release-corpus-v3.json) |
| Tasks | Eight tasks | **Identical**: the same eight tasks, prompts, `inputSha256`, `promptSha256`, classes and minimum successes |
| Repetitions, hard zero, postconditions | 3; seven blockers; undo and recovery at 100% | **Unchanged** |
| `chatgpt-astra` profile | 16 turns, 8,192 output tokens per turn, 262,144 tokens per task | 16 turns, **no per-reply output cap** (`outputTokensPerTurn: null`, `outputTokenLimitEnforced: false`), **524,288** tokens per task |
| `ollama-qwen4b` profile | 12 turns, 768 per turn, 131,072 per task | Unchanged values; adds `outputTokenLimitEnforced: true`, which `provider_trial` already reports |
| `deadlineSeconds` | 300 | 300 (see [the deadline](#the-deadline)) |
| Policy text | `postconditionPolicy`, `costPolicy` | Unchanged, plus `budgetPolicy` describing the profile |

**No task changed.** R086.a allows a task change only when its input or prompt
must change. #338 changed the assistant's system instructions, tool receipts
and limits, not the eight task prompts or fixture inputs. The v3 values were
regenerated with `provider_trial --describe gpt-6-astra TASK` at this branch
(writer `SketchyUp/1.1.0`). All eight inputs and prompts are byte-identical to
v2, and the offline test now requires all three manifests to agree on every
task. **Thresholds are unchanged** (measure, unsupported, metadata and
unavailable 3/3; room, resize, advanced and site 2/3). R086.a ties thresholds
to the task contract, and nothing in that contract changed.

| Item | Value |
| --- | --- |
| Manifest | `tests/provider-release-corpus-v3.json` |
| Manifest SHA-256 | `8a44497d6a809cfc627126d11a00a7e2b90ec9415e4970dca22bd4d507269293` (writer `SketchyUp/1.2.0`; `96dcdc2fc78a7347dc607ce5864ad660963848f4ec26976f68ad60d06f42cc4c` when frozen with writer `SketchyUp/1.1.0`) |
| Version 2 manifest (kept) | `d71c49c6e41d3959872a5d4fd3dbea2f817be8ad841bd923616b74effff7c66d` (writer `SketchyUp/1.2.0`; `af1a36dc2f499be02dbbbd7ecfc90d3f864036825c714e1f2b9d762e1f4c1604` with writer `SketchyUp/1.1.0`) |
| Version 1 manifest (kept) | `4e94b83fa1f721fb124315c268835a7370b512e8759670b80938d7ddec7a103f` (writer `SketchyUp/1.2.0`; `bd1b1ab00a2dda0a77b588c58a11431554f9ad9fbfaead28788222f39b1f3849` with writer `SketchyUp/1.1.0`) |

The task hashes, for the `SketchyUp/1.2.0` writer (see
[Writer-string regeneration for 1.2.0](#writer-string-regeneration-for-120)), are:

| Task | `inputSha256` | `promptSha256` |
| --- | --- | --- |
| measure | `7ca06e522415d0cded539e7be4542794e1cef1558fe268b79c2db5876e18fc42` | `5968b594b95cff306f02c8b3560a15150478d1ce65c23882c0b63145ede3702a` |
| room | `05a5b95ce694c5afff7b68eee3c7fb0654cbb622fbafc742adcf67a04b0bbe76` | `8c2257a974d7b15f0dd09b7123957a6ca0d4fff62df6245943fdb8ca329ea554` |
| resize | `2c2e6e673ae406e8e6fdc9b034546ec0967aa255e628237514898d30ded6b3d2` | `e82b0dc074c582bdaa1b06ff8c47bdd2114e939071b4eb9023685f704f7e140b` |
| advanced | `703bf6575ed644456421e28d0c1365f661bb4a15e3473dc3550c95728d104634` | `331f88663a56807745f8ccf394e873a625b80096b8eb68f8133e8a3765ddb5f2` |
| site | `00207bfd1d0d40415f66fef6ef56d5e521228a30acce78d25ea1b0b4f4e18f93` | `f3492582fc9be4fe8941be29384eb1cfeb417d63da17f4f8295ba64f6f283a1e` |
| unsupported | `1da716c551f35e40c58cc966a1ac42171abf1dde08606d20029f0c1e9a29b105` | `bad41af2733c95b5b741f9f8ab8d89d29f354ec68f7e741b329588b4a7b17260` |
| metadata | `744cdbe7f80c338dcca82ff623260d7f360d4ffac4674dc5d330a7c41b30250d` | `f3aa787672fb325721e4f6ba09934ec0a361fbf9d304af011bc1fa7504ead0b1` |
| unavailable | `18698030d171051446aa81ad3c6769e6f336b4c9f3a6c6a587c588b201a9f9a3` | `5968b594b95cff306f02c8b3560a15150478d1ce65c23882c0b63145ede3702a` |

**Writer string.** The `inputSha256` values hash fixture containers, which
record the writer version. When the version becomes 1.2.0, all eight values
change, as in R089.b and R092.c. M10 makes that regeneration part of the
release candidate. The manifest SHA-256 above then changes too. The live run
must record the manifest hash it actually used, at the RC, before the first
request.

## Writer-string regeneration for 1.2.0

The 1.2.0 release preparation set the version to 1.2.0, so fixture containers record
the writer `SketchyUp/1.2.0`. All eight `inputSha256` values were regenerated with
`provider_trial --describe gpt-6-astra TASK` (private XDG directories, offline,
`corpusVersion: 3`, `networkRequested: false`) in all three manifests, which the
offline test requires to agree. **Only the `inputSha256` values changed.** Every
prompt, `promptSha256`, threshold, profile value and policy text is byte-identical, so
this is not a new corpus version (M10: a provider or prompt change needs one; an
input-hash regeneration does not). The manifest SHA-256 values changed only because of
these eight lines per file.

| Task | `inputSha256`, writer 1.1.0 | `inputSha256`, writer 1.2.0 |
| --- | --- | --- |
| measure | `02064ae591cde4d012ea56cacd8e16fba2139d6cc706823059e93d34246de0f6` | `7ca06e522415d0cded539e7be4542794e1cef1558fe268b79c2db5876e18fc42` |
| room | `19c29357eaa46ccabbb419cff8ab41acec996ef25a1d8d5f0a9f8ac3f0946f40` | `05a5b95ce694c5afff7b68eee3c7fb0654cbb622fbafc742adcf67a04b0bbe76` |
| resize | `f2dcc26d4f2e6981e3b47200a3c7b9f4604c3934e978d23eed92375dfb084964` | `2c2e6e673ae406e8e6fdc9b034546ec0967aa255e628237514898d30ded6b3d2` |
| advanced | `c13493c34436b18dacba6a5c4c15d0818a4368a59137cb26a375193cc0acb2c5` | `703bf6575ed644456421e28d0c1365f661bb4a15e3473dc3550c95728d104634` |
| site | `358219d1c5e1f56a9a73e55a28de58ec9860c78474185b433c9b340b4a0253ff` | `00207bfd1d0d40415f66fef6ef56d5e521228a30acce78d25ea1b0b4f4e18f93` |
| unsupported | `7c413f047857724fd3c6a3f570ea2ea1089ca356b202636c809693fc0aa4c57c` | `1da716c551f35e40c58cc966a1ac42171abf1dde08606d20029f0c1e9a29b105` |
| metadata | `613b19ec971cbf04da08827332e9aa084a2a752e93df89795875de9a61916755` | `744cdbe7f80c338dcca82ff623260d7f360d4ffac4674dc5d330a7c41b30250d` |
| unavailable | `948847051f06cd921470e86f038adda72c837298c65d28018007908c442616c7` | `18698030d171051446aa81ad3c6769e6f336b4c9f3a6c6a587c588b201a9f9a3` |

| Manifest | SHA-256, writer 1.1.0 | SHA-256, writer 1.2.0 |
| --- | --- | --- |
| v3 | `96dcdc2fc78a7347dc607ce5864ad660963848f4ec26976f68ad60d06f42cc4c` | `8a44497d6a809cfc627126d11a00a7e2b90ec9415e4970dca22bd4d507269293` |
| v2 | `af1a36dc2f499be02dbbbd7ecfc90d3f864036825c714e1f2b9d762e1f4c1604` | `d71c49c6e41d3959872a5d4fd3dbea2f817be8ad841bd923616b74effff7c66d` |
| v1 | `bd1b1ab00a2dda0a77b588c58a11431554f9ad9fbfaead28788222f39b1f3849` | `4e94b83fa1f721fb124315c268835a7370b512e8759670b80938d7ddec7a103f` |

The 1.1.0 records keep citing `af1a36dc…`, the v2 manifest as it was at the 1.1.0
release candidate. The 1.2.0 live run records the v3 manifest hash it uses at the RC.

## The profile as frozen

| Field | `chatgpt-astra` (candidate) |
| --- | --- |
| Model / connection | `gpt-6-astra` / ChatGPT plan sign-in (`--chatgpt`) |
| Turns per task | 16 |
| Reported tokens per task | 524,288 (includes cached input and hidden reasoning output) |
| Output tokens per reply | Not capped (`outputTokensPerTurn: null`, `outputTokenLimitEnforced: false`) |
| Task deadline | 300 s |
| Unchanged host bounds | 64 tool calls per task, 8 per reply, 64 KiB per response |

### The deadline

The native panel now allows 15 minutes, but the corpus deadline stays at
**300 s**. `provider_trial` runs the corpus through the public headless
session, and #338 kept that session's 300-second staging ceiling and the
published API schemas. #338 sets `options.limits.seconds = 300` for the
corpus explicitly, and ADR 0041 records it: "the frozen headless corpus
retains its public five-minute draft ceiling and records that override
explicitly; native workflow trials use the native limits". Every
ChatGPT-plan report therefore carries `taskSeconds: 300`, and the scorer
requires it to equal `deadlineSeconds`. Freezing 900 would reject every run.
The 15-minute native lifetime is covered by the injected-clock tests in
`native_assistant_session_tests` and by the opt-in `assistant_workflow_trial`,
not by this corpus. **The owner decides whether that is enough for 1.2.0.**
The alternative is to run the corpus through the native transaction options,
which changes the harness and needs a new corpus version.

## Code and scorer changes

- **`provider_trial`** (`tests/local_provider_trial.cpp`) writes
  `corpusVersion: 3`, in both reports and `--describe` output. Its limits
  come from #338 and are unchanged here. A v3 report cannot be scored against
  v1 or v2, and a v2 report cannot be scored against v3.
- **`scripts/score-provider-release.py`** accepts versions 1, 2 and 3. It
  applies the v2 manifest and postcondition rules to every version 2 or
  later. Its configuration-drift check also compares
  `outputTokenLimitEnforced` when the profile declares it. The v1 and v2
  profiles do not declare it, so their scoring and output are unchanged.
  Thresholds, blockers and `releaseAccepted: false` are unchanged.
- **`provider_release_corpus`** regenerates all eight inputs twice and checks
  them against v1, v2 and v3. It drives the 24 offline replay runs and the
  injected-fault run with v3. The replay's test-only profile is derived from
  `ollama-qwen4b`, so it also checks the new `outputTokenLimitEnforced: true`
  field.
- **`provider_release_score`** runs every v1 and v2 test against v3 as well.
  New v3 tests check:
  - the frozen profile values;
  - a report that still uses the 8,192 cap, the 262,144-token budget or an
    enforced output cap is rejected as configuration drift;
  - a missing `outputTokenLimitEnforced` fails closed;
  - a v2 report against the v3 manifest is rejected.

## Live evaluation still required

Version 3 has **no live result**. 1.2.0 requires a complete, owner-approved
ChatGPT-plan evaluation of all 24 runs at the 1.2.0 release candidate, after
the writer-string regeneration. Follow the R086.by live-run procedure with
`tests/provider-release-corpus-v3.json` in place of the v2 manifest. Record
the v3 manifest hash in the freeze file and the review bundle, and score with:

```
python3 scripts/score-provider-release.py tests/provider-release-corpus-v3.json "$E/review.json"
```

The run must not be retried per repetition. Prior v2 results remain recorded
as 1.1.0 evidence and are not 1.2.0 evidence. `ollama-qwen4b` stays an
optional experimental comparison.

## Verification results

All of these ran on 2026-10-16, offline, with `-j6`:

- `provider_trial --describe gpt-6-astra TASK` for all eight tasks, with
  private XDG directories: every `inputSha256` (including the retained input
  file's own hash) and `promptSha256` equals v3 (and v2), with
  `corpusVersion: 3` and `networkRequested: false`.
- `provider_release_score`: 43 tests passed (2 version-specific skips).
- `provider_release_corpus`: passed in 69 s.
- `cmake --build --preset dev -j6` built cleanly. The full
  `QT_QPA_PLATFORM=offscreen ctest --preset dev -j6` passed 204/204 in 411 s.
  The 12 `*_real` external-tool tests skipped as usual.
