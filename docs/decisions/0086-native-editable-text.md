# Native editable 3D text

R066.e, 2026-10-06. View → 3D text opens the Organization tray's Text panel.
Create and Edit expose names, Unicode/multiline strings, local font family/style,
nominal em height, extrusion depth, line spacing and substitution policy. New text
uses a position in the current editing context. Lengths accept explicit units and
the document's default units. Untouched values retain their exact stored precision.

Missing local families/styles are reported while cached geometry remains usable.
The editor never silently rewrites a missing saved font to the first installed font.
Substitution and changed-font acceptance are explicit controls. A rename needs no
font access. An unchanged editor creates no history. Regenerate can explicitly
rebuild unchanged source. Bake removes source while retaining exact geometry, and
Undo restores it. Independently changed geometry remains visible and cannot be
silently overwritten by source regeneration.

Save prepares the shared command batch on a private immutable snapshot in a worker
thread. The font helper remains a separate bounded offscreen process. The GUI event
loop remains responsive; Cancel requests interruption and discards the dialog's
completion callback. Closing the panel cancels and joins its outstanding workers.
No background operation accesses the live document or widgets. Only the GUI thread
publishes the completed prepared edit, after its revision and snapshot checks.
Changes made while generation is running leave the stale result unapplied.

Editing or baking text inside a component uses the instance-specific command path;
the editor states that only that component instance becomes unique. Shared-definition
text editing remains available through the explicit shared command API. New text
within an instance similarly uses its current context with instance-specific scope.

Text geometry uses ordinary native surfaces, materials, transforms, sections,
selection, export and rendering. Fonts are not embedded and no provider or user
credential settings are changed. Input/framebuffer fixtures cover units, Unicode,
background responsiveness, cancel/stale results, font absence, history and portable
save/reopen behavior on native display backends.
