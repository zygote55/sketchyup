# R086.e — Provider release corpus, version 4: native assistant limits

2026-10-17. This freezes corpus **version 4** for the SketchyUp 1.2.0 release
(F02) before any live run. **No live provider request, provider network call or
credential was used for this layer.** It replaces [version 3](R086d-corpus-v3.md),
which has no live result, for 1.2.0. Version 2 stays the 1.1.x corpus.

## The owner decision

[R086.d](R086d-corpus-v3.md#the-deadline) left one item open. Since PR #338 the
Assistant panel allows 15 minutes per ChatGPT-plan task, but version 3 ran the
corpus through the public headless session with its 300-second draft ceiling. R086.d
named the alternative: "run the corpus through the native transaction options, which
changes the harness and needs a new corpus version".

On **2026-10-17** the owner decided:

- **Corpus deadline.** For 1.2.0 the corpus deadline is raised to the app's native
  15-minute limit.
- **Method.** "Corpus v4, native limits". The same eight tasks and prompts run
  through the native assistant limits, with harness and scorer changes and a new
  corpus version.
- **Public API.** It does not change. The headless session keeps its 300-second
  public ceiling and its published API schemas ([ADR 0041](../decisions/0041-chatgpt-plan.md)).

## What changes, and what does not

| | Version 3 | Version 4 |
| --- | --- | --- |
| Manifest | [`tests/provider-release-corpus-v3.json`](../../tests/provider-release-corpus-v3.json), kept byte-for-byte | [`tests/provider-release-corpus-v4.json`](../../tests/provider-release-corpus-v4.json) |
| Tasks | Eight tasks | **Identical**: the same eight tasks, prompts, `inputSha256`, `promptSha256`, classes and minimum successes |
| Repetitions, hard zero, postconditions, thresholds | 3; seven blockers; undo and recovery at 100% | **Unchanged** |
| Session the tasks run in | Public headless `AutomationSession` | **`NativeAssistantSession`** on the opened document, with `nativeAssistantTransactionOptions()`, as the Assistant panel creates it |
| Draft ceiling | 300 s (public) | **900 s** (`maximumTrustedStagingTtlSeconds`, native only) |
| `deadlineSeconds` / report `taskSeconds` | 300 | **900** |
| `chatgpt-astra` limits | 16 turns, 524,288 tokens, no per-reply cap, 300 s override | `chatGptAssistantLimits()` **unchanged**: 16 turns, 524,288 tokens, no per-reply cap, 900 s |
| `ollama-qwen4b` profile | 12 turns, 768 per turn, 131,072 per task | Same token limits; 900 s like every v4 task |
| Assistant instructions | Not hashed | **Frozen**: `instructionsSha256` per profile; every recorded provider request must carry them |
| Tool surface | Not hashed | **Frozen** per task: `allowedCommands`, `clarificationAvailable: false` and `toolsSha256` |
| Policy text | `postconditionPolicy`, `costPolicy`, `budgetPolicy` | Same `postconditionPolicy` and `costPolicy`; new `budgetPolicy`; new `assistantPolicy` |

**No task changed.** R086.a ties a task to its input, prompt, authorized commands
and thresholds. The v4 values were regenerated with
`provider_trial --describe gpt-6-astra TASK` at this branch (writer
`SketchyUp/1.2.0`). All eight inputs and prompts are byte-identical to v3, and the
offline test requires all four manifests to agree on every task.

### Why a new version

The deadline is part of the frozen profile, and the scorer requires every report's
`taskSeconds` to equal `deadlineSeconds`. Raising it to 900 s changes what a run is
allowed to do, so v3 reports cannot be scored against v4 and v4 reports cannot be
scored against v3. The harness also changed: the tasks no longer pass through the
headless session, so the report now records which session ran them.

### The instruction gap

Versions 1 to 3 froze each task's input and prompt but not what the assistant was
told. PR #338 changed the instructions without changing any frozen value; only the
profile limits forced v3. Version 4 closes that gap:

- **`instructionsSha256`** (per profile) is the SHA-256 of the UTF-8 provider
  instructions (the system prompt `AssistantTask` builds) up to the per-turn
  marker `"\nHost task budget: "`. What follows that marker is the per-turn budget
  (remaining turns, tokens, seconds) and the document binding, which change on
  every turn. The remote (`chatgpt-astra`) and local (`ollama-qwen4b`) values
  differ because local connections get extra draft guidance.
- **`toolsSha256`** (per task, under `assistant.surfaces`) is the SHA-256 of the
  first request's compact `tools` array. That is the same measure as `toolsSha256`
  in [`assistant-tool-surface-baseline.json`](../../tests/assistant-tool-surface-baseline.json).
  For the five tasks that authorize commands the values differ from that baseline.
  The only differing input is the task lifetime: the native catalog advertises the
  900-second draft ceiling in `transaction.begin` (`ttlSeconds` maximum 900, not 300).
  The baseline (default limits) is unchanged, and `assistant_tool_surface` still
  passes against it.
- **`allowedCommands`** and **`clarificationAvailable`** freeze the inputs to that
  tool list.

`provider_trial` computes both hashes with the same `AssistantTask` code the run uses
(a probe task that never dispatches) and records them in each report and in
`--describe`. The scorer additionally recomputes the instruction hash from every
recorded provider request (`instructions` on the ChatGPT Responses wire,
the `system` message on the Ollama wire). A live run therefore cannot pass with
instructions other than the frozen ones.

## Frozen values

| Item | Value |
| --- | --- |
| Manifest | `tests/provider-release-corpus-v4.json` |
| Manifest SHA-256 | `d35a6cc5bfc5ddfb6f8a9da0a1d7cab850747a7f9011642e8e1bbe99a6648f4d` (writer `SketchyUp/1.2.0`) |
| `chatgpt-astra` `instructionsSha256` | `313b9f911735f28e21bb1c9310d727416568d0c460971c43315faf4dd1f376be` |
| `ollama-qwen4b` `instructionsSha256` | `eca8889bf17247c4a5c1cb634b3934364a03fd14723ea0c5b5528c28f7293c61` |
| Version 3 manifest (kept) | `8a44497d6a809cfc627126d11a00a7e2b90ec9415e4970dca22bd4d507269293` |
| Version 2 manifest (kept) | `d71c49c6e41d3959872a5d4fd3dbea2f817be8ad841bd923616b74effff7c66d` |
| Version 1 manifest (kept) | `4e94b83fa1f721fb124315c268835a7370b512e8759670b80938d7ddec7a103f` |

| Task | `allowedCommands` | `toolsSha256` |
| --- | --- | --- |
| measure | none (inspection only) | `92bfcc7604827de6fad1ec6403a0dfaa4fdadb56b214e2ffad3bb432e5add25f` |
| room | `assembly.room` | `d981d283ddd1531de00674ea79b38ef3ee6c0dc40a63f9c449a3f52681477d15` |
| resize | `assembly.window.resize` | `f60877da17c1074d1acb67a7dc05e0f85c3a673885ef283e3f9a98b1c19d35bb` |
| advanced | `assembly.roof`, `assembly.stairs`, `assembly.table`, `assembly.cabinet`, `assert.measurement` | `5b508555942d8e436ff3db936887e9ca6b9e76d3c408bd88966d5122af9fbf15` |
| site | `assembly.site_place`, `assert.measurement` | `909e03849cc351d76163e4a74511f1aecfd7d4602558ec278b9fcea6615132c9` |
| unsupported | `assembly.room` | `d981d283ddd1531de00674ea79b38ef3ee6c0dc40a63f9c449a3f52681477d15` |
| metadata | none (inspection only) | `92bfcc7604827de6fad1ec6403a0dfaa4fdadb56b214e2ffad3bb432e5add25f` |
| unavailable | none (inspection only) | `92bfcc7604827de6fad1ec6403a0dfaa4fdadb56b214e2ffad3bb432e5add25f` |

The three inspection-only tasks share the tool list of the
`inspection-only` baseline surface, so their hash equals the baseline's
(`92bfcc76…`). Room and Unsupported share one authorization and so one hash.

The task `inputSha256` and `promptSha256` values are the v3 values for writer
`SketchyUp/1.2.0`, listed in [R086.d](R086d-corpus-v3.md#writer-string-regeneration-for-120).

**What the manifest hash covers.** The manifest SHA-256 binds every value above: the
tasks (input and prompt hashes, thresholds), the profiles (limits and instruction
hashes), the native session declaration (`session: native`,
`maximumDraftTtlSeconds: 900`, `clarificationAvailable: false`), each task's command
authorization and tool-list hash, and the policy text. It does not hash the
`provider_trial` binary, the scorer or the source commit; the live run records those
in its freeze file, as before.

## The profile as frozen

| Field | `chatgpt-astra` (candidate) |
| --- | --- |
| Model / connection | `gpt-6-astra` / ChatGPT plan sign-in (`--chatgpt`) |
| Session | `NativeAssistantSession`, `nativeAssistantTransactionOptions()` |
| Task lifetime | **900 s** (`taskSeconds: 900`); draft ceiling 900 s, never past the task deadline |
| Turns per task | 16 |
| Reported tokens per task | 524,288 (includes cached input and hidden reasoning output) |
| Output tokens per reply | Not capped (`outputTokensPerTurn: null`, `outputTokenLimitEnforced: false`) |
| Unchanged host bounds | 64 tool calls per task, 8 per reply, 64 KiB per response |
| Transport | `OpenAiProvider` in ChatGPT-plan mode (streamed Responses, `sketchyup` tool namespace), as in the panel |

## Where the corpus path differs from the desktop

The limits, transaction options, instruction text, tool-catalog code and transport are
the desktop's. These differences remain, each for a stated reason:

- **Command authorization.** The panel authorizes `nativeAssistantCommands()`. Each
  corpus task keeps its v1 to v3 authorization (for example `assembly.room` only for
  Room, nothing for Measure). R086.a makes the authorized commands part of the frozen
  task, and the Unsupported review depends on it. Changing it would be a new task, not
  the same task under new limits. The tool lists are therefore subsets of the desktop
  list, built by the same code, and are hashed.
- **No clarification.** The panel advertises `assistant.ask_user`. The corpus has no
  user to answer, so a question would only wait for the deadline. Versions 1 to 3 did
  not advertise it either.
- **No attached context.** The panel attaches the selection and the editing context by
  default. The corpus prompts name their targets by ID, and versions 1 to 3 attached
  nothing, so the first request matches theirs apart from the instructions and tools.
- **Host stand-ins.** The harness binds a `Selection` synced to the document instead
  of a viewport selection, has no modeling gesture in progress, records exchanges
  through a `QNetworkAccessManager` subclass, applies a preview itself (acting as the
  host reviewer for its disposable fixture, as before) and saves with
  `saveDocument`. The panel's outcome journal lives under the app data directory; the
  corpus uses the run's private evidence directory.
- **Recovery postcondition.** The unclean-exit check still reopens the input with
  the headless `--recover-latest` path, now reading the native session's outcome
  journal. This is a corpus engine check, not a desktop step.
- **Other connections.** API-key and local connections use 300 s in the panel. In v4
  every task, including the offline control and the experimental `ollama-qwen4b`
  comparison, gets the corpus deadline of 900 s, because the scorer requires one
  deadline for every run.

## Code and scorer changes

- **`provider_trial`** (`tests/local_provider_trial.cpp`) writes `corpusVersion: 4`.
  It opens the fixture as a document and runs the task through
  `NativeAssistantSession` with `nativeAssistantTransactionOptions()`. ChatGPT-plan
  tasks use `chatGptAssistantLimits()` with no override. Reports and `--describe`
  output add `assistantSession`, `maximumDraftTtlSeconds`, `allowedCommands`,
  `clarificationAvailable`, `toolsSha256` and `instructionsSha256`; `--describe` also
  gives `taskSeconds` and `localInstructionsSha256`. The harness's own wall-clock
  guard follows the task lifetime (910 s). It can no longer write a v3 report.
- **`scripts/score-provider-release.py`** accepts versions 1 to 4. For version 4 it
  also requires the native session declaration in the manifest, and in every report
  the frozen session, draft ceiling, command authorization, clarification setting
  and `toolsSha256`, the profile's `instructionsSha256`, and the frozen instructions
  in every recorded provider request. Versions 1 to 3 declare none of these fields,
  so their scoring and output are unchanged.
- **`provider_release_corpus`** regenerates all eight inputs twice and checks them
  against v1 to v4, together with the whole v4 assistant surface. Its 24 offline
  replay runs and the injected-fault run now go through the native session. Each
  replayed edit begins a draft with a lifetime above 300 s, which the headless
  session would reject. The set scores with the instruction check applied to the
  recorded Ollama-protocol requests.
- **`provider_release_score`** runs every v1 to v3 test against v4 as well. New v4
  tests check the frozen values and that the tasks equal v3, and they reject a report
  with `taskSeconds: 300`, a 300 s draft ceiling, a headless session, a changed
  instruction hash, a recorded request with other instructions, a changed tool hash,
  authorization or clarification setting, missing v4 fields, a v3 report, and a
  manifest without the native surface.

## Live-run procedure (after this manifest merges)

Run from a clean checkout of the release-candidate commit. Use fresh private evidence
paths, keep them out of git, and do not retry a failed repetition. The owner must
first sign in through Assistant Preferences → ChatGPT, because `provider_trial
--chatgpt` reads the client ID from the `assistant/chatgptAccount` setting and the
session from the Secret Service item
`application=org.sketchyup.SketchyUp provider=ChatGPT account=<client>`. Before each
task the ChatGPT session is renewed if fewer than 930 seconds remain, so the
credential covers the whole 15-minute task.

```
cd SKETCHYUP_CHECKOUT            # clean checkout of the RC commit
git rev-parse HEAD
cmake --preset dev && cmake --build build/dev --target provider_trial --parallel 3
E=PRIVATE_EVIDENCE/r086-v4-chatgpt-astra   # new, owner-only, outside the repo
mkdir -m 700 -p "$E"
sha256sum build/dev/provider_trial tests/provider-release-corpus-v4.json \
  scripts/score-provider-release.py > "$E/freeze.txt"
git rev-parse HEAD >> "$E/freeze.txt"
for repeat in 1 2 3; do
  for task in measure room resize advanced site unsupported metadata; do
    build/dev/provider_trial --chatgpt gpt-6-astra "$task" "$E/$task-$repeat.json"
  done
  build/dev/provider_trial http://127.0.0.1:1 gpt-6-astra unavailable "$E/unavailable-$repeat.json"
done
```

The commands are the version 2 and 3 commands; only the manifest changes. Each task
may now take up to 15 minutes, so the 24 runs can take up to six hours in the worst
case (the 1.1.0 run took 13 minutes). Each line prints a summary that includes
`undoRestores` and `recoveryRestores`. A false value is an engine defect: record it,
keep the run and do not retry. Then write `$E/review.json` in the R086.a bundle
format: `profile: "chatgpt-astra"`, `manifestSha256` (the SHA-256 of
`tests/provider-release-corpus-v4.json`), a named `reviewer`, and 24 `runs`. Score
it:

```
python3 scripts/score-provider-release.py tests/provider-release-corpus-v4.json "$E/review.json"
```

**What the live run records.** Each report records `corpusVersion: 4`,
`taskSeconds: 900`, `assistantSession: "native"`, `maximumDraftTtlSeconds: 900`, the
task's `allowedCommands`, `clarificationAvailable: false`, `toolsSha256`,
`instructionsSha256`, the limits above, every provider exchange (including the
instructions it sent), the executed receipts, the oracles and the undo and recovery
postconditions. Publish only the scorer output, the completion record (source,
binary, scorer and manifest hashes, start and end times, the instruction hash) and
the review summary. `ollama-qwen4b` stays an optional experimental comparison.

## Verification results

All of these ran on 2026-10-17, offline. No ChatGPT or OpenAI request was made.

- **Describe.** `provider_trial --describe gpt-6-astra TASK` for all eight tasks, with
  private XDG directories: every `inputSha256`, `promptSha256` and prompt equals v3,
  with `corpusVersion: 4`, `taskSeconds: 900`, `maximumDraftTtlSeconds: 900`,
  `assistantSession: "native"` and `networkRequested: false`. All eight give the same
  two instruction hashes, which the manifest freezes.
- **Offline v4 run.** A loopback replay of the Ollama protocol drove the real
  `provider_trial` through all 24 runs. Every report has `taskSeconds: 900` and
  `assistantSession: "native"`. Each edit task began its draft with
  `ttlSeconds: 899`, which the public headless session would reject, and was applied
  and verified. The recorded wire requests carried the frozen local instructions and
  advertised `transaction.begin` `ttlSeconds` up to 900. Undo and recovery restored
  24/24. The scorer CLI accepted the set (exit 0, `corpusVersion: 4`,
  `corpusPassed: true`, no blockers, every task 3/3, `releaseAccepted: false`)
  against a copy of the v4 manifest whose only change is the test-only
  `offline-replay` profile, derived from `ollama-qwen4b`.
- **`provider_release_score`**: 70 tests passed (3 version-specific skips).
- **Scoring of earlier versions.** All 86 scorer evaluations made by the v1, v2 and v3
  test classes (results and rejection messages) are identical between the previous
  scorer and this one.
- **`provider_release_corpus`**: passed, including the 24 native replay runs and the
  injected-fault run.
- **`dev` preset.** `cmake --build --preset dev` built cleanly, and
  `QT_QPA_PLATFORM=offscreen ctest --preset dev -E "_real$"` passed 198/198,
  including `provider_release_corpus`, `provider_release_score`,
  `native_assistant_session`, `assistant` and `assistant_tool_surface`. The tool-surface
  baseline is unchanged.
- **Sanitizers.** With `cmake --preset sanitize -DSKETCHYUP_BUILD_CLI=ON` (the CI
  "Sanitized bounded inspection and CLI" configuration), the provider and assistant
  suites of that step passed under ASan and UBSan: `chatgpt_auth`,
  `native_assistant_session`, `ollama_provider`, `openai_provider`,
  `assistant_tool_surface`, `assistant` and `automation_session`, plus
  `provider_release_score` and `provider_release_corpus` (203 s, all 25 replayed runs
  under ASan). A first `provider_release_corpus` attempt, run in parallel with the
  other suites while the machine's load average was about 20, stopped when one
  Advanced run hit the test's 120-second per-run timeout. Timed alone, one sanitized
  Advanced run took 46.3 s with this harness and 45.6 s with the v3 harness, so the
  native path adds no measurable cost; the rerun alone passed.
- **CI line inventory.** No workflow line changed; the inventory check matches.
