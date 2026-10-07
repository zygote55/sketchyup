# Explicit Eevee preview and renderer provenance

R069.c, 2026-10-06. Render setup offers Cycles and Eevee preview. Render settings
capture `engine: cycles|eevee`; omission retains Cycles. Both engines consume the
same immutable geometry, sided materials, textures, normals, instances, camera,
sections and lighting package. Both retain the existing Standard color transform,
opaque film, exact output dimensions and verified image publication.

Cycles retains explicit compute-backend/device selection and optional one-time CPU
fallback. Eevee uses the active OpenGL context. Its capability probe initializes that
context and reports renderer, vendor, device type and driver version. The user selects
that reported renderer; its canonical SHA-256 fingerprint must match again before
rendering. This identifies the active renderer/driver, not a unique physical adapter
when identical GPUs exist. Eevee does not enumerate or switch physical GPUs. A software
OpenGL renderer is reported by its actual name. A failed or changed context never
silently switches to Cycles CPU, even if the caller requests CPU fallback.

The `eevee-preview-v1` preset uses the requested temporal sample count, fast GI,
shadows at resolution scale 1, one shadow ray and six shadow steps. Ray tracing is
disabled. Indirect illumination is an approximation and appears in the transfer
report and native result label. The Cycles sampling seed is not applied by Eevee;
a nonzero requested seed is explicitly reported as unapplied. Its preset omits
Cycles-only bounce and denoising claims. Capability and result verification check
engine identity, sampling policy, samples, renderer fingerprint, lighting and losses.

Blender 5.2.x remains the supported range. Cycles probes enumerate only the requested
compute backend; Eevee probes only its OpenGL context. Both run in isolated factory
preferences without model-provided code. Unsupported engines/backends or mismatched
captured settings fail before process launch.

Actual Blender acceptance runs the worker lifecycle with explicit Eevee selection,
changed-renderer rejection and sun/HDR input. Independent tolerant image checks run
both engines for solar direction/shadows, HDR rotation/strength, textured alpha/sides,
section caps and smooth/mirrored normals. Native setup tests verify engine changes
invalidate the old device proof and preview results identify their approximation.
