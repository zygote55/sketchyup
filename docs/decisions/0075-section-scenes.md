# Saved named section activation

R064.d, 2026-10-06. Named activation is document state. A scene which opts into
section state now captures the complete context-to-plane activation map, together
with its existing optional free clipping plane. An empty map turns named cuts off;
an omitted section property leaves both named activation and free clipping alone.
Plane geometry, names and appearance remain owned by the live section records.

`saved_scene.recall` restores named activation, style and intrinsic visibility in
one atomic edit and one Undo step. Native recall uses that same command, then
applies camera, temporary hiding and free clipping as view navigation. Undo
restores named activation without rewinding those temporary view properties.
Equal recalls retain the existing no-op behavior. Capture/update validate live
references and the eight-active-plane ancestor limit; recall rechecks the limit
against current context ancestry before changing anything.

Scene snapshots retain stable context and section IDs. Deleted sections, missing
contexts and sections relocated to another context are diagnosed and skipped on
recall. They are never silently replaced by a different plane. Missing section
IDs join the existing inspection/panel diagnostics. Creation and update reject
missing references; later edits may leave retained references in existing scenes.
Per-scene activation is bounded to 256 entries and included in snapshot/history
storage accounting. Plane IDs must be nonzero and unique across the snapshot.

The section snapshot JSON accepts `plane` and optional `active`, a strict array
of `{context, section}` canonical uint64 strings; context zero denotes the model.
The writer omits an empty activation array. Schema **20**, document encoding
`json-v20`, and required container feature `section-scenes-v1` make the new
semantics explicit. Earlier versions migrate with empty captured named activation;
their existing geometry, sections and saved views remain unchanged. Old-version
documents reject the new nested field. Older readers reject the new required
feature. An actual schema-19 writer fixture guards migration independently of
synthetic version changes.

This layer supplies scene ownership, history, persistence and native capture/
recall. Native section authoring, derived clipping, cap selection and export
integration remain subsequent R064 work.
