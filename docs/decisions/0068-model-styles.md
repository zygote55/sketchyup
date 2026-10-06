# Model style belongs to the document

R062.a, 2026-10-06. This layer defines persistence and editing semantics before
connecting styles to viewport drawing and native controls.

A document owns one value-type `ModelStyle`. Its display mode is `textured`,
`shaded`, `monochrome`, `wireframe` or `xray`. It stores background, ground,
front-face, back-face and edge RGB colors; ground visibility and height; grid,
axes and edge visibility; profile enablement and width; and X-ray opacity.
These settings are independent of application chrome/theme preferences and do
not modify geometry, materials, image assets or component definitions.

RGB channels are finite display-sRGB values in [0,1]. Ground height is in metres,
finite and bounded to ±1,000,000. Profile width is finite in [1,8] logical screen
pixels. X-ray opacity is finite in [0.01,0.95]. Visibility/profile flags are
booleans. Mode names are exact, case-sensitive strings. Defaults select textured
mode, a light neutral background, visible grid/axes/edges, hidden ground,
disabled profiles, width 2 and opacity 0.25. A default record is deterministic;
it never depends on the current user's application theme.

`Document::setStyle` publishes one labeled atomic edit. Identical assignments are
no-ops. Before/after records are validated and stale before-values reject without
mutation. Undo/Redo restore the complete value and the saved-state identity.
Snapshots and prepared edits retain the value. Compound scene publication includes
style differences; fixed-size history and snapshot accounting includes the record.
A style edit may be amended using the normal one-operation rule. An amendment
whose original operation did not change style cannot acquire that scope. Shared
component geometry drafts inherit the document style for context, but cannot
change it. Geometry-only site recipe validation also preserves the style.

JSON schema 17 adds a required root `style` object with exactly these 14 fields:
`mode`, `background`, `ground`, `front`, `back`, `edge`, `groundVisible`,
`groundHeight`, `gridVisible`, `axesVisible`, `edgesVisible`, `profiles`,
`profileWidth` and `xrayOpacity`. Every RGB array has exactly three numeric
channels. Unknown/missing fields, wrong JSON types, unknown modes and out-of-range
values reject. RGB bounds are checked before conversion to float, so a value just
outside [0,1] cannot round into validity. Decoding validates the complete candidate
before document restoration publishes it.

Container manifests add required feature `model-style-v1` and document encoding
`json-v17`. The feature set, encoding and payload version must agree. Older
readers reject the required feature instead of silently discarding presentation.
Schemas 1–16 migrate to the default style and retain their existing strict record
validation. Historical v16 textured fixtures remain unchanged and exercise
migration of real image assets and independent side mappings. Saving, immutable
save snapshots and verified crash recovery carry style alongside model records.

This PR does not yet apply these values to rendering, picking, exports, scene
presets or native controls. Those integrations follow in R062 and later roadmap
items. M7 delivery remains conditional on the M6 gate.
