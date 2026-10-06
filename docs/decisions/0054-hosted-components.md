# 0054 — Explicit face frames and hosted component openings

Status: R059.a immutable placement frame and R059.b immutable opening geometry
locally verified. Persistent bindings, opening maintenance and native/automation
workflows follow in separate layers.

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

## Bounded through openings

`cutHostedOpening` cuts one simple, host-local outline through a closed solid's
selected face. It chooses the nearest parallel opposing face that fully contains
the outline, then checks the entire swept prism for intervening geometry. This
supports a wall between an outer shell and an enclosed room cavity without cutting
the opposite room wall. Sloped exits, incomplete coverage, crossed interior walls
and intervening cavities reject explicitly; host bounding-box depth is never used.

The outline needs four modeling tolerances of clearance from the selected and exit
face boundaries, including all existing holes. Full polygon containment, rather
than corner-only checks, rejects outlines bridging concave voids or enclosing an
existing hole. Input winding is independent of component mirroring. Coordinates
within plane tolerance snap only along the selected face normal. The tunnel follows
that local normal; a body's later placement transform applies to the entire result.

Both host faces retain their IDs and gain a hole loop; original vertices and all
unrelated face records remain unchanged. Explicit new jamb IDs and entry-face
lineage support later appearance transfer. The helper assigns no materials. Rebuilding
topology against the previous topology preserves original edge identities.

The input and result must pass closed-shell analysis with outward material and
inward cavity boundaries. A checked material-volume difference agrees with the
profile-area/depth prism within triangulation precision. A globally inverted host
must be explicitly oriented before this directional operation. Open sheets and
edge-touching notches are outside this through-wall kernel.

Preflight reserves output space within 1,000 faces, 10,000 vertices, 32,000 total
corners, 4,096 corners per loop and 64 loops per face. Outlines have 3–256 corners;
containment/sweep clipping has a four-million work budget. Existing bounded solid
analysis applies independently. Every failure leaves source geometry and identity
allocators untouched. Multiple nonoverlapping cuts are supported, but storing the
uncut host and regenerating cuts after component edits remains subsequent work.
