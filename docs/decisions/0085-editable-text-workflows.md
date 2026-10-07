# Shared editable-text workflows

R066.d, 2026-10-06. `text.create`, `text.update` and `text.bake` use the bounded
font worker and shared atomic command/history path. Create requires a name, source
string, font family and nominal em height in metres. Optional style, extrusion
depth, line spacing, substitution policy, parent and parent-local position are
explicit. Creation produces a group whose own native surface contains every glyph
region; transforms, materials and selection use ordinary model behavior.

Update is partial: omitted settings retain their saved values. A rename or unchanged
settings do not load fonts. `regenerate:true` explicitly rebuilds unchanged source.
Regeneration preserves body identity and placement but gives every glyph vertex,
face and edge a fresh identity. Old glyph identities have explicit empty descendant
sets, so coincident new edges are not mistaken for semantic continuations and attached
annotations become visibly missing. One Undo restores geometry, source and references.

All returned geometry is validated privately before publishing one edit. Failed
font resolution or a later command failure leaves the live model unchanged. Locked
objects reject edits. Independently edited geometry is detected by a digest of the
local native surface and analytic records; regeneration refuses to overwrite it.
Per-face/per-edge appearance must be cleared or baked before regeneration, since its
glyph identity mapping would be lost. Body placement and whole-body appearance stay.
Baking removes source metadata while preserving exact geometry, including manual edits.

Missing family/style requires explicit `allowSubstitution:true` or an installed font
choice. When a previously used family/style resolves to a different font-table
fingerprint, regeneration requires `acceptFontChange:true`. Reports retain actual
font identities, substitutions and fallback. Native files remain readable without
fonts; inspection reports cached provenance and says local availability is unchecked.
No query loads fonts or regenerates a model. The native editor may check installed
family availability separately before requesting regeneration.

`texts.query` is paginated. `text.describe` returns complete saved settings/provenance
and whether cached geometry still matches its source digest. Both use the existing
revision/identity inspection envelope. Instance-specific and shared-definition edit
paths support scoped text commands through the ordinary component command mapping.
All seven capability artifacts are generated from the executable catalogs.

Input strings, geometry, worker bytes/time and document complexity retain the
bounds in contracts 0082–0084. Text shaping uses only local fonts and does not touch
provider credentials or settings. Native authoring UI follows this command layer.
