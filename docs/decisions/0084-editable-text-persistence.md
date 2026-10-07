# Editable text source and cached geometry

R066.c, 2026-10-06. A body can carry an optional typed `TextSource` alongside its
ordinary native surface. Source records contain the UTF-8 string, requested font
family/style, metre dimensions, line spacing, substitution policy, resolved font
provenance and a generated-geometry digest. Fonts are neither embedded nor needed
to load, render, transform, export or recover cached geometry. Regeneration and
font-availability reporting are subsequent authoring workflows.

Records participate in immutable body snapshots, atomic edits, history accounting,
Undo/Redo, recovery and component member storage. Component normalization moves the
source with geometry into its canonical member. Whole-body copies preserve source;
partial geometry extraction produces ordinary baked geometry. Manual geometry edits
can retain source, but authoring must compare its digest before replacing geometry.
The digest is a consistency check, not an authenticity or security claim.

The source validator checks UTF-8, string/line bounds, finite dimensions, font and
glyph counts, unique font descriptors, substitution authorization and SHA-256 syntax.
The JSON decoder requires all version-1 source fields and rejects unknown fields,
wrong types, null records and unsupported versions. Generic entity properties cannot
forge or overwrite this typed metadata.

Native JSON schema 22 and container encoding `json-v22` require `editable-text-v1`.
Schemas 1–21 migrate without text sources. An actual schema-21 annotated-panel fixture
checks migration. Old readers reject the new required feature rather than dropping
source data. GLB retains native surfaces while explicitly reporting omitted editable
text sources. Source strings and generated geometry remain portable in native files;
font provenance supports explicit decisions when editing on another machine.
