# 0054 — Explicit face frames and hosted component openings

Status: R059.a immutable placement frame locally verified. Persistent bindings,
opening maintenance and native/automation workflows follow in separate layers.

## Face placement

`componentPlacementOnFace` aligns an explicit canonical component glue frame with
a selected host face and host-local anchor/tangent. It returns a world transform,
the orthonormal world host frame, the projected local anchor and whether the anchor
lies on a boundary. It does not modify either object or create a relationship.

The anchor must lie on the face plane within modeling tolerance. Only its normal
component is snapped; points outside the outer boundary or inside a hole reject.
Outer and hole boundary anchors are allowed and identified explicitly. Hole winding
does not affect containment. A placement point alone does not establish that an
opening profile fits the host: cutting will require a separate full-profile check.

The host's physical normal uses inverse transpose, including reflected transforms.
The tangent is projected onto the host plane before applying its world transform,
so a caller's off-plane tangent component cannot change alignment under a shear.
The resulting frame is orthonormal. Host scale/shear/reflection affects the anchor
and orientation, while component dimensions come from the explicit signed scale.

The resulting matrix is `hostFrame * rotationZ(angle) * scale * inverse(glueFrame)`.
The component glue frame must already be unit, orthogonal and right-handed; invalid
source frames are rejected rather than silently changing component coordinates.
Signed component scale permits mirrored placements independently of host reflection.

## Bounds and failures

Before triangulation, the chosen face is limited to 64 loops and 4,096 total corners.
Only that face is validated; unrelated host faces are not copied or traversed.
Existing finite coordinate and affine-transform bounds apply. Origin-relative
integer containment uses 1e-8 coordinate precision within the document bounds.

Typed failures distinguish invalid host faces, work limits, invalid glue frames,
off-plane anchors, anchors outside material and invalid placement transforms. No
failure mutates source geometry or consumes identities. This kernel makes no
promise yet about save/reopen bindings, moving/deleting a hosted component,
definition replacement, opening regeneration or user-facing placement controls.
