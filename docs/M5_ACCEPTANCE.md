# M5 alpha acceptance procedure

M5 remains open until live OpenAI and native end-to-end acceptance are recorded.
Deterministic tool replies are labeled fixtures. A successful render, tool receipt
or provider explanation alone is not proof that the requested geometry is right.

## Build and deterministic evidence

From the source checkout:

```sh
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev
scripts/verify-m5-offline.sh build/dev /absolute/new/evidence-directory /usr/bin/blender
```

The last command needs Python 3, Xvfb/xauth and FFmpeg with X11 capture and H.264
encoding. It records only isolated X servers, never the user's desktop. Blender
is optional: omit the third argument for worker fixtures; supplying an absolute
Blender 5.2 LTS executable adds real native CPU rendering. Run against a trusted
build. Use a new evidence directory each time; existing evidence is preserved.

The runner retains measured native before/after models, the executable CLI recipe
receipts, GLB/manifest, native screenshots and recordings, per-suite logs and an
SHA-256 artifact inventory. Its report always leaves `liveProviderTested` and
`m5GatePassed` false. The assistant recording uses injected Responses replies and
a disposable credential helper. Native manual modeling and recovery are covered
by `m4_workflow_tests`; that fixture's depth edit is distinct from the M5 window
width edit. Exact window widening is checked independently by `model_recipes_tests`.
The render check reopens that widened fixture and verifies the returned manifest
against its document identity and revision.

The native room fixtures use **6 × 4 m outside dimensions**, 200 mm walls and
2.7 m height. Window dimensions are **outer frame**, initially 1.2 × 1.0 m,
with 900 mm sill, 100 mm depth and 80 mm members. Widening one to 1.4 m gives
1.24 m clear width. The other stays 1.2 m wide. Wall solid volume changes from
9.888 to 9.848 m³. Eight selected opening vertices move horizontally by 100 mm;
center, sill, height, depth, frame members and sibling records remain unchanged.
See the [authored recipe contract](decisions/0031-room-window-recipes.md).

## Native demonstration with a live provider

1. Launch `build/dev/sketchyup`. Ordinary drawing and saving need no provider.
   The manual baseline exercise draws a 6 × 4 m rectangle, enters its context,
   draws a 5.6 × 3.6 m inset at `[0.2,0.2,0]`, deletes the interior face, and
   pushes the wall ring 2.7 m. On the front wall, draw 1.2 × 1.0 m rectangles at
   sill 0.9 m and push them through the 0.2 m wall. The existing native M4
   workflow records these same drawing/selection/push-pull paths.
2. For the authored window-width acceptance, open `m5-room-before.sketchyup`
   from the installed examples (or `fixtures/room-before.sketchyup` from the
   runner). This separate fixture includes reusable frames, glass, materials and
   explicit host-opening relationships. Arbitrarily drawn windows do not acquire
   recipe bindings automatically; this initial exact resize recipe rejects them.
3. Press Ctrl+J → Preferences. Choose OpenAI → ChatGPT subscription → Continue
   with ChatGPT, finish browser sign-in, choose an available model and Save.
   Alternatively, choose the API-key connection and supply an explicit model ID.
   Linux needs `secret-tool`
   (`libsecret` on Arch) and an available, unlocked Secret Service. Never put a
   key in model files, command arguments, reports or chat. Approve the disclosed
   context transmission on the first request.
4. Enter the room context and select its first window in the Outliner. Request:
   “Widen only the selected window by 0.2 m in outer-frame width. Keep its center,
   sill, height, 80 mm frame members and the other window unchanged. Update its
   actual wall opening and make this instance unique as needed. Measure the
   private result and present a preview.” Resolve clarification explicitly.
5. Inspect the hatched proposal, changed-object list and host measurements. Verify
   the dimensions and preservation checks above; world bounds alone are not a
   clear-opening measurement. Apply once. Undo and Redo must each operate on the
   whole assistant change. Save to a new native path, reopen, and remeasure.
6. Choose Camera → Render, configure Blender, CPU, 512 × 512 and 32 samples.
   Render the current camera. Make another manual edit while rendering. The
   verified image must identify the earlier source revision and say the model
   has changed. Save the PNG and preserve the native model separately.
