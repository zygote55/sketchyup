# 0054 — Explicit face frames and hosted component openings

Status: R059.a placement, R059.b opening geometry, R059.c canonical glue records,
R059.d immutable host regeneration and R059.e persistent attachments are locally
verified. CI/dependency merges remain pending. R059.f exposes shared commands and
bounded inspection. R059.g adds native placement, and R059.h adds explicit copy/array
relationships. Recipe adoption follows.

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

## Canonical component glue behavior

A definition optionally stores `ComponentGlue`: a direct canonical member ID, face
ID, member-local anchor/tangent and `cutsOpening` boolean. It never guesses a window
silhouette from arbitrary members or nested component references. The referenced
face supplies the physical normal; its outer loop supplies the cutting outline.
An anchor can lie within a ring's hole or elsewhere on the face plane. Alignment-only
behavior uses the frame without producing a cut outline.

`resolveComponentGlue` validates the bounded face and canonical hierarchy, projects
the anchor within modeling tolerance, and expresses the orthonormal frame and cut
outline in definition coordinates. Member transforms retain physical normals under
reflection/nonuniform scale/shear. At most 128 hierarchy levels and 4,096 face corners
are accepted; a cutting outer loop is limited to 256 corners.

`setComponentGlue` publishes one definition edit. With hosted attachments, the same
edit also regenerates their opening geometry. The behavior participates in immutable previews, Undo/Redo, snapshots,
freezing and existing component memory accounting. An identical setting is a no-op.
A locked placement protects its shared glue behavior. Make Unique copies the
reference; subsequent edits to that copy do not affect its peers. Changing component
axes preserves world anchor/profile coordinates. Shared geometry capture retains
the explicit reference and resolves its new outline; deleting the referenced face
or moving it away from the anchor plane rejects atomically until the behavior is
explicitly cleared or changed.

JSON schema 14 requires a nullable `glue` field on canonical definitions. Populated
records require exact stable ID strings, finite three-coordinate anchor/tangent
arrays and a boolean cutting flag; unknown fields reject. Versions 1–13 migrate
without invented glue behavior. Containers require `component-glue-v1` with
`json-v14`, preventing older readers from silently dropping it. Immutable saves,
recovery checkpoints and durable transaction reconstruction retain glue-only edits
and their Undo baseline. There is still no persistent host attachment in this layer.

## Identity-preserving host regeneration

`regenerateHost` accepts an uncut surface, the current authoritative body, prior
opening records and requested profiles keyed by their component owner. Each profile
corner has a stable source key. Each opening records its entry/exit faces, native
entry/exit vertices by source corner, and native reveal faces by unordered source
edge. At most 16 openings per host and the existing bounded opening geometry apply.

Before rebuilding, the helper reproduces the old cuts and verifies their exact
native surface against the current host. An independent host geometry change or
inconsistent/colliding identity record rejects; current geometry is never silently
replaced by an older baseline. Openings cannot terminate on another opening's reveal.

Construction and verification use a temporary dense identity space. The result maps
original geometry back to its original IDs and reuses still-live opening vertices
and reveal faces for matching source keys. New features allocate only above current
floors. Rebuilding topology against current records retains unchanged endpoint/edge
identities. Moving or resizing a profile with the same topology therefore consumes
no identities, even with exhausted live allocators. Deleted features retire normally;
recreating a removed opening cannot resurrect their old IDs.

The uncut baseline stores geometry only. Current host metadata, original-face paint,
materials and edge flags remain authoritative. Retained reveal faces preserve their
paint; new reveals inherit the current entry face's physical material sides and
color. Retired appearance entries are removed, and cleared original edge flags do
not reappear from a saved baseline. Analytic curves are rebound or retired through
the existing bounded curve policy; guides and unrelated body state are retained.

Removing one request rebuilds the remaining openings. Removing the final request
restores uncut geometry while retaining current metadata and allocator floors.
The result supplies explicit face lineage and is compatible with an immutable
PreparedEdit and one Undo item. Callers must mark its edge appearance as resolved
when publishing, so later edit composition cannot re-inherit retired styles.
This helper does not yet store host attachments in Document or automatically observe
instance movement/deletion; that authoritative integration follows separately.


## Authoritative attachment lifecycle

