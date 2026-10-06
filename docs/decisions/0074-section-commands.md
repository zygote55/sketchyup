# Shared section authoring and bounded inspection

R064.c, 2026-10-06. Section records use the same command, staging, transaction and
inspection paths as other document edits. Native controls and clipped rendering/
export follow separately; these commands do not destructively cut native faces.

`section.create` takes an explicit name, context, `local`/`world` space and plane
object (`normal` plus signed `offset`, in metres). Its unit normal points into the
retained half-space. Optional fill, edges and RGB color use the record defaults.
World input is transformed into the selected context with the inverse transpose;
mirroring, shear and nested placement preserve the requested half-space.
The result returns stable IDs in `createdSections`.

`section.update` replaces name, context, plane and all three display properties
for an inspected section ID. A context move deactivates the source scope without
silently replacing the destination's selection. Existing orphaned records may be
updated in local coordinates; creating, relocating or using world coordinates
requires a live editing context. `section.delete` removes the record and any active
selection in one edit. `section.activate` chooses an inspected plane belonging to
that context; an explicit null turns that context off.

Authoritative validation checks unit normals, canonical uint64 identities,
name/context ownership, count and active-path limits, field types and finite
values. Published JSON Schemas include complete scalar/array shapes and bounds.
Batches preserve geometry, support immutable preview/private staging and atomic
rollback, and publish one Undo item. Equal whole batches retain the existing no-op
rejection. Shared component-definition scopes reject section commands explicitly.

Bounded read-only inspection adds:

- `sections.query`: paginated records, optionally filtered by context, including
  diagnosed missing contexts and active state.
- `section.describe`: one record, its local coefficients and world coefficients
  when its context exists.
- `sections.effective`: paginated root-to-body cuts from persisted activation,
  limited to that body's ancestor path. Body zero describes the model scope.

Each record distinguishes retained active references from effective live contexts.
A missing context has null world coefficients. These are document queries;
saved-scene recall restores named activation through document history (decision 0075). Standard revision
binding, cursor validation, immutable sessions and response budgets apply.
