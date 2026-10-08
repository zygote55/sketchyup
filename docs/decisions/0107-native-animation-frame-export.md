# Native PNG animation frame export

R072.d, 2026-10-07. Render setup offers Export scene animation after a verified
Blender/device check. Users choose saved camera scenes in presentation order,
1–60 fps, transition and hold durations, and a new output folder. Current render
resolution, samples, engine, environment and device settings apply. The exporter
uses immutable scene capture; editing the native model cannot change later frames.

The output is a PNG sequence and `animation.json`, not an encoded movie. Frame
preparation runs asynchronously and one Blender frame worker runs at a time, sharing
the existing process limits, timeout, verification and cancellation behavior.
At most 240 frames and a conservative 2 GiB output admission budget are accepted.
Each successful frame has a verified PNG, SHA-256 and a JSON transfer record with
camera, scene, source hash, actual settings/device, losses and bounded diagnostics.

Only a new folder is accepted. Each PNG and frame record is committed atomically;
the progress manifest references the pair only afterward. A completed batch marker
is written only after all frames succeed. Cancel preserves completed frames and
writes a canceled outcome. Worker/preparation/storage failure cannot report success;
last worker diagnostics remain in the manifest. Disk limits are application bounds,
not an OS disk reservation. An unreferenced file after an interrupted write can be
inspected manually but is not claimed as a completed frame.

The modeless native progress view supports cancellation and reopening from the
status chip. Render capture, device probing and Blender handoff wait while animation
runs. Reduced-motion preferences affect native scene transitions; explicitly
requested animation output still uses the selected timeline.

A process crash may leave a `running` manifest with already committed frames. That
means incomplete output; this first sequence exporter does not resume interrupted
batches. Users retain those frames or export again to a new folder. Video encoding,
editable spline paths and broader batch presentation remain R099.
