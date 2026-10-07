# Planar DXF export and independent validation

R077.c, 2026-10-07. Export writes an AC1032 UTF-8 ASCII DXF containing model-space
LINE, LWPOLYLINE, ARC and CIRCLE records with explicit $INSUNITS. Supported output
units are inches, feet, millimetres, centimetres and metres. All model edges,
including hidden objects and face contours, are considered without section clipping.
World geometry outside the XY plane rejects; this is not an implicit projection.
Residual Z within native modeling tolerance is written as zero.

Connected remaining edges form deterministic open or closed polylines; branch
vertices split chains. Native circular curves export analytically when their world
frame preserves circles and lies in XY. Reflection reverses the traversal used for
DXF's counterclockwise arc convention. Nonuniform/elliptical and pie curves fall
back to their chord edges with explicit counts. Native curves and body boundaries
may split an original DXF polyline into separate entities. Filled faces become
unfilled contours; no surfaces, hatches or face material fidelity is claimed.

Tags become flat DXF layers. Effective tag visibility is retained. Valid distinct
names are retained; reserved/duplicate/oversized names receive generated names with
an explicit native-to-DXF mapping. Folder hierarchy, per-body locking/visibility,
materials, textures, annotations, guides, parametric and view metadata are omitted
and reported. Export never mutates native bytes or history.

Bounds: 256 MiB source snapshot, 100,000 source vertices and edges, 100,000 output
entities, 1,024 flat layers and 64 MiB output. Export bytes are hash-verified against
the report before the shared non-replacing file publisher writes them. Existing
files and dangling symlinks reject; publication uses same-directory temporary data,
fsync, atomic non-replacing hard-link and directory fsync (ADR 0118). Reports are
returned separately; DXF itself stores its unit code and layer names.

Fixtures cover analytic sweeps/radii, layer visibility, connected edge polylines,
closed face contours, reflection, elliptical fallback, nonplanar rejection, invalid
units, source/history preservation and unsafe destinations. An independent ezdxf
1.4.4 consumer verifies exported entity inventory, units, layer assignments, hidden
state, 20/180-degree arcs, circle radius and complete drawing extents. CI installs
pinned test-only dependencies from `tests/dxf-oracle-requirements.txt` into an
isolated build venv; they are not installed with the application. R12/R2018 producer
fixtures and their regeneration script remain the independent import reference.
CLI/native workflow and final R077 acceptance follow separately.