7. Repeat the room creation request from an empty document and the width request
   in direct mode. Direct mode still uses a sealed, validated transaction and
   one undo entry. Record original prompts, clarifications, model ID, receipts,
   actual dimensions, preserved records and failures. Keep a recording of the
   live sequence distinct from the fixture recordings.

The live gate must also cover a manually authored target without recipe metadata,
using ordinary supported geometry tools or clearly recorded rejection. Do not
claim that the authored fixture proves general window recognition or automatic
host discovery. If the required manual-target edit fails, M5 remains open.

## Repeatable provider corpus

`provider_trial` is an opt-in development executable, not an installed app command.
Only its synthetic fixture is sent. `--chatgpt` selects the native subscription
account, renews its OS-stored session if needed, and checks model availability.
`--openai` selects the separate API key. Both use the fixed OpenAI endpoint.
`configured` reads the corresponding model ID from Preferences; an explicit ID
can be supplied instead. OAuth traffic is excluded from reports.

```sh
build/dev/provider_trial --chatgpt configured measure /absolute/new/measure.json
build/dev/provider_trial --chatgpt configured room /absolute/new/room.json
build/dev/provider_trial --chatgpt configured resize /absolute/new/resize.json
build/dev/provider_trial --chatgpt configured unsupported /absolute/new/unsupported.json
```

Each report retains prompts, timed requests/replies without authorization headers,
actual execution receipts, reported tokens, failures and independent measurements.
HTTP error bodies are omitted because authentication errors can echo credential
fragments; status and timing remain recorded.
A private sibling `.json.files` directory retains the synthetic baseline, any
committed output, journal and manual-save/reopen fixture. A zero exit status means
the measurement completed, not that the model passed. Read `geometryVerified`,
`inspectionVerified`, phase and outcome. Unsupported explanations require human
review; they are not automatically graded by keyword matching. Do not calculate
an accuracy rate from process exit codes. Run each live task three times and
report every result, including failed attempts. Record billing cost separately
when available; reported token counts do not establish the final charge.

The local command uses the same corpus:

```sh
build/dev/provider_trial http://127.0.0.1:11434 qwen3:4b-instruct room /absolute/new/local-room.json
```

The measured Ollama 0.35.1 / Qwen3 4B Instruct CPU profile (32,768 context, eight
threads) timed out on all four initial live tasks. It is experimental. No runner
installs a runtime, pulls a model, changes a server or falls back to the cloud.

## Failure evidence

The build plan lists six failure repeats; the roadmap also requires post-commit
response loss. The eighth row below explicitly covers an unknown durable outcome,
which must be distinguished from a confirmed commit whose response was lost.

| Case | Required observation | Executable evidence |
| --- | --- | --- |
| Blender absent | Useful setup state; manual editing/save work | `render_input_tests` |
| Provider disconnected | Bounded retries; no false success; manual save/reopen works | Provider tests; stopped-loopback `provider_trial … unavailable REPORT` |
| Stale AI revision | Preserve intervening human edit; reject old proposal | `assistant_tests`, `assistant_panel_tests` |
| Canceled preview | Clear private geometry; no live publication | `assistant_panel_tests` |
| Invalid geometry | Whole batch rolls back, including earlier valid commands | `model_recipes_tests`, staging/transaction suites |
| Interrupted save | Complete old/new file and valid backup at process-kill boundaries | `persistence_tests`, native recovery workflow |
| Post-commit response loss | Retry original identity returns same receipt, one undo entry | `transaction_coordinator_tests` |
| Unknown durability | No claim of unchanged/success; fence edits/save/close; reconcile once | `assistant_tests`, `assistant_panel_tests` |

These are controlled fault-injection checks; do not unplug user storage or alter
the user's network to reproduce them. Live normal-path provider results remain
required separately. Record unsupported geometry, ambiguity, provider errors and
any wrong-target edit explicitly. An unresolved required failure keeps M5 open.

## Native package

Generate a source archive using `scripts/package-source.sh NEW_DIRECTORY`, then
build its PKGBUILD with `makepkg --syncdeps` in a disposable Arch build environment.
The package includes the native app/CLI, MIT license, third-party license, API
contracts, this procedure and examples under `/usr/share/doc/sketchyup/`.
Install the resulting package through the normal package manager. Blender and
Secret Service remain optional; their absence must not block manual modeling.
Package CI already builds, installs, runs installed recipes and removes the
package in a disposable container. The source archive never includes credentials,
build evidence, user models or the removed original prototype.
