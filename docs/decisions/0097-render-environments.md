# Captured HDR environments for render jobs

R069.b, 2026-10-06. The render setup accepts an optional Radiance RGBE 2:1 panorama,
strength 0–100 and world rotation −360–360 degrees. These settings belong to one
render snapshot; they do not alter the document. The headless render-settings JSON
accepts `environment: {path, strength, rotationDegrees}` with the same validation.
A blank environment retains the existing studio or sun-study behavior.

Capture owns the image bytes. The original file can change or disappear afterward.
The GLB package carries `environment.hdr` plus exact dimensions, byte count, SHA-256,
projection, strength, rotation and color-space metadata in its manifest. A standalone
GLB does not encode world lighting and reports `environmentLightingOmitted`; the
Blender worker clears that loss only when it applies the captured environment.
Publication validates the sidecar before creating the destination and removes all
partial artifacts on failure. No original filesystem path enters the manifest.

Inputs are bounded to 40 MiB encoded, an 8 KiB header, 4096×2048 pixels and nonnegative
RGBE radiance at most one million. Flat and modern per-channel RLE scanlines use the
standard −Y +X orientation; legacy repeat encoding, XYZE, other orientations, invalid
runs, incomplete/trailing data and mismatched metadata reject before publication.
Blender interprets channels as scene-linear Rec.709. Photometric header adjustments
are not applied; convert color-managed panoramas to this space before use.
The format follows the [Radiance picture specification](https://radsite.lbl.gov/radiance/refer/Notes/picture_format.html).

The worker hashes the packaged image, decodes exactly those bytes from a private
temporary file and packs it into Blender before rendering. Equirectangular sampling
uses linear filtering, the chosen strength and rotation around native +Z. Positive
rotation rotates the environment counterclockwise when viewed from +Z. The HDR world
replaces the constant ambient world and studio area lights. An enabled native sun
remains an additional directional light (`solar-hdri-v1`); otherwise `hdri-v1` applies.
The desktop verifies the complete environment/lighting/loss report before publishing
pixels. Output retains the existing transparent film and Standard view transform.

Actual Cycles checks verify a rotated red/blue lighting panorama, reduced/zero
strength and rendering after source removal. Native capture/export tests exercise
strict JSON fields, every byte truncation, invalid runs/dimensions/settings, immutable
snapshots, self-contained relocation and atomic rejection. The real worker lifecycle
also renders a packaged environment after removal of its original file.
