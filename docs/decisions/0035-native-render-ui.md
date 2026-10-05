# ADR 0035: native snapshot rendering and result provenance

Status: accepted implementation contract for R050; acceptance in progress.

Camera → Render and the command palette open an optional, nonmodal Blender setup
sheet. Nothing probes Blender during ordinary editor startup. The sheet explains
the optional dependency and offers executable selection, capability/device checks,
current-view or fit-visible-model camera, resolution, samples and one CPU retry.
Only discovered devices are offered. Scenes, styles and job presets reserved for
M7 are not represented by inactive controls.

Current-view capture uses the viewport's position, target, vertical field of view,
orthographic extent, clipping distances and top/bottom up vectors. A changed output
aspect ratio preserves vertical framing; the sheet states that behavior. Temporary
hidden geometry is captured. Active modeling gestures and temporary clipping,
opacity or benchmark overrides must be resolved before capture so the exported
scene has an honest relationship to the view.

The GUI thread captures the immutable document/camera value. GLB preparation runs
on a worker and Blender runs through R049's controlled child process. One job per
window is active; R049 also caps process jobs globally at two. Canceling preparation
discards its eventual output, while canceling Blender follows terminate/kill and
verified terminal states. Modeling, undo and document replacement continue during
rendering. No render action adds model history or modifies persistence.

A status chip opens the setup/job sheet, with elapsed time, actual phase, Cancel
and bounded plain-text worker diagnostics. Closing the sheet leaves the job active.
Only the R049 verified image produces a result tab. The Model tab remains and cannot
be closed. Images fit available space without changing their saved pixels.

Each result retains the captured document session and revision. The provenance
line distinguishes a changed revision from a replacement/reopened model session,
and shows Blender, Studio preset, selected device, CPU fallback and human-readable
transfer losses. An old image is never relabeled as a new model result. Results
survive document replacement until closed or evicted by the two-result bound.
Save image as uses QSaveFile to publish the exact verified PNG; it is independent
of model Save. A tooltip explains that only two recent results remain in memory.
Render again opens setup and captures the current document when started.

Executable path, dimensions and samples are ordinary QSettings preferences. No
provider credential, shell command or executable script is a model operation.
Blender remains optional for all manual editing and native document operations.
