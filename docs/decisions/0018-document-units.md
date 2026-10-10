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

## Display precision (R084.r)

Each document also stores a display precision: Full, or a fixed number of decimal
places for lengths in the active units. Fixed ranges are 0–6 for `m`, 0–3 for `mm`
and 0–3 for the inches part of the `ft-in` compound readout. Areas and volumes use
the same number of decimals in their squared or cubed unit. Fixed precision keeps
trailing zeros and rounds once, so `0.9999 m` at 2 shows `1.00 m`, and inches that
round to 12 carry into feet.

Full is stored as `-1`. It is the default for new documents, for every unit change
that does not name a precision, and for every migrated schema. Full is exactly the
earlier fixed formatting: trimmed lengths with 8 (`m`), 5 (`mm`) or 6 (inches)
decimals, and areas/volumes with 8 significant digits. Existing documents therefore
display unchanged. A sentinel was chosen over extending the fixed ranges because
the earlier output trims trailing zeros and uses significant digits for areas,
which no fixed decimal count reproduces.

Precision is display only. It never rounds geometry, command arguments or typed
input. Read-only readouts follow it: Measurements previews, tape, Entity info
lengths/area/volume, selection area, viewport hints, dimension annotations and
measured drawings, and human-formatted assistant previews. Editable numeric
fields keep Full formatting, and unedited Info fields preserve exact original values.
Live Measurements previews now format through the same function. They previously
showed raw metres even in `mm` or `ft-in` documents.

A precision change is one labeled, undoable Edit with the ordinary revision,
dirty-state, atomic-batch, save-stamp, amendment and stale-revision rules. The
after-state precision must be valid for the after-state unit. Shared-definition
drafts cannot change it. `document.units` accepts an optional `precision` with the
value `"full"` or an integer in the unit's range, and rejects out-of-range values
before publication. `document.describe` and the inspection document query report
`displayPrecision` in the same form.

Native model JSON advances from schema 24 to 25 and adds required integer
`displayPrecision`. The container adds required feature `display-precision-v1` and
uses `json-v25`, so older readers reject rather than lose the setting. Following
[ADR 0108](0108-public-native-format.md), the public schema name advances to
`sketchyup-document-v25`. Schemas
1–24 migrate to Full. Save snapshots and recovery checkpoints capture it. The
desktop dialog lists Full and each fixed precision with a sample formatted by the
real formatter. Selecting another unit repopulates the list and selects Full.
"Use for new documents" also stores the precision. First-run setup asks only for
units.
