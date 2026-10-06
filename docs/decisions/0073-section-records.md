# Persistent context section planes

R064.b, 2026-10-06. This layer adds immutable section records and active context
state to the document. Native authoring, saved-scene section selection and
viewport/export adapters follow separately; R064 is not complete at this layer.

Each plane has a stable monotonic ID, context ID, a context-local unit-normal plane,
name, fill/edge flags and RGB fill color. Context zero means the model frame;
other contexts use existing body editing frames. Names are unique within a context.
At most 256 plane records are retained, with names bounded to 1024 bytes and finite
plane/color values. The active map chooses at most one plane per context. A path
through nested contexts may contain at most eight active planes; sibling contexts
may independently use different planes.

Effective cuts are ordered from model root to the target body, using each context's
full world transform and inverse-transpose plane placement. They include only
that body's ancestor path, so a group's plane cannot clip an unrelated sibling.
Mirrors and nonuniform transforms preserve the chosen local retained half-space.
The geometry adapter from decision 0072 remains derived and never rewrites faces.

Creation, full record update, deletion and activation are atomic document edits.
Equal updates/activation are no-ops. Moving an active plane to a different context
deactivates it without replacing the destination's chosen plane. Deleting it also
deactivates it. Undo restores the exact prior record and active map; allocator
floors remain monotonic across Undo and branching. Publishing and restore freeze
caller-owned records. Read/prepared snapshots, compound edits, history byte budgets,
saved-state stamps, amendment scopes and staging include section changes.

Deleting a context preserves its section records and active references, reports
the missing context and makes those cuts inert. Undoing the deletion reactivates
the original scope. A new/relocated plane or a newly selected active plane requires
a live context. Renaming or changing display flags on an existing orphan remains
possible. Shared component-definition edits cannot author document-scoped planes
or activation; placements can carry their own independent sections.

Schema 19 adds `sections`, `nextSectionId` and `activeSections`. Container payloads
use `json-v19`, require `section-planes-v1` and repeat the exact next section ID in
the allocator manifest. IDs and contexts are canonical decimal strings, including
zero only for the model context. Arrays, fields and scalar types are strict;
unknown/missing fields, duplicate IDs/contexts, invalid active ownership and
mismatched version/feature/floor metadata reject before publication.

Versions 1–18 migrate to empty plane/active tables and allocator 1. A real version-18
writer fixture retains its geometry, styles and saved scenes unchanged. Missing
contexts survive current-format save/reopen and recovery. Older readers reject the
new required feature rather than dropping section state. This layer does not change
the earlier saved-scene camera/visibility/style/free-clip snapshot semantics.
