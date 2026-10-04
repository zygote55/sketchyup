# R036.b: managed assets and resource manifests

Date: 2026-10-04. Local validation passed; CI pending.
Requires [PR #45](https://github.com/zygote55/sketchyup/pull/45).

The document now owns immutable asset bytes and explicit missing records. Material
swatches reference stable asset IDs; global replacement preserves all bindings.
History, allocator floors, component-scope boundaries and byte budgets use the
same authoritative transaction path as other document records. The
[asset decision](../decisions/0011-managed-assets.md) defines ownership, limits,
logical keys, checksum/range validation and the distinction between storage and
future image texture rendering.

Schema 11 has inline canonical base64 for standalone JSON and external raw chunks
for native containers. The native manifest lists present and missing resources,
including their logical keys, sizes and checksums. No document-provided path is
opened or extracted. A historical schema-10 fixture preserves prior materials.

Four asset commands extend the catalog to 60. Material create/edit commands can
bind or clear an asset; read-only material queries explicitly distinguish missing,
present and absent resources. `assets.describe` omits payload bytes.

## Validation

- Development suite: 35/35 passed. ASan/UBSan suite: 28/28 passed.
- Native X11 Info and component input checks plus application smoke passed.
- The managed-assets CLI recipe saved and reopened with its 68-byte embedded PNG,
  explicit missing resource and both material bindings intact.
- Core checks cover immutable source/metadata ownership, missing-resource resolution,
  material references, shared-component scope, allocator floors, amendment, undo,
  per-asset and aggregate byte limits, and atomic rejection.
- I/O checks import a local PNG, package its bytes, relocate a copy, delete the
  original document and source image, and reopen the copy with identical bytes.
  Missing records survive relocation and can be resolved without changing IDs.
- Both standalone JSON and native binary chunks roundtrip exactly. Corrupt hashes,
  malformed base64, truncated/trailing bytes, duplicate chunks, zero IDs/ranges,
  unsafe logical paths and inconsistent manifest metadata reject explicitly.
- Captured save snapshots retain the original immutable payload while a newer
  replacement remains dirty. Oversized local files reject before reading payloads.
- Public checks cover missing/present status, read-only manifests, one-step combined
  asset/material creation, and rollback of an asset replacement when a later
  material binding is invalid.

This is implementation-agent evidence. Native asset controls, swatches,
paint/sample and front/back opacity rendering remain R036.c.
