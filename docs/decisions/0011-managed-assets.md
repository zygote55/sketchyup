# Managed assets are immutable embedded resources

R036.b, 2026-10-04.

Each document owns a stable-ID asset table. An asset contains a display name,
media type and immutable bytes, or an explicit missing state with no payload.
Names may repeat; IDs distinguish records. A material can reference one asset,
including a missing record. Unknown IDs reject. Deleting any asset referenced
by a swatch rejects until that binding is removed. Global asset replacement
keeps material identity and geometry unchanged. Component geometry scopes can
use existing references but cannot modify the global asset table.

Payload construction copies input bytes into a private const vector with no
mutable accessor or assignment operator. Asset metadata is copied on publication;
its payload can then be shared safely by snapshots, history and component drafts.
This avoids copying large payloads for every canonical component validation.
Undo, amendment, atomic batches and monotonically increasing allocator floors
apply to assets just as to geometry and materials. History accounts conservatively
for referenced payload sizes even when physical storage is shared.

Limits are 1,024 assets, 1,024 bytes per display name, 128 bytes per media type,
16 MiB per nonempty payload and 64 MiB of payloads per document. A missing record
has zero bytes. A zero-byte input is rejected rather than silently interpreted as
missing. Media types are bounded lowercase type/subtype tokens. Stored bytes are
opaque resources; image decoding, texture coordinates and image rendering are
separate presentation capabilities. This layer does not claim texture rendering.

An explicit local-file import reads a bounded regular file once and captures its
bytes. The original path is not retained or resolved by the document. Display
names, including names resembling relative paths, are never extraction paths.
Native assets use generated logical keys `assets/<canonical ID>.bin`. The reader
requires that exact key and never extracts an archive or opens a path from the
manifest. A copied native document therefore owns its resources independently of
its original directory or imported source files.

The native envelope remains version 2 and adds `assets-v1`/`json-v11`. Its manifest
lists every asset's identity, name, media type, missing state, logical key, byte
count and SHA-256 (null for missing). The first chunk is bounded 32 MiB model JSON;
subsequent chunks contain each present asset's raw bytes. Every chunk is checked
for its declared range, checksum and unique identity. Ranges must be contiguous,
nonoverlapping and consume the file exactly. The manifest must agree with the
model metadata, material references and allocator floors. Corruption is an error;
it is never converted into a fabricated missing record.

The manifest remains bounded to 1 MiB; a native file can contain up to 97 MiB plus
its 16-byte header. Standalone schema-11 JSON uses canonical base64 inline payloads
and a 128 MiB bound. Native model JSON declares external payload storage and can
only decode present resources when the corresponding verified chunks are supplied.
Legacy schemas 1–10 remain bounded to 32 MiB and acquire no invented assets.
A historical schema-10 material fixture verifies migration.

`asset.import`, `asset.missing`, `asset.replace` and `asset.delete` expose atomic
operations. Replacement data null marks an existing asset missing; base64 bytes
resolve it with the same ID. Material create/edit commands accept an asset ID,
with zero clearing the binding. `assets.describe` reports the manifest without
raw payloads; material queries report none/present/missing explicitly. The recipe
driver retains its existing 1 MiB script-input limit, even though explicit file
imports and the in-process document API support the larger asset budget.
