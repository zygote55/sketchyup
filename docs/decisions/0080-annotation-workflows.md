# Shared annotation commands and inspection

R065.c, 2026-10-06. `annotation.create`, `annotation.update` and
`annotation.delete` use the same immutable records and geometry association as
native workflows. They are available to command batches, preview/staging, CLI,
transactions and assistant transports. Document annotations cannot be authored
inside a shared component-definition command scope.

Creation requires a unique name, `distance` or `label` kind, and two or one anchors
respectively. Labels require nonempty text; distance text is an optional prefix.
Color, logical-pixel text size, leader and world-metre offset use record defaults
when omitted. Updates are partial and require at least one property. Omitted
anchors preserve current association, including missing/ambiguous state. Supplying
anchors explicitly rebinds them and requires live, unambiguous geometry. Metadata
editing never guesses a replacement attachment. Deletion and batch operations
publish one history item; failure rolls back the entire batch.

Authoring anchors have four strict shapes, distinct from persisted support data:

- `point`: explicit `space: world` and three-coordinate `point` in metres.
- `vertex`: canonical decimal `body` and `vertex` identities.
- `edge`: canonical `body` and `edge`, plus `fraction` in [0,1] from the topology
  edge's canonical a endpoint toward b.
- `face`: canonical `body` and `face`, `space: local|world`, and a point on its
  actual face surface. World authoring accounts for the full placement transform.

Callers cannot forge support triangles, fallback positions or broken states.
Schema validation covers shapes, enums, numeric bounds and unknown fields; core
validation also enforces geometry, finite precision, byte limits, uniqueness and
kind-dependent anchor counts. Updates changing kind must supply any properties
necessary for a valid resulting record. Staging lists `annotation` resources and
batch results include `createdAnnotations` for surviving newly created records.

`annotations.query` pages stable identities, optionally filtered by kind.
`annotation.describe` returns the stored record plus `resolvedAnchors`, world
`textPoint`, aggregate `state`, `distanceMetres` and document `displayUnits`.
The numeric distance is null for labels and broken dimensions. Per-anchor states
make partial failures inspectable. Both queries enforce document/revision stamps;
page cursors bind to the request and revision. Native MCP exposes these read-only
queries. This layer does not change schema 21 or add native authoring controls.
