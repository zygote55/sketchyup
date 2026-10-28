# Camera paths and batch presentation output

Status: **Accepted (2026-10-23)**: owner decisions 1 to 17 approved as recommended on 2026-10-23. Roadmap row
[R099](../PR_ROADMAP.md#r099) (M10, scope row [F05](../SCOPE.md)); builds on
[ADR 0099](0099-retained-render-jobs.md) / [ADR 0100](0100-render-queue-lifecycle.md)
(R070), [ADR 0104](0104-camera-motion-and-timelines.md) /
[ADR 0106](0106-immutable-animation-capture.md) /
[ADR 0107](0107-native-animation-frame-export.md) (R072), and on the closed
1.0 gate (R089). This row takes ADR number 0173 because 0172 is reserved for
the parallel STEP exchange draft. R099.e, the acceptance record ([R099e-presentation.md](../verification/R099e-presentation.md), 2026-10-27), measures §5 and finds the Verify line partly met: eight of the ten §5 rows pass, batch capture fails (1,747 ms p95 against 500 ms) and the preparation row cannot be frozen (no row to compare with). See Implementation notes (R099.e).

F05 asks for "named camera paths, export controls and batch presentation jobs;
no requirement for a general character-animation suite". R099 delivers the first
half of that: **named camera paths** in the document, **named render presets**,
and **repeatable batch output across scenes × render presets** with the immutable
snapshot provenance that R070 and R072 already establish. Movie encoding and
resume of interrupted *one-shot* exports were explicitly out of the R072
contract; the batch job defined here gets both, under its own contract.

## Context

What the tree gives us (surveyed at `34910469`, `origin/main`):

- **Saved scenes** (`src/core/scene_records.hpp`, `src/core/scene_records.cpp`):
  a `SceneRecord` (nonzero ID, unique name of at most 1024 bytes with no leading
  or trailing space and no control characters, order position) carrying a
  `SceneSnapshot` of optional camera, visibility, model style, section and solar
  study. The `SceneCamera` is a target within ±1e9 m, canonical yaw in
  [-180, 180] and pitch in [-90, 90] degrees, distance 0.05 to 1e7 m, field of
  view 5 to 120 degrees and an orthographic flag. Up to 256 scenes within an
  8 MiB record budget; `MissingSceneReferences` reports scene references whose
  scene or context is gone. A scene without a camera cannot drive a transition.
- **Camera motion** ([ADR 0104](0104-camera-motion-and-timelines.md),
  `src/core/camera_motion.hpp`, `src/core/camera_motion.cpp`):
  `CameraPathTiming` (frames per second 1–60, default 24; transition steps
  1–600, default 24; hold steps 0–600, default 12), `CameraFrame` (pose,
  `sceneIndex`, `index`, `keyframe`) and `planCameraFrames`, which plans for 2–32
  scenes and at most 240 frames: `1 + N·hold + (N−1)·transition`, each transition
  endpoint, and every requested hold step. Transitions use a cubic ease-in-out;
  the target and pitch interpolate linearly, the yaw takes the shortest arc
  (mod 360), the distance interpolates in log space, the field of view linearly;
  a change of projection (perspective to orthographic or back) is a cut at the
  keyframe, and the up vector is the fixed model Z.
- **One-shot animation export** ([ADR 0106](0106-immutable-animation-capture.md),
  [ADR 0107](0107-native-animation-frame-export.md),
  `src/integrations/animation_capture.hpp`,
  `src/integrations/animation_export.cpp`): `AnimationCapture::capture` freezes
  one document once on its owner thread and yields per-frame immutable
  `RenderSnapshot`s; `AnimationExport` renders one frame at a time through a
  `BlenderJob`, writes `frame-NNNN.png` plus a per-frame `frame-NNNN.json`
  (index, file, bytes, SHA-256, scene ID, camera, source manifest SHA-256, result
  manifest and diagnostics) and an `animation.json` progress manifest (API
  version 1, adapter `sketchyup-animation-v1`, state, document ID and revision,
  frames per second, total frames, per-frame file and hash). The contract: at
  most 240 frames, a 2 GiB output budget with a per-frame pre-check, a new
  subdirectory inside an existing parent (an existing destination is refused),
  cancellation at frame boundaries that keeps finished frames, **no resume of an
  interrupted export**, and **no movie encoding**.
- **Render snapshot and capture** (`src/integrations/render_snapshot.hpp`):
  `RenderSnapshot::capture` / `captureSavedScene` freeze the document and its
  settings (`RenderSettings`: width and height 64–4096, samples 1–1024, seed,
  Cycles or Eevee), camera and optional `RenderEnvironment` (an immutable
  Radiance RGBE equirectangular image, at most 40 MiB encoded, 4096×2048 or
  smaller) into an immutable value that may cross into workers.
  `PreparedRender` (in `src/integrations/blender_job.hpp`) exports that snapshot
  to a private directory with the versioned GLB manifest (document ID,
  revision, scene file SHA-256 and byte count, settings, camera, transfer
  losses); `BlenderJob` runs the isolated Blender 5.2 LTS worker (at most two
  jobs, 180 000 ms deadline including the CPU fallback, hash-verified PNG at most
  64 MiB, 2 MiB bounded output per attempt).
- **Durable jobs** (`src/integrations/render_store.hpp`,
  `src/integrations/render_queue.hpp`, [ADR 0099](0099-retained-render-jobs.md),
  [ADR 0100](0100-render-queue-lifecycle.md)): a locked store under
  `AppLocalDataLocation/render-jobs` with up to 8 pending and 16 retained
  jobs within 2 GiB; immutable source packages; atomic versioned records with
  attempt history and bounded logs; queue order persisted independently of
  wall-clock time; a job that was running when the process dies reopens as
  `Interrupted` with its bounded log retained and its scratch cleaned; `retry`
  (from Failed, Canceled or Interrupted, up to 1000 attempts) removes the job's
  own `image.png` and `result.json` before re-running the same capture;
  `remove` only for terminal jobs; no silent result eviction; at most two
  concurrent jobs. The desktop Jobs list
  (`src/app/render_jobs.hpp`, `src/app/render_jobs.cpp`) offers Cancel,
  "Retry capture", Remove and "Remove all finished jobs", with the provenance
  line ("from revision N · restored capture / another model session / model has
  changed since").
- **Render panel** (`src/app/render_panel.cpp`): the setup sheet (engine, camera
  from the current view or a saved scene, width 64–4096, height 64–4096,
  samples 1–1024, backend and device; values remembered in preferences), the
  one-shot animation sheet ("Choose 2–32 camera scenes in presentation order.
  Export uses the current render size, engine, lighting and device settings. Up
  to 240 PNG frames and a timing manifest are saved in a new folder. Cancel keeps
  finished frames.") and the Jobs list.
- **CLI and automation** (`src/automation/main.cpp`,
  `src/automation/catalog.cpp`): `sketchyup-cli` has a standalone
  `--export-glb <directory> --input <model> [--render-settings <file>]` (settings
  file at most 16 KiB), `--capabilities`, `--describe-command`, `--query`,
  `--mcp`, `--session`, `--recipe` and friends; **there is no CLI rendering
  today — rendering is an application-side operation.** The MCP surface holds
  113 versioned `noun.verb` command specs, including `saved_scene.create/rename/update/reorder/
  delete/recall`; a transaction batch "has no external effects in a batch"
  (`src/automation/commands.hpp`). `assistantVisible`
  (`src/automation/assistant.cpp`) keeps the in-app assistant to a deliberate
  surface, `assistant_tool_surface_tests` pins it to the 1.0.1 baseline, and
  R095.g's precedent is: MCP exposes every command, the assistant sees only the
  read-only queries whose token cost was measured offline before the baseline
  was re-pinned.
- **Storage** (`src/io/native_format.hpp`, `src/io/container.cpp`): the current
  writer emits **schema 29** (container version 2) and writes the *lowest* schema
  the content needs (26 sheet-free, 27 parameter-free, 28 terrain-free, 29 full);
  the container manifest carries `requiredFeatures` (for example
  `terrain-v1`, `drawing-sheets-v1`) and the reader checks the feature set, so a
  document whose schema a given build does not know is refused with the
  new-format error rather than silently downgraded. Schema 29 shipped in the
  1.3.0 writer.

What is missing:

- the camera path exists only as **timing** (`CameraPathTiming`) and as the
  one-shot animation sheet's scene selection; it is not a named, persisted
  record, and nothing references it by name;
- render settings are **ad hoc** (the setup sheet plus remembered preference
  values); there is no named, persisted **render preset** a batch can address;
- the animation export is one settings set, one document, no video, and no
  resume; there is no job that renders **scenes × presets**;
- the CLI and the assistant/MCP surface have no camera-path or batch
  presentation entry point.

Constraints this design works inside: no external effects inside a transaction
batch; the assistant token budget and the pinned 1.0.1 surface baseline; Blender
as an optional external executable (5.2 LTS) with its existing isolation
contract; the 2 GiB output and store budgets; the shipped schema 29 (1.3.0) and
the lowest-schema writer promise; at most two concurrent Blender jobs per
surface.

## 1. Scope

**In, for R099:**

- the named camera path record (keyframes + timing), persisted with the document;
- the named render preset record, persisted with the document;
- the batch presentation job: one capture of the document, a planned frame
  sequence, one image sequence per selected preset, keyframe stills, the
  optional video, and the versioned manifest with immutable snapshot
  provenance;
- cancellation and retry with partial output that never touches an unrelated
  job;
- the CLI surface, the MCP/assistant surface, and the desktop UI needs
  (commissioned as a future `UX_DESIGN` section, §7);
- the verification plan against fixtures (§6) and the budgets (§5).

**Out, for R099 (stays in F05's remaining items or later rows):**

- a general character-animation suite — F05 explicitly does not require one;
- non-cubic easing curves, loops and keyframe trimming in the *viewport* —
  the batch uses the existing motion core unchanged (decision 16);
- free-pose keyframes that are not saved scenes (decision 1);
- inline HDR environments in presets (decision 3);
- multi-model or remote batches; a video codec menu (one default, decision 7);
- presentation pages or sheet-driven output (that is F01's territory);
- audio; resume of the *one-shot* animation export (its ADR 0107 contract is
  unchanged — the resume contract here belongs to the batch job, decision 6).

## 2. The camera path record

**Shape.** A `CameraPathRecords` table on the `Document` (next to
`SceneRecords`, `src/core/model.hpp`), each record:

```json
{
  "id": 1,
  "name": "Tour",                       // unique, ≤ 1024 bytes, no edge spaces, no control chars
  "position": 0,
  "keyframes": [3, 7, 12],             // saved-scene IDs, 2–32, in presentation order
  "timing": { "framesPerSecond": 24, "transitionSteps": 24, "holdSteps": 12 }
}
```

The keyframe list is validated against `planCameraFrames`' existing limits: 2–32
keyframes and **at most 240 planned frames** (`1 + N·hold + (N−1)·transition`).
A record whose planned count exceeds 240 is refused at edit time with the
formula in the message, so a path can never reference a plan the motion core
would reject.

**Keyframes are saved-scene references only.** A path stores scene IDs, not
camera poses. Free poses become keyframes the existing way: capture the current
view into a new saved scene in the same edit, then reference it (decision 1).
The alternatives were: storing `SceneCamera` poses inline in the path (a second
camera source of truth, duplicated validation, and a path that could not report
missing references through the existing `MissingSceneReferences` machinery) or a
third "path pose" record type (the same duplication plus a new allocator and
history charge). The scene reference reuses the scene's name uniqueness, the
256-scene cap, the missing-reference report, and the `AnimationCapture`
contract, which already takes scene IDs: `frame(i)` keeps the frame `i`
`sceneIndex`'s scene visibility and state until the next keyframe.

**Broken paths, not broken documents.** A path whose keyframe scene is gone, or
whose scene has no camera, is reported broken with the named reasons
(`sceneMissing`, `sceneHasNoCamera` — the `captureDrawingView` precedent,
`src/integrations/render_snapshot.hpp`); the document itself loads and saves.
The batch refuses to start on a broken path and names the reasons.

**Budgets (record level).** At most **64 paths** per document within a **512 KiB**
record budget (measured at load, like the scene record budget); timing values in
the existing ranges (fps 1–60, transition steps 1–600, hold steps 0–600).
Decision 2.

**Edit and history.** Creating, editing and deleting a path is an ordinary model
edit: one undoable history entry per edit, no allocator floor beyond the
record's own ID allocator, no model mutation on read. The existing
`saved_scene.*` command shapes are the template for the new `camera_path.*`
commands (§7).

## 3. The render preset record

**Shape.** A `RenderPresetRecords` table on the `Document`, each record:

```json
{
  "id": 1,
  "name": "Final",
  "settings": { "width": 3840, "height": 2160, "samples": 256, "seed": 0,
                 "engine": "cycles" }
}
```

`settings` is exactly `RenderSettings` (width and height 64–4096, samples
1–1024, seed, Cycles or Eevee) — the same values the setup sheet already
offers, now named and persisted. **No inline environment** in v1: the preset
carries settings only (decision 3). An environment, where wanted, stays what it
is today — an explicit per-job Radiance RGBE load (at most 40 MiB encoded,
4096×2048) named in the batch request — because inlining it in up to 16
presets would be up to 640 MiB of image bytes in the document, and a path
reference to a local `.hdr` would not travel with the file.

**Budgets (record level).** At most **16 presets** per document within a
**256 KiB** record budget. Decision 4.

**Persistence with the document — and the schema bump.** The records live in
the native document, not in preferences or a sidecar file:

- *Why not preferences/QSettings:* presets and paths are part of the
  presentation setup a model carries; preferences are machine-local, are not
  undoable, and are not shared when the file is shared.
- *Why not a sidecar file:* two files that must stay in sync are a new failure
  mode (renamed, copied or deleted on one side) and no atomicity guarantee.

Consequences for storage (decision 15):

- a new payload key `presentation` (the two tables above), a new container
  feature `presentation-v1`, and **schema 30**;
- `nativeDocumentVersionFor` returns 30 for a document carrying camera paths or
  presets and stays at the current schema otherwise — the lowest-schema writer
  promise (the ADR 0163 decision-2 rule) is unchanged: a document without
  presentation records still writes schema 29;
- the reader's feature-set check gains `presentation-v1`; a 1.3.0 (schema 29)
  writer **refuses a schema 30 file with the new-format error** rather than
  silently dropping the tables;
- the record decoders get the standard validation (canonical IDs, unique names,
  budget) and a libFuzzer target alongside the other record decoders.

## 4. The batch presentation job

**Semantics.** A batch job renders, for a camera path and an ordered list of
at most **4** presets, one **image sequence per preset** over the path's
planned frame sequence (at most 240 frames, the `planCameraFrames` output).
The "scene × preset" still grid is satisfied without extra rendering: every
keyframe pose is one of the planned frames (the first frame of each hold, and
every transition endpoint), and those frames are **flagged `keyframe` in the
manifest** — the still for scene *i* under preset *j* is that preset's flagged
frame, never a second render (decision 5).

One `AnimationCapture`-style freeze covers the whole batch: the document is
captured once (document identity and revision, the capture SHA-256), and every
frame of every preset renders from that one snapshot. A preset's camera is the
frame's interpolated camera; its settings are the preset's; the environment,
when named, is the batch request's.

**Output layout (decision 9).** The destination must be a **new subdirectory
inside an existing parent** — the existing rule from the animation export and
the GLB export; an existing destination is refused, a symlink is refused.

```
<root>/<path name>/
  manifest.json                 # the batch manifest
  <preset 1>/
    frames/frame-0001.png
    frames/frame-0001.json      # the existing per-frame metadata
    ...
    video.mp4                   # optional; only for a completed batch
  <preset 2>/
    ...
```

Frame files keep the existing names (`frame-NNNN.png`, four digits, from one)
and the existing per-frame `.json` (index, file, bytes, SHA-256, scene ID,
camera, source manifest SHA-256, result manifest, diagnostics).

**The batch manifest** (`manifest.json`): API version 1, adapter
`sketchyup-presentation-v1`, the overall state (`running` / `completed` /
`canceled` / `failed`), the message, the document ID and revision, the capture
SHA-256, frames per second, the total planned frames, per preset: the
directory, its state, its settings and engine/device, its per-frame list
(index, file, SHA-256, keyframe flag) and, when present, the video record
(file, bytes, SHA-256, encoder, container and codec); the total output bytes.
The manifest is the resume ledger: a frame counts as done only when its file
exists, its bytes match, its per-frame `.json` matches and its SHA-256 matches
the manifest entry.

**The optional video (decision 7).** Off by default. For a *completed* batch,
each preset's video is encoded from the hash-verified frame bytes — not from
GPU frames — by the same isolated Blender worker (Blender 5.2 already bundles
FFmpeg; no new dependency), MP4 container. The codec default is H.264 **if the
pinned Blender's FFMPEG build exposes it — unverified; to be established in
R099.b** — with MJPG/AVI as the recorded fallback. The manifest records the
encoder string (`blender-<version>`), container, codec and the input frame
hashes. A canceled, failed or interrupted batch never writes a partial video;
the file appears only when the preset's sequence is complete and verified.
(An external FFmpeg binary was weighed as the alternative: it is a new optional
dependency with its own discovery, version and licensing surface; encoding
through the already-isolated, already-versioned worker keeps one external
executable.)

**The job model (decision 4: storage).** The batch is a long job with per-frame
partial output, which is exactly the shape the R070 store machinery exists for
(immutable source package, atomic versioned records, attempt history, bounded
logs, scratch cleanup, `Interrupted` recovery on process death). A new
`PresentationJobStore` — the same machinery in its own `AppLocalDataLocation/presentation-jobs` directory with the same limits
(8 pending, 16 retained, 2 GiB) — rather than an extension of the R070
`RenderJobStore`, so the R070 contract, its fixtures and its acceptance record
stay untouched. One **capture** per batch job: the shared `PreparedRender`
per frame, the shared `BlenderJob` per frame (180 000 ms deadline including the
CPU fallback), one frame worker at a time (the animation export's model). At
most **one active presentation batch**; it may run alongside the render
queue's up-to-two single renders, which is the status quo — the one-shot
animation export and the render queue already coexist the same way.

**Cancellation (decision 8).** At frame boundaries: the in-flight frame is
discarded, the finished frames of all presets are kept, the manifest goes to
`canceled` with the per-preset states, and no video is written. The job's
terminal state follows the job state machine, and the job stays in the store
with its bounded log.

**Retry and resume (decision 6).** A retried or recovered batch **resumes from
the first frame that is not done-and-verified**: hash-verified frames are not
re-rendered (their files are never deleted or rewritten), and the capture is the
stored one, so the resume re-renders the same model, not the live document.
This deliberately differs from the one-shot animation export, whose ADR 0107
contract says an interrupted export is not resumed: the batch's per-frame
verification makes a resumed output byte-for-byte the same job. A process death
reopens the batch as `Interrupted` with its bounded log retained and its
scratch cleaned (the R070 store recovery), and `retry` continues from the first
unverified frame. `retry` (up to 1000 attempts) never touches another job's
files: the batch's output directory is the job's own, created at enqueue, and a
retry rewrites only that job's unverified frames. **No unrelated job is ever
replaced** — the Verify line's check is run on a job enqueued around the batch
(§6).

**Refusals.** A broken path (missing keyframe scene, scene without camera), an
existing destination, a symlinked destination, a preset list over 4, a planned
frame count over 240, the 2 GiB budget (checked per frame, like the animation
export, with the remaining budget in the message), and an absent or unsupported
Blender all refuse with the reason; none of them leaves a partial directory or
a broken manifest.

## 5. Budgets

The rows marked *proposed* are the owner-decision defaults; R099.a–c's
measurements confirm them before they are frozen (the ADR 0168/0171 practice).
Measurement conditions: the pinned image, software rendering for the CPU rows
(the AMD session for the frame rows), like the R070 and R095 records.

| Operation | Budget | Measured (R099.e, Release) | Verdict |
| --- | --- | --- | --- |
| `planCameraFrames`, 32 keyframes at the maximum timing | p95 ≤ 10 ms (*proposed*; in memory, no rendering) | p50 0.008 ms, p95 0.015 ms, max 0.023 ms (200 runs; 218 frames at 24 fps, transition 7, hold 0) | **pass** |
| A path record edit (create, edit, delete) | Within the ordinary document-edit row; the 512 KiB record budget measured at load (*proposed*) | core edit p95 0.002 to 0.003 ms (100 cycles each); the tables at load: 10,571 bytes for 64 paths and 16 presets | **pass** |
| A preset record edit | Within the ordinary document-edit row; the 256 KiB record budget measured at load (*proposed*) | core edit p95 0.002 ms (100 cycles each) | **pass** |
| Batch capture (one freeze + 240-frame plan) on a 250k-triangle document | p95 ≤ 500 ms (*proposed*) | p50 1,602 ms, p95 1,747 ms (20 runs; 249,218-triangle terrain, 218 frames); encoding alone p95 1,721 ms | **fail**: owner decision 1 |
| Per-frame preparation (`PreparedRender::prepare`) | Within the existing GLB preparation row, measured per frame | p50 304 ms, p95 323 ms (6 runs over 3 frames; 92,500 triangles, 3,700 bodies); the row it cites has no number | **not frozen**: owner decision 2 |
| Per-frame render | ≤ 180 s including the CPU fallback (the `BlenderJob` deadline); PNG at most 64 MiB, hash-verified | per-frame p50 14.5 s, p95 15.9 s, max 20.8 s (48 frames, Cycles CPU, 1024×768, 32 samples; every hash verified) | **pass** |
| Whole batch, 240 frames × 4 presets at 1024×768 / 32 samples | Measured and reported (no fixed cap; progress and cancel at frame boundaries, the remaining 2 GiB budget surfaced per frame) (*proposed*) | 48 frames in 604 s (mean 12.6 s per frame); projected 960 frames about 3.4 h and about 0.65 GB; the full run was not made | **pass, reported**: owner decision 3 |
| Store | 8 pending, 16 retained, 2 GiB (the R070 limits, reused) | defaults 8, 16 and 2 GiB; enforced by `presentation_store_tests` | **pass** |
| Manifests | Per-frame `.json` at most 1 MiB (the animation export bound); the batch `manifest.json` at most 1 MiB (*proposed*) | per-frame JSON 70,208 bytes; batch manifest 14,514 bytes for 48 frames | **pass** |
| Records on load | 64 paths / 512 KiB and 16 presets / 256 KiB refuse over budget (*proposed*) | 64 paths and 16 presets decode at p95 0.42 ms; 10,571 bytes (the byte budgets cannot bind) | **pass** (owner decision 5) |

## 6. Verification plan

Fixtures (in-repo, generated, no third-party data):

- a **4-scene path fixture** with pinned per-frame pose digests (the plan's
  output is deterministic; the digests pin the motion core's behaviour
  through the batch);
- a **32-keyframe path fixture** (the maximum) and a **240-frame budget
  fixture** (the timing that reaches the cap, and the one just over it that
  must be refused);
- a **3-preset batch fixture** (Cycles and Eevee, two resolutions) against the
  R070 source package;
- the **corruption fixtures**: a blocked output destination, a blocked job
  record, and a process force-killed mid-frame (the R070 store-recovery test,
  applied to the batch store);
- the **unrelated-job fixture**: a single render job enqueued before, during
  and after the batch, verified byte-for-byte and by state at the end.

| Verify clause | Check |
| --- | --- |
| Frame count and timing match fixtures | The planned count equals `1 + N·hold + (N−1)·transition` per fixture; the over-budget path is refused with the formula; frames per second is recorded in the manifest and per-frame metadata; every keyframe flag lands on a planned keyframe frame |
| Camera poses match fixtures | Each frame's recorded camera equals the fixture digest **bit for bit** (the plan is deterministic); the rendered frame's dimensions equal the preset's and its bytes and SHA-256 match the manifest; the rendered pose (decoded from the result manifest) is within 1 µm of the recorded camera (the GLB float transfer tolerance, the R070 verification practice) |
| Provenance is immutable | The batch's document ID, revision and capture SHA-256 are pinned; destroying the original capture and editing the live document leaves the retained output matching its capture (the R070 "destroy the original capture" test); the provenance line shows the R070 wording, including "model has changed since" |
| Cancellation | Cancel at frame *k*: state `canceled`; frames before *k* kept and hash-verified; no video; the manifest complete and bounded |
| Retry and resume | A failed frame is retried and completes; a force-killed batch reopens `Interrupted` with its log retained and scratch cleaned; `retry` resumes from the first unverified frame; verified frames are not re-rendered (byte-identical, unmodified); a retried batch never rewrites another job's files (the unrelated-job fixture byte-for-byte) |
| Refusals | Broken path (named reasons), existing destination, symlinked destination, preset list over 4, over-budget frame count, over-budget output (remaining budget in the message), absent Blender. Each leaves no partial directory and no broken manifest |
| Records | Create, edit, delete round-trip through an undoable edit; Undo restores the encoding bit for bit; over-budget and malformed records refuse at load; save and reopen bit for bit |
| Schema | Schema 30 documents carry `presentation-v1` and reopen bit for bit; a document without the tables writes schema 29 (the lowest-schema promise); a 1.3.0 writer refuses a schema 30 file with the new-format error; the record decoders pass the libFuzzer target twice per input |
| Determinism | The same batch on two copies of the document gives identical planned poses, identical per-frame metadata and identical PNG bytes (same Blender, same settings, same seed) |

The acceptance record (R099.e, `docs/verification/R099e-presentation.md` with a
JSON summary) covers: the Verify line and every check above; the §5 budgets,
proposed rows confirmed by measurement before freezing; the native matrix on
X11 and Wayland at 1× and 2× for the UI slice (R099.d); sanitizers (ASan/UBSan
with leak detection and halt-on-error) over the new suites; and the installed
smoke.

## 7. Interaction surface

**CLI.** A standalone operation in the `--export-glb` mould (no rendering exists
in the CLI today; this adds the batch, not a general render command):

- `--export-presentation <request.json>`: the request file (at most 16 KiB, like
  the render settings file) names the input model, the path by name (or an
  explicit keyframe list), the ordered preset list (by name, at most 4), the
  output destination and the optional video flag; the run executes the batch to
  a terminal state in `AppLocalDataLocation/presentation-jobs` and prints the
  manifest JSON on stdout; `SIGINT` cancels at the next frame boundary
  (state `canceled`, finished frames kept);
- `--capabilities` lists the operation; `--describe-command` covers its request
  schema.

**MCP and the assistant** (decision 12, 13). New commands in the
`noun.verb` catalogue, document edits inside the transaction batch:
`camera_path.create`, `camera_path.update`, `camera_path.delete`,
`camera_path.describe`; `render_preset.create`, `render_preset.update`,
`render_preset.delete`, `render_preset.describe` — shaped on the existing
`saved_scene.*` commands. `presentation.describe` is a read-only query
(planned frame count, budget estimate, output layout, Blender availability).
**There is no presentation execution over MCP or the session in v1**: running
the batch is an external effect, and a batch "has no external effects in a
batch"; execution stays with the CLI operation and the application. MCP exposes
every command. The in-app assistant sees only the read-only queries
(`camera_path.describe`, `render_preset.describe`, `presentation.describe`) on
the R095.g precedent, with their token cost measured offline before the
1.0.1-pinned `assistant_tool_surface_tests` baseline is re-pinned; the
create/update/delete commands are hidden from the provider.

**Desktop UI — the decisions a future `UX_DESIGN` section (proposed number 17; §16 is the IFC design, PR #420)
must make**, commissioned before R099.d (decision 14):

1. **Where the path editor lives** (a section of the existing render panel
   beside the one-shot animation sheet, or a separate Presentation panel),
   including the create flow: pick the saved scenes with cameras in
   presentation order, set frames per second (1–60), transition and hold
   seconds (the existing sheet's seconds-to-steps mapping), and see the planned
   frame count live against the 240 cap;
2. **Preset management** (a named list reusing the existing setup sheet's
   width, height, samples, engine controls, plus name; duplicate and delete),
   and how a preset differs visually from the ad-hoc "current settings";
3. **The batch dialog**: choose a path, check the presets (at most 4), name the
   output destination (the existing "new folder in an existing parent" rule),
   toggle the video, and see the live budget (frame count, the 2 GiB estimate,
   Blender required with the existing setup-card state when it is absent);
4. **The Jobs row**: the batch appears in the existing Jobs list as one row
   with per-preset progress, Cancel at frame boundaries, "Retry capture"
   (resume wording), Remove, and the existing provenance line;
5. **Results**: opening the output folder, and how a completed video is
   surfaced (open in folder / system player);
6. **Errors and refusals**: the named broken-path reasons, the over-budget
   message with the remaining budget, the absent-Blender setup card (the
   existing UX_DESIGN §9 pattern);
7. **Coexistence** with the one-shot animation export (the R072 sheet stays
   unchanged; the batch is the multi-preset surface);
8. **Accessibility**: full keyboard reachability, the text-size extensions and
   screen-reader progress for the Jobs row (the R095/R096 practice).

The mockups land in `docs/mockups/presentation.html` with the section.

## 8. Out of scope

For R099:

- anything from the "Out, for R099" list in §1 (character animation, easing
  curves beyond the existing cubic, loops, free-pose keyframes, inline preset
  environments, multi-model or remote batches, the codec menu, audio);
- editing the one-shot animation export's contract (ADR 0107 stands: no resume,
  no movie);
- a presentation preview *in the viewport* (the existing scene-transition
  playback stays as is; the batch renders, it does not preview);
- packaging changes beyond the existing optional-Blender model.

## 9. Risks

- **Yaw wrap-around.** The shortest-arc yaw interpolation (mod 360, the existing
  motion core) takes the short way; a keyframe pair whose true intent is the
  long way around (for example a 350-degree pan across the ±180 boundary)
  rotates the short way. The pose digests pin the behaviour; the UX design
  should name it in the path editor's help. No change to the motion core in
  R099.
- **Long batches.** 240 frames × 4 presets at high settings is a very long job.
  Mitigations: the per-frame remaining-budget surface, frame-boundary cancel,
  resume, and the measured whole-batch row in the acceptance record.
- **The H.264 question.** Whether the pinned Blender 5.2's FFMPEG build exposes
  the H.264 encoder is unverified. Mitigation: R099.b establishes it; the
  MJPG/AVI fallback is recorded in the manifest; the owner may instead choose
  the external-FFmpeg or no-video options (decision 7).
- **Schema 30 widens the matrix.** A new feature and a new lowest-schema
  branch. Mitigation: the feature-set check, the 1.3.0 new-format refusal test,
  the record decoder fuzz target, and the round-trip fixtures — the same
  pattern as `terrain-v1`.
- **Broken paths are easy to create and hard to notice** (delete a keyframe's
  scene; the path survives, broken). Mitigation: the named reasons in
  `presentation.describe` and the batch refusal, plus the UI's broken-state
  display (UX decision 6).
- **Assistant token budget.** Three new visible queries push the pinned
  baseline. Mitigation: offline measurement before the re-pin, the R095.g
  procedure.
- **Two long-job surfaces coexisting** (batch and render queue) can mean two
  Blender processes plus a one-shot animation export at once. That is the
  status quo (the animation export and the render queue already coexist); the
  batch adds at most one active worker, and the store limits keep the durable
  side bounded.

## Implementation split

Each slice is an independently testable review unit. The UX design must be
approved before R099.d.

- **R099.a — Records and schema.** Requires R070 (the store machinery) and the
  1.0 gate. Delivers: the `CameraPathRecords` and `RenderPresetRecords` tables
  with validation, the missing-reference and no-camera reports, the ID
  allocators and history charge; schema 30, `presentation-v1`, the
  lowest-schema branch and the 1.3.0 refusal; the record decoders with their
  libFuzzer target; the API schema and round-trip fixtures. No rendering.
- **R099.b — The batch engine.** Requires R099.a. Delivers: the
  `PresentationJobStore` (the R070 machinery in
  `AppLocalDataLocation/presentation-jobs`), the one-capture frame-sequence
  render, the per-frame output and metadata, the batch manifest, cancellation
  at frame boundaries, resume-on-retry, the refusals, and the `--export-presentation`
  CLI operation with its SIGINT cancel. Establishes the H.264 availability
  question. No UI.
- **R099.c — Video and the command surface.** Requires R099.b. Delivers: the
  optional video encoding (or the owner's fallback from decision 7) from the
  hash-verified frames with the manifest record; the `camera_path.*` and
  `render_preset.*` commands and `presentation.describe`; the assistant
  visibility change with the measured token cost and the re-pinned baseline.
- **R099.d — Desktop UI.** Requires R099.c and the approved `UX_DESIGN`
  section 17. The path editor, preset management, the batch dialog, the Jobs
  row, the results and error states, and the input test suite with the
  accessibility, keyboard-reachability and text-size extensions.
- **R099.e — Acceptance record.** `docs/verification/R099e-presentation.md` with
  a JSON summary. Covers: the Verify line and every check of §6; the §5
  budgets, proposed rows confirmed before freezing; the native matrix on X11
  and Wayland at 1× and 2×; sanitizers and the fuzzing; the installed smoke.

## Owner decisions

Each decision lists the **recommended default** and a one-line reason. The owner
decides; nothing here is decided yet.

1. **Keyframes: saved-scene references only.** Free poses are captured into a
   new saved scene in the same edit, then referenced. *Recommended: yes —* one
   camera source of truth; reuses scene validation, unique names and the
   missing-reference report, and matches `AnimationCapture`, which already takes
   scene IDs.
2. **Path budgets: 64 paths per document, 2–32 keyframes, at most 240 planned
   frames, 512 KiB record budget.** *Recommended: adopt —* the keyframe and
   frame limits are `planCameraFrames`' existing limits, so the record can
   never name a plan the motion core would reject.
3. **Presets carry settings only (no inline environment) in v1.** The
   environment stays an explicit per-batch Radiance load, as today.
   *Recommended: settings only —* inlining would put up to 640 MiB of image
   bytes in the document; a path reference would not travel with the file.
4. **Preset budgets: 16 presets per document, 256 KiB record budget; the job
   storage is a new `PresentationJobStore` in its own `presentation-jobs`
   directory with the R070 limits.** *Recommended: adopt —* the batch's
   partial-output shape is the R070 store's, and a parallel store leaves the
   R070 contract, fixtures and acceptance record untouched.
5. **Batch semantics: one capture, one image sequence per preset; the
   keyframe stills are flagged frames, never extra renders.** *Recommended —*
   it satisfies the scene × preset grid without doubling the work, and one
   capture keeps every preset's output on the same immutable snapshot.
6. **Retry resumes from the first unverified frame (hash-verified frames are
   kept, the stored capture is re-used).** *Recommended: resume —* per-frame
   verification makes the resumed output byte-for-byte the same job; this
   differs from the one-shot animation export on purpose (ADR 0107 stands).
7. **Video: optional, default off, encoded by the isolated Blender worker from
   the hash-verified frames (no new dependency); H.264 if the pinned Blender
   exposes it (to be established in R099.b) with MJPG/AVI fallback; an
   incomplete batch never writes a partial video.** Alternatives: external
   FFmpeg (a new optional dependency) or no video. *Recommended: the
   Blender-encoded default —* it keeps one external executable and one
   isolation contract.
8. **Cancellation at frame boundaries; a canceled batch keeps finished frames,
   writes no video, and ends `canceled` in the store with its log.**
   *Recommended —* the animation export's proven contract, extended to the
   manifest.
9. **Output layout: a new subdirectory in an existing parent;
   `<root>/<path>/<preset>/frames/frame-NNNN.png` and `.json`, the optional
   `video.mp4`, and the top-level `manifest.json`.** *Recommended —* the
   existing destination rules and the animation export's naming, with one
   manifest per batch.
10. **Job budgets: at most 4 presets per batch, at most 240 frames per preset,
    a 2 GiB output budget, at most one active batch, a per-frame deadline of
    180 s including the CPU fallback; the store keeps the R070 limits (8
    pending, 16 retained, 2 GiB).** *Recommended: adopt —* each bound is an
    existing bound (frame cap, output cap, `BlenderJob` deadline, store
    limits) rather than a new one.
11. **CLI: a standalone `--export-presentation <request.json>` (request at most
    16 KiB, manifest on stdout, SIGINT cancels at the next frame boundary);
    listed in `--capabilities` and `--describe-command`.** *Recommended —* it
    mirrors `--export-glb`, the CLI's only other standalone export, and adds no
    interactive surface to a non-interactive tool.
12. **MCP: every new command and `presentation.describe` exposed; no
    presentation execution over MCP or the session in v1.** *Recommended —* a
    transaction batch has no external effects, and the R072 render is an
    application-side operation; the CLI already carries headless execution.
13. **Assistant: only `camera_path.describe`, `render_preset.describe` and
    `presentation.describe` visible; the token cost measured offline before the
    1.0.1-pinned baseline is re-pinned.** *Recommended —* the R095.g
    precedent, which is the only procedure that has kept the provider surface
    inside its token budget.
14. **Desktop UX: commission `UX_DESIGN` section 17 (the §7 questions, with
    mockups in `docs/mockups/presentation.html`) before R099.d; the one-shot
    animation sheet stays unchanged.** *Recommended —* the R096 practice: the
    interaction is approved before the UI slice, and the R072 surface is not
    rewritten.
15. **Persistence: the records live in the native document; schema 30 with the
    `presentation-v1` feature; the lowest-schema writer promise unchanged; a
    1.3.0 writer refuses schema 30 with the new-format error.** *Recommended —*
    the presentation setup belongs to the model it moves, undoably and
    shareably; the container's feature-set check already makes the refusal
    safe.
16. **Motion: the existing cubic ease-in-out, shortest-arc yaw and
    projection cut, unchanged; no new curves or loops in v1.** *Recommended —*
    the digests pin the behaviour, and the batch stays a consumer of the
    motion core, not a second implementer.
17. **Scope gate: R099 ships with no viewport preview of the path and no
    changes to the ADR 0107 one-shot export contract.** *Recommended —* each
    would be a second contract on the same surface; F05's remaining items take
    them in later rows.

## Implementation notes (R099.a)

Recorded 2026-10-25 for the records slice, branch `feat/r099a-presentation-records`.

- **Records.** `CameraPathRecords` and `RenderPresetRecords` sit on `Document`
  (`src/core/presentation_records.*`) with the §2 and §3 shapes. Create, update and delete
  are ordinary undoable edits (`src/core/presentation.*`), each one history entry and
  charged its record bytes. Creating a path, or changing its keyframes, requires each
  keyframe to name a saved scene with a camera. A path whose scene is later deleted, or
  loses its camera, still loads; `brokenCameraPaths` reports it with `sceneMissing` and
  `sceneHasNoCamera`. Renaming or retiming a broken path is allowed.
- **Order and names.** A path has a unique position, as scenes do. A preset has none, since
  §3's shape has none. Names follow the saved-scene rule within each table.
- **Keyframe repeats.** The ADR does not say whether a scene may repeat in a path. This slice
  allows it (a loop back to a view); the motion core accepts it.
- **`RenderSettings` moved to core.** §3 says a preset carries "exactly `RenderSettings`".
  That type lived in `integrations/render_snapshot.hpp`, which core cannot include, so it now
  lives in `core/render_settings.hpp` unchanged, with the same bounds check.
- **Schema 30 and the lowest-schema rule (contradiction with §3, kept by design).** §3 says
  "a document without presentation records still writes schema 29". Read literally, that
  breaks the lowest-schema rule (ADR 0163 decision 2) for documents without terrain, which
  write 26 to 28 today. This slice keeps the rule: a document without presentation records
  writes what it wrote before (26 to 29 by content), and a terrain document still writes 29.
  Tests pin both cases.
- **Cumulative features.** Schema 30 keeps the earlier features, so a document with paths
  or presets also carries `terrain-v1` even with no terrain, as parameter documents carry
  `drawing-sheets-v1`. The reader's terrain flag is implied by `presentation-v1`.
- **Allocator floors** `nextCameraPathId` and `nextRenderPresetId` appear in the container
  manifest at schema 30 only, and never raise the schema.
- **Byte budgets cannot bind (measured).** The longest path record is 1,432 bytes and the
  longest preset 1,152 bytes. Sixty-four paths total at most 91,648 bytes against the
  512 KiB budget; sixteen presets total at most 18,432 bytes against 256 KiB. The count caps
  bind first. The byte checks stay as guards. Owner: drop the byte budgets or lower them;
  nothing else changes.
- **Released-reader refusal (measured).** The released 1.2.0 CLI (reads schema 28) refuses
  the schema-30 fixture `tests/fixtures/presentation-v30.sketchyup` with exit 1,
  `{"code":"INVALID_OPERATION","error":"Unsupported container field","status":"failed"}`,
  and the file is byte-identical afterwards. That is not the newer-format wording §6 names:
  the container's field check fires first. The released **1.3.0** CLI (`sketchyup-cli 1.3.0`, the
  published `sketchyup-1.3.0-1-x86_64.pkg.tar.zst`, sha256 `5f5c0cf8…a833b`) was run in review on
  `presentation-v30.sketchyup` with `--validate-native`: exit 1 with the same
  `Unsupported container field` refusal, and the file byte-identical afterwards. Both released
  readers refuse schema 30 safely; neither uses the newer-format wording §6 names.
- **Package acceptance.** `packaging/arch/verify-package.sh` refuses every fixture newer than
  the prior CLI reads, so the new fixture is checked whenever `SKETCHYUP_PRIOR_RELEASE_PACKAGE`
  is set. The script's prior is still 1.2.0-1 (reads 28). The next format release needs
  1.3.0-1 added to `released_packages` and `known_release_sha256` before it runs; the version
  in `CMakeLists.txt` is still 1.3.0 and was not changed here.
- **Tests.** `presentation_record_tests` covers the bounds, references, allocators, undo and
  redo, the history charge and the count limits. `presentation_io_tests` covers schema
  selection, the round trip, refusals, malformed records, the fixture and the prior-release
  hook. `native_format_tests`, `component_parameter_io_tests`, `drawing_sheet_io_tests` and
  `terrain_io_tests` named schema 29 as the newest; they now name 30 and skip the new fixture
  where a loop assumes older schemas. Their other assertions are unchanged.
- **Fuzzing.** `parser_fuzz_presentation` (format `presentation`) decodes each input twice and
  requires equal results, checks the bounds of an accepted object, and requires a canonical
  re-encode. Its seeds: a tour with a broken path and two presets, an empty table, and three
  rejected inputs (one keyframe, a zero keyframe, a 63-pixel preset).

## Implementation notes (R099.b)

Recorded 2026-10-25 for the batch engine, branch `feat/r099b-batch-engine`.

- **Store.** `PresentationJobStore` (`src/integrations/presentation_store.*`) is R070's machinery
  in `AppLocalDataLocation/presentation-jobs`, with the R070 limits (8 pending, 16 retained,
  2 GiB). Each job owns `<id>/job.json` and `<id>/capture.sketchyup`. A changed capture is
  refused on every use, and a damaged job record reopens as failed. Reopening a store with a
  Running or Canceling batch marks it `Interrupted`, keeps its bounded worker log and removes its
  scratch. One batch at a time may be Running or Canceling. The store never creates or removes
  the output folder, so removing a job leaves the user's files in place.
- **Capture SHA-256.** §4 names the capture hash without defining it. This slice defines it as
  the SHA-256 of `encodeDocument` of the frozen snapshot. The same bytes are stored in the job and
  checked on resume.
- **One capture.** `PresentationCapture` freezes the document once, on the owner thread. Every
  frame of every preset renders through `RenderSnapshot::captureSavedScene` from that snapshot,
  with the preset's settings and the frame's camera. A live edit made while a batch runs changes
  no frame: the per-frame source manifest hashes match an unedited twin (test
  `presentation_export`).
- **Output layout.** `<output>/manifest.json` and `<output>/NN-<preset>/frames/frame-NNNN.png`
  with `frame-NNNN.json`. `<output>` is the new folder the user names, which is the `<path name>`
  level of §4's tree; no second folder is created. `NN` numbers the presets, and the name keeps
  ASCII letters, digits, `-` and `_` (48 characters at most). Frame files are 1-based, as §4 says;
  the JSON `index` is 0-based, as in the animation export. Each frame's JSON records the index,
  file, bytes, SHA-256, preset, keyframe flag, scene ID, camera, source manifest SHA-256, result
  manifest and diagnostics.
- **Manifest.** API 1, adapter `sketchyup-presentation-v1`, published after every frame. Its
  `state` uses the job state names, so it also carries `queued` and `interrupted`; §4 lists only
  running, completed, canceled and failed. Each preset lists its verified frames (index, file,
  SHA-256, keyframe flag), its settings, its state, and the device of its last frame. No video
  record is written; that is R099.c.
- **Resume.** A frame counts as done only when its PNG and its JSON agree with the manifest entry
  (file name, SHA-256, byte count and index). Retry renders the other frames, removing their stale
  files first. A verified frame is never opened for writing. A damaged or foreign manifest refuses
  the retry and leaves the finished frames as they are. Tested with exact worker launch counts:
  retry after a two-frame cancel launches ten workers; a corrupted frame with four frames kept
  launches nine.
- **Cancellation.** `cancel()`, and SIGINT in the CLI, stop at the next frame boundary. An in-flight
  worker is killed and its frame is discarded (tested with a worker stopped mid-frame). The store
  keeps the batch `Canceled`, and the manifest says `canceled`.
- **Refusals (each before any folder or job exists).**
  - A broken path names the missing scenes or the scene without a camera.
  - The R099.a plan limits apply: one to four presets, two to thirty-two keyframes and 240 planned
    frames.
  - Output budget: checked per frame, as §4 says. A frame that would pass 2 GiB fails the batch
    with the remaining budget in the message, writes nothing, and keeps the finished frames. There
    is no up-front refusal on the worst-case bound (changed in review; see contradiction 3).
  - Disk: free space must cover the worst-case bound `min(64 MiB, w·h·5 + 1 MiB) + 1 MiB` per
    frame, capped at the 2 GiB budget, plus the capture and a 256 MiB reserve (`QStorageInfo`).
  - Blender: a named executable must be absolute and exist; with none named, `blender` must be on
    `PATH`.
  - Destination: it must not exist, must not be a link, must be absolute, and its parent must
    exist.
  - Device: an Eevee preset needs an OpenGL device, and a Cycles preset needs any other device. One
    batch therefore uses one device family.
  - A second running batch is refused.
- **Not in this slice.** No video. The request has no environment field, because §7's request
  does not name one. The CLI cannot resume a batch; retry is an engine operation, and the Jobs row
  that exposes it is R099.d. The CLI error codes are coarse: refusals are `INVALID_REQUEST`, a
  request over 16 KiB is `LIMIT_EXCEEDED`, and an unreadable request is `INPUT_ERROR`.
- **H.264 (§9 risk, decision 7): available.** Measured with the local Blender 5.2.2 LTS (Arch
  `17:5.2.2-3`, FFmpeg 9.0.2). CI installs `17:5.2.2-2` (`docs/RELEASE_NOTES.md`), which has not
  been run here. Blender's FFMPEG codec list includes `H264`. Six 320×240 frames written through
  `media_type = VIDEO`, `file_format = FFMPEG`, the MPEG-4 container and the `H264` codec gave a
  2,825-byte MP4. `ffprobe` reports `codec_name=h264`, High profile, `yuv420p`, six frames, and an
  x264 encoder string (core 165). The H.264 decision in §4 therefore stands, subject to the CI
  Blender package confirming it in R099.c.
- **MJPG/AVI fallback (contradicts §4): unavailable.** Blender 5.2.2's codec list has no MJPEG.
  Selecting it fails with `enum "MJPEG" not found`. The measured alternatives are MPEG-4 Part 2 in
  MP4 (`mpeg4`, six frames, 320×240) and FFV1 in MKV (`ffv1`, lossless). For R099.c this note
  recommends MPEG-4 Part 2 in MP4, which is lossy and plays everywhere; FFV1 is the lossless
  option. The owner chooses the fallback. No code in R099.b depends on it.
- **Budgets measured against §5.** Measured 2026-10-25 on this machine (Arch, Blender 5.2.2-3, 22
  cores, 30 GB), not the pinned image. The libraries came from the `dev` preset, which is a **Debug**
  build, so library timings are upper bounds. Release timings are not measured. The Blender frame
  times are real. Measurement harness: a throwaway program linked against the dev static libraries,
  not committed.
  - *`planCameraFrames`, 32 keyframes at the largest timing the frame cap admits.* The planner itself
    refuses more than 240 frames, so the §5 row's "maximum timing" is read as the largest timing
    that stays at or under 240. Transition 7 with hold 0 gives 218 frames, p50 0.13 ms and p95 0.14
    ms. Transition 6 with hold 1 gives 219 frames, p95 0.13 ms. Budget p95 ≤ 10 ms: **met**.
  - *Batch capture (one freeze, plan and hash) on a large document.* The §5 row names a 250,000-triangle
    document, which the model cannot hold: the document's limit is 100,000 unique vertices, and
    separate convex faces give about one triangle per vertex. The largest document measured is 3,700
    bodies of 27-gon faces: 92,500 triangles, 99,900 vertices, 141 planned frames. Capture p50 888 ms
    and p95 1,185 ms, with `encodeDocument` (the capture hash and stored bytes) taking p50 810 ms of
    that. Budget p95 ≤ 500 ms: **not met at this size, in a Debug build**. Scaling would be worse at
    250,000 triangles. The cost is in the native encoding; see the contradictions below.
  - *Per-frame preparation (`PreparedRender::prepare`) on the same 92,500-triangle document, 1024×768.*
    The scene package is 8.65 MB. p50 2.07 s and maximum 3.31 s over three runs (Debug). The §5 row
    defers to the existing GLB preparation row, which is not re-measured here.
  - *Per-frame render, real Blender, Cycles CPU, 1024×768, 32 samples, 92,500 triangles.* A CLI batch
    of four frames (explicit keyframes, transition 3, hold 0) completed in 108 s wall, including
    load, capture and preparation. That is about 26 s per frame. Each PNG is 462–471 KB, far below the
    64 MiB cap, and each frame's hash verified. Budget ≤ 180 s: **met**.
  - *Whole batch, 240 frames × 4 presets at 1024×768 / 32 samples.* **Not run.** Extrapolated from
    the per-frame time: 960 frames at about 26 s is roughly seven hours on this machine. The actual
    output would be about 0.65 GB (per frame, 0.47 MB PNG and 0.21 MB JSON). The worst-case bound
    is 5.75 MiB per frame (5.4 GiB for the batch); as first written the engine refused the batch up
    front on that bound. In review the up-front refusal was removed, so the batch now starts and the
    2 GiB budget is checked per frame.
  - *Store.* 8 pending, 16 retained and 2 GiB, as §5 requires. Enforced and tested.
  - *Manifests.* The per-frame JSON is about 207 KB. Its diagnostics and result repeat the worker's
    attempt log (about 67 KB each, three copies), which the animation export shares. The 1 MiB bound
    holds with about 5× headroom. The batch manifest is 2,034 bytes for four frames, about 134 bytes
    per frame entry, so 960 entries stay well under 1 MiB.
  - *Records on load.* Unchanged from R099.a.
- **Contradictions with the ADR, for the owner.**
  1. The 250,000-triangle capture row cannot be built under the 100,000 unique-vertex limit. The
     row should name a vertex-based size, or the limit should be reconsidered.
  2. Capture is over its 500 ms row at 92,500 triangles, in a Debug build, and the cost is the
     native encoding that doubles as the capture hash. Options: measure a release build and keep
     the row; or derive the provenance hash from a cheaper digest. Not changed here.
  3. *Resolved in review.* An up-front refusal on the animation export's worst-case bound (about
     5 bytes per pixel, about eight times a real PNG) refused the §5 whole-batch case (240 × 4 at
     1024×768), although the real output is about 0.65 GB. §4 asks for a per-frame check, so the
     up-front refusal was removed; the per-frame check, with the remaining budget in its message,
     is tested with a lowered limit (`setOutputLimit`).
  4. A SIGTERM during a batch leaves the job `Running` until the store is next opened, which then
     marks it `Interrupted`. Ctrl-C (SIGINT) cancels cleanly; a hard kill does not.

## Implementation notes (R099.c)

Recorded 2026-10-26 for branch `feat/r099c-video-commands`, on top of R099.b.

- **Video (decision 7).** The isolated worker gains an `encode` operation. It reads each
  frame of a preset, hashes it against the request, and copies the verified bytes into its
  private attempt directory, so Blender encodes only those copies. The encode is a video
  sequence editor image strip written as FFMPEG in an MPEG-4 container with the H.264 codec
  (constant rate factor MEDIUM, GOOD preset, no audio). `BlenderJob::encode` verifies the
  result before anything is published: the file hash and size, the codec, the MP4 container,
  the frame count read from the `stsz` sample table in the `moov` box, and an `avc1` sample
  entry with an `avcC` configuration. Only then is `video.mp4` written, with `writeNew`. The
  engine hashes each frame again on disk before its preset encodes, so a frame that changed
  after it was verified is named and not encoded.
- **H.264 at run time.** The worker checks the codec list of the running Blender. Without
  `H264` it refuses with `codec_unavailable`, and the batch ends `failed` with the message
  "Video not written (codec_unavailable): ... The finished frames are kept." No second encode
  starts. A frame that fails its hash refuses with `invalid_frame`. There is no fallback
  codec.
- **Manifest.** The top level gains `videoRequested`. Each preset with a verified video gains
  `video`: `file`, `bytes`, `sha256`, `encoder` (`blender-5.2.2`), `container` (`mp4`),
  `codec` (`h264`), `frames`, `framesPerSecond` and `inputSha256` (the frame hashes in
  order). The batch total counts the videos.
- **Partial batches.** A cancel during an encode, a refusal and a failure write no video. A
  retry checks the videos that exist and encodes the rest without rendering a verified
  frame. The plan stores the request's `video` flag, and a job stored before R099.c reads as
  no video. A video needs even preset dimensions; an odd size is refused when the plan is
  validated.
- **Budget.** A video counts against the 2 GiB output budget, checked when it is written.
  The disk check reserves one frame bound per frame for each video.
- **CLI.** `--export-presentation` accepts `"video": true`. `--describe-command` documents
  the field, and `--capabilities` lists it.
- **Measured H.264 answer.** On this machine, Blender 5.2.2 LTS (Arch `17:5.2.2-3`, FFmpeg
  9.0.2), the worker wrote `codec_name=h264`, High profile, `yuv420p`, the planned frame
  count and 24/1 fps. The real test, `presentation_export_tests --real` with
  `SKETCHYUP_BLENDER_TEST=/usr/bin/blender`, passed here. **Not run:** CI installs
  `17:5.2.2-2`, which this machine does not have. The CI Blender job runs
  `presentation_export_real` with ffmpeg installed, so that job is the first check of the
  CI package.
- **Commands (decision 12).** `camera_path.create`, `camera_path.update`,
  `camera_path.delete`, `render_preset.create`, `render_preset.update` and
  `render_preset.delete` are catalogue commands. Each is one undoable edit through the staged
  batch. Keyframes must be saved scenes with cameras. A broken path can still be renamed, and
  an update with no change is a refusal. The three `*.describe` names are inspection queries,
  shaped on `saved_scene.describe`. `presentation.describe` reports the planned frames per
  preset, the worst-case output against the 2 GiB limit, each preset's folder, whether a
  `blender` executable is on the path, and every refusal reason. It renders nothing. No
  presentation execution exists as a command or a query.
- **Gap fixed in the shared change set.** The snapshot diff and the staging diff did not
  carry camera paths or render presets. A camera-path-only batch failed with "Batch has no
  committed changes", and a mixed batch dropped the path without a message. Both diffs now
  carry them.
- **Assistant (decision 13).** The assistant sees the three queries as top-level tools, and
  no camera path, render preset or presentation command. The descriptions give units and
  the refusals.
- **Token measurement (R095.g method).** The first provider request of each
  `assistant_tool_surface_tests` surface was written with
  `assistant_tool_surface_tests --requests <dir>`, in the ChatGPT-plan wire form. Tokens were
  counted with `tiktoken` `o200k_base`, the R095.g proxy. The before surface was rebuilt from
  the after request by removing the three queries and their `transaction.inspect` choices.
  That rebuild reproduces every pinned `chatGptPlanToolsBytes` exactly, which checks the
  method.

  | Surface | Tools | Wire tool bytes | Tool tokens | Request tokens |
  | --- | --- | --- | --- | --- |
  | inspection-only | 30 → 33 | 25,000 → 27,505 (+2,505) | 6,830 → 7,476 (+646) | 8,154 → 8,800 (+646) |
  | room | 37 → 40 | 51,424 → 55,452 (+4,028) | 14,282 → 15,362 (+1,080) | 15,606 → 16,686 (+1,080) |
  | resize | 37 → 40 | 51,137 → 55,165 (+4,028) | 14,191 → 15,271 (+1,080) | 15,515 → 16,595 (+1,080) |
  | advanced | 37 → 40 | 54,392 → 58,420 (+4,028) | 15,116 → 16,196 (+1,080) | 16,440 → 17,520 (+1,080) |
  | site | 37 → 40 | 52,244 → 56,272 (+4,028) | 14,484 → 15,564 (+1,080) | 15,808 → 16,888 (+1,080) |
  | every command + clarification | 38 → 41 | 135,375 → 139,403 (+4,028) | 36,581 → 37,661 (+1,080) | 37,905 → 38,985 (+1,080) |

  Over a 16-turn task the edit surfaces add 16 × 1,080 = 17,280 tokens, 3.3% of the 524,288
  token per-task budget. R095.g recorded +1,029 for the same surfaces. This slice adds more
  because its three descriptions are longer. The per-task budget is the one that applies:
  the surface test has no token constant. The cost is inside that budget, so the budget was
  not raised.
- **Pinning.** `tests/assistant-tool-surface-baseline.json` is re-pinned, with these figures
  in `source.repinned`. The R095.g record is kept under `previousRepins`. Every visible tool
  list changed, so the frozen corpus gains version 6 (`tests/provider-release-corpus-v6.json`).
  Its tasks, prompts, profiles, limits and instructions are the v5 values, and only each
  surface's `toolsSha256` moved. The trial harness, the corpus test chain and the scorer
  accept version 6. A live version 6 run is required before the next release and has not
  been made.
- **Contradictions with the ADR, for the owner.**
  1. §7 lists the `*.describe` names in the `noun.verb` catalogue. Decision 13 calls them
     read-only queries, and `saved_scene.describe` is a query. They are implemented as
     queries, and they reach MCP and the assistant through the inspection registry.
  2. §4 says the codec default is H.264 "if the pinned Blender's FFMPEG build exposes it".
     R099.b measured that it does on the local 5.2.2 build. The CI package still has to
     confirm it (above).
- **Open owner questions.**
  1. **Fallback codec.** R099.b found no MJPEG in Blender 5.2.2, so no fallback is
     implemented. A Blender without H.264 refuses the video. The owner chooses among MPEG-4
     Part 2 in MP4 (`mpeg4`, lossy, plays everywhere), FFV1 in MKV (`ffv1`, lossless), and no
     video fallback.
     **Decided by the owner on 2026-10-27:** MPEG-4 Part 2 in MP4, offered only as an explicit
     choice when H.264 is missing (UX_DESIGN §17.12, choice 1). R099.d builds it.
  2. **Early refusal.** A Blender without H.264 is found only after every frame has rendered.
     A probe at batch start would refuse before any frame. The brief placed the refusal at
     run time, so no probe was built, and a long batch can finish before the refusal.
- **Not in this slice.** The desktop UI (R099.d). No codec probe before a run. The
  `presentation.describe` Blender check reports whether a `blender` executable is on the
  path, not its version or codec. The codec is checked when a video encodes. The worst-case
  output in `presentation.describe` is an estimate, not a measurement. The whole-batch video
  time is not measured, and the real test uses 64 by 64 frames at one sample.

## Implementation notes (R099.d1)

Recorded 2026-10-27 for branch `feat/r099d1-presentation-engine`, on origin/main 794cf12c. The
engine and worker the presentation UI needs. No UI.

- **Codec probe (UX_DESIGN 17.5, gap 1).** The worker's `probe` reports `videoCodecs`, the sorted
  FFmpeg identifiers of the build. `BlenderJob::videoCodecs()` returns them validated: at most 64
  names of 1 to 32 capitals, digits or underscores. An absent field leaves the list unset; a
  malformed one fails the probe as `INVALID_RESULT`. `checkPresentationVideo` runs a fresh CPU
  probe and blocks its caller until it ends, at most the probe's 15 s deadline.
  `PresentationExport::start` and `retry` run it before the job is enqueued, so a refused video
  creates no folder, no job and no frame. A missing chosen codec refuses with the 17.5 wording
  ("Video is not available in this Blender: its FFmpeg build has no H.264 encoder. No frames were
  rendered. Clear Write video to render the frames only."). A probe that does not finish, or does
  not report a list, refuses too, and names the reason.
- **Describe.** `presentation.describe` reports `blender.videoEncoders` whenever a Blender is found:
  `checked`, `h264` and `mpeg4`, or `checked: false` with a message. The input schema and the
  description are unchanged, so the assistant's pinned tool surface does not move. The probe is
  installed at startup by the CLI and the desktop (`installPresentationVideoProbe`), so the query
  blocks for the probe's time while a Blender starts. Without an installed probe the video is
  reported as not checked. A video plan whose default codec is missing is refused in the describe
  too, and names the MPEG-4 request as the alternative.
- **Codec choice (UX_DESIGN 17.12 choice 1).** `PresentationPlan::videoCodec` is `h264` (the
  default) or `mpeg4` (MPEG-4 Part 2 in MP4). The request field `videoCodec` is accepted only with
  `video: true`, and the CLI's `--describe-command` and `--capabilities` document it. The worker
  encodes the named codec and never switches; a missing encoder is `codec_unavailable`. The codec
  reaches the worker in the encode request (the first run of this slice did not send it, and the
  fake worker's default hid that until the real-Blender test). The verifier reads the first sample
  entry of the video track: `avc1` with `avcC` for H.264, and `mp4v` with `esds` and no `avcC` for
  MPEG-4. The video record's `codec` is `h264` or `mpeg4`, and the manifest has a top-level
  `videoCodec`. The job record stores the codec; a job stored before this slice reads as H.264.
  This replaces the R099.b and R099.c "no fallback" position with the owner's explicit choice.
- **Engine text (UX_DESIGN 17.11, gaps 3 and 8).** Frame and video failures name the frame or the
  preset and the reason, and the worker's code is kept in the stored report, not in the sentence:
  "Video not written for Draft: This Blender's FFmpeg build has no H.264 encoder. The finished
  frames are kept." The frame limit has one message, the record's, with its formula
  (`cameraPathFrameLimitMessage`); the batch and `presentation.describe` both use it, and the
  planner's "Camera path exceeds 240 captured frames" no longer reaches a person. The interrupted
  message reads "SketchyUp stopped before this batch finished. Resume continues at the first
  unverified frame." (it said "Retry", which the UI does not use).
- **Per-frame metadata (UX_DESIGN 17.11, gap 7).** The attempt log is kept once, under
  `diagnostics.attempts`. The duplicate `diagnostics.logTail` and the worker report copied into
  `diagnostics` are gone; `result` keeps the worker's report without `attempts`. Measured on real
  Blender 5.2.2 with the v30 fixture (2 frames, `Final` preset, 1920 x 1080, 256 samples; attempt
  log 889 bytes): the per-frame JSON was **8,868 bytes before** and **4,202 bytes after**. At the
  64 KiB log cap, a projection from the same structure gives **138,103 bytes before** and
  **68,807 after**; the fake-worker batch test measured 68,450 bytes after, with the same cap.
  Every field the manifest and the retry check read (`file`, `sha256`, `bytes`, `index`, `result.device`)
  is unchanged.
- **Stop (UX_DESIGN 17.12 choice 5).** `PresentationExport::stop()` kills the in-flight worker,
  discards its frame and records the batch `Interrupted` at once, keeping every verified frame.
  Closing the engine mid-batch does the same (the destructor), so the record reads interrupted before
  any store reopens; a crash still leaves it running until reconciliation. A SIGTERM to
  `sketchyup-cli --export-presentation` now stops this way and exits 1; SIGINT still cancels
  (exit 130). Resume works from the stopped batch with its frames kept (tested).
- **Tests.** Fake Blender (`blender_job_tests`, `presentation_export_tests`,
  `presentation_cli_tests`, `presentation_command_tests`, `presentation_store_tests`): the probe's
  codec field present, absent and malformed; an explicit MPEG-4 encode; no silent fallback; the
  sample-entry check in both directions; `codec_unavailable` at start and at encode; the 17.5
  wording exactly; the frame-limit message exactly; no codes in the batch text; one copy of the log
  and the 80 KiB bound; stop and close leave `Interrupted`; retry after stop renders the other eleven
  frames; the SIGTERM CLI stop. Real Blender 5.2.2 (`presentation_export_tests --real`, with
  `ffprobe`): an H.264 MP4 reads back `h264,6`, and an MPEG-4 MP4 reads back `mpeg4,6`, six frames
  per preset. `blender_job_tests --real` passed, including the real probe. `presentation.describe`
  through the CLI reported `h264: true` and `mpeg4: true` on this machine.
- **Not run.** The CI Blender package (17:5.2.2-2) and the sanitizer jobs were not run locally.
  The desktop entry point (`src/app/main.cpp`) compiles; the desktop executable was not linked
  here.
- **Contradictions with the ADR and UX design, for the owner.**
  1. §5 and UX_DESIGN 17.11 gap 7 say the per-frame JSON is about 207 KB, with the log in three
     copies. The measured frame was 8,868 bytes with two copies (`result.attempts` and
     `diagnostics.logTail`); the 207 KB figure was not reproduced. The 64 KiB-log projection is
     138 KB before and 69 KB after.
  2. `animation_export.cpp` still puts the full `BlenderJob` report, with its log, under
     `diagnostics` in the one-shot export (ADR 0107 unchanged). The duplicate is there too.
  3. The probe in `start` blocks its caller (a nested event loop) for a few seconds. The UI
     slice should run its own probe at dialog open, and Start still runs the fresh one this slice
     requires.
  4. `presentation.describe` now starts a Blender when one is found, so each assistant call pays
     that cost. Its input is unchanged, and its codec answer covers both codecs.
- **Open for R099.d2 and R099.d3.** The Presentations dialog (d2) and the batch dialog, Jobs row,
  results and quit prompt (d3). The quit prompt calls `stop()`.

**Review fix (R099.d1): cached, non-blocking codec check.** The review found that
`presentation.describe` launched a Blender probe on every call and waited for it on a nested event
loop on the UI thread, so each query could freeze the desktop for up to 15 s and re-enter UI code.
This resolves contradictions 3 and 4 above, and the "Codec probe" and "Describe" notes describe the
blocking form that this supersedes. `PresentationVideoChecks` caches the probe per Blender
executable, keyed by its canonical path, size and modification time. A successful answer stands
until the key changes. A failed one stands for 60 s, so a broken Blender is not launched on every
call. `cached()` only reads. `refresh()` starts a `BlenderJob` probe and emits `finished()` when it
fills the cache, with no nested loop. `check()` is the CLI's blocking form. The desktop installs
`installPresentationVideoProbe`, so `presentation.describe` reports the cached check, or
`checked: false, pending: true` with "Checking the video encoder…" while a refresh runs. The CLI
installs `installBlockingPresentationVideoProbe`. The describe input schema and description are
unchanged. `PresentationExport::setVideoChecks` makes `start` and `retry` read the cache only. Without
a fresh answer for the current Blender, they refuse before capture with "The video encoder has not
been checked yet. Check again, then Start."; the 17.5 wording for a missing codec is unchanged. The
batch dialog runs `refresh()` on open, reads `cached()` on Start, and refreshes first when the answer
is missing, which covers a changed executable. The cache was chosen over passing a result in: a
passed result would need its own key, and a caller could pass a stale one. Unset, `start` and
`retry` check synchronously, as the CLI does. The nested event loops that remain are the CLI's only:
`PresentationVideoChecks::check()` and the wait in `runPresentationRequest`. The desktop path has none.

## Implementation notes (R099.d2)

Recorded 2026-10-27 for branch `feat/r099d2-presentations-dialog`, on top of origin/main `794cf12c`. The design is
[UX_DESIGN §17](../UX_DESIGN.md#17-presentation-batches); this slice builds §17.1 to §17.3, §17.8 (the path and preset
refusals), §17.10 and the coexistence rule of §17.9. The batch dialog, the Jobs row, results and the quit prompt are R099.d3.

- **Where the tools live (§17.1).** Camera > Presentations… follows Render…, and it is a public action, so Ctrl+K finds it.
  The render sheet gains *Export presentation batch…* beside *Export scene animation…*. That button is disabled, with a
  tooltip, until the batch dialog exists. The footer's *Export batch…* is disabled for the same reason. The palette entry
  for the batch is not added here: an action with no dialog behind it fails the all-dialog audit. The one-shot sheet is
  unchanged (§17.9).
- **The dialog (§17.2, §17.3).** `PresentationsDialog` is a non-modal window with two tabs, Camera paths and Render presets.
  Each edit calls `Viewport::editPresentations`, which runs one `camera_path.*` or `render_preset.*` batch with its own
  history label, so each edit is one applied history step. The commands are the existing catalogue commands; the dialog
  does not write records.
- **The create flow.** New path makes a draft in the dialog, named `Path N` and selected. The draft is not in the model
  until Create path, which is one `camera_path.create`. Closing the dialog keeps the draft; Cancel discards it.
- **The live count.** The planned frames come from `cameraPathFrameCount`, the planner's formula. Within the cap the line
  reads "✓ Plans · N of 240 frames · S scenes · T s at F fps. At this timing, up to K scenes fit". Over the cap it reads
  "✕ Too long", with the planner's formula in small type. At the defaults, 7 scenes fit; 8 scenes plan 265 frames; 2 scenes
  plan 49 frames.
- **Timing.** The record stores steps. The fields show seconds (steps ÷ fps, to 0.01 s). A change of fps recomputes the
  steps from the seconds shown, so a one-second transition stays one second. This is exact for the defaults and for any
  seconds that are whole steps at the current fps.
- **Refused before the commit (§17.8).** A change that would plan more than 240 frames, or that would leave a path with a
  broken keyframe, is not sent. The fields go back to the record's values, and the banner reads "✕ Refused · … Nothing was
  changed." with the limit. The planner's own sentence is never shown.
- **Broken paths (§17.8, §17.11 gap 4).** The list reads "✕ broken: keyframe N names a missing scene" or "… has no camera".
  A keyframe whose scene was deleted shows its position and its old ID ("Missing scene, old ID 17"). A broken path changes
  only to a valid path, which the dialog checks before the commit; the engine's check is the backstop.
- **Names.** The §17.2 and §17.3 name rule is checked before the commit. A clash uses the engine's words, "Camera path names
  must be unique" and "Render preset names must be unique". A rename to the same name adds no step.
- **Presets (§17.3).** The fields use the render sheet's ranges and labels: width and height 64 to 4096, samples 1 to 1024,
  and the seed 0 to 1,000,000. The engine is a pair of radio buttons, as in the mockup. *New from current settings* reads
  the sheet's live width, height, samples and engine (`RenderPanel::setupSettings`) once the sheet has been built, and its
  saved values otherwise. The sheet has no seed, so the seed is 0. The sheet's own code is not refactored.
- **Keyboard and accessibility (§17.10).** The Tab order follows the design. Close is not the default button, and the
  dialog's buttons are not auto-default. Qt sends Enter from a field to the default button, or to the first auto-default
  button when there is none; in testing that closed the dialog, and then pressed New path. Enter commits a field, and Esc
  closes. A radio group is one Tab stop, and the arrow keys choose, which is Qt's rule. Every Tab stop has an accessible
  name, and a spin box names its inner line edit too.
- **Text size (§17.10).** The Interface text size is read when a window is built. At 200% the path list stacks above its
  editor when the dialog is narrower than 300 px plus forty "M" widths, and the footer stays inside the dialog.
  `presentation_input_tests` builds a 200% window to check this.
- **Undo and redo.** Undo and redo end in `Window::sync`, which refreshes the open dialog.
- **Teardown.** `~Window` detaches the dialog before the document goes, as it does for the R094.c editors. Without that, a
  focused name field committed a rename into a freed document when the window closed (found by the suite).
- **Deviations from the mockups.**
  1. The mockup's limit and draft screens say that 2 scenes plan 37 frames. The planner's formula gives 49
     (1 + 2 × 12 + 1 × 24), and the dialog shows 49. The mockup's figure is wrong.
  2. *Export presentation batch…* and *Export batch…* are disabled, not enabled (R099.d3).
  3. Close is not the default button; the mockup draws it as the primary, default button.
  4. A draft's footer keeps Close, beside Cancel and Create path. The mockup's draft footer shows the two draft buttons only.
  5. The keyframe status reads "camera (repeat)" and "missing: remove or replace it". The §17.8 wording is in the banner
     and the accessible names.
- **Contradictions with ADR 0173 or UX_DESIGN.** None found. §7 of this ADR lists *Export presentation batch…* among the
  palette entries; it waits for R099.d3, as above.
- **Checks run.** `presentation_input_tests` passes offscreen (`QT_QPA_PLATFORM=offscreen`) and at `QT_SCALE_FACTOR=2`
  offscreen. `native_accessibility_audit --dialogs-keyboard=presentations` has no findings, and
  `--dialogs-keyboard=inventory` reports an empty `uncoveredEllipsisActions`. Not run here: the X11 (xcb) and Wayland
  display runs. This machine has no Xvfb and no compositor; CI runs them from `native.yml`.
- **Render group (`--dialogs-keyboard=render`).** Two file-chooser cases fail offscreen: `blender-executable` and
  `hdr-environment` ("Dialog focus transfer completes"). The same two fail at the base commit `794cf12c`, built from its
  own archive, so they are not from this slice. With the render sheet's new button hidden they still fail. `render-setup`
  and `render-jobs` pass. The X11 and Wayland runs in CI are the authority for these cases.

## Implementation notes (R099.d3)

Recorded 2026-10-27 for branch `feat/r099d3-batch-jobs-results`, on the R099.d2 merge `27b18b78`, with origin/main `ffc7ff95` merged in. The design is
[UX_DESIGN §17](../UX_DESIGN.md#17-presentation-batches); this slice builds §17.4 to §17.7, §17.8 (the batch refusals
and the run messages), §17.10 (announcements and the keyboard path) and §17.12 choice 5 (the quit prompt), and enables
the three entries R099.d2 left disabled.

- **The controller (`src/app/presentation_batches.*`).** `PresentationBatches` owns the job store, one batch engine
  and a `PresentationVideoChecks`. It holds no widgets, so the dialog, the Jobs list, the results sheet and the quit
  prompt all call it. The store opens on first use at `AppLocalDataLocation/presentation-jobs`, so a window that never
  shows batches creates no folder. Tests pass their own folder.
- **The dialog (§17.4, §17.5).** `PresentationBatchDialog` is modal and opened with `open()`, so it never runs a nested
  event loop. On open it runs `PresentationVideoChecks::refresh()` and a device probe of the chosen Blender, both through
  signals. Start waits while either runs. When Start finds no cached answer for the executable (the Blender changed), it
  refreshes once and starts when the answer arrives; a second miss refuses with "The video encoder has not been checked
  yet. Check again, then Start." The four probe states, the Blender line and the setup card are as §17.5 and §17.8 give
  them. Start is the dialog's default, and no other button is auto-default, so an enabled setup button never takes Enter.
- **The budget (§17.4).** Frames are `cameraPathFrameCount` times the presets. The ceiling is the sum of
  `presentationFrameBudget` per frame (doubled with video), capped at `presentationOutputLimit`. Free space comes from
  `QStorageInfo` on the folder's parent. Time is "Not estimated".
- **The codec choice (§17.12 choice 1).** A "Codec" list sits under Write video. H.264 is the default. MPEG-4 Part 2 in
  MP4 is listed only when H.264 is missing and the probe reports MPEG-4, so it is never chosen for the person.
- **The Jobs rows (§17.6, gap 10).** `RenderJobsDialog` lists the render jobs as before, then each batch: a parent row
  with the state word, the path name and the provenance, and one child row per preset in plan order. The manifest
  names a preset's state only once one of its frames verifies, so while a batch runs the first unfinished preset is the
  one shown as running. Its child row has a determinate bar. The rows are rebuilt on each refresh and the selection is
  restored by ID. Cancel batch, Resume, Show folder, Show results… and Remove are enabled as §17.6 says.
- **Results (§17.7).** `PresentationResultsDialog` opens from Show results… only. A video is listed only when the
  manifest has it, and Play video opens the system player. The stills grid has one cell per keyframe and preset,
  labelled in text.
- **Quit (§17.12 choice 5).** Closing with a batch running asks "Closing stops the batch. Finished frames are kept, and
  you can resume it later." with "Stop batch and close" and "Keep running" (the default and Esc). The prompt is a
  modal `QMessageBox`, as the unsaved-changes prompt already is. The batch is stopped only after the close is certain,
  so a close cancelled by the unsaved-changes prompt leaves it running.
- **Announcements (§17.10).** `PresentationBatches::announced` carries one sentence per milestone: the start or resume,
  each preset's frames finished (by preset name), each video written and each final state. The window announces them
  through `QAccessibleAnnouncementEvent`. Frames are never announced.
- **Enabled entries.** The render sheet's *Export presentation batch…*, the palette entry under Camera (command search
  only, no menu item) and the Presentations footer's *Export batch…* open the same dialog.
- **Tests.** `presentation_batch_input_tests` drives the dialog through every probe state, each refusal and its wording,
  the odd-size and device refusals, a keyboard start to completion, the Jobs parent and child rows, the results sheet
  with and without video, Remove keeping the folder, cancel and resume, the announcements, the quit prompt declined and
  confirmed (the batch is then Interrupted), the missing-Blender setup card, the MPEG-4 fallback and 200% stacking. Blender
  is `blender_job_tests` as the worker. The fake probe now reports its version. Each block has its own data folder, so the
  batches of one block are not in another's store. `presentation_input_tests` now expects the footer's *Export batch…*
  enabled. `native_accessibility_audit --dialogs-keyboard=inventory` reports an empty `uncoveredEllipsisActions`, and the
  `presentations` and `render` groups pass.
- **Checks run.** In the pinned image (`sketchyup-ci-pinned:r097a-ifc`, Qt 6.11.2), offscreen and under Xvfb with
  `xcb`: `presentation_batch_input_tests`, `presentation_input_tests`, `render_input_tests` (under Xvfb, as CI runs it;
  offscreen it waits for a GL renderer that is not there), and the audit's `presentations` and `render` groups. Not
  run: the Wayland runs and the sanitizer jobs, which need the CI runners, and the X11 2x scale for the new suite.
- **Deviations from the mockups.**
  1. The stills grid has text cells only. The mockup's thumbnails are not drawn.
  2. The codec is a "Codec" list under Write video, not a second checkbox. It is shown only when H.264 is missing.
  3. The Blender card's first line reads "✓ Blender 5.2.2 · CPU device ready", as the mockup does, and its second line
     reads "Video: H.264 available.".
  4. An interrupted batch's row says "⚠ Interrupted ·" and the engine's message ("Resume continues at the first
     unverified frame"). The mockup's "frame 81 of 157" needs the engine to name the frame, which the record does not.
  5. A failed batch's row says "✕ Failed ·" and the engine's message, not the mockup's "frame 12 of 157 failed" wording.
  6. The Jobs row's child for a preset with no video and no frames yet reads "waiting for <previous preset>", and a
     preset not started in a batch that ended reads "not started".
- **Gaps, honest.**
  1. The disk figure in the dialog is the ceiling plus the 256 MiB reserve. The engine adds the encoded capture, so its
     refusal number at Start is a little larger.
  2. A batch with no finished frame on an Eevee preset cannot resume: the device it used is not recorded. Resume says
     so and asks for a new batch. Cycles batches resume on the CPU.
  3. The status chip keeps its "Show results" text until the next batch or a restart, not until the model closes.
  4. Not tested in the suite: the disk refusal (the engine's own tests cover the check), the Canceling row, Resume and
     Resume: encode video from the results sheet, Show results… from the chip, Esc in the dialog, and a changed Blender
     between opening the dialog and Start (the re-check path is by code reading only).
  5. Qt: the APIs used are Qt 6.8 APIs, but the suites ran on the pinned image's Qt 6.11.2, not on 6.8.
- **Contradictions with ADR 0173 or UX_DESIGN.** None found in the design. §17.4's Start line for the disk rule uses
  the engine's number; the dialog's number differs by the capture size (gap 1).

## Implementation notes (R099.e)

Recorded 2026-10-27 for branch `docs/r099e-presentation-acceptance`, on origin/main `36f1c42f` (PR #484). The acceptance record is [R099e-presentation.md](../verification/R099e-presentation.md) with its [summary](../verification/R099e-presentation.json). No product code under `src/` changed.

- **Verify line: partly met.** Frame count and timing match, and cancellation works on real Blender. "Camera poses match fixtures" holds at the planner: `camera_motion_tests` pins 11 per-frame pose digests for a four-scene path, but the batch's per-frame camera is not digested and the 1 µm rendered-pose check is not built. "Without replacing unrelated jobs" has no unrelated-job fixture. The roadmap status is "Delivered, with open findings".
- **§5 measured in Release** (local, Cycles CPU, 22 threads; the ADR's conditions were the pinned image and software rendering, so these numbers are not those conditions):
  - `planCameraFrames`, 32 keyframes, 218 frames: p95 0.015 ms against 10 ms. **Pass.**
  - Path and preset edits: core p95 0.002 to 0.003 ms, against the ordinary edit row (BUILD_PLAN: routine primitive edit, p95 below 100 ms). **Pass.** The byte budgets cannot bind: 64 paths and 16 presets are 10,571 bytes (R099.a's measured maxima are 91,648 and 18,432 bytes).
  - Batch capture on a 250,000-triangle document (`quantities-b`'s terrain, 249,218 triangles, no placements): p50 1,602 ms, p95 1,747 ms against 500 ms. **Fail** (owner decision 1). Encoding alone is 1,721 ms p95, so the native encoding (which doubles as the provenance hash) is the cost.
  - Per-frame preparation on the 92,500-triangle, 3,700-body document: p50 304 ms, p95 323 ms. **Not frozen**: the "existing GLB preparation row" has no number in the ADRs, BUILD_PLAN or verification records (owner decision 2).
  - Per-frame render, real Blender, 1024×768, 32 samples: p50 14.5 s, p95 15.9 s, max 20.8 s per frame. **Pass** against 180 s. Every frame is hash-verified.
  - Whole batch: a 48-frame run (4 presets × 12 frames) took 604 s, a mean of 12.6 s per frame. The 960-frame run (240 × 4) is projected at about 3.4 hours and about 0.65 GB, within the 2 GiB budget. **Pass, reported**, with the full run not made (owner decision 3).
  - Store, manifests: **pass** (8, 16, 2 GiB; the per-frame JSON is 70,208 bytes and the batch manifest 14,514 bytes for 48 frames).
  - Codec probe: the first probe takes 1,004 ms; `presentation.describe` takes 1,150 ms the first time and 0.25 ms once cached (R099.d1). No §5 row; measured as asked.
- **The §6 checks.** The record's check map lists each. Unmet as written: the unrelated-job fixture, the rendered-pose tolerance, the destroy-the-original-capture test, and bit-for-bit encodings after undo. Partly met: the batch's determinism row (see contradiction 3).
- **The native matrix** (main's full-tier run `40630475876`): `presentation_input_tests`, `presentation_batch_input_tests` and `render_input_tests` pass at X11 1×, X11 2×, Wayland 1× and Wayland 2×. The dialog audit passes at X11 2× and Wayland 1×; X11 1× and Wayland 2× are not audit cells.
- **Sanitizers and fuzzing.** `Sanitized builds` passed 141 of 141, including the presentation record, I/O, store, export and command suites. `presentation_input_tests`, `presentation_batch_input_tests` and `presentation_cli_tests` are not in the sanitize lists. `parser_fuzz_presentation` was run locally under ASan and UBSan for 241 s (5,249,423 runs, no finding); CI does not run it. No fuzz target exists for `PresentationJobStore`.
- **Installed smoke.** CI's package acceptance passed on `main`, including the prior-release refusal of the schema-30 fixture by the 1.2.0 CLI. The installed 1.3.0 CLI refuses the same fixture, unchanged. The 1.4.0 package smoke is carried to the RC walkthrough, because no 1.4.0 package exists.
- **Gaps closed here.** The R099.d3 untested paths: Esc closes the batch dialog, the chip opens a failed batch's results, and "Resume: encode video" writes the missing video (new tests in `presentation_batch_input_tests`). The mockup's "37 frames" for two scenes is corrected to 49, the planner's formula. The H.264 answer on CI's Blender (`17:5.2.2-2`) is confirmed by `presentation_export_real`. The MPEG-4 fallback is run for real (owner decision of 2026-10-27).
- **Contradictions with this ADR or UX_DESIGN §17, for the owner.**
  1. §5's "250,000-triangle document" cannot be batched as a model: the render snapshot refuses more than 20,000 bodies, and the GLB export refuses more than 100,000 visible faces. The 250,000 triangles can only be a terrain, which captures but does not prepare (`GLB has too many visible faces`, observed). The batch cannot render such a document.
  2. §6's "identical PNG bytes" does not hold: Blender writes a `Date` and `RenderTime` into each PNG's `tEXt` chunks. Pixels, decoded video and planned poses are identical across two copies of the document.
  3. §6's schema row says a 1.3.0 writer refuses schema 30 "with the new-format error". The released readers refuse with "Unsupported container field", unchanged (R099.a's measurement, re-run here with the installed 1.3.0 CLI).
  4. UX_DESIGN 17.11 gap 7 and §5 say the per-frame JSON is about 207 KB. It is 70,208 bytes with one copy of the log.
  5. R099.b documents `LIMIT_EXCEEDED` for a request over 16 KiB. The CLI returns `INPUT_ERROR`.
  6. **A defect, not fixed.** UX_DESIGN 17.5 says Start re-checks the encoder when the Blender has changed. In `presentation_batch_dialog.cpp`, `startBatch` calls `startProblem` first, and `videoAvailable` reads only the cache. With no answer for the changed file, Start reports "H.264 is not in this Blender's FFmpeg build" and returns before the re-check, so no batch starts. The batch test block that found it was removed from the record's PR, and product code is unchanged (owner decision 6).
- **Owner decisions** (listed in the record's §8): the capture budget (1), the missing preparation row (2), the whole-batch length (3), 250,000-triangle terrains (4), the byte budgets (5), the changed-Blender defect (6), the determinism row (7), the version-6 assistant run (8), the 16 KiB code (9) and the roadmap status (10).

## Implementation notes (capture encode, §5 budget)

Measured on the §5 batch-capture document: quantities-b's terrain, 249,218 triangles in 256 tiles, no
placements, 32 saved scenes and a 32-keyframe plan of 218 frames. Local Release build (GCC 16.2.1,
`-O3 -DNDEBUG`, BFD). Captures use `presentation_benchmark capture 20`. The normal save uses
`saveDocument`: 20 new files, then 20 overwrites of one file. Other workers' builds shared the machine,
so each run records the load average at its start.

- **Profile.** `encodeDocument` took about 1,000 ms per call. Phase timers in a local, uncommitted build
  put about 860 ms of that in the 256 terrain tiles. For each tile, `encodeBodies` built the vertices and
  faces (about 330 ms), the edges (about 240 ms) and the rest of the record (about 290 ms, not split
  further; it includes the edge-appearance object). It then replaced those keys with empty values. The
  mesh has been in the terrain-mesh chunk since schema 29, so that JSON was never written. A second pass
  over the bodies added about 100 ms, the terrain-mesh chunk about 25 ms and the JSON serialisation about
  11 ms.
- **Change.** `encodeBodies` does not build a terrain tile's vertices, faces, edges or edge
  appearances. The keys are still written, with the same empty values. Nothing else changes.
- **Bytes.** The output is byte-identical. The proof covers the 37 native documents under
  `tests/fixtures` that decode (33 `.sketchyup`, 4 raw JSON). Each was encoded three ways (JSON with
  inline assets, JSON with external assets, container): 111 encodings. Four documents built in memory
  were encoded the same way: the tour (quantities-b with 32 scenes), quantities-b with 300 placements, a
  four-part model and a 40 × 40 terrain (12 encodings). A built document's `documentId` is random, so it
  was replaced with a fixed one before encoding, and the normalised JSON was compared byte for byte. The
  3,700-body model file was compared the same way, with its identity normalised. The SHA-256 of the full
  256-line result list (the 123 encodings above, and the 133 files that are not native documents) is
  `ecd2d2916a2e894a53dd16db45854dd3ac35aba80688a29f8a5798528097f505`, before and after.
- **Numbers.** Milliseconds, p50 / p95 over 20 runs.

  | Run | Batch capture | `encodeDocument` alone | Save, new file | Save, overwrite | Load at start |
  | --- | --- | --- | --- | --- | --- |
  | Before, run 1 | 1,111 / 1,128 | 997 / 1,009 | 1,011 / 1,029 | 1,482 / 1,510 | 0.9 |
  | Before, run 2 (another build running at start) | 1,160 / 1,225 | 1,028 / 1,070 | 1,024 / 1,066 | 1,502 / 1,548 | 1.0 |
  | Before, run 3 | 1,152 / 1,247 | 1,025 / 1,051 | 1,053 / 1,214 | 1,517 / 1,537 | 2.0 |
  | After, run 3 | 161 / 183 | 31 / 35 | 33 / 38 | 534 / 563 | 2.2 |
  | After, run 4 | 154 / 160 | 29 / 30 | 32 / 49 | 506 / 533 | 1.8 |

- **Budget.** The §5 row (p95 ≤ 500 ms) is met, with about three times headroom: p95 160 to 183 ms
  after the change, against 1,128 to 1,247 ms in the quiet runs before it. The R099.e record's 1,747 ms
  was measured in a different run, under different load, so the two are not directly comparable. Its
  encode was 1,721 ms, which points to the same cause.
- **Full encode, or not.** The capture still encodes the whole document. Its bytes are the job's
  `capture.sketchyup`. Resume decodes them (`PresentationCapture::restore`), the job record's SHA-256 is
  checked on every read, and the disk-space check uses their size. A capture that skipped the encode
  would need its own persisted format and decoder. After the change the encode is about 30 ms of about
  155 ms, so that is not proposed.
- **Not split, not needed for the budget.** The rest of the capture (about 125 ms) is the 32 saved-scene
  snapshots, the document freeze, and the SHA-256 of the 8.6 MB capture. The rest of an overwrite save
  (about 475 ms more than a new-file save) is reading and decoding the previous file, and writing its
  backup. Both are outside the encoder.
- **Contradiction with the ADR, resolved.** The R099.b notes (contradiction 2) say the cost is "the
  native encoding that doubles as the capture hash", and offer a cheaper digest as one option. The
  digest is not in `encodeDocument`. The cost is the encoder, which took about 1,000 of the capture's
  roughly 1,150 ms and built JSON that it then discarded. With that fixed, the 500 ms row holds in a
  Release build, so the digest option is not needed. No format or byte changed.
- **Tests.** `ctest -R` over 21 suites that encode or decode documents passed (Release, `ulimit -v
  8000000`, `-j2`): the ten terrain suites (`terrain`, `terrain_io`, `terrain_history`,
  `terrain_regenerate`, `terrain_edit`, `terrain_replay`, `terrain_import`, `terrain_sculpt`,
  `terrain_stamp`, `terrain_drape`), `native_format`, `persistence`,
  `m4_persistence`, `document_limits_io`, `recovery`, `presentation_io`, `presentation_store`,
  `geographic_site`, `material_texture_size`, `m6_workflow`, `m8_exchange`. Result: 21 of 21 passed.
