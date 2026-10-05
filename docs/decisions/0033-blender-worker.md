# ADR 0033: optional, isolated Blender snapshot workers

Status: accepted for R049. Blender remains an optional external executable.

`PreparedRender` exports an already captured immutable snapshot off the document
thread. `BlenderJob` runs on its Qt owner thread and launches an absolute executable
(or discovers `blender` on PATH). No live document reference crosses into the
worker. Every job owns a private temporary directory; reusable prepared input is
held independently through completion. At most two jobs run concurrently.

The embedded Python adapter supports **Blender 5.2 LTS**, with Cycles present.
Probes run in a child process with a maximum 15-second deadline. CPU probing does
not initialize GPU drivers. GPU discovery queries only the explicitly requested
backend; selection requires its persistent device ID. Unsupported versions and
missing executables produce an unavailable state without changing the document.

Execution uses fixed argv, no shell, factory startup, disabled auto-execution,
private configuration/script directories and four render threads. Inherited
Blender/Python overrides and display connections are removed. This isolates
preferences and accidental startup scripts; it is not an OS security sandbox for
an untrusted executable. The selected installed Blender must be trusted.

The `studio-v1` preset uses Cycles, captured resolution/sample count/seed,
non-adaptive sampling, no denoising, eight bounces, Standard/None color management,
exposure zero, gamma one, a 0.25-strength world and two area lights sized/positioned
from scene bounds. Area-light power scales with squared scene radius (key 80,
fill 25). Output is opaque 8-bit RGBA PNG. GLB transfer losses remain in the result.
The initial preset favors reproducibility and availability over photorealism.

A selected GPU failure can retry once on CPU when enabled. Each attempt has a
fresh output directory and retains its bounded diagnostic tail. Unsupported
versions, invalid snapshots, cancellation and timeout do not trigger fallback.
The total deadline includes both attempts and image verification. Cancellation
terminates the owned process and escalates to kill after one second; destruction
kills it and waits up to two seconds. The terminal state follows process exit.
No child process group is managed; the fixed adapter does not spawn subprocesses.

Output is bounded to 2 MiB per attempt with a 64 KiB tail. Structured progress is
limited to known phases. The result manifest is at most 64 KiB, PNG at most 64 MiB,
and decoded dimensions must match the captured settings (maximum 4096 squared).
Success requires normal process exit, supported protocol/version, matching source
identity/revision/settings/hashes, selected device, preset, PNG hash/size and full
image decoding. Decoding uses the exact hash-verified bytes on a worker thread.
Cancellation during verification discards the candidate. Neither a stale file nor
an exit-zero claim alone can publish a successful image.

R050 will own setup controls, native camera capture, progress, image saving and
revision-provenance UI. This layer provides no assistant filesystem or shell tool.
