# Formline import and complete M4 persistence

Date: 2026-10-04. R037. Native schema remains 11; packaged assets remain container v2.

## Source contract

The removed Formline prototype's validator, mesh creation and OBJ unit annotation
were inspected on 2026-10-03. Those recorded source excerpts establish the contract
below. The importer restores no prototype runtime. Fixtures are original synthetic
project data.

A source is a JSON object with `format: "formline"`, numeric `version: 1`, a string
`name` (up to 200 UTF-16 code units), and at most 1,000 `objects`. Each object has:

- A unique nonempty string `id` (at most 100 code units), `name` (at most 200),
  `type` (`box` or `cylinder`), and six-digit hexadecimal `color` prefixed by `#`.
- Finite numeric `x`, `y`, `z`, `w`, `h`, `d`, `rotation`, each of absolute value at
  most 10,000; dimensions must be at least 0.01.
- Optional `visible`; only boolean false hides the object, as in the prototype.

Extra fields are ignored and reported. Present non-boolean visibility values are
normalized to visible and reported. Invalid Unicode that cannot survive UTF-8
conversion rejects instead of silently changing a name or source identity. The
native record validator also applies, including its string and geometry limits.
Input is bounded to 32 MiB and must be a regular file. Expanded geometry must fit
native document/container limits before publication. No source resource is loaded.

## Geometry and identity

Source units are meters and the source frame is right-handed Y-up. The proper
rotation `C(x,y,z) = (x,-z,y)` converts to native Z-up. Source Y is the base height:
the prototype positioned centered primitives at `y + h/2`. Its Y-axis yaw in
degrees becomes a native Z-axis rotation with the same sign.

Boxes use width × height × depth. Cylinders use width as their diameter and have
48 sides; **the prototype ignored cylinder depth**. Import preserves that shape,
records the original depth, and reports any difference from width. Native
cylinders are editable closed faceted prisms, without invented analytic curves.
Both primitives retain outward winding.

The model becomes a named group with a child geometry context for every object.
`formline.sourceId` and `formline.type` preserve source identity independently of
native IDs. The report includes the full source-to-native mapping. Equal RGB
colors share a native swatch applied to both sides; original RGB fallback values
are retained too. Names, visibility and transforms survive save/reopen. The whole
import is one undoable edit in a new dirty document.

## Desktop and CLI lifecycle

File → Import Formline parses and validates privately before asking about unsaved
current edits. Cancel leaves the current document intact. Success clears the native
save path, opens a readable conversion report and fits the imported model. Saving
asks for a native `.sketchyup` path. Opening the imported group makes its faces
available to normal modeling tools.

`sketchyup-cli --import-formline source.formline --output copy.sketchyup` exposes
the same converter and a structured `importReport`. It can also run a query or
editing script against the imported document. `--input` and `--import-formline`
are mutually exclusive. An output resolving to the source path, including a
symbolic-link alias, rejects before writing.

## Complete persistence and geometry validation

Earlier M4 layers already added each authoritative record to native schema 11.
The complete golden fixture combines hierarchy, shared and unplaced definitions,
mirrored/nonuniform instances, tags, visibility/locks, properties, curves, guides,
front/back materials, embedded/missing assets and retired allocator floors. R037
verifies their combined relocation, save/reopen, immutable save snapshots, and
shared editing after reopening. It does not serialize undo history.

The 48-sided cylinder exposed two solid-classifier defects. Rounded triangle
contacts on an authoritative shared edge could fail a collinearity test that
extrapolated a short noisy segment. Endpoints contained within the same shared-edge
tolerance tube now establish the whole segment's coverage before the existing
multi-edge fallback. Tiny triangle normals also incorrectly used a linear
threshold for an area-valued cross product. Cross products now use the area
threshold and explicit normalization. Ordinary, 1 cm and tall cylinders have
regressions alongside the existing self-intersection/nonmanifold fixtures.
