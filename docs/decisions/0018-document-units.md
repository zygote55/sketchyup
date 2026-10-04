# Per-document display units

Date: 2026-10-04. R039.c. Core/API/persistence slice; native preferences follow.

Geometry and automation numeric coordinates remain meters in a right-handed,
Z-up frame. A separate document preference selects `m`, `mm` or `ft-in` for human
measurement entry/display. Feet/inches uses feet for bare lengths; an explicit
suffix overrides that default. The preference never rescales geometry, component
placements, material data or allocator identities.

A new document accepts its initial units as a clean revision-zero baseline.
Changing an existing document's units is one labeled, undoable Edit, with the
ordinary revision, dirty-state, atomic-batch, save-stamp and history semantics.
Invalid enum values, stale before-values and stale revisions reject before
publication. Numeric amendment cannot change units outside the original edit's
scope. Shared-definition drafts inherit the document preference and reject
attempts to change it inside a geometry scope.

The public `document.units` batch command accepts one of the three canonical
codes. `document.describe` reports both coordinate `units: m` and `displayUnits`.
Command numeric arguments remain meters regardless of the preference. Unit edits
can be previewed and combined atomically with modeling commands.

Native model JSON advances from schema 11 to 12 and adds required `displayUnits`.
Container v2 remains unchanged; its required features add `display-units-v1` and
its document chunk uses `json-v12`. The manifest's `units: m` continues to describe
stored geometry. Readers retain explicit migration for schemas 1–11, defaulting
their display units to meters. Unknown or missing v12 unit values reject. Older
readers reject the required feature rather than silently losing the preference.

Immutable save snapshots and complete recovery checkpoints capture the preference
with the rest of the model. Reopening restores it without inventing history;
recovery remains dirty until explicitly saved. The complete v11 golden fixture
continues to exercise migration of components, tags, materials and managed assets.

## Native preferences and measurement controls (R039.d)

The normal application asks for default units on first run. The choice initializes
a pristine blank document without adding an edit; opening an existing model
retains its stored preference. File → Document units changes the current model
and optionally the default for later new documents. Cancel preserves both. A
stale dialog retains its choice with an inline error. Automation smoke/capture
and benchmark launches do not show first-run setup.

Bare lengths and coordinates follow document units in drawing, push/pull,
transforms, guides, component placement/axes origins, construction-plane origins
and Entity info. Scale factors, segment/copy counts, normals and angles retain
their dimensionless/angle semantics. Explicit suffixes override document units.
The Measurements label and accessible name, viewport unit/grid hints, tape
readout, selection area and Info lengths/area/volume follow the same preference.
Feet/inches lengths use a compound readout; areas and volumes use square/cubic
feet. Fixed display precision retains length round trips within modeling
tolerance. Formatted numeric input omits digit grouping and uses the active
locale's decimal separator. Unedited Info fields preserve exact original values.
