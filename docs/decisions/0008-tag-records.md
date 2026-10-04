# Tags remain separate from geometry ownership

R034.a, 2026-10-04.

A document owns immutable tag/folder records with stable, monotonic IDs. An
entity has one optional tag; ID zero represents Untagged and is never a stored
record. Folders organize tags and cannot be assigned to entities. Scene parents
continue to define transforms and ownership independently of tag parents.

A tag is effectively visible only when it and every folder ancestor are visible.
An entity is hidden when its own or a scene ancestor's tag is invisible, or an
existing persistent/temporary hide flag applies. Visibility changes never replace
geometry records. Locked entities may be hidden through tags, but assigning a
new tag to a locked entity rejects like other entity metadata changes.

Component placement-root tags belong to individual placements. Canonical roots
are Untagged; canonical member tags are shared and require explicit component
editing scope. Global tag-table edits are outside that scope. Tag removal rejects
while any scene or canonical member uses it; folders must be empty before removal.

Consolidation partitions raw records by their complete inherited tag sets. This
keeps different visibility assignments separate; each compatible partition can
still weld in a single compound undo item. Removing a group boundary removes its
boundary tag along with its name/properties; member assignments remain intact.

The table is bounded to 1,024 tags/folders, a 32-node ancestor chain and 1,024-byte
nonempty names. Exact sibling names are unique across tags and folders. Names
are case-sensitive. Tag/folder kind cannot change for an existing identity.
Transactions freeze caller-owned records, validate references and cycles before
publication, and include metadata in undo, redo and history budgets. Undo never
rewinds allocator floors.

JSON schema 9 and container feature `tags-v1` persist the table, assignments and
allocator. Versions 1–8 migrate to an empty table and Untagged entities. Container
manifest floors must match the payload. The container envelope remains version 2.