A document stores one immutable `HostedSurface` per host and one
`ComponentAttachment` per attached placement. The surface retains only original
geometry and the current opening correspondence maps. The attachment records its
host/face, an affine glue-coordinate-to-host frame, and an explicit signed inset
in host-local units. Its X/Y axes must stay parallel to the original face; its
anchor projected by the inset must lie on that face. The inset permits a window's
canonical front face to sit within or beyond wall thickness without changing the
opening's entry plane.

The initial scope accepts ordinary raw geometry hosts and independent component
placements, including placements beneath ordinary groups. Component-owned hosts
and nested canonical placements reject. Alignment-only attachments retain the same
host baseline but create no opening. An explicit canonical cutting face is required;
no outline or relationship is inferred from names, proximity or recipe properties.

Every unresolved Document edit privately expands attachment effects before normal
scene validation and publication. Explicit placement moves infer a new relative
frame; a host-only movement carries its attachments. A shared definition edit keeps
the attachment frame fixed and adjusts each instance root to retain its glue anchor.
Common-ancestor transformations preserve an equivalent stored frame to avoid drift.
Opening profiles derive from that frame and the authoritative definition, never
from rounded materialized instance coordinates.

Moving or resizing a cutter regenerates all cuts from the original surface and
preserves surviving native correspondence identities. Rehosting restores the old
region and cuts the new host in one edit. Deleting a component, removing its instance
binding, or replacing/removing its glue behavior restores its opening and releases
the attachment. Deleting a host releases surviving placements without moving them.
Detach restores the opening; Bake releases every attachment on a host while retaining
its current geometry. Independent host geometry edits reject until attachments are
released. Appearance, guides, state and other nongeometry edits remain authoritative.

Prepared previews, composed transactions, numeric amendment, Undo/Redo and read
snapshots include the attachment aggregate. Nested records are frozen at publication.
Before/after baselines and caches count toward history and snapshot budgets. Locks
protect both former/new hosts and affected component roots, including metadata-only
binding, detachment and baking. Resolved composition skips inference but still
validates complete pose/profile/baseline coherence. Amendment cannot alter unrelated
attachment records or change attachment/host creation counts.

Bounds are 16 hosts and 64 attachments per document, with 16 cutters per host.
Stored baselines collectively allow 20,000 vertices, 2,000 faces, 20,000 wires and
64,000 loop corners; cached profiles collectively allow 4,096 corners. Each host
also obeys the existing opening geometry limits. Invalid/stale records, missing
references, allocator collisions, off-plane movement, boundary contact and overlapping
cuts reject atomically with the original document and history intact.

Schema 15 requires a `hosted` object containing strict `hosts` and `attachments`
arrays. Stable IDs remain decimal strings; transforms contain exactly 16 finite
numbers, and insets are finite numbers. Baselines contain only `nextId`, vertices,
faces and wires. Opening records persist source corner IDs, entry/exit vertex pairs
and unordered source-edge-to-reveal mappings. Duplicate keys, unknown fields,
extra material state or a cache that fails exact geometry reconstruction reject.
Containers require `hosted-components-v1` and `json-v15` alongside earlier features.
Versions 1–14 migrate without invented attachments. Immutable saves and recovery
retain the captured aggregate, and durable transaction recovery reconstructs one
Undo even for an alignment-only metadata edit.

These core attachment APIs also back the shared commands, native placement and
copy/array behavior described below. Explicit migration of existing recipe
relationships remains separate R059 work; unbound copies do not acquire a host.


## Shared attachment commands and inspection

Five catalog operations use the authoritative document APIs: `component.glue`,
`component.attach`, `component.bind`, `component.detach`, and `component.bake_host`.
Glue requires an explicit definition and either null or exactly the canonical
member/face, member-local anchor/tangent, and boolean cutting flag. Attach requires
an independent instance, ordinary host/face and host-local anchor. Its optional
rotation is radians; scale is signed and defaults to one; tangent defaults to local
X, and signed host-normal inset defaults to zero. Bind preserves the current pose
and requires an explicit inset rather than guessing one. Detach restores the
opening; Bake retains current cut geometry and releases every attachment on that host.

