# Atomic library insertion

R079.c, 2026-10-07. Inserting a component bundle prepares its recursive definition
and resource closure privately, validates the resolved placement, then publishes
one composed edit. Undo removes the new placement and all newly imported records;
redo restores their bindings. Failed placement, resource validation, allocation or
budget checks leave the destination and its history unchanged. Display units,
presentation defaults and existing geometry remain destination-owned. Placement
uses model metres and an explicit transform/parent, including mirrored transforms.

Asset reuse requires equal media type and byte-for-byte equal embedded payload.
SHA-256 is only an indexing aid; a hash match alone never substitutes a resource.
Materials reuse only an exactly matching name, color, opacity and remapped asset.
Missing referenced assets reject. A conflicting material or asset name is renamed
with a bounded ` (library N)` suffix; existing resources are never overwritten.
Tags and their parent folders are imported independently, preserving visibility.
Their names are made unique within each destination folder. Names are bounded
without splitting a UTF-8 sequence.

Each insertion receives fresh component definition identities, including nested
references, so later definition edits do not unexpectedly update an earlier library
insertion or the bundle on disk. Definitions, member tag/material/reference-image
assignments and nested references are remapped before publication. Canonical member
identities remain scoped to their definition. Existing native component limits and
a 256 MiB source/destination/result snapshot admission budget remain enforced.

Tests cover recursive persistence, reflected placement, one-step resource undo/redo,
source and existing-record preservation, identical resource reuse, conflicting names,
independent repeated insertions and atomic rejection for locked/missing parents or
invalid transforms. Native selection/search is the following R079 workflow layer.
