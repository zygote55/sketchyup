# Self-contained template bundles

R079.a, 2026-10-07. A `.sketchylib` template is one relocatable file containing
strict JSON metadata, a validated native document container and a static PNG
thumbnail. It never follows a manifest path or downloads a dependency. Embedded
assets must be present before publication; missing assets reject with an error.

The 24-byte header contains the eight-byte `SKYLIB\0\1` signature followed by
little-endian manifest (32-bit), native model (64-bit) and thumbnail (32-bit)
lengths. Limits are 32 KiB manifest, 128 MiB model, 4 MiB thumbnail and 130 MiB
combined. Exact lengths reject trailing or truncated bytes. Model and thumbnail
SHA-256 hashes and recorded sizes are verified before use. The PNG must be a
complete supported static image no larger than 1024 pixels on either side.

Version 1 template manifests contain exactly `bundleVersion`, `kind`, `name`,
`description`, `labels`, `defaultScene`, `model` and `thumbnail`. The kind is
`template`; unsupported versions/kinds reject. Names are nonempty UTF-8 up to
256 bytes, descriptions up to 4096 bytes, and at most 32 case-insensitively unique
search labels of 64 bytes each. Control characters are forbidden except newline
and tab in descriptions. The default scene is a canonical decimal identifier
referencing a retained saved scene, or zero. Native records retain their existing
validation and a 256 MiB snapshot admission budget.

Instantiation preserves units, style, solar settings, geometry, component
bindings, resources, annotations, sections and saved views while giving the new
document a fresh identity/session, no undo history and an explicitly unsaved state.
Immutable shared records preserve source and sibling instances when one is edited.
The selected default scene is metadata for the later native opening workflow.

Publication validates the complete bundle and uses the existing atomic new-file
publisher. Existing files, including source bundles, cannot be overwritten.
Relocation requires only the one bundle file. No scripts or executable extension
payloads are supported by this format.

Normal and ASan/UBSan tests exercise embedded resources, component bindings,
saved defaults, independent new-document edits, relocation, source protection,
malformed lengths/hashes/metadata, unsupported versions, missing assets and
oversized thumbnails. Search, component insertion and native selection workflows
are subsequent R079 layers; this contract does not claim those layers complete.