All five participate in the shared catalog, strict field validation, private batches,
immutable preview, native persistence, durable transactions and one-step Undo.
Metadata-only bindings are real edits. A late invalid command, stale revision,
invalid scope or lock rejects the whole batch. Hosted commands cannot run inside a
canonical `component.edit` draft: an explicit scene-level operation is required.
Receipts include affected host/attachment IDs and bounded attachment/opening details.
Paged stage changes include `host` and `attachment` record kinds, including edits
with unchanged body geometry.

`entity.describe` supplies the nearest canonical member/definition binding for
materialized geometry, preserving the inspected entity ID within that member.
`component.instances` returns canonical glue plus paged instance summaries. Attached
instance summaries expose their host, face, relative affine frame, inset, cutting
flag, entry/exit and reveal IDs, along with typed current host and face references.
Original surfaces and full opening caches are not copied into ordinary inspection.
Existing page and response byte budgets still apply.

Native assistant policy exposes attach/bind/detach routinely, glue only with shared
edit permission, and baking only with destructive permission. Native editor locks
cover changed host/attachment records and glue-only definition edits in addition to
body geometry. Locks are checked both for preview and again before publication,
including an editor lock acquired after a metadata-only preview was sealed.

## Native placement controls

Edit → Set glue face requires one editable face inside its directly owning component.
It resolves the canonical member and records member-local anchor/tangent plus an
explicit cutting checkbox. The dialog states the shared definition's placement count;
its snapshot, revision, context and selection are rechecked before publication.

Shift+H requires one whole independent component and one ordinary host face in the
same editing context. The initial anchor is the centroid of the largest uncut face
triangle. Pointer motion intersects the selected host's local plane, and the existing
command preview validates containment, cut clearance, locks and staleness. A bad
preview cannot commit. Click or Enter commits one command; Escape discards it. The
placement tool does not use drawing inference locks, including on shortcut release.
Alt-drag camera navigation remains available.

Measurements accepts one signed host-local inset length or three host-local anchor
coordinates, with normal document units and locale rules. Numeric re-entry uses the
existing amendment token and preserves one Undo item. Placement options explicitly
set rotation in degrees, signed XYZ scale and inset; they cancel a pending preview,
and the user restarts placement afterward. Scale one uses the definition's original
size. Bind current pose uses only the explicit inset and leaves the instance pose
unchanged. Detach restores the opening; Bake keeps geometry while releasing every
attachment on that host; clearing glue affects all shared placements. Local editor
locks cover all affected placements and former/new hosts before publication.

These controls expose the same bounded command and persistence behavior. Nested
canonical placements and component-owned hosts remain unsupported. Explicit recipe
adoption remains separate R059 work.


## Copy and array relationships

Copying a whole attached instance retains its original host, face, inset and shared
component definition. The copied pose supplies a new host-local frame, and the host
regenerates an independent opening. No binding is inferred for an unbound instance;
`component.instance` remains an explicit way to create an unbound placement.

Copying a host together with **all** its attached instances, directly or through an
enclosing group, creates an independent assembly. Host and attachment IDs are remapped;
the original uncut surface, opening identity cache and current reveal paint follow the
copied host. Alignment-only attachments also count when checking complete coverage.
Copying only the host retains its visible cut geometry without attachment records.
Copying a host with a nonempty partial subset of its attachments is rejected.

Linear and radial arrays apply the same rules to each copy in a private candidate.
Out-of-plane placement, overlap, missing clearance, locks, aggregate limits or a late
invalid copy reject the entire operation without consuming document IDs or history.
Surviving original and copied openings retain their identities during later edits.
Native Ctrl-copy, `xN`, `/N` and exact spacing revisions use these shared operations.
Numeric revisions remain one Undo step, including changes to the number of copies.

Amendment accepts replacement copies with fresh IDs only within the original copy
scope: the same component definition, host face, inset and existing host, or a fresh
host with the original copied assembly's uncut surface. Existing unrelated records
cannot be changed. Ordinary amendment preserves copy counts; the explicit copy-array
policy permits positive count changes. It does not permit discarding every new
binding, substituting a different definition, or rehosting onto unrelated geometry.

Persistent document locks remain authoritative. Native preview and commit also check
editor locks on indirectly affected hosts and attachments: copying an attached
instance cannot modify an editor-locked host, and moving a host cannot carry an
editor-locked attachment. Complete assembly copies only change their fresh records.
