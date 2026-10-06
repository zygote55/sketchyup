# Versioned bounded inspection

R040.a adds a read-only inspection boundary alongside the experimental local
command driver. It does not expose the legacy full-model `executeQuery` results
as provider tools. R040.b adds [retained snapshots](0020-inspection-snapshots.md); R040.c adds [desktop view feedback](0021-desktop-inspection.md);
transactions and transports remain R041–R043.

## Requests and identity

Every request requires `apiVersion: 1`, `documentId`, `expectedRevision` and
`query`. Revisions and entity IDs are canonical unsigned 64-bit decimal strings.
The CLI's `--inspect NAME --input FILE` convenience form obtains this envelope
from that explicit file; `--inspect-file FILE --input MODEL` accepts the complete
envelope. Inspection rejects editing, saving and other operation flags. Results
use compact JSON on stdout; structured errors go to stderr with exit status 1.
File-only inspection explicitly reports editor selection as unavailable.

References contain `documentId`, `contextPath`, `body`, `kind` and `id`.
`contextPath` lists ancestor body IDs from the document root to the immediate
parent, excluding the referenced body. For kind `body`, `id` equals `body`.
Other namespaces are `vertex`, `edge`, `face`, `guide` and `curve`; their IDs
are local to that body. A definition ID returned for a component is a distinct
document-level namespace, never a scene body target. A changed hierarchy, wrong
document or missing typed ID is rejected without positional substitution.

The C++ editor adapter supplies a `Selection` belonging to the document's current
session. This is ephemeral context, not a request-supplied claim about a desktop
selection. A null editor pointer means unavailable; it does not mean empty.
Calls run synchronously under the caller's document/editor ownership. Retained
snapshots and concurrent desktop dispatch are outside this slice.

## Registry and limits

The executable registry generates [the versioned request schemas](../api/inspection-v1.json)
and validates their types, required fields, enums, bounds and unknown fields.
Document identity, uint64 range, reference context and cross-field preconditions
are checked afterward. Tests execute every registered operation and compare the
published artifact with the live registry. Discovery is included under
`sketchyup-cli --capabilities` → `inspection`.

Requests are at most 16 KiB of compact JSON; the file reader also bounds raw input.
Each response is at most 256 KiB of compact JSON. Page size defaults to 50 and is
limited to 100. A page's record budget is 192 KiB to reserve space for its envelope
and continuation token. Records are serialized only for the requested page, plus at most
one candidate that exceeds its remaining byte budget; byte pressure can return fewer rows than requested. An individual
record that cannot fit fails explicitly. Strings follow native model limits;
metadata is not truncated into an ambiguous value. Model size limits bound scans
and geometry work, but these synchronous queries have no wall-clock deadline.

Pages return `items`, `total` and nullable `nextCursor`. Scene and topology rows
use ascending stable IDs; property rows use key order; loops and curve bindings
use their geometric order. Tokens encode an offset and SHA-256 fingerprint of
the complete query envelope/filter plus supplied editor selection, context,
hiding and locks. Page size may change between requests. Changing other inputs
invalidates the token. Tokens are continuation aids, not authorization secrets.
Revision mismatch rejects before token use. No full body/topology JSON dump is
built and then sliced. Large face loops and curve bindings have their own pages.

Errors include `UNSUPPORTED_VERSION`, `UNSUPPORTED_CAPABILITY`, `INVALID_REQUEST`,
`LIMIT_EXCEEDED`, `WRONG_DOCUMENT`, `STALE_REVISION`, `STALE_SELECTION`,
`UNAVAILABLE_CONTEXT`, `CONTEXT_MISMATCH`, `NOT_FOUND`, `INVALID_TARGET`,
`UNSUPPORTED_TARGET`, `INVALID_CURSOR` and `INVALID_MEASUREMENT`.
Inspection never edits geometry, history, dirty state, files or editor state.

## Queries and geometry semantics

| Query | Result |
| --- | --- |
| `document.describe` | Counts, display units, dirty state and available editor context; no scene dump |
| `selection.get` | Paged typed selected entities and owner summaries |
| `entities.query` | Immediate children by default, optional subtree/name/kind filter and hidden inclusion |
| `entity.describe` | Typed reference, owner metadata/counts, transforms and material assignments |
| `entity.properties` | Paged semantic key/value data for a body |
| `component.instances` | Paged scene instances of a definition |
| `topology.query` | Paged vertices, edges, face summaries, guides or curve parameterizations |
| `topology.face_loop` | Ordered vertices/positions in one closed face loop |
| `topology.curve_edges` | Ordered oriented edge bindings for a curve |
| `topology.incidence` | Incident edges for a vertex, or face-loop incidences for an edge |
| `geometry.diagnose` | Bounded findings for one body record, local volume and explicit analysis completeness |
| `measure.entity` | Bounds, unique edge length, surface area and valid solid volume |
| `measure.distance` | Distance between two explicitly framed points |
| `measure.angle` | Oriented angle around the specified normal, in radians |

Coordinates and lengths use meters, areas m², volumes m³; Z is up. Display units
never rescale API values. Topology and measurements require `space: local|world`.
Local space is the owning body's frame; group measurements express descendants
in that group's axes. Matrices are column-major affine 4×4. Face normals follow
ordered loop orientation, including reflection. Curves return center plus
`cosineAxis*cos(angle) + sineAxis*sin(angle)` so nonuniformly transformed circles
are represented without inventing a world-space radius. Angles are radians.
Point-based local measurements require a body `frame`; world measurements reject
that field. Input and output use the same stated frame, without implicit scaling.

Body measurements include descendants, even hidden ones, and state this explicitly.
Volume is nullable: multipart assemblies are not assumed to form one watertight
solid, and faces/edges/guides have no solid volume. Infinite guides have null
length plus `infiniteLength: true`. Bounds may be null for empty/infinite-only
geometry. Core tolerance is reported as 1e-7 m; it is not a guarantee that every
derived area, angle or volume has that absolute error bound.

Discovery excludes persistently or temporarily hidden bodies by default, even
when the editor displays hidden geometry. `includeHidden` opts in. Summaries
report persistent/effective hidden state, inherited locks, editor availability
and the show-hidden setting. Vertex/curve visibility follows the owning body;
typed face/edge/guide hiding is preserved where supported by selection. This
reports visibility state rather than claiming screen occlusion or pixel coverage.
Names and properties remain untrusted model data, never tool instructions.

## Geometry diagnostics

`geometry.diagnose` requires a typed body `target` and the usual document/revision
guards. It reads that record's native geometry, including hidden geometry, in its
local frame. `scope: body_record` and `includesDescendants: false` explicitly exclude
child records: inspect assembly members separately. Mirrored or nonuniform placement
does not alter native shell orientation or the reported local material volume.

The [diagnostic contract](0053-geometry-diagnostics.md) defines exact versus lower-bound
counts, analysis completeness, typed reference samples and repair eligibility. Each
query retains all scalar findings while limiting references to 192 KiB in aggregate,
in addition to the kernel's 64-reference/category cap. This covers deeply nested
contexts with large IDs without losing defect counts to a response-size error. Byte
truncation sets `truncated` and clears `reverseShellsEligible`, just like sample-count
truncation. Eligibility describes geometry only; it never grants edit permission or
bypasses context, locks, shared-component scope, preview or explicit Apply.

The same query is available through CLI inspection, retained snapshots, native
assistant inspection and MCP discovery/dispatch. No edit, selection or view change
is performed by diagnosis.
