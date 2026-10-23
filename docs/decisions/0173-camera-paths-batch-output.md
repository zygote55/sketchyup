# Camera paths and batch presentation output

Status: **Proposed** for the decisions below. Roadmap row
[R099](../PR_ROADMAP.md#r099) (M10, scope row [F05](../SCOPE.md)); builds on
[ADR 0099](0099-retained-render-jobs.md) / [ADR 0100](0100-render-queue-lifecycle.md)
(R070), [ADR 0104](0104-camera-motion-and-timelines.md) /
[ADR 0106](0106-immutable-animation-capture.md) /
[ADR 0107](0107-native-animation-frame-export.md) (R072), and on the closed
1.0 gate (R089). This row takes ADR number 0173 because 0172 is reserved for
the parallel STEP exchange draft.

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

| Operation | Budget |
| --- | --- |
| `planCameraFrames`, 32 keyframes at the maximum timing | p95 ≤ 10 ms (*proposed*; in memory, no rendering) |
| A path record edit (create, edit, delete) | Within the ordinary document-edit row; the 512 KiB record budget measured at load (*proposed*) |
| A preset record edit | Within the ordinary document-edit row; the 256 KiB record budget measured at load (*proposed*) |
| Batch capture (one freeze + 240-frame plan) on a 250k-triangle document | p95 ≤ 500 ms (*proposed*) |
| Per-frame preparation (`PreparedRender::prepare`) | Within the existing GLB preparation row, measured per frame |
| Per-frame render | ≤ 180 s including the CPU fallback (the `BlenderJob` deadline); PNG at most 64 MiB, hash-verified |
| Whole batch, 240 frames × 4 presets at 1024×768 / 32 samples | Measured and reported (no fixed cap; progress and cancel at frame boundaries, the remaining 2 GiB budget surfaced per frame) (*proposed*) |
| Store | 8 pending, 16 retained, 2 GiB (the R070 limits, reused) |
| Manifests | Per-frame `.json` at most 1 MiB (the animation export bound); the batch `manifest.json` at most 1 MiB (*proposed*) |
| Records on load | 64 paths / 512 KiB and 16 presets / 256 KiB refuse over budget (*proposed*) |

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
