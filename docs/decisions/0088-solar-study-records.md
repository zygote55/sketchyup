# Sun study records, scenes and render snapshots

R067.b, 2026-10-06. A document owns complete `SolarSettings`: enable/shadow flags,
latitude, east-positive longitude, model north rotation and explicit civil time with
UTC offset. The algorithm is pinned to `noaa-meeus-geometric-v1` (contract 0087).
Defaults are deterministic with solar lighting disabled. Settings never come from a
network location service, current wall clock, application preferences or OS time zone.

Settings use immutable read snapshots and the ordinary atomic Edit path, including
Undo/Redo, prepared edits, batch composition, durable recovery and stale/replacement
scope checks. Equal settings are a no-op. Shared component edits cannot modify this
document-level state. Display unit changes do not reinterpret angles or civil time.

Saved scenes can opt into a complete sun study independently of camera, style,
visibility and sections. A solar-only scene is valid. Recall applies the saved study
as a model edit, and Undo restores the preceding study. Scenes that omit this property
do not change solar state. Source records and scene copies retain exact values.

Schema 23 and required container feature `solar-study-v1` prevent older readers from
discarding study settings. The strict record contains an explicit algorithm identifier
and every published field. Unknown/missing/null fields, invalid numeric types, invalid
Gregorian dates and out-of-range resolved UTC instants reject before restore. Versions
1–22 migrate to the deterministic disabled default; an actual schema-22 text sign
fixture verifies preservation of editable sources and cached geometry.

Render snapshots retain the document's complete solar settings. The GLB handoff
manifest carries both those frozen inputs and their calculated UTC instant, unit
direction, elevation, geographic azimuth and geometric horizon classification. These
metadata do not yet change Blender lighting; adapter lighting conversion is R069.
No solar metadata is embedded as geometry. Shared authoring and native controls follow.
