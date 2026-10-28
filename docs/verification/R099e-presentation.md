# R099.e — Camera paths and batch presentation output: acceptance record

2026-10-27. Branch `docs/r099e-presentation-acceptance`, based on `origin/main` at `36f1c42f` (the merge of PR #484, R099.d3). The record adds one benchmark tool, `tests/presentation_benchmark.cpp` (not built by default, like `report_benchmark`), two batch-dialog checks, one pinned-digest check in `camera_motion_tests`, the CMake lines for them, and the documents. **The 2026-10-27 record changed no product file.** One product defect was found and reported (§7, finding O6). **Updated 2026-10-28** for [PR #501](https://github.com/zygote55/sketchyup/pull/501) (R099.e2: O6 fixed, the unrelated-job, destroy-the-capture and undo checks, PNG byte determinism) and branch `fix/r099e3-presentation-close` (R099.e3: the rendered pose, the render queue bug, the MPEG-4 refusal wording, the provenance wording). Those changed product code (§11). The sections below carry the 2026-10-28 state; the rows that changed say so.

The checks are those of [ADR 0173 §6](../decisions/0173-camera-paths-batch-output.md#6-verification-plan), the Verify line of [R099](../PR_ROADMAP.md#r099), the §5 budgets and the Implementation split. [Summary](R099e-presentation.json).

**Status (2026-10-28).** The Verify line holds: frame count and timing, camera poses (the planner's digests are pinned, each frame's recorded camera equals its planned pose bit for bit, and the rendered pose is within 1 µm on real Blender), cancellation and retry, and the unrelated-job fixture. Every §6 row holds, two of them as amended by the parent's wording decisions (§8, P4 and P5): the schema row names the released readers' `Unsupported container field` refusal (contradiction C2), and the determinism row's "identical per-frame metadata" names the fields the batch compares, not the diagnostics log. The roadmap status is **"Accepted (R099.e)"**. Owner decisions 2, 3, 4 and 5 are carried to the 1.4.0 release decisions; decision 8 is the live corpus run at the 1.4.0 RC, which the owner approved on 2026-10-27.

**Budgets (§5, measured in Release).** Nine of the ten §5 rows pass, and one is not frozen (§4). The 2026-10-27 numbers are kept below; the capture row passes after PR #500:

- **Batch capture on a 250,000-triangle terrain document: pass after PR #500.** p95 160 to 183 ms against 500 ms (1,747 ms before #500; owner decision 1 is no longer needed).
- **Per-frame preparation: not frozen.** The "existing GLB preparation row" it cites has no number in the repository, so there is nothing to freeze against (owner decision 2). The measurement is 304 ms p50 on a 92,500-triangle document.
- The whole-batch row passes as "measured and reported": a 48-frame run was measured, and the full 240 × 4 run is projected at about 3.4 hours, not run (owner decision 3).
- The codec probe and `presentation.describe` timings have no row; they are measured as the brief asks.

## 1. Check map

Each row names the test and assertion that prove the clause, the CI job from the most recent green full-tier run that covers it, and the scripted run made for this record. "Not run" always comes with the reason. Test line numbers are in the file named.

Run identifiers: **M** is the latest green `main` run at `36f1c42f`, `40630475876` (full tier, CI verdict `122656931241`, success). **P** is PR #484's full-tier run, `40619301523` (head `3963d9c4`, label `full-ci`). **Q** is PR #475's full-tier run, `40550719319` (head `9e13f190`). **F** is `main`'s fast-tier run `40626827071` at `6b0909e0`, which runs the presentation fast-tier shards.

| Clause (ADR 0173 §6, and the Verify line) | Proof: test and assertion | CI job (run) | Scripted run (this record) | Result | Gap |
| --- | --- | --- | --- | --- | --- |
| **Verify, frame count and timing match fixtures** | `presentation_record_tests` (lines 89–98: a plan of exactly 240 frames is accepted, 241 is refused, and the message names the formula); `presentation_command_tests` (line 98: 8 frames = 1 + 3 × 1 + 2 × 2); `presentation_export_tests` ("Capture plans six frames for three keyframes…", line 217; "A plan over 240 frames is refused", line 451) | M: Development build (`122625478073`, 274 of 274); Sanitized (`122625478092`, 141 of 141); fast-tier tests shards (F, `122614160178` and others) | R1: 8 frames per preset as planned; S6: 12 frames per preset; refusal e: "Camera path would plan 1129 frames (1 + 32 x 12 + 31 x 24); the limit is 240" | **Pass** | none |
| **Verify, camera poses match fixtures** | NEW `camera_motion_tests` (the four-scene fixture, 11 frames, the block before the final print in `main`): the SHA-256 of each planned frame's index, scene, keyframe flag and camera, at 17 significant digits, equals its pinned digest. The second transition crosses 180° and takes the short way | M: `camera_motion` (`Sanitized builds`, 24/74) | not scripted (the planner is the same one the batch calls) | **Pass (R099.e3).** The pinned digests hold at the planner. `verifyPoses` (`presentation_export_tests`, line 196) checks each frame's recorded camera for equality with its plan, and `verifyRenderedPoses` (line 215) checks the camera Blender rendered within 1 µm, through the stand-in and on real Blender (§3a) | none |
| **Verify, cancellation and retry handle partial output** | `presentation_export_tests` cancel block (around line 291), the retry blocks (lines 348–374: a verified frame is byte-identical after retry; only the damaged and unrendered frames run), the stopped-worker block (lines 378–393), the damaged-manifest refusal (lines 402–422); `presentation_cli_tests` (SIGINT and SIGTERM stops) | M: `presentation_export` and `presentation_export_real` (`Fast tier`, F, and the fractional job `122638743025`, real Blender, 47.8 s); `presentation_cli` (F) | S2: real Blender, SIGINT after two verified frames: exit 130, state `canceled`, 2 frames kept, the in-flight third frame discarded; all kept frames hash-verified | **Pass** | none for these two |
| **Verify, without replacing unrelated jobs** | `presentation_export_tests`, the unrelated-job block (lines 1402–1483, R099.e2, PR #501): a single render runs beside a batch that is canceled, resumed and stopped; after each step the render's state, attempts, manifest, worker process, store record and absence of a result are unchanged | — | Local stand-in suite (R099.e3) | **Pass (PR #501)** | none |
| §6 Frame count and timing: keyframe flags, fps in the manifest | `presentation_export_tests` line 221 ("Every keyframe pose is one flagged planned frame"); `verifyVideos` (line 118) checks `framesPerSecond` 24 | M: `presentation_export_real` | R1, S6: `framesPerSecond` 24 in the manifest and in each video record | **Pass** | none |
| §6 Camera poses: fixture digests (bit for bit) | as Verify, camera poses (above). Each frame's recorded camera is compared for equality with `describeRenderCamera(renderSceneCamera(plan))` (`verifyPoses`, line 196) | M: `camera_motion` | Local stand-in and real suites (R099.e3) | **Pass (R099.e3)** | none |
| §6 Camera poses: rendered frame size equals the preset, bytes and SHA-256 equal the manifest, rendered pose within 1 µm | size and hash: `presentation_export_tests` line 162 (the PNG's SHA-256 equals the manifest entry); size checked by hand in the scripted runs. Pose within 1 µm: `verifyRenderedPoses` (R099.e3, §3a) | M: `presentation_export_real` | R1: 640×480 and 512×384 by PNG header and by `ffprobe`; S4 512×384; S6: all 48 frames 1024×768. Every frame and video hash verified (§3) | **Pass (R099.e3).** Size and hashes pass; the rendered pose is within 1 µm on real Blender (§3a) | none |
| §6 Provenance is immutable | "The engine leaves the live document unchanged" (`presentation_export_tests` line 264); "Real batch leaves the live document unchanged" (line 1152); "a live edit made while the batch runs changes no frame" (R099.b, `presentation_export`); the results banner "The model is unchanged." (`presentation_batch_input_tests`) | M: `presentation_batch_input` (`122638742862`, `…2806`, `…2866`, `…2816`) | R1 and its rerun on a second copy of the same document: identical capture SHA-256 (§4 determinism) | **Pass (R099.e2, R099.e3).** Capture and live-edit immutability pass. "Destroy the original capture" is tested (`presentation_export_tests`, the destroy block, lines 1292–1361, PR #501). The "model has changed since" wording is asserted by the completed-batch block of `presentation_batch_input_tests` (R099.e3) | none |
| §6 Cancellation | as Verify, cancellation (above) | M: `presentation_export` | S2 | **Pass** | none |
| §6 Retry and resume | as Verify, retry (above); exact worker launch counts (R099.b, `presentation_export`) | M: `presentation_export` | not scripted (the CLI cannot resume; retry is an engine operation, covered in the suite and the UI) | **Pass** | none |
| §6 Unrelated-job fixture | as Verify, unrelated jobs (lines 1402–1483, PR #501) | — | Local stand-in suite (R099.e3) | **Pass (PR #501)** | none |
| §6 Refusals: broken path (named reasons) | `presentation_command_tests` line 317 ("A broken path names a refusal in presentation.describe"); `presentation_export_tests` line 439 ("A keyframe without a camera is refused") | M: `presentation_command` | — | **Pass** | none |
| §6 Refusals: existing destination | `presentation_export_tests` line 474 (an existing folder is refused) | M: `presentation_export` | scripted b: "Choose a new batch folder that does not exist yet", exit 1, nothing changed | **Pass** | none |
| §6 Refusals: symbolic-link destination | `presentation_export_tests` lines 481–490 ("A symbolic link destination is refused") | M: `presentation_export` | — | **Pass** | none |
| §6 Refusals: preset list over four | the batch refusal "A presentation names one to four render presets" (`presentation_command_tests`/CLI) | M: `presentation_cli` (F) | scripted d: five presets, exit 1, "A presentation names one to four render presets", no folder | **Pass** | none |
| §6 Refusals: over-budget frame count | `presentation_export_tests` line 451; `presentation_store_tests` line 180 | M: `presentation_export`, `presentation_store` | scripted e: 1,129 frames, exit 1, the formula | **Pass** | none |
| §6 Refusals: over-budget output (remaining budget in the message) | `presentation_export_tests` line 567 (`setOutputLimit(1)`) and line 704 (a per-frame limit) | M: `presentation_export` | — | **Pass** | none |
| §6 Refusals: absent Blender | `presentation_batch_input_tests` (the missing-Blender setup card, §17.8, in the batch suite) | M: `presentation_batch_input` | — | **Pass** | none |
| §6 Refusals: each leaves no partial folder and no broken manifest | scripted refusals; `presentation_export_tests` lines 474–510 | M: `presentation_export` | a–f: no folder created (`ls` after each); e and g: no folder | **Pass** | none |
| §6 Records: create, edit, delete undoable; undo restores the record | `presentation_record_tests` lines 221–244 (one history entry per edit; undo restores the exact record, the delete, the order and the allocator; undo to the saved state is clean); lines 287–294 (undo restores a preset) | M: `presentation_records` (`Sanitized builds`) | — | **Pass (R099.e2).** After undo every byte of the encoding is equal except the revision, which advances as ADR 0005 requires; the row's wording is amended to say so (parent decision P3, §8). `presentation_record_tests` links `sketchyup_io` for the check | none |
| §6 Records: over-budget and malformed records refuse at load | `presentation_io_tests` (malformed and newer records refused; "A schema newer than this build refuses with the newer-format error", line 239); `presentation_record_tests` count caps | M: `presentation_io` | load of 64 paths and 16 presets: §5 | **Pass** | none |
| §6 Records: save and reopen bit for bit | `presentation_io_tests` lines 192–199 ("Paths and presets reopen bit for bit through the JSON document" and "…the container"); line 221 (the saved file reopens with the same paths and presets) | M: `presentation_io` | — | **Pass** | none |
| §6 Schema: schema 30 with `presentation-v1` reopens; a document without the tables writes schema 29 | `presentation_io_tests` line 152 ("writes schema 29 without presentation-v1"), lines 208–221 | M: `presentation_io` | — | **Pass** | none |
| §6 Schema: a 1.3.0 writer refuses a schema-30 file with the newer-format error | the installed 1.3.0 CLI (`/usr/bin/sketchyup-cli`, 1.3.0) on `presentation-v30.sketchyup`: exit 1, `Unsupported container field`, file byte-identical | CI package job (`122625478121`): the 1.2.0 prior CLI refuses 4 newer fixtures unchanged | scripted §3, the installed 1.3.0 check | **Pass, as amended.** Refused and unchanged, with the container's field wording, which the row names since parent decision P4 (§8; R099.a, contradiction C2). | contradiction C2 |
| §6 Schema: the record decoders pass the libFuzzer target twice per input | `parser_fuzz_presentation` (`tests/fuzz/parser_harness.hpp`, the `presentation` format, which decodes each input twice and compares) | not run in CI (§5) | local clang build with ASan and UBSan: 5,249,423 runs in 241 s, no finding (§5) | **Pass** (locally) | CI does not run it (gap 6) |
| §6 Determinism: same batch on two copies gives identical planned poses, metadata and PNG bytes | planned poses: pinned digests (above). Metadata: manifests differ only in `jobId` and in the hashes of the PNG and MP4 files | — | R1 run on `polygon-model-final.sketchyup` and on a copy: capture SHA-256 equal; **every frame's decoded pixels equal** (IDAT SHA-256 equal); decoded video frames equal (`framemd5`); the PNG files differ only in their `tEXt` chunks (`Date`, `RenderTime`), so the file bytes differ | **Pass, as amended (P1, P5).** PNG bytes are identical across two real batches, with the stamp off and the text chunks stripped (`presentation_export_tests`, R099.e2). Planned poses and decoded pixels are identical (R1-b). "Identical per-frame metadata" holds for the fields `verifySameFrames` compares (camera, scene, keyframe, source manifest), not for the frame's `diagnostics` log, which is Blender's own output from the attempt. The row names those fields since parent decision P5 (§8). | P5 |
| Verify, frame count and timing (video: H.264 on the CI Blender package) | `presentation_export_real` (the H.264 MP4 read back by `ffprobe`) | M: fractional job `122638743025`: `presentation_export_real` passed on `17:5.2.2-2`, the CI Blender | R1: H.264 High profile, `yuv420p`, 8 frames per preset | **Pass** | none |

## 2. Test runs

| Run | Where | What ran | Result |
| --- | --- | --- | --- |
| Local, Release (`build/release`, GCC 16.2.1, `-O3 -DNDEBUG`, BFD linker, Qt 6.12.0, `SKETCHYUP_BUILD_DESKTOP=ON`, `SKETCHYUP_BUILD_CLI=ON`, 467 steps) | this record | `ctest -R` over `presentation_records`, `presentation_io`, `presentation_store`, `presentation_export` (with `presentation_export_real`, real Blender), `presentation_cli`, `presentation_commands`, `m7_presentation`, `blender_job`, `blender_handoff`, `render_store`, `render_queue`, `camera_motion`, `saved_scenes`, `animation_export`, `-j2`, under `ulimit -v 8000000` | 15 of 15 passed (37.8 s). The real batch test took 36.6 s |
| Local, Release, offscreen | this record | `presentation_input_tests` (the Presentations dialog) | passed (exit 0) |
| Local, Release, offscreen | this record | `presentation_batch_input_tests`, with the two new blocks (§6, R099.e note) | "all checks passed" |
| Local, Release, offscreen | this record | `native_accessibility_audit --dialogs-keyboard=presentations` | 2 dialogs, 0 failed, 0 uncovered ellipsis actions |
| Local, Release | this record | `camera_motion_tests` with the pinned digests | passed |
| Main, full tier (`40630475876`, `36f1c42f`) | [run](https://github.com/zygote55/sketchyup/actions/runs/40630475876) | `Development build and checks` (`122625478073`): 274 of 274 CTest tests, including the presentation suites (§1) | 274 of 274 passed |
| Main, full tier, real Blender | same run | `Native tests: fractional scaling, Blender worker and M5` (`122638743025`): 10 real tests, `presentation_export_real` 47.8 s, `m7_presentation_real`, `animation_export_real`, `blender_real` | 10 of 10 passed |
| Main, full tier, sanitizers | same run | `Sanitized builds` (`122625478092`): 141 CTest tests (with `presentation_records`, `presentation_io`, `presentation_store`, `presentation_export`, `presentation_commands`, `blender_job`, `m7_presentation`, `blender_handoff`, `render_store`, `render_queue`, and the six report suites) | 141 of 141 passed (§5) |
| PR #484, full tier (`40619301523`, head `3963d9c4`, label `full-ci`) | [run](https://github.com/zygote55/sketchyup/actions/runs/40619301523) | the same native cells and the Development build | CI verdict `122624771773` success; every native cell passed (§4) |
| PR #475, full tier (`40550719319`, head `9e13f190`) | [run](https://github.com/zygote55/sketchyup/actions/runs/40550719319) | the R099.d2 code: `presentation_input` in the native cells and the audit | CI verdict `122398627238` success |
| Local, Debug (`build/dev`, the `dev` preset: desktop and CLI on), 2026-10-28, branch `fix/r099e3-presentation-close` (R099.e3) | this record | `presentation_export_tests` (the stand-in suite, then `--real` with `SKETCHYUP_BLENDER_TEST=/usr/bin/blender`); `blender_job_tests` (fake, then `--real`); `render_queue_tests`; `presentation_cli_tests`; `presentation_batch_input_tests` offscreen | stand-in suite passes; `presentation_cli_tests` passes; real suite passes (its last of three runs is reported, §3a); `blender_job_tests` passes both ways; `render_queue_tests` passes, and fails on the R099.e2 code at its new check (ADR 0173 R099.e3); `presentation_batch_input_tests` "all checks passed", with the new provenance block |

## 3. Scripted runs

Local Release binaries (`build/release/sketchyup-cli`, `build/release/presentation_benchmark`). Blender is `/usr/bin/blender`, Blender 5.2.2 LTS. Every run used a fresh `HOME` and `XDG_DATA_HOME`. Inputs and outputs are in `/home/keith/projects/sketchyup/.local/r099e-scripted` and are not committed. The model is the 3,700-body document of the per-frame row (92,500 triangles, 4 saved scenes, presets `Draft`, `Preview`, `Final 1` to `Final 4`).

| # | Run | Result | Clause |
| --- | --- | --- | --- |
| R1 | `--export-presentation` with `video: true`, presets `Draft` (640 × 480, 16 samples) and `Preview` (512 × 384, 8 samples), 3 keyframes, timing 24 fps, transition 2, hold 1 | exit 0; 16 frames; 142.7 s wall (an earlier run on an older copy of the model: 112.1 s); manifest 9,731 bytes; both videos H.264 High, 8 frames each, `yuv420p`, `video.mp4` 48,842 and 35,110 bytes; every frame and video hash verified | Verify (frames, timing), §6 video |
| R1-b | the same request on a second copy of the model, output `r1-video-b` | capture SHA-256 equal; every frame's decoded pixels equal; decoded video frames equal; PNG and MP4 bytes differ (§1, determinism) | §6 determinism |
| S2 | the same batch with `Final 1` and timing 24/2/1 (5 frames); SIGINT sent after frame 2 was verified | exit 130; state `canceled`; `completedFrames` 2; the in-flight third frame discarded (no `frame-0003` files); both kept frames hash-verified | §6 cancellation |
| S4 | `videoCodec: "mpeg4"`, preset `Preview`, 2 keyframes, 5 frames | exit 0; `video.mp4` MPEG-4 Simple Profile, 512 × 384, 5 frames, 21,883 bytes; `videoCodec` `mpeg4` in the manifest | §7 decision 1 (MPEG-4 fallback, owner 2026-10-27) |
| S6 | the whole-batch reduced run: 4 presets (`Final 1` to `Final 4`, 1024 × 768, 32 samples, CPU), 2 keyframes, timing 24 / transition 3 / hold 4: 12 frames per preset, 48 frames, no video | exit 0; state `completed`; 48 of 48 frames; 604 s wall (21:41:09 to 21:51:13); every frame hash-verified; every frame 1024 × 768; manifest 14,514 bytes | §5 whole batch, per-frame render, manifests |
| refusals a–g | a: missing parent folder; b: existing folder; c: unknown preset; d: five presets; e: 1,129 planned frames; f: relative output path; g: a 17 KB request | each exit 1; a: "Choose a new batch folder within an existing directory"; b: "…does not exist yet"; c: "Render preset Nope does not exist"; d: "…one to four render presets"; e: "…would plan 1129 frames (1 + 32 x 12 + 31 x 24); the limit is 240"; f: the same parent message; g: `INPUT_ERROR`, "Cannot read presentation request or it exceeds 16 KiB" (contradiction C5). No output folder was created in any case | §6 refusals |
| prior-release | the installed 1.3.0 CLI (`/usr/bin/sketchyup-cli`, `sketchyup-cli 1.3.0`) on a copy of `presentation-v30.sketchyup`: `--validate-native` and `--inspect-native` | exit 1 for both, `INVALID_OPERATION`, "Unsupported container field"; the copy is byte-identical afterwards (SHA-256 `c7058b68…`) | §6 schema (1.3.0 refusal) |
| bench | `presentation_benchmark` modes `plan`, `edit`, `load`, `capture`, `prepare`, `probe`, `describe` (§4) | as §4 | §5 |

## 3a. Rendered pose (R099.e3)

The camera each frame's Blender rendered, read by the worker after the scene is set up (`renderedCamera` in the frame's result), against the planned pose through `renderSceneCamera`. Model units (a millimetre), tolerance 0.001 (1 µm). Method and conventions: ADR 0173, Implementation notes (R099.e3). The export fixture is 12 frames: two presets, three keyframes, the default camera at a distance of 14 model units, changing only in yaw.

| Run | Position | Target | Up | Clipping | Field-of-view edge | Result |
| --- | --- | --- | --- | --- | --- | --- |
| Stand-in (`presentation_export_tests`, stand-in suite, final code) | 0 | 1.8 × 10⁻¹⁵ | 0 | 0 | 0 | pass |
| Real Blender 5.2.2 LTS (`presentation_export_tests --real`, final code) | **2.1 × 10⁻⁶** | **1.5 × 10⁻⁶** | **5.6 × 10⁻⁶** | **5 × 10⁻¹¹** | **3.1 × 10⁻⁷** | pass, 500 times under 0.001 |

The real run also passes the recorded camera's equality with its plan, the PNG hashes of R099.e2 (unchanged), and two-run PNG identity (lines 1567–1592). Blender's axis convention needs no conversion (the exporter's and the importer's mappings compose to the identity); the real run confirms it. Orthographic cameras are in no fixture, so the orthographic field of view and scale are not checked.

## 4. Budgets (ADR 0173 §5), measured in Release

Measured on `build/release` (Release, GCC 16.2.1, `-O3 -DNDEBUG`, BFD, Qt 6.12.0) on this machine: 22 CPU threads, 30 GB RAM, other workers building during some runs. **The ADR's conditions were the pinned image and software rendering; these runs are local and use Cycles on the CPU.** The 1-minute load average at the start of each row is given, because the timings depend on it. Raw output is in `/home/keith/projects/sketchyup/.local/r099e-logs/bench-*.jsonl` (not committed). Budgets are quoted from the ADR and not changed.

| §5 row | Budget | Measured | Runs | Load (1 min) | Verdict |
| --- | --- | --- | --- | --- | --- |
| `planCameraFrames`, 32 keyframes at the largest timing ≤ 240 frames (24 fps, transition 7, hold 0: 218 frames) | p95 ≤ 10 ms (*proposed*) | p50 0.008 ms, **p95 0.015 ms**, max 0.023 ms | 200 | 3.08 | **pass** |
| A path record edit (create, update, erase) | within the ordinary document-edit row (BUILD_PLAN: routine primitive edit, p95 feedback below 100 ms) | core edit, p95: create 0.002 ms, update 0.002 ms, erase 0.003 ms (max 0.009 ms) | 100 cycles each | 3.08 | **pass** |
| The 512 KiB path budget, measured at load | the byte budget (*proposed*) | 64 paths and 16 presets: 10,571 bytes of presentation tables; the budget cannot bind (R099.a: a path is at most 1,432 bytes, so 64 paths total at most 91,648 bytes) | — | — | **pass** (the count caps bind first; owner decision 5) |
| A preset record edit | within the ordinary document-edit row | core edit, p95: create 0.002 ms, update 0.002 ms, erase 0.002 ms | 100 cycles each | 3.08 | **pass** |
| The 256 KiB preset budget, measured at load | the byte budget (*proposed*) | the same 10,571 bytes; the budget cannot bind | — | — | **pass** (owner decision 5) |
| Records on load: decode of 64 paths and 16 presets | (within the same load measurement) | decode p50 0.335 ms, **p95 0.422 ms**, max 0.453 ms; document 12,125 bytes | 20 | 2.84 | **pass** |
| Batch capture (one freeze, plan and hash) on a 250,000-triangle document | **p95 ≤ 500 ms** (*proposed*) | **p50 1,602 ms, p95 1,747 ms**, max 1,767 ms. The document: `quantities-b`'s 249,218-triangle terrain, no placements; 218 frames. `encodeDocument` alone: p50 1,514 ms, p95 1,721 ms | 20 | 2.05 to 4.01 | **fail** in the 2026-10-27 run; **pass after PR #500**: p50 154 to 161 ms, p95 160 to 183 ms (ADR 0173, capture encode notes, runs 3 and 4). Owner decision 1 is no longer needed |
| Per-frame preparation (`PreparedRender::prepare`) | within the existing GLB preparation row, measured per frame | **p50 304 ms, p95 323 ms** (3 frames, each prepared twice; 3,700 bodies, 92,500 triangles, 1024 × 768 settings). The row it cites has no number in the repository | 6 | 2.08 | **not frozen**: no row to compare with (owner decision 2) |
| Per-frame render, real Blender, CPU, 1024 × 768, 32 samples | ≤ 180 s including the CPU fallback; PNG at most 64 MiB, hash-verified | per-frame interval (completion times of the frame records, S6): **p50 14.5 s, p95 15.9 s, max 20.8 s**; mean 12.6 s; PNG about 0.61 MB; all 48 hash-verified | 48 | 2.29 (end 4.92 at 5 min) | **pass** |
| Whole batch: 240 frames × 4 presets at 1024 × 768 / 32 samples | measured and reported (no fixed cap; progress and cancel at frame boundaries; the remaining 2 GiB surfaced per frame) (*proposed*) | the 48-frame run (S6) measured in full. **Projected for 960 frames: about 3.4 hours at the mean (3.9 hours at p50)**, about 0.65 GB of output against the 2 GiB budget. Progress: the manifest is published after every frame; cancel at a frame boundary: S2; the remaining budget is checked per frame (`presentation_export_tests`, `setOutputLimit`). **The 960-frame run was not made** | 48 | 2.29 | **pass, reported** (owner decision 3). The projection is the only number for the full row |
| Store | 8 pending, 16 retained, 2 GiB (the R070 limits) | defaults are 8, 16 and 2 GiB (`presentation_store.hpp` line 70); enforced by `presentation_store_tests` | — | — | **pass** |
| Manifests: per-frame `.json` ≤ 1 MiB | (the animation export bound) | 70,208 bytes (a copy of the attempt log, 67,103 bytes of it) | 48 | — | **pass** |
| Manifests: batch `manifest.json` ≤ 1 MiB (*proposed*) | 1 MiB | 14,514 bytes for 48 frames (about 300 bytes per frame); about 0.29 MB at 960 frames | 48 | — | **pass** |
| Codec probe cache (R099.d1, no §5 row) | (measured, as the brief asks) | first probe (a Blender start) **1,004 ms**; `presentation.describe` first **1,150 ms**; `presentation.describe` cached **p50 0.25 ms, p95 0.30 ms**; cached read 0.006 ms; the probe reports H.264 and MPEG-4 | 1 / 5 / 5 | 2.07 / 2.73 | measured |

**The §5 table in ADR 0173 carries these values and verdicts** (its Implementation notes, R099.e).

## 5. Sanitizers and fuzzing

- **Sanitizers, CI.** `Sanitized builds` (`122625478092`, `main` at `36f1c42f`): 141 of 141 passed, including the presentation record, I/O, store, export and command suites, `blender_job`, `m7_presentation`, `blender_handoff`, `render_store`, `render_queue`, `camera_motion`, `saved_scenes` and the six report suites; the first sanitize pass (74 tests) included `camera_motion` and `presentation_records`. Also in that job: `m7_presentation_input_tests` and `render_input_tests` on Wayland. **Not under sanitizers:** `presentation_input_tests`, `presentation_batch_input_tests`, `presentation_cli_tests` and `presentation_benchmark`. The CI sanitize lists name none of them (gap 5).
- **Sanitizers, local.** Not built. The fuzz build below ran under AddressSanitizer and UBSan.
- **Fuzzing.** `parser_fuzz_presentation` exists (`tests/fuzz/parser_harness.hpp`, the `presentation` format; R099.a). It was run here: a clang build (`-fsanitize=fuzzer,address,undefined`, RelWithDebInfo, no desktop, no CLI, `SKETCHYUP_BUILD_FUZZERS=ON`), the 5 seeds from `parser_fuzz_seeds`, `-max_total_time=240 -seed=1 -max_len=1048576`, under the 10 GB cgroup cap: **5,249,423 runs in 241 s, no finding, no artifact**; the corpus grew to 242 units. The earlier R099.a run was 2,344,784 runs in 181 s with no finding. **CI does not run this target**; the only fuzz job in CI is the SKP package decoder (gap 6).
- **The job store.** No fuzz target exists for `PresentationJobStore` (`job.json`). Its records are checked by `presentation_store_tests` (a damaged record reopens as failed; a changed capture is refused) and the corruption fixtures of §6, not by a fuzzer. Carried (gap 7).

## 6. Installed smoke

- **CI package, `main` (`122625478121`).** `Arch package acceptance` passed: "Install, desktop launch, MIME, upgrade, reopen and removal acceptance passed." Its prior-release check (the 1.2.0 CLI) refused four newer fixtures unchanged. The fixture scan covers every `tests/fixtures/*.sketchyup` above schema 28, so `presentation-v30.sketchyup` is among the refused fixtures. The package is built from the 1.3.0 version in `CMakeLists.txt`. The same job passed on PR #484 (`122589202639`).
- **The installed 1.3.0 CLI, local.** Refuses `presentation-v30.sketchyup`, unchanged (§3).
- **Carried to the 1.4.0 RC walkthrough.** The installed smoke that proves this feature needs the 1.4.0 release package: install it, run `--export-presentation` with real Blender from the installed package (R1 and S2 as written here), check the Presentations dialog and the batch dialog in the installed desktop, and rerun the prior-release check with `1.3.0-1` in `released_packages` (`packaging/arch/verify-package.sh`, R099.a). The version in `CMakeLists.txt` is still 1.3.0, so no 1.4.0 package exists to test.

## 7. Gaps

Each gap is resolved in this record, or carried with its owner decision (§8) or the work it needs. Gaps from the implementation notes of R099.a to R099.d3 come first.

### Gaps from R099.a to R099.d3

| Note | Gap as recorded | Here |
| --- | --- | --- |
| R099.a | The 512 KiB and 256 KiB byte budgets cannot bind | **carried**: owner decision 5 (drop or lower them; nothing else changes) |
| R099.a | The released 1.2.0 and 1.3.0 readers refuse schema 30, but with the container's wording, not §6's newer-format wording | **carried** as contradiction C2; the refusal itself is measured (§3, prior-release; §1) |
| R099.a | `verify-package.sh`'s prior is 1.2.0-1; 1.3.0-1 must be added at the next format release | **carried** to the 1.4.0 RC walkthrough (§6) |
| R099.a | Keyframe repeats are allowed | **record decision** (§8, R1): accepted; no change |
| R099.a | Schema 30's lowest-schema rule is kept, against §3's literal words | **record decision** (§8, R2): accepted as recorded |
| R099.b | The H.264 answer on CI's Blender package (`17:5.2.2-2`) was not run | **resolved**: `presentation_export_real` passed on CI's package (§1, Verify, video) |
| R099.b | The MJPG fallback does not exist in Blender 5.2.2 | **resolved** by the owner's 2026-10-27 choice (MPEG-4 Part 2); run for real (S4) |
| R099.b | The batch capture row (500 ms) is not met in a Debug build; measured in Release now | **resolved by PR #500**: p95 160 to 183 ms against 500 ms (§4). The 1,747 ms figure was the 2026-10-27 run |
| R099.b | A 250,000-triangle model cannot be built under the 100,000-vertex limit | **measured on a different 250,000-triangle document**: the terrain (249,218 triangles), which a capture can hold. Per-frame preparation and rendering cannot run on it (finding C4, owner decision 4) |
| R099.b | Per-frame render time, and the whole batch, were not measured | **resolved in part**: 48 frames measured (S6); the full 960-frame run is projected, not run (owner decision 3) |
| R099.b | A SIGTERM left the batch Running until the store reopened | **resolved in R099.d1** (SIGTERM stops now); `presentation_cli` covers it |
| R099.b | Contradiction 1 (the 250,000-triangle row cannot be built) | **open**: see finding C4 |
| R099.b | Contradiction 4 (SIGTERM) | **resolved** (above) |
| R099.b | The 16 KiB request refusal is documented as `LIMIT_EXCEEDED` | **open deviation** C5: it is `INPUT_ERROR` now (§3, g); owner decision 9 |
| R099.c | The CI Blender package had not confirmed H.264 | **resolved** (see R099.b above) |
| R099.c | A live version-6 assistant run is required before the next release and was not made | **carried** to the 1.4.0 release (owner decision 8) |
| R099.c | The whole-batch video time was not measured; the real test used 64 × 64 frames | **resolved**: R1 and S4 are real encodes at 512 × 384 and 640 × 480 (the encode step is about 0.03 s for five frames in Blender's log) |
| R099.c | Contradiction 1 (`*.describe` as queries) and the H.264 confirmation | **accepted** (the queries are as decision 13 describes); H.264 confirmed (above) |
| R099.d1 | The per-frame JSON was described as about 207 KB (UX 17.11 gap 7; ADR §5) | **resolved by measurement**: 70,208 bytes here, one copy of the log; the 207 KB figure does not reproduce (contradiction C1) |
| R099.d1 | `animation_export.cpp` puts the full BlenderJob report under `diagnostics` (duplicate log) | **carried**: ADR 0107's contract is unchanged, so not in R099 |
| R099.d1 | The CLI's blocking probe nests an event loop | **accepted**: the CLI only, by design |
| R099.d1 | `presentation.describe` launched a Blender on every call | **resolved** in the d1 review (cached; 0.25 ms when cached) |
| R099.d1 | CI's Blender and sanitizer jobs were not run | **resolved**: `main` run `40630475876` (§2) |
| R099.d2 | The mockup says 2 scenes plan 37 frames; the planner's formula gives 49 | **resolved**: `docs/mockups/presentation.html` corrected to 49 |
| R099.d2 | Mockup deviations (the disabled entries, Close not the default, the draft footer, the keyframe status wording) | **accepted**: deliberate, as the d2 note records; no change |
| R099.d2 | The render group's file-chooser cases fail offscreen | **resolved in CI**: the audit passed at X11 2× and Wayland 1× (`122638742895`) |
| R099.d2 | The X11 2× scale and the Wayland runs for the new suite were not run here | **resolved in CI** (§4 native matrix) |
| R099.d3 | The disk figure in the dialog differs from the engine's by the capture size | **carried**: documented, cosmetic |
| R099.d3 | A batch with no finished Eevee frame cannot resume (the device is not recorded) | **carried**: the dialog says so; Cycles batches resume |
| R099.d3 | The status chip keeps "Show results" until the next batch or a restart | **carried**: cosmetic. The chip now opens the results (below) |
| R099.d3 | **Untested: the disk refusal at the dialog** | **carried**: the engine's own disk check is tested (`presentation_export_tests`); the dialog uses the same `QStorageInfo` figure |
| R099.d3 | **Untested: the Canceling row** | **carried**: the state lasts only while the worker is killed, so a check races. The text is `render_jobs.cpp` line 255 |
| R099.d3 | **Untested: Resume and "Resume: encode video" from the results sheet** | **resolved in part**: "Resume: encode video" is tested (a new block in `presentation_batch_input_tests`). A plain Resume from the results sheet is carried; Resume from the Jobs list is tested |
| R099.d3 | **Untested: Show results… from the chip** | **resolved**: the new block opens the chip and checks the results sheet |
| R099.d3 | **Untested: Esc in the dialog** | **resolved**: the new block closes the dialog with Esc and starts no batch |
| R099.d3 | **Untested: a changed Blender between opening the dialog and Start** | **resolved in PR #501** (finding O6, below). The test found it and was removed from the PR; #501 fixed the product code and added the blocks |
| R099.d3 | The suites ran on Qt 6.11.2, not 6.8 | **resolved in CI**: the Qt 6.8 reference compile passed on `main` (`122625478047`), and the native cells run on the CI image's Qt |

### This record's gaps

1. **Unrelated-job fixture (§6).** **Resolved in PR #501** (R099.e2): a single render beside a batch that is canceled, resumed and stopped, checked after each step.
2. **Camera poses (§6).** **Resolved in R099.e3.** The rendered pose is decoded from the frame's result and compared with the plan within 1 µm, through the stand-in and on real Blender (§3a). The recorded camera is compared for equality with the plan. The planner's digests are pinned in `camera_motion_tests`.
3. **Immutability (§6).** **Resolved**: "destroy the original capture" is tested in PR #501, and the "model has changed since" wording is asserted in R099.e3.
4. **Undo and encodings (§6).** **Resolved in PR #501**: every byte of the encoding but the revision is equal after undo. The row's wording is amended to match (parent decision P3, §8). `presentation_record_tests` links `sketchyup_io`.
5. **Sanitizers for the UI and CLI suites.** `presentation_input_tests`, `presentation_batch_input_tests` and `presentation_cli_tests` are not in the sanitize lists. Carried.
6. **Fuzzing in CI.** `parser_fuzz_presentation` is not run by CI; it was run here (§5). Carried to the owner, with the CI cost.
7. **The job store has no fuzz target.** Carried.
8. **Windows and macOS.** Not run; not in the support matrix (Linux x86_64 only).

### Findings

- **C1. The per-frame JSON size.** UX 17.11 gap 7 and ADR §5 say about 207 KB with three copies of the log. Measured: 70,208 bytes with one copy of the attempt log (`diagnostics.attempts`). Within the 1 MiB bound.
- **C2. The schema-30 refusal wording.** §6 names a newer-format error. The released readers refuse with `Unsupported container field` (§3, prior-release). Refused and unchanged; the wording differs. The released 1.3.0 CLI cannot change. **Resolved by parent decision P4:** the §6 row names the container's wording.
- **C3. "Identical PNG bytes."** §6's determinism row says the same batch gives identical PNG bytes. It does not: Blender writes a `Date` and `RenderTime` into each PNG's `tEXt` chunks. Pixels, decoded video and planned poses are identical (§1). Owner decision 7. **Resolved by parent decision P1 (R099.e2):** the PNG bytes are kept identical, with the stamp off and the PNG text chunks stripped (`blender_worker.py`, `STAMP_FLAGS` and `strip_png_text`); the test is run on real Blender.
- **C4. The 250,000-triangle document cannot be rendered.** The render snapshot refuses more than 20,000 bodies (`src/integrations/render_snapshot.cpp` line 250), so `quantities-b`'s 10,000 placements cannot be captured. The 249,218-triangle terrain document captures, but preparing it is refused with `GLB has too many visible faces` (`src/integrations/glb_export.cpp` line 277, observed). A 72,000-face model of 6,000 placements is refused with `Export artifacts exceed bounds`, because its JSON manifest passes 16 MiB (`glb_export.cpp` lines 577 and 640, observed). A batch on such a document cannot start a frame. Owner decision 4.
- **C5. The request-size error code.** R099.b documents `LIMIT_EXCEEDED` for a request over 16 KiB; the CLI returns `INPUT_ERROR` ("Cannot read presentation request or it exceeds 16 KiB"). Owner decision 9. **Resolved by parent decision P2 (R099.e2):** an over-size request is `LIMIT_EXCEEDED`; a request that cannot be read stays `INPUT_ERROR`.
- **O6. A changed Blender refuses Start with the wrong reason.** Reproduced in the batch suite (the block now removed). Steps: open the batch dialog with Write video on and its checks finished; change the Blender executable in place (or replace it); click Start. `startBatch` (`src/app/presentation_batch_dialog.cpp`, near line 918) first calls `startProblem` (near line 863), which calls `videoAvailable` (near line 683). With no cached answer for the new file, `videoAvailable` is false, so `startProblem` returns "H.264 is not in this Blender's FFmpeg build, so no video can be written." `startBatch` returns on that message before its re-check branch (near line 930), so nothing is re-checked and no batch starts. The UX design (17.5) says Start re-checks the encoder when the Blender has changed. The product defect is real; it is reported here, and product code is unchanged. Owner decision 6. **Fixed in PR #501 (R099.e2):** a missing answer is no refusal; Start re-checks the encoder and says so, and a refusal names the codec the batch chose.
- **R099.e2 gaps, closed in R099.e3.** `RenderQueue::result()` on a running job made `jobs()` show it as Failed, and the entry outlived the job (`render_queue.cpp`; fixed, with a test that fails on the old code). The MPEG-4 refusal said "H.264" (fixed: `presentationVideoRefusal` names the chosen codec; the text is tested for both codecs). The worker reported no camera (fixed: the rendered camera, §3a).

## 8. Owner decisions

Each item gives the recommendation the record makes. None is decided by this record.

1. **The batch capture budget (§4).** p95 1,747 ms against 500 ms on the 249,218-triangle terrain document. The capture runs once per batch, before the first frame, so its cost is small against a batch that takes hours. Options: (a) keep 500 ms and fund the encode work (the encoding, which doubles as the provenance hash, is about 95% of capture); (b) accept a larger budget for batches on documents of this size; (c) a cheaper provenance digest (R099.b's option). **Recommendation: (b), with (a) as later work.** **Status (2026-10-28): resolved by PR #500 (p95 160 to 183 ms against 500 ms); no choice needed.**
2. **The per-frame preparation row (§4).** The row cites an "existing GLB preparation row", and no number exists in the ADRs, BUILD_PLAN or the verification records. Measured: p50 304 ms per frame on the 92,500-triangle document. **Recommendation: name a row, and freeze this measurement against it.** **Open (2026-10-28).**
3. **The whole-batch length (§4).** The reduced 48-frame run projects about 3.4 hours for 240 × 4 at 1024 × 768, 32 samples, on this machine. **Recommendation: accept the length, and let the existing progress and cancel carry it.** The 960-frame run is still to be made before the 1.4.0 release if the owner wants the number measured, not projected. **Open (2026-10-28).**
4. **250,000-triangle terrains cannot be rendered or batched (C4).** The 100,000-face GLB limit and the 20,000-body snapshot limit are product limits from R070. **Recommendation: keep the limits, and state in the support matrix that a terrain of this size is not batched or rendered.** **Open (2026-10-28).**
5. **The byte budgets (512 KiB and 256 KiB) cannot bind (§4).** **Recommendation: drop them, or set them at a small multiple of the measured maxima (R099.a: 91,648 bytes for 64 paths, 18,432 bytes for 16 presets). The counts bind first.** **Open (2026-10-28).**
6. **The changed-Blender Start defect (O6).** A product fix in `startBatch`/`startProblem`: when the answer is missing, start the re-check first and say so, instead of reporting an H.264 refusal. **Recommendation: fix before the 1.4.0 release.** **Fixed in PR #501.**
7. **"Identical PNG bytes" (C3).** Either strip Blender's date and time chunks from the PNG (a change in the worker), or change the determinism row to "identical pixels, decoded video and planned poses". **Recommendation: the second; the metadata is Blender's and is useful.** **Decided by the parent (P1), as the first option, against this recommendation: the chunks are stripped, so the row holds as written.** The metadata is still in Blender's own log, which the frame's `diagnostics` keeps.
8. **The live version-6 assistant run (R099.c).** Required before the 1.4.0 release. Not made here. **Carried to the 1.4.0 RC:** the owner approved one live corpus run at the RC on 2026-10-27.
9. **The 16 KiB refusal code (C5).** **Recommendation: restore `LIMIT_EXCEEDED` for an over-size request, or amend R099.b's note. Either is small.** **Decided by the parent (P2): restored, in R099.e2.**
10. **The roadmap status (this record).** "Delivered, with open findings" because the Verify line is partly met and one §5 row fails. **Recommendation: keep it, until O6 and the unrelated-job fixture are done.** **Status (2026-10-28): O6 and the unrelated-job fixture are done, and P4 and P5 are decided. The status is "Accepted (R099.e)".**

Record-level decisions (made here, not owner decisions):

- **R1. Keyframe repeats** (R099.a) are accepted: the motion core takes them.
- **R2. Schema 30's lowest-schema rule** (R099.a, against §3's words) is accepted as recorded.
- **R3. The capture row's document** is `quantities-b`'s terrain (249,218 triangles, no placements), the only 250,000-triangle document the batch can capture: the render snapshot refuses more than 20,000 bodies. Preparation and rendering use the 3,700-body, 92,500-triangle document, the R099.b document, because the GLB limits refuse the terrain.
- **R4. The whole-batch row** is frozen "pass, reported" on a measured 48-frame run and the projection, not on a full run.
- **R5. The per-frame preparation row** is not frozen, because it has no row to compare with (owner decision 2).
- **R6. The pinned pose digests** are at planner level, in `camera_motion_tests`, because the planner is what the batch calls, and the digests cannot be taken from the batch without the rendered-pose decode.
- **R7. The native matrix** is taken from `main`'s run `40630475876`, which is the merged tree, and corroborated by PR #484's and PR #475's runs.
- **R8. The roadmap status** was "Delivered, with open findings" while a clause failed, as the brief requires. It is "Accepted (R099.e)" since R099.e3 and the P4 and P5 decisions.
- **R9. The benchmark** is committed as a tool, not a test (`EXCLUDE_FROM_ALL`), as `report_benchmark` is, so the numbers can be reproduced.

Parent decisions (2026-10-28, R099.e2 and R099.e3):

- **P1. Identical PNG bytes are kept** (owner item 7): the stamp is off and the PNG text chunks are stripped (R099.e2). The row holds as written. This is the first option, not the recommended second.
- **P2. `LIMIT_EXCEEDED`** for an over-size request (owner item 9, R099.e2). A request that cannot be read stays `INPUT_ERROR`.
- **P3. The undo wording.** §6's Records row now reads "every byte of the encoding except the revision, which advances (ADR 0005; amended in R099.e3, parent decision)". Undo advances the revision, so "bit for bit" cannot hold for it. The amendment is in place, as R101.f amended V5.
- **P4. The schema row's wording (C2).** §6 said a 1.3.0 writer refuses a schema-30 file "with the new-format error". The released CLI refuses it with "Unsupported container field" and cannot change. **Decided: the row is amended to the container's wording, in place, as P3 was.**
- **P5. The determinism row's per-frame metadata.** §6 said "identical per-frame metadata". `verifySameFrames` checks the camera, scene, keyframe and source manifest. It does not check the `diagnostics` log, which is Blender's own output from the attempt and is not expected to match between runs. **Decided: the row is amended, in place, to the fields it checks.**

## 9. Native matrix

ADR 0173's Implementation split names X11 and Wayland at 1× and 2× for the UI slice. The support matrix supports Linux x86_64 only, so the matrix is Linux only. Results are from `main`'s full-tier run `40630475876` (`36f1c42f`). Each cell passed (the job concluded success); the suites print "all checks passed" only in `presentation_batch_input_tests`, and `presentation_input_tests` and `render_input_tests` exit 0.

| Cell | Job | `presentation_input_tests` | `presentation_batch_input_tests` | `render_input_tests` | Dialog audit groups |
| --- | --- | --- | --- | --- | --- |
| X11 1× (`xcb`, Xvfb, scale 1) | `122638742862` | passed | passed ("all checks passed") | passed | not run here: the audit runs only at X11 2× and Wayland 1× (`native.yml`) |
| X11 2× (`xcb`, `QT_SCALE_FACTOR=2`) | `122638742806` | passed | passed | passed | passed (`presentations`, `presentation-batch`, `presentation-batch-sheet`, `render-setup`, `render-jobs`, `blender-executable`, `hdr-environment`; job `122638742895`) |
| Wayland 1× (`test-wayland.sh`) | `122638742866` | passed | passed | passed | passed (the same groups) |
| Wayland 2× (`SKETCHYUP_TEST_SCALE=2`) | `122638742816` | passed | passed | passed (and with the section fixture) | **not run**: the audit runs only at X11 2× and Wayland 1× |

- The same suites in PR #475's run (`40550719319`): `presentation_input_tests` passed in X11 1×, X11 2×, Wayland 1× and Wayland 2×, and the audit (`122379471554`) passed. PR #484's run (`40619301523`) matches `main`'s.
- `m7_presentation_input_tests` passed in Wayland 2× (`122638742816`).
- Windows and macOS: not run; not in the support matrix.
- The Qt 6.8 reference compile passed on `main` (`122625478047`): compile only.
- The offscreen runs here (Qt 6.12.0) are not native cells. `presentation_input` and `presentation_batch_input` pass offscreen; the audit's `presentations` group passes offscreen.

## 10. Publication

- **ADR 0173.** The status line names this record. The §5 table carries the measured values and the verdicts. "Implementation notes (R099.e)" records the measurements, the findings, the contradictions and the decisions.
- **Roadmap.** The R099 status is "Accepted (R099.e)" (§8, decision 10): the Verify line and every §6 row hold, with P4 and P5 made.
- **Support matrix.** A row for camera paths and batch presentation output (F05), in "Exchange boundaries", not claimed in the 1.3.0 declaration, with the limits.
- **User guide.** "Presentation batches", after "Quantities, reports and schedules".
- **Mockup.** `docs/mockups/presentation.html`: 37 frames corrected to 49.
- **CHANGELOG.** Entries under Unreleased for R099.e2 (the changed-Blender Start, the frames' PNG text chunks, the request limit) and R099.e3 (the rendered camera, the render queue, the MPEG-4 refusal).

## 11. Commits and tests

- `36f1c42f` — `origin/main` at the start of this work. The product code this record ran at.
- The commit that adds this record and its publication is the head of PR `docs/r099e-presentation-acceptance`.
- `8268248d` — `origin/main` at the start of R099.e3 (PR #501, R099.e2, and PR #503 are merged). The branch `fix/r099e3-presentation-close` starts here.
- The R099.e3 commit on `fix/r099e3-presentation-close` (the PR's head, set at push).

**The 2026-10-27 work changed no product code under `src/`. R099.e2 (PR #501) and R099.e3 did:** the changed-Blender Start and the request limit (R099.e2); the frames' PNG text chunks and the worker's rendered camera (`blender_worker.py`); `RenderQueue::result()` (`render_queue.cpp`); the MPEG-4 refusal text (`presentation_store`, the batch dialog). Tests and tools added: `tests/presentation_benchmark.cpp` (new, not built by default; `CMakeLists.txt`), two blocks in `tests/presentation_batch_input_tests.cpp` (Esc closes the dialog; the chip opens a failed batch's results, and Resume: encode video writes the video), a harness fix in the same file (the quit test's Discard button is found by its standard button, because Qt 6.12 names it "Close without Saving"), and the pinned four-scene digests in `tests/camera_motion_tests.cpp` (linked to `Qt6::Core` in `CMakeLists.txt`). The changed-Blender block was written, found the defect in O6, and was removed. The mockup correction is in `docs/mockups/presentation.html`.
