# Configurable component interface

Status: **Accepted (2026-10-16)** for the decisions below; the layers are implemented by
R094.a–h (see "Layer plan"). Roadmap [R094](../PR_ROADMAP.md#r094) (M10, scope row
[F02](../SCOPE.md)). This builds on [ADR 0165](0165-constrained-component-parameters.md)
(R093). The interaction design is the owner-approved
[R094 UX design (canvas; private to the owner)](https://claude.ai/artifact/2jGARq96WrYiFnSAmcyptY),
approved on 2026-10-16. It has four artboards: **Main** (placement parameters in the
Tray), **Authoring** (definition parameters and drivers), **States** (errors, warnings
and refusals) and **Assemblies** (insert with values). This ADR does not repeat that
design. It decides how the code delivers it, names where the design meets the code or
[UX_DESIGN](../UX_DESIGN.md) awkwardly, and splits the work into layers. The choices
that belong to the owner are listed under "Open for the owner"; work proceeds on the
stated defaults.

## Context

R094 must "add parameter inspector, definition authoring, regeneration previews and
reusable architectural/furniture assemblies". Its Verify line: "instance overrides and
shared defaults behave predictably; regeneration preserves edit scope/materials and
warns about incompatible manual changes".

### What R093 provides

ADR 0165 and its R093.b implementation notes (branch `build/r093b-component-parameters`)
define the model; R093.c (schema 28) and R093.d (commands and queries) are in progress.
This ADR uses the names of ADR 0165 §6.

- **Records.** A definition has at most one parameter record (ordered parameters and
  drivers). A top-level placement holds overrides for input parameters. Nested
  placements take their values from defaults or from the parent's `childParameter`
  drivers.
- **Variants.** `ComponentVariantEngine` (`src/core/component_regeneration.hpp`)
  derives a placement's geometry from the template and its values. Placements with
  equal values share one `VariantPtr`, and a variant shares every member a driver does
  not change with the template. `Document::componentVariant(placement)` is null when
  the placement shows its template.
- **Identity.** Regeneration keeps the placement root's and every template member's
  scene ID; repeat copies keep theirs while they exist. Stretch, position, rotation and
  visibility never change topology IDs. So a pick (body, face, edge, vertex) on a
  variant names the same records as the same pick on the template.
- **Commands.** `component_parameter.define`, `.update`, `.rename`, `.remove`,
  `.drive`, `.undrive`, `.rebind`, `.override` and `.reset`. Results add
  `regenerated {placements, variants}` and `parameterWarnings` (drivers that became
  `changedSinceBound`).
- **Queries.** `component_parameters.describe {definition}`,
  `.values {body}` (each value with its SI value, display string and source:
  `override`, `default`, `derived` or `parent`, plus driver states) and
  `.evaluate {definition, overrides?, expression?}` (a read-only dry run that returns
  values or the first error with code, offset and message).
- **Errors.** `PARAMETER_SYNTAX`, `PARAMETER_NAME`, `PARAMETER_TYPE`,
  `PARAMETER_CYCLE`, `PARAMETER_DOMAIN`, `PARAMETER_DRIVER_TARGET`,
  `PARAMETER_REGENERATION` and `PARAMETER_TEMPLATE_SCOPE`, plus the existing
  `LIMIT_EXCEEDED`, `LOCKED_ENTITY`, `NOT_FOUND`, `INVALID_TARGET` and
  `STALE_REVISION`. Each carries the definition, the parameter name or driver ID and,
  for expressions, a 1-based byte offset and an excerpt of at most 64 bytes.
- **Scope rules** (ADR 0165 §4). `component.edit` through a placement is allowed only
  while that placement shows its template; otherwise it is refused with
  `PARAMETER_TEMPLATE_SCOPE`. `component.edit_instance` makes the placement unique and
  promotes its values to the copy's defaults (`parametersPromoted: true`).
- **Assistant.** `assistantVisible()` hides every name that starts with
  `component_parameter` (which covers `component_parameters.*`); the assistant tool
  surface baseline does not change.
- **Rendering** (R093.b notes). The viewport and inference still match scene records
  against definition members, so a variant placement renders with its own meshes, not
  shared ones. Instanced variant rendering was left to R094.

### What the desktop has today (surveyed for this ADR)

- **Tray.** `OrganizationPanel` (`src/app/organization_panel.cpp`) is one `QTabWidget`
  with 12 tabs: Outliner, Tags, Info, Materials, History, Styles, Scenes, Sections,
  Annotations, 3D Text, Sun, Images. Its focus proxy maps hard-coded tab *indexes* to
  panels (`organization_panel.cpp:330-344`). The Tray is not a stack of collapsible
  sections; only the Sheets Tray (ADR 0164 §7) uses `CollapsibleSection`
  (`src/app/collapsible_section.hpp`, 28 px header, one keyboard stop, accessible
  description "Expanded"/"Collapsed").
- **Entity info (model).** `EntityInfoPanel` (`src/app/entity_info_panel.cpp`, 383
  lines) shows **read-only labels** (name, selection, origin, bounds, length, area,
  volume, tag, materials, semantic properties) in a `QFormLayout` with
  `WrapLongRows`. Editing happens in a modal "Edit entity" dialog (F2) that commits
  through `Viewport::organize`. The Sheets Entity info (`sheet_item_info.cpp`) edits
  inline and commits on `editingFinished` (Enter or focus loss).
- **Committing.** `Viewport::commitCommands(commands, shared)`
  (`viewport_components.cpp:12`) wraps the commands in the component scope command and
  calls `executeBatch`. It passes **no history label**, so a one-command batch gets the
  command's catalogue label and a longer one "Command batch (n operations)". The batch
  request already accepts `history: {label}` (`commands.cpp:585-600`, up to 512 bytes);
  the Sheets code uses it (`window_sheets.cpp:753`).
- **Dry runs.** `ToolSession::preview` → `previewBatch` (`tool_session.cpp:28`) stages
  any batch without committing and returns the changed bodies' geometry with world
  transforms; `Viewport::previewCommand` (`viewport.cpp:2213`) turns that into preview
  edges. Hosted placement uses it for its live ghost.
- **Queries in process.** `inspectDocument(doc, request)` (`src/automation/inspection.hpp`)
  answers the shared queries on the GUI thread; `DesktopInspection` wraps it for MCP.
- **Component context.** Double-click or Enter on a selected placement calls
  `Viewport::enterContext` (`viewport_selection.cpp:816`, `:850`). The scope is the
  enclosing placement (`Viewport::componentScope`). The banner `componentBanner_` is a
  rich-text `QLabel` parented to the viewport (`window.cpp:293-305`), filled by
  `Window::syncComponentActions` (`window_components.cpp:136`) with "Editing *name* ·
  Changes affect n instances · Make unique". Breadcrumb and banner are focusable and
  return Backtab out of their links (`backtabOutOfLinks`).
- **Component menu.** Make component (`G`), Place component…, Replace component…,
  Change component axes…, Make component unique, Browse component library…, Save
  component to library…, then the hosted actions (Set glue face…, Attach component to
  selected face, Bind…, Detach, Bake) (`window_components.cpp:12`,
  `window_hosted.cpp:37`).
- **Library.** "Browse component library…" is a **dialog** (`window_library.cpp:52`)
  over a flat user folder (`QSettings` `libraryFolder`, default
  `AppDataLocation/library`) scanned by `scanLibraryDirectory` for `.sketchylib`
  bundles. A bundle requires a bounded PNG thumbnail (`library_bundle.cpp:56`).
  `insertLibraryComponent` (`src/io/component_library.hpp`) is a direct core edit
  ("Insert library component"), not a command, and every insertion creates
  **independent** definitions ("Imported definitions remain independent of prior
  insertions"). Nothing is installed under `share/sketchyup` today except docs,
  examples and licences.
- **Hosted placement.** Shift+H (`Viewport::beginHostedPlacement`,
  `viewport_hosted.cpp:157`) needs an existing placement **and** a host face selected,
  previews `component.attach` through `previewCommand`, and accepts typed anchor
  coordinates or an inset (`hostedMeasurements`).
- **Rendering.** `BodyCache.source` and `PlacementKey` (`viewport.hpp:430-470`) share
  meshes between placements whose records match a canonical definition member
  (R082.dd); inference indexes each definition member once in local space (R082.ee).
- **Shortcuts.** `Window::action` and `ShortcutBindings` (ADR 0148, 0153, 0164 §4).
  Plain and Shift-only keys are `Qt::WidgetShortcut` on the viewport. In use with
  Shift: `Shift+B`, `Shift+F` (Follow Me), `Shift+H` (Attach component to face),
  `Shift+O`, `Shift+Z`, `Shift+F6`; `Ctrl+Shift+D` is the Annotations "Dimension…".
  **`Shift+D` and `Ctrl+Backspace` are unbound.**
- **Focus and keys.** F6 cycles `modelRegions_` (`window.cpp:943`). The viewport claims
  digits for the Measurements box through `ShortcutOverride`; Esc backs out one level.
- **Dialogs on Wayland.** `SettledInputDialog` (`settled_dialog.hpp`) closes only once
  it is the focus window; `ModalReturn` (`window.cpp:134`) returns activation to the
  window when a modal closes (ADR 0164, R091.h conventions).
- **Theme.** `ThemeColors` has `warning`, `failure` and `selectionStroke` since R091;
  there is no informational blue. Accent-filled controls use the inset focus ring.
- **Tests.** About 60 native `*_input_tests` built by `sketchyup_native_fixture()` and
  listed in `.github/workflows/native.yml` (X11 under `xvfb-run` and Wayland under
  `scripts/test-wayland.sh`, each at 1× and 2×; some suites also at 1.5 and 1.75).
  Keyboard and accessibility: `keyboard_reachability_tests`,
  `native_accessibility_audit`, `visible_focus_input_tests`, `shortcut_input_tests`,
  `text_size_input_tests`, `theme_input_tests`, `tests/accessibility_bridge_test.py`
  (AT-SPI). Related suites: `entity_info_input_tests`, `component_input_tests`,
  `hosted_input_tests`, `library_input_tests`.
- **Sidebar.** The right sidebar has a fixed width. Making it horizontally resizable is
  the owner's requirement and is R094.a, being implemented on
  `build/r094a-resizable-sidebar` (the design's separator: drag, double-click resets,
  Ctrl+Alt+[ and Ctrl+Alt+] with focus on the separator, 240–720 px, default 360 px).

## Where the design meets the code awkwardly

1. **Sections versus tabs.** The design draws the Tray as stacked sections (Entity
   info, Parameters, Outliner, History; Components). The model Tray is a 12-tab widget
   with index-based focus. Converting it is out of scope. → §1, §6.
2. **Read-only Entity info.** Parameters need inline editable rows with per-row reset;
   the model Entity info has labels and a dialog. → §1.
3. **No history labels from the viewport.** `commitCommands` cannot label an entry, so
   every parameter change would read "Set component parameter overrides". → §1.4.
4. **Editing a definition "at its default values" through a placement that has its own
   values.** R093 refuses that scope (`PARAMETER_TEMPLATE_SCOPE`), and about 90 direct
   `doc_.bodies()` reads in the viewport tools mean the viewport cannot show one
   geometry while the tools act on another. → §4.
5. **Make unique and edit** needs promotion without a geometry edit;
   `component.edit_instance` promotes only as part of an edit, and adding a field to
   the assistant-visible `component.make_unique` would change the assistant tool
   surface baseline. → §4.2.
6. **Library insertion creates independent definitions and is not a command.**
   Placing a second door from the library would create a second door definition, so
   placement values could not be overrides on one definition. Inserting, overriding
   and attaching to a wall would be three entries. → §6.
7. **Hosted placement needs an existing placement.** The design places a door straight
   from the library onto a hovered wall face. → §6.3.
8. **Variant placements do not share meshes** (R093.b). → §7.
9. **In-row Rebind buttons.** The design puts Rebind inside the driver row; buttons
   inside list rows are poor Tab stops and invisible to the tree's accessible model.
   → §3.3.
10. **Ctrl+Backspace.** The design resets the focused parameter with Ctrl+Backspace;
    `QLineEdit` uses the same key to delete the previous word. → §1.2.
11. **A diagonal stair.** ADR 0165's `repeat` copies along one axis, but stair treads
    step along two. → §6.1.
12. **Thumbnails.** Bundles require a PNG thumbnail; the design's cards have none. → §6.1.

## 1. Placement parameters in Entity info

### 1.1 Where it lives

- A new widget, `ComponentParametersSection` (`src/app/component_parameters_section.{hpp,cpp}`),
  is a `CollapsibleSection` titled "Parameters · this placement" added **inside the
  Info tab**, below `EntityInfoPanel`'s existing form. The Info tab becomes a vertical
  stack of `CollapsibleSection`s: "Entity info" (the existing form) and, when they
  apply, "Parameters · this placement" (§1) or, in a definition context,
  "Parameters · definition" and "Drivers" (§3). The tab widget, its indexes and its
  focus proxy are unchanged.
- The section is shown when exactly one top-level placement whose definition has a
  parameter record is selected, outside its own context. The header shows the count
  ("2 overridden"). With several placements selected it reads "Select one placement
  to see its parameters". A nested placement (inside an open context) shows its
  values read-only, with source "From *parent*" for parent-supplied values, because
  nested placements have no overrides (ADR 0165 §1).
- The Tray does not switch tabs by itself when a parametric placement is selected
  (no focus stealing).
- The rows come from `component_parameters.values {body}` and
  `component_parameters.describe {definition}` through `inspectDocument`, refreshed
  from `Window::sync()` like every panel, and cached by `(revision, body)`.

### 1.2 Rows and controls

One row per parameter in display order, three columns (label, value, reset), wrapping
the label above the value when the Tray is narrower than the row (as `WrapLongRows`):

| Type | Control | Notes |
| --- | --- | --- |
| `length`, `area`, `angle`, `number` | `QLineEdit` (monospace) | Display string in document units and precision |
| `count` | `QSpinBox` with the design's − and + buttons | Up, Down, PageUp, PageDown step; the buttons take no focus |
| `boolean` | `QCheckBox` with the text "Shown"/"Hidden" or the parameter's label | |
| `choice` | `QComboBox` of option labels | |
| derived | read-only `QLineEdit` | Focusable so screen readers reach it; caption "Derived · *formula*" |

- **Caption under each label**: "Override · default 900 mm", "Default", "Default ·
  700–780 mm" (bounds when present), "Derived · width − 120 mm", "From *parent*".
  Override rows also get the override border and fill; the caption and the reset
  button carry the state, so colour is never the only carrier. A new theme token,
  `information` (from the design's `#2c5aa0` / `#eef3fb`, with a dark-theme pair), is
  added and checked for contrast in both themes.
- **Reset** (↺) appears on override rows only. It is a normal Tab stop (keyboard
  reachability and the accessibility audit require every button in the chain), so Tab
  goes field → its reset → next field. Accessible name "Reset Width to the default,
  900 mm".
- **Ctrl+Backspace** resets the focused parameter. While a line edit's text is
  unchanged from the stored value, an event filter accepts the `ShortcutOverride` and
  resets. While the user is editing text, Ctrl+Backspace keeps its text meaning
  (delete the previous word).
- **Reset all** and **Edit definition…** buttons follow the rows. "Edit definition…"
  opens the context exactly as Enter on the placement does (§4).
- The footer hint text follows the design ("Tab moves to the next parameter ·
  Ctrl+Backspace resets the focused one · Enter edits the definition") while focus is
  in the section; otherwise the status bar is unchanged.

### 1.3 Entry, live validation and commit

- **Syntax.** Value fields accept the Measurements box syntax (UX_DESIGN §4.3):
  `1.2m`, `1200`, `47"`, `3'11"`, with the document unit when none is given; angles
  take degrees by default and `rad`. The desktop parses with `parseLength` /
  `parseMeasurements` and sends a JSON number in SI units, so feet-and-inches entries
  work although the ADR 0165 literal grammar has no `'`/`"`. Counts take whole
  numbers; numbers take the locale decimal.
- **Live validation.** Each edit restarts a 150 ms timer. When it fires, the desktop
  parses the text (a parse failure is shown at once) and calls
  `component_parameters.evaluate {definition, overrides}` with the placement's
  current overrides plus the edited value. The result's error (code, offset, message)
  is shown under the field (§5). `evaluate` is bounded by ADR 0165's limits, so it
  runs on the GUI thread. Typing never waits on it: a newer edit cancels an older
  pending check, and a result whose revision is older than the document is dropped.
- **Commit** on `editingFinished` (Enter or focus leaving the field), as the Sheets
  Entity info does, when the value is valid and differs from the stored one: one
  batch `component_parameter.override {body, values: {name: value}}`. A combo box or
  check box commits on change; a spin box on `editingFinished` or a button step.
  **Esc** restores the stored value and clears the error. An invalid value is never
  committed: on Enter the error stays and focus stays; on focus loss the text and
  error stay until fixed or Esc (the document is unchanged and the field says so).
- **Values are never clamped**: a value outside its bounds is refused with the
  design's sentence, also when the core refuses it on commit.
- **Measurements box.** With the Select tool and exactly one parametric placement
  selected, the Measurements box label is the parameter last focused in the section,
  else the first input parameter of a length, angle, number or count type (the
  design's footer shows "Width 1200 mm"). Typing in the viewport goes to the box as
  usual; Enter sets that parameter (same batch as above). Re-entry before another
  operation **amends** the entry through `ToolSession`'s amend stamp, as Measurements
  re-entry does for tools. Without a parametric selection the box is unchanged.

### 1.4 Undo

- `Viewport::commitCommands` gains an optional history label, passed as
  `history: {label}`; existing callers are unchanged.
- One change is one entry. Labels quote the parameter's label, truncated to 64 bytes
  with "…": "Change Width", "Reset Width", "Reset all parameters" (Reset all is one
  `component_parameter.reset {body, all: true}`). A Measurements amend keeps the entry
  ("Change Width").
- The section never batches several fields into one entry; each committed field is
  its own entry, as the design's "Each change is one Undo step" says.

## 2. The Preview ghost

The design has a non-mutating Preview in the definition context (Authoring: a strip of
values and a ghost of "the regenerated shape … Nothing is changed until you apply") and
a ghost while placing an assembly (Assemblies).

- **How it is computed.** The ghost is a **dry run of the real command**:
  `previewBatch` of `component_parameter.override {body: <the context placement>,
  values}` through `ToolSession::preview`, exactly as the hosted placement ghost is a
  dry run of `component.attach`. The staged edit regenerates only that placement, runs
  every check the commit would (bounds, regeneration, limits, host re-cut), and is
  discarded. Its result's changed bodies are the regenerated members with world
  transforms, which `Viewport::previewCommand` already draws. Nothing is committed,
  the revision and history do not change, and the error of a failing preview is shown
  in the strip with the §5 wording.
- **What the core must guarantee** (checked in R094.b): the dry run's geometry includes
  every member the variant changes, including repeat copies and members hidden by
  visibility (reported as removed), and a host re-cut's changed host faces. If the
  preview payload caps the bodies it returns below a definition's worst case (128
  parameters, 256 drivers, 4,096 reserved IDs), R094.b adds a bounded `regenerated`
  geometry section to the dry-run result. No new query is added for the ghost, so MCP
  clients get the same dry run with `transaction.inspect`.
- **Drawing.** The ghost uses the existing preview overlay (dashed edges and hatch, as
  assistant previews do; UX_DESIGN §10 "previews use hatch"), drawn over the current
  geometry. The ghost is cleared by "Back to defaults", Esc in the strip, closing the
  context, or any committed edit.
- **Cost.** Strip edits are debounced (150 ms). Each dry run regenerates one placement,
  which the ADR 0165 per-definition budget (200,000 node visits) bounds.
- **Entity info does not ghost while typing** (default; "Open for the owner" item 3):
  the design shows the ghost only in Authoring and Assemblies, and the Entity info
  commit already shows the real result with one Undo.

## 3. Definition authoring

### 3.1 The context and its banner

- Opening a parametric definition (Enter, double-click, "Edit definition…") follows §4
  and then enters the ordinary component context through the placement. The
  breadcrumb reads "Model › *Definition* (definition)".
- The banner becomes the design's informational strip: "Editing the shared definition
  at its default values. Changes apply to all *n* placements; their overrides are
  kept." with **Close definition** (Esc). It stays the existing `componentBanner_`
  `QLabel`, so its focus and Backtab behaviour stay; Close definition is a link like
  today's "Make unique". The non-parametric banner is unchanged.
- **Preview strip.** A `QFrame` child of the viewport, under the banner like the
  banner itself, holds one field per input parameter (prefilled with the defaults,
  wrapping), the note "Shows the regenerated shape as a ghost. Nothing is changed until
  you apply." and **Back to defaults**. It follows the banner in the Tab chain. Its
  fields use §1.3 parsing; it never commits (there is no Apply in the design; the
  strip is for looking).
- **Regeneration keeps the scope.** A default, formula or driver change regenerates
  every placement in the same edit; scene IDs, materials, the open context and the
  selection (by ID) persist, which is the R094 Verify line. A selected repeat copy
  that disappears drops from the selection.

### 3.2 Parameters · definition

- A `QTreeWidget` with the design's columns (Name with the label over the mono name,
  Type with bounds or "derived", Default or formula). Selecting a row opens the
  inline editor below it (the design's "Width" panel): Label, Name, Default (input) or
  Formula (derived), Bounds (min, max), Step, Options (choice), and the read-only
  "Used by" from `describe`'s `usedBy`.
- **+ Parameter** adds a row in create mode, where Type and Mode are also editable
  (ADR 0165 fixes them after creation). Enter in the editor commits; Esc cancels.
- **Keys** on the tree: F2 focuses the Name field (rename), Delete removes (refused
  while referenced, listing the referrers), Alt+Up and Alt+Down reorder (`position`),
  Enter moves into the editor.
- **Commands and labels**: `component_parameter.define` "Add parameter Width",
  `.update` "Edit parameter Width", `.rename` "Rename parameter width",
  `.remove` "Remove parameter Width", reorder "Move parameter Width".
- The editor's note under a default says "Changing the default re-baselines the shape
  and keeps each placement's own values."

### 3.3 Drivers and Add driver

- **List.** A `QTreeWidget`, one row per driver: kind, target ("Top · x past 450 mm",
  "Right legs · x", "Shelf · up, 300 mm"), formula (mono), and state. State is text
  plus a glyph: "Bound", or amber "Changed" (`changedSinceBound`); the tooltip and
  accessible description give the sentence. Selecting a row selects the target member
  in the viewport and opens the driver editor below the list. Delete removes
  (`component_parameter.undrive`, "Remove Stretch driver").
- **Rebind.** A Changed row shows the design's in-row Rebind button for the pointer;
  that button takes no focus. The keyboard path is the **Rebind** button under the
  list, enabled when the selection includes a Changed driver, plus **Rebind all** when
  several are Changed. `component_parameter.rebind {definition, drivers}` or
  `{definition, all: true}`; labels "Rebind driver", "Rebind 3 drivers". The design's
  explanatory line under the list appears while any driver is Changed.
- **Add driver** (`component.add_driver`, default **Shift+D**, model scope,
  `Qt::WidgetShortcut` on the viewport like other Shift-only keys; also in the
  Component menu, the palette and as **+ Driver** in the Drivers header). It is enabled
  in a parametric or non-parametric definition context with exactly one non-root,
  non-generated member selected. In the tool rail it is a non-checkable action after a
  separator, shown only while a definition context is open; it does not join the
  exclusive tool group. With nothing suitable selected it reports "Select a part
  inside the definition, then Shift+D". The first driver on a definition without a
  record creates the record in the same batch.
- **Kinds offered by member.** Only kinds the core accepts for the selected member are
  listed (ADR 0165 §1):

  | Selected member | Kinds offered |
  | --- | --- |
  | Geometry (faces and edges) | Stretch, Position, Rotation, Visibility, Repeat |
  | Group | Position, Rotation, Visibility, Repeat (its subtree) |
  | Nested placement (reference member) | Position, Rotation, Visibility, Repeat, Child parameter (when the referenced definition has input parameters) |
  | Text, reference image | Position, Rotation, Visibility, Repeat |
  | The glue member | as its type, without Rotation and without Position along the glue normal |
  | A repeat copy, the root | none (the status line says why) |

- **Axes.** x, y and z, each shown with its axis colour **and** its letter: the
  member's own axes for Stretch and Rotation, the parent frame's for Position and
  Repeat. Stretch defaults to the member's longest bounding-box axis; the others to x.
- **Formula fields** default to the first parameter of the right type (a length for
  Stretch, Position and Repeat spacing, an angle for Rotation, a boolean for
  Visibility, a count for Repeat count, the same-named parameter for Child parameter)
  and behave as §3.5 says. Repeat also has **Max count** (1–1,000). Child parameter has
  a combo of the child definition's input parameters.
- **Commit.** Enter or **Add** commits `component_parameter.drive` ("Add Stretch
  driver"; editing an existing driver is "Edit Stretch driver"). Before that, every
  form change is checked by a dry run of the same command through `previewBatch`
  (debounced 150 ms), which gives `PARAMETER_DRIVER_TARGET`, `PARAMETER_TYPE` and
  `PARAMETER_REGENERATION` exactly as the commit would.

### 3.4 Picking the stretch plane

- While the Stretch form is open, the viewport draws the plane `at` as the design's
  dashed quad across the member's bounds, perpendicular to the axis, in the member's
  template coordinates (the context placement shows the template, §4). The default is
  the middle of the member's bounds on that axis.
- **Pointer:** drag the plane along the axis; it snaps (8 logical px) to the member's
  distinct vertex coordinates and to the midpoints between neighbouring ones, which
  are the places where the set of moving vertices changes. Alt disables snapping.
- **Keyboard:** the form's **Plane at** field (member-local, document units) and the
  Measurements box, labelled "Plane", take the same value; Enter in the box moves the
  plane, it does not commit the driver.
- **Feedback:** vertices that will move are drawn in the selection colour with an
  arrow, and the form states the count ("12 of 24 points move"), so colour is not the
  only carrier. A curve crossing the plane is reported live from the dry run
  ("The arc crosses the stretch plane; move the plane").

### 3.5 Formula fields

All formula and default-expression fields (parameter editor, driver editor) share one
widget, `FormulaField`:

- Monospace `QLineEdit`, ASCII only, at most 1,024 bytes (ADR 0165 limit; longer
  input is refused by the field with the limit message).
- **Checked as you type**: each keystroke calls
  `component_parameters.evaluate {definition, expression}` for syntax, names and unit
  algebra (cheap: no geometry). After 300 ms idle the field dry-runs the actual
  command (`component_parameter.update` or `.drive`) through `previewBatch`, which adds
  the result type, cycles, bounds and regeneration of every placement. If R093.d's
  `evaluate` cannot check an expression against a target type, R094.b adds an optional
  `for {parameter | driver kind}` to it (additive).
- An error shows the design's red border, a caret marker under the byte offset (a
  label below the field, positioned with the field's font metrics, so it follows text
  size and scrolling), and the sentence (§5). The document keeps the last valid
  formula until the field is fixed; Esc restores it.
- **One-click unit fix.** For `PARAMETER_TYPE` whose offset points at a bare number
  literal that should be a length or angle, the field offers "Use 120 mm" (the
  document's display unit appended). The button is shown only if the fixed text
  checks clean through `evaluate`. Clicking it edits the text; Enter still commits.
- After commit the field shows the engine's canonical text.

## 4. Opening a placement that has its own values

### 4.1 The dialog

Double-click, Enter or "Edit definition…" on a placement with effective overrides opens
the design's dialog, `ParametersScopeDialog`: title "*Placement* has its own values",
the text naming the differing values, and three buttons: **Cancel**, **Make unique and
edit**, **Edit definition at defaults** (default button, accent fill with the inset
focus ring). Esc is Cancel. It closes like `SettledInputDialog` (settles before
closing on Wayland) and `ModalReturn` returns activation to the window on both X11 and
Wayland. It is added to `native_accessibility_audit` and the dialog-keyboard
inventory. A placement without overrides opens directly, as today.

A **nested** placement whose values come from its parent cannot be reset: the dialog
says "Its values come from *parent*" and offers Cancel and Make unique and edit only.

### 4.2 Make unique and edit

One batch, `component_parameter.promote {body}`, labelled "Make *placement* unique",
then the context opens through it. **`component_parameter.promote` is new (R094.b)**: it
makes the placement unique when its definition is shared, then re-baselines the unique
definition so the placement's current values become its defaults and clears its
overrides: the `component.edit_instance` promotion of ADR 0165 §4 without a geometry
edit. Its name starts with `component_parameter`, so the assistant never sees it and
the assistant tool surface baseline does not change; adding a `promote` field to the
assistant-visible `component.make_unique` would have changed it. The dialog's footnote
("Make unique keeps the parameters: this placement's values become the copy's
defaults") is ADR 0165 open item 4's default.

### 4.3 Edit definition at defaults

The context must show the template, because shared edits work in template coordinates
and the viewport tools read `doc_.bodies()` directly. Showing one geometry while the
tools act on another is not possible without rewriting those reads. The decision is to
**suspend the placement's overrides for the time the context is open, through ordinary
edits**:

1. **Open:** one batch `component_parameter.reset {body, all: true}`, labelled "Edit
   *Definition* at defaults". The placement now shows the template, so the context
   through it is the ordinary R093 template scope; nothing else in the core changes.
   The desktop remembers the placement and its overrides (SI values) for the context.
2. **Edit:** every edit is an ordinary scoped edit, one entry each.
3. **Close** (Esc, Close definition, the breadcrumb, a click outside):
   - if the top history entry is still the reset (nothing was edited), the desktop
     **undoes** it: the records and the saved state return exactly (the entry moves to
     the redo branch), so opening and closing changes nothing;
   - otherwise one batch `component_parameter.override {body, values}` restores the
     remembered values, labelled "Restore *placement* values". Values the new
     definition no longer accepts (a parameter removed, or bounds that now exclude the
     value) are left out after checking each with `evaluate`; the status bar names them
     and the History entry is still one entry.
4. **Undo inside the context** works per entry. Undoing the reset itself, or an
   external edit that gives the placement overrides again, ends the context (status:
   "*Placement* has its own values again; the definition was closed") and forgets the
   restore.
5. **Save and recovery** while the context is open write the placement at its defaults,
   which is the document's true state; the History shows the reset entry.

The banner adds "*Placement*'s own values return when you close the definition." The
cost is two visible entries when something was edited. The alternative, a core
"template scope" that shows a placement at defaults without an edit, is listed under
"Open for the owner" (item 1).

## 5. Errors, warnings and refusals

Every message is shown **at the control that caused it**: under the field (red border,
error icon, sentence), or in the strip, dialog or form that issued the command. Each
field with an error gets the error sentence as its accessible description prefixed
"Invalid:", and a `QAccessibleEvent` description change is sent. Warnings use the
amber `warning` token, errors the red `failure` token, each with an icon and text.

The desktop composes the sentence from the error code and its fields; the core message
is the fallback and the tooltip. Where the sentence needs a detail the error object does
not carry, the desktop takes it from `describe` (bounds, labels); if that is not enough,
R094.b adds the field to the error details (additive, never to a request schema):

| Code | Where it appears | Sentence (design wording where it has one) | Action |
| --- | --- | --- | --- |
| `PARAMETER_SYNTAX` | formula field | "A closing ")" is missing at character 16." with the caret | Esc restores |
| `PARAMETER_NAME` | name field, formula field, Remove | "Use lowercase letters, digits and _, starting with a letter", "*name* is already a parameter", "*name* is a reserved word", "No parameter is called *name*", "Still used by Leg spacing · Stretch top" | — |
| `PARAMETER_TYPE` | formula field, value field | "Can't subtract a number from a length. Add a unit, for example **120 mm**." | **Use 120 mm** (§3.5) |
| `PARAMETER_CYCLE` | formula field | "These depend on each other: **Width → Leg spacing → Width**. One of them needs a value instead of a formula." (labels, up to 8) | — |
| `PARAMETER_DOMAIN` (bounds) | value field, strip | "2.6 m is above the maximum, 2.4 m. Nothing was changed." / "… below the minimum …" / "… is not a step of 50 mm …" | never clamps |
| `PARAMETER_DOMAIN` (other) | formula or value field | "Division by zero at character 9", "Shelves must be a whole number; use round()", "The square root of a negative value at character 4" | — |
| `PARAMETER_DRIVER_TARGET` | driver form | "Text can't be stretched", "The arc crosses the stretch plane; move the plane", "This would move the glue face off its wall", "*Child* has no parameter *name*" | — |
| `PARAMETER_REGENERATION` | value field, strip, formula field | "At 300 mm the top's right edge would pass its stretch plane (Stretch top). Nothing was changed." | **Show the driver**: opens the definition (§4) and selects the driver and its member |
| `PARAMETER_TEMPLATE_SCOPE` | status bar, context | Normally prevented by §4. If another client gives the placement values while the context is open: "*Placement* now has its own values. Close the definition and open it again." | closes the context |
| `LOCKED_ENTITY` | the field or editor that committed | amber: "Changing the default Width would change **Dining table #1**, which is locked. Unlock it or keep the default." | **Select #1** closes the context if open and selects the placement |
| `LIMIT_EXCEEDED` | where committed | the core's D06 limit message (it already quotes at most 64 bytes) | — |
| `STALE_REVISION` | where committed | "The model changed. Try again." (the GUI thread builds batches at the current revision, so this is a race only) | — |
| `NOT_FOUND`, `INVALID_TARGET` | section | the section refreshes; "The placement was removed" / "This placement can't hold values" | — |
| `UNSUPPORTED_CAPABILITY` | assistant only | the assistant's existing refusal | — |

`LOCKED_ENTITY` must name the locked placement for "Select #1"; R094.b adds `body` to the
parameter commands' `LOCKED_ENTITY` details if R093.d does not.

**Changed since bound.** When a committed edit's result lists `parameterWarnings`, the
viewport shows the design's amber status strip under the banner (a `QFrame` with
`role` status semantics: an accessible alert is sent): "Moved the shelf. **Repeat
shelves** now starts from a changed shape. Placements may not look as expected until
you rebind." with **Rebind** (those drivers, one entry) and **Undo** (Ctrl+Z). The
first part of the sentence is the History label of the edit. The strip stays until
Rebind, Undo, another edit or closing the context; the Drivers list keeps the Changed
state either way.

## 6. Assemblies

### 6.1 How the six assemblies ship

- **Sources in the repository.** Each assembly is a JSON file under
  `resources/assemblies/` (`interior-door.json`, `casement-window.json`,
  `dining-table.json`, `bookshelf.json`, `base-cabinet.json`, `straight-stair.json`).
  A file holds library metadata (name, description, labels), the assembly identity
  `{id, version}` (§6.2), and one transaction batch in the public transaction format
  that builds the geometry and the component, defines the parameters and drivers, and
  sets the glue face. They are text, reviewable, authored by the project, and
  **MIT-licensed like the rest of the repository; they contain no third-party models,
  textures or fonts** (materials are plain colours). A README in the folder states
  this and how to regenerate.
- **Generated at build time.** A new CLI mode, `sketchyup-cli --build-assembly <source>
  <output.sketchylib>`, applies the batch to a new document, captures the definition
  with `encodeComponentBundle` (ADR 0130; schema 28 because it has parameters), and
  writes a 320 × 240 thumbnail drawn with the R092 vector compositor (QPainter on a
  `QImage`, technical lines of an isometric view; Qt Gui only, which the CLI links).
  CMake runs it for each source into `<build>/share/sketchyup/assemblies/` and installs
  that folder to `share/sketchyup/assemblies/`. The bundles are not committed.
- **Found at run time** relative to the executable
  (`applicationDirPath()/../share/sketchyup/assemblies`), which is the same in the
  build tree and the installed package, so native tests and the installed app see the
  same files. The folder is read-only and scanned with `scanLibraryDirectory`. The
  Tray's note follows the design: "Bundled with SketchyUp and saved into your model
  when placed. SketchUp Dynamic Components are not imported or converted."
- **Starting parameter sets** (the design's cards give the counts; R094.f records the
  final lists in this ADR):
  - Interior door (4): width 600–1200 mm, height 1900–2400 mm, hinge side (left,
    right: two leaves with `visibility`), leaf open 0–90° (`rotation`). Cuts its
    opening.
  - Casement window (5): width, height, panes (count, `repeat` of the sash with
    spacing `width / panes` and a sash `stretch` by the same amount), frame depth,
    opening angle. Cuts its opening.
  - Dining table (5): width, depth, height, legs (square, tapered), apron (boolean),
    with derived leg spacing; the R093 acceptance table generalised.
  - Bookshelf (4): width, height, depth, shelves (`repeat`, spacing derived from
    height).
  - Base cabinet (6): width, height, depth, doors (count), shelves (count), toe-kick
    height.
  - Straight stair (5): total rise, going, width, riser count (derived:
    `round(rise / 175 mm)`), nosing. **Risk:** ADR 0165's `repeat` copies along one
    axis. The stair repeats a tread inside a frame pitched by a `rotation` driver and
    counter-rotates the tread by the same Δ (rotation applies before repeat, so copies
    inherit it), with spacing the step's diagonal. If R094.f finds this unworkable, the
    owner chooses between a fixed-pitch stair and a core `repeat` with a second axis
    while schema 28 is still open ("Open for the owner" item 7).
- **Tests.** `assembly_bundle_tests` (headless) builds every source twice
  byte-identically, decodes each bundle, checks its parameters with `describe`, places
  each at its defaults and at its bounds' extremes without a regeneration error, and
  checks the door and window cut an opening in a fixture wall.

### 6.2 Reusing the definition: assembly identity

- R094.b adds an optional `assembly {id, version}` to the parameter record (schema 28 is
  open until the F02 release, ADR 0165 open item 2): `id` matches
  `[a-z0-9_-]{1,64}(/[a-z0-9_-]{1,64})?` (for example `sketchyup/interior-door`),
  `version` is a positive integer. It is copied by bundle capture, make-unique keeps it
  off the copy (the copy is no longer the library's), and the v28 writer fixture is
  regenerated once.
- **Placing from the library reuses** a definition already in the model with the same
  `id` and `version`: the placement is `component.instance` of that definition plus
  its overrides, so every door in the model is one definition with per-placement
  values, and variants share geometry (§7). Without a match the bundle is inserted
  (new independent definitions, ADR 0130). A card whose assembly is already in the
  model says "In this model". If the user edited that definition, later placements use
  the edited one (SketchUp's library behaves the same way). "Open for the owner" item 2
  is the alternative.

### 6.3 The Components tab and insert with values

- **Where.** A **Components** tab is appended to the Tray's tab widget (index 12, so
  the existing index-based focus proxy is untouched and only gains one case),
  realising UX_DESIGN §5's "Components" panel. It holds the design's sections:
  **Components** (Show: "Configurable assemblies" | "All components"; cards in a
  two-column grid), **values for this placement**, and **About these assemblies**.
  "All components" adds the user library folder's entries. The existing "Browse
  component library…" dialog stays for 1.2.0.
- **Cards** are a `QListView` in icon mode with one Tab stop, arrows to move, and
  accessible text "Interior door, cuts opening, 4 parameters". Activating a card
  (Enter, click) selects it and starts placement; the design's pressed card is the
  current one.
- **Values form.** One row per input parameter of the bundle's definition, built from
  `describe` on the decoded bundle document, with §1.2 controls and §1.3 validation
  against that document's definition (in process, `evaluate`). The form shows bounds as
  captions. **Use defaults** resets the form; **Cancel** (Esc) ends placement. Values
  equal to the defaults are not stored as overrides.
- **Placing.** The viewport enters a placement mode with the status text from the
  design ("Placing Interior door · Click a wall face to place · the opening is cut in
  the same step · Esc cancels"; for free assemblies "Click to place").
  - The ghost is the assembly's variant at the form's values, computed once per value
    change by a dry run of `component_parameter.override` on the decoded bundle
    document's root placement, then moved with the pointer, so mouse moves cost no
    regeneration. For a gluing assembly it snaps to the hovered face through the
    hosted placement frame (ADR 0054) and draws the opening outline; after 150 ms
    without movement a dry run of the full placement shows the real cut.
  - **Click** commits **one entry**, "Place Interior door".
  - **Keyboard:** with a face already selected (Tab cycling), Enter places the
    assembly on that face at its centre; the Measurements box takes the hosted
    placement grammar (anchor coordinates, inset) and re-entry amends, as Shift+H does
    today. A free assembly takes `[x,y,z]` and Enter.
- **One edit.** A new core function, `placeLibraryAssembly(Document&, bundle,
  placement, overrides)` in `src/io/component_library.{hpp,cpp}`, does everything on
  one draft and publishes one edit: reuse or insert the definition (§6.2), place it,
  set the overrides, and for a gluing assembly attach it to the host face with the same
  builders `component.attach` uses, so the opening is cut in the same step (ADR 0054
  regeneration from the uncut host). It is desktop-only, like `insertLibraryComponent`;
  MCP clients can do the same with `component.instance`,
  `component_parameter.override` and `component.attach` in one batch.
- **After placing**, the placement is selected and Entity info shows its parameters;
  changing Width there re-cuts the opening in the same edit (R093).

## 7. Instanced rendering of variants

**Required for R094 (slice R094.g).** Copying a placement copies its overrides, so a
building with one non-default window copied 200 times has 200 placements of one
variant. Today each renders with its own meshes, which loses the R082.dd sharing (1,000
default placements: 89 kB of body caches and 35 kB of GPU vertices, against 101 MB and
35 MB unshared). That would break the M9 memory and frame budgets
([R082.ii](../verification/R082ii-amd-hardware-session.md): 10,000 placements,
complete-frame p95 within 33.3 ms, edit feedback within 100 ms) for an ordinary model.

- **Viewport.** A variant placement's member shares by its **variant** member record:
  `BodyCache.source` and `PlacementKey.source` become `variant->members.at(member)`
  when `Document::componentVariant(placement)` is non-null, else the definition member
  as today. Placements with equal values share one `VariantPtr`, so they share meshes;
  members no driver changes are the template's own records, so they share with
  default placements too.
- **Inference.** The R082.ee local-space index keys by the same source record, so a
  variant's driven members are indexed once per variant.
- **Bounds.** At most 4,096 distinct variants per document (ADR 0165 §3); each costs
  meshes only for its driven members.
- **Checks.** Framebuffer and picking hashes equal with sharing on and off for a
  variant fixture; 1,000 placements of one non-default variant have body-cache and GPU
  vertex payloads within 10 % of 1,000 default placements; the R082 10,000-placement
  benchmark with every placement overridden passes the frame and edit budgets in
  software, and on the AMD machine when available (recorded in R094.h; the owner's
  hardware is not required for CI).

## 8. Keyboard, accessibility, scaling and platforms

- **Everything is reachable without a pointer:** Tab and F6 reach the Tray, its Info
  and Components tabs, each section header (one stop each), every field, reset
  button, tree, form, button and the cards; the banner, Preview strip and status strip
  follow the breadcrumb in the viewport chain. Placement and stretch planes have typed
  equivalents (§3.4, §6.3). The dialog has a default button and Esc.
- **Shortcuts.** New: `component.add_driver` (Shift+D, model scope), checked against
  every default by `shortcut_input_tests` and the binding validation (ADR 0148).
  `Ctrl+Backspace` is section-local (§1.2), not a registered action, so it is not
  configurable; `component_parameter_input_tests` covers it. Assembly
  placement, Rebind and Reset all are palette commands without default keys.
- **AT-SPI names and descriptions.** Each value field's accessible name is the
  parameter's label; its description is the caption ("Override, default 900 mm,
  400–2400 mm"), with "Invalid: …" first while it has an error. Reset buttons: "Reset
  *Label* to the default, *value*". Trees: rows read kind, target, formula and state.
  Cards and the dialog as above. `accessibility_bridge_test.py` reads them.
- **Text size.** Rows lay out from font metrics, never fixed pixel heights except the
  28 px section headers; at 200 % UI text and the 240 px minimum Tray width (R094.a)
  labels wrap above their fields and nothing scrolls sideways or clips
  (`text_size_input_tests`).
- **Display scaling.** Every native suite R094 adds runs on X11 and Wayland at 1× and
  2× in `native.yml`; `component_parameter_input_tests` also at 1.5 for the wrapping
  rows. The caret marker and stretch-plane handle are placed in logical pixels.
- **Wayland.** No global shortcuts, no pointer grabs beyond the existing drag capture,
  dialogs settle before closing. Nothing in R094 depends on window positions.
- **Theme.** The `information` token (§1.2) joins `theme_input_tests`; warning and
  failure are reused.

## 9. The in-app assistant

- **It cannot set parameters in 1.2.0** (ADR 0165 open item 3, default hidden). R094 adds
  only names under the `component_parameter` prefix (`component_parameter.promote`), so
  `tests/assistant-tool-surface-baseline.json` and the assistant's per-turn schema
  tokens do not change, and `assistant_tool_surface_tests` passes unedited.
- The assistant's instructions and `document.describe` are unchanged, so it does not
  know parameters exist. Its assistant-visible commands that touch a parametric
  placement follow ADR 0165 §4 (for example `component.edit_instance` promotes values;
  `entity.dimensions` scales the placement and keeps its overrides). The release notes
  say the assistant cannot change component parameters.
- Assistant previews and the parameter ghost are separate overlays; any committed edit
  makes an assistant preview stale by the existing revision rule and clears the
  parameter ghost. Parameter entries are never marked "AI" in History.
- Exposing `override`, `reset` and `values` later is ADR 0165 open item 3; it would add
  an Entity info hint and a corpus re-run, and is not part of R094.

## 10. Tests

- **Headless:** `component_parameter_desktop_tests` for the R094.b additions
  (`component_parameter.promote`, the `assembly` field round trip and writer fixture,
  error details, dry-run geometry completeness, history labels from
  `commitCommands`), `assembly_bundle_tests` (§6.1), `assembly_placement_tests`
  (`placeLibraryAssembly`: reuse by identity, one edit, cut in the same edit, undo).
- **Native** (`sketchyup_native_fixture`; X11 and Wayland at 1× and 2×):
  - `component_parameter_input_tests` (R094.c): rows and captions from `values`;
    typed entry in document units and feet-and-inches; live validation; commit on
    Enter and focus loss; Esc; per-row reset and Ctrl+Backspace (and its
    word-delete meaning while editing); Reset all; one labelled entry per change;
    Measurements box routing and amend; bounds refusal leaves the document unchanged.
    Also at 1.5.
  - `component_authoring_input_tests` (R094.d): context and banner, parameter tree and
    editor, F2, Alt+Up/Down, Delete; Add driver with Shift+D for each member kind and
    the kinds offered; stretch plane by drag, snapping, Alt, typed value; formula field
    checking, caret, unit fix; Preview strip ghost without a revision change; context,
    selection and materials kept across regeneration.
  - `component_parameter_state_input_tests` (R094.e): the dialog's three choices
    (promotion, suspend and restore, undo when nothing changed, partial restore,
    undoing the reset), nested placements, every row of the §5 table produced and
    shown at its control, changed-since-bound strip, Rebind and Rebind all.
  - `assembly_library_input_tests` (R094.f): the Components tab, filter, cards by
    keyboard, values form, ghost, placing a door on a wall face with the opening cut in
    one entry, keyboard placement on a selected face, reuse of the in-model definition,
    window, table, bookshelf, cabinet and stair at their defaults.
  - `variant_instancing_input_tests` (R094.g): sharing on/off hashes and payloads.
  - `component_acceptance_input_tests` (R094.h): the acceptance script.
- **Extensions:** `keyboard_reachability_tests` (sections, trees, Components tab,
  banner, strips), `native_accessibility_audit` (the dialog), `visible_focus_input_tests`
  (new controls, accent-filled default button), `shortcut_input_tests` (Shift+D),
  `text_size_input_tests`, `theme_input_tests`, `accessibility_bridge_test.py`,
  `entity_info_input_tests` (the Info tab's sections), `library_input_tests` (dialog
  unchanged).
- **Acceptance (R094.h).** A scripted session: place the dining table with values;
  override width and shelves on one of three placements; reset one; open it (dialog →
  edit at defaults), change the default width, add a driver, hand-edit the shelf
  (Changed), Rebind; close (values restored); make another unique and edit
  (promotion); place a door and a window on a wall (openings cut), change the door's
  width (re-cut); check every entry's label, undo to the start and redo; save, reopen
  and compare bytes.

## Decision

Build R094 as:
- placement parameters as an inline section of the Info tab, validated through
  `component_parameters.evaluate` and committed one labelled entry per change;
- definition authoring in the component context, with a parameter tree and editor, a
  drivers tree with Rebind, Shift+D driver creation, a visual stretch plane and formula
  fields checked as you type;
- previews as dry runs of the real commands (`previewBatch`), with no new query;
- "Edit definition at defaults" by suspending the placement's overrides with ordinary
  edits, and "Make unique and edit" through a new hidden
  `component_parameter.promote`;
- every error code shown at its control with the design's wording;
- six assemblies built at build time from in-repo sources into
  `share/sketchyup/assemblies`, reused by assembly identity, placed with values and
  cut into walls in one edit;
- instanced rendering and inference of variants;
- the layers below.

## Consequences

- Schema 28 gains the optional `assembly` field before it is frozen; the v28 fixture is
  regenerated once (R094.b).
- One new command, `component_parameter.promote`, hidden from the assistant; one new
  CLI mode, `--build-assembly`; one desktop-only core function,
  `placeLibraryAssembly`. The generated API files change for the command.
- The Info tab becomes a stack of collapsible sections, and the Tray gains a
  Components tab. The model Tray is otherwise unchanged.
- Editing a definition at defaults through a placement with values leaves two History
  entries (suspend and restore) when something was edited, and none when nothing was.
- The package installs `share/sketchyup/assemblies/` (six bundles); the package
  verification and the package budget record must include them.
- Seven new native suites and extensions to nine existing ones join the X11/Wayland
  1×/2× matrix.

## Open for the owner

Each item states the default this ADR assumes.

1. **Edit definition at defaults.** Default: suspend the placement's overrides with a
   visible "Edit *Definition* at defaults" entry and restore them on close (§4.3).
   Alternative: a core template scope that shows the placement at defaults while the
   context is open, without history entries; it touches the variant index, history
   and inspection snapshots and would be its own core slice.
2. **Library reuse.** Default: placing an assembly reuses the in-model definition with
   the same assembly identity (§6.2). Alternative: every placement inserts independent
   definitions, as ADR 0130 does for library components today.
3. **Ghost while typing in Entity info.** Default: no; the ghost appears in the
   definition Preview strip and while placing assemblies (§2).
4. **Components in the Tray.** Default: a new Components tab appended to the Tray,
   with the library dialog kept (§6.3). Alternative: only extend the dialog.
5. **Instanced variant rendering is release-blocking for 1.2.0.** Default: yes
   (R094.g).
6. **Assistant.** Default: unchanged from ADR 0165 (hidden; no instruction change; the
   release notes say so).
7. **Assembly parameter sets and the stair.** Default: the starting sets of §6.1, with
   the stair built from a pitched frame. Alternative for the stair: a fixed pitch, or a
   core two-axis `repeat` while schema 28 is open.
8. **Nested placements with parent-supplied values.** Default: the dialog offers only
   Make unique and edit (§4.1).
9. **Release preparation.** Default: R094.h is the R094 acceptance record; preparing
   1.2.0 and the F02 release record (`docs/verification/F02-1.2.0.md`) follow as their
   own slices, as R092.c and R092.d did for 1.1.0.

## Layer plan

The roadmap entry [R094](../PR_ROADMAP.md#r094) lists the same slices with Build and
Verify lines. R094.a is independent. R094.b requires R093.c and R093.d. R094.c–f
require R094.b and merge in order (they share the Info tab and the component context).
R094.g requires R093.b only and may merge at any point. R094.h requires all of them.

**R094.a: resizable sidebar.** On `build/r094a-resizable-sidebar`.
- Scope: the separator between the content row and the side tabs, 240–720 px, default
  360 px, drag, double-click reset, Ctrl+Alt+[ and Ctrl+Alt+] on the focused
  separator, persisted width.

**R094.b: core and command additions.** §§2, 4.2, 5, 6.2.
- Scope: `component_parameter.promote`; the optional `assembly {id, version}` on
  parameter records (schema 28, codec, `native-format-v1.json`, the ADR 0108 "Schema
  28" addendum, the regenerated v28 fixture, fuzz seeds); error details (`body` for
  `LOCKED_ENTITY` on parameter commands, the violated bound for bounds errors, and an
  `evaluate` `for` target if missing); dry-run geometry completeness for regenerated
  placements; `Viewport::commitCommands` history labels; regenerated `docs/api`.
- Files: `src/automation/component_parameter_commands.*`, `catalog.cpp`,
  `src/core/component_parameter_records.*`, `src/io/component_parameters_io.*`,
  `src/app/viewport_components.cpp`, `docs/api/*`.
- Tests: `component_parameter_desktop_tests`, the API schema tests,
  `assistant_tool_surface_tests` unchanged.

**R094.c: Entity info parameters.** §1.
- Files: `src/app/component_parameters_section.{hpp,cpp}`,
  `src/app/entity_info_panel.cpp` (sections), `src/app/parameter_value_field.{hpp,cpp}`,
  `src/app/theme.hpp` (`information`), `src/app/window.cpp` (Measurements routing).
- Tests: `component_parameter_input_tests`; extensions to `entity_info_input_tests`,
  `text_size_input_tests`, `theme_input_tests`, `keyboard_reachability_tests`.

**R094.d: definition authoring, drivers and preview.** §§2, 3.
- Files: `src/app/component_definition_section.{hpp,cpp}` (parameter tree and editor),
  `src/app/component_drivers_section.{hpp,cpp}`, `src/app/formula_field.{hpp,cpp}`,
  `src/app/viewport_parameters.cpp` (stretch plane, ghost), `src/app/window_components.cpp`
  (banner, Preview strip, Add driver action).
- Tests: `component_authoring_input_tests`; `shortcut_input_tests` (Shift+D).

**R094.e: states and dialogs.** §§4, 5.
- Files: `src/app/parameters_scope_dialog.{hpp,cpp}`, `src/app/viewport_selection.cpp`
  (open flow), `src/app/window_components.cpp` (suspend and restore, status strip),
  the §5 message table in `src/app/parameter_messages.{hpp,cpp}`.
- Tests: `component_parameter_state_input_tests`; `native_accessibility_audit` (the
  dialog), `visible_focus_input_tests`, `accessibility_bridge_test.py`.

**R094.f: assemblies and the Components tab.** §6.
- Files: `resources/assemblies/*.json` and README, the `--build-assembly` CLI mode,
  `CMakeLists.txt` (generation and install), `src/io/component_library.{hpp,cpp}`
  (`placeLibraryAssembly`), `src/app/components_tab.{hpp,cpp}`,
  `src/app/viewport_assembly.cpp` (placement mode), `src/app/organization_panel.cpp`
  (tab 12), `packaging/arch/verify-package.sh` (installed assemblies).
- Tests: `assembly_bundle_tests`, `assembly_placement_tests`,
  `assembly_library_input_tests`; `library_input_tests` unchanged.

**R094.g: instanced variants.** §7.
- Files: `src/app/viewport.{hpp,cpp}` (sharing source), `src/app/viewport_inference.cpp`
  and the inference index, the R082 benchmark fixture generator.
- Tests: `variant_instancing_input_tests`; the R082 benchmark in software.

**R094.h: acceptance.**
- Scope: `component_acceptance_input_tests` (§10 script); a "Configurable components"
  section in UX_DESIGN (Tray Info sections, Components tab, Shift+D in the shortcut
  table); the acceptance record `docs/verification/R094h-configurable-components.md`
  with a JSON summary (the Verify line, the X11 and Wayland 1× and 2× matrix, the
  R094.g measurements, installed smoke of the assemblies); this ADR's status and the
  roadmap status.

## Implementation notes (R094.b)

R094.b settled these points in the core and command layers. They refine §§1.4, 2, 4.2,
5 and 6.2; where they differ, these notes govern.

- **`component_parameter.promote {body}`** (catalogue label "Make a placement's
  parameter values its definition's defaults") refuses a nested placement and a
  placement whose definition has no parameter record with `INVALID_TARGET`, a missing
  one with `NOT_FOUND`, and a locked placement (or one under a locked ancestor) with
  `LOCKED_ENTITY`. It makes the placement unique only when another placement, nested
  or top-level, shares its definition; an unshared placement is promoted in place and
  keeps its definition and assembly identity. Either way the batch is one History
  entry, and its `componentOperations` entry adds `madeUnique` and
  `parametersPromoted`. A placement whose variant has repeat copies still has a variant
  after promotion (copies are generated even at the defaults); its values all come
  from the defaults.
- **Assembly identity.** `ComponentParameters::assembly` (`ComponentAssembly {id,
  version}`) with `setComponentAssembly(Document&, definition, identity)`, a core
  builder that is one edit and changes no geometry, for R094.f's bundle builder; no
  command sets it. `version` is 1–2,147,483,647. Record validation and the reader
  refuse a malformed identity (`PARAMETER_NAME` from the builder). The field is written
  only when set, so records without one and their canonical encoding are unchanged. A
  unique copy made by `component.make_unique`, `component.edit_instance`,
  `component_parameter.promote` or the ancestor chain of `component.replace` has none.
  Removing a definition's last parameter and driver removes the record and the
  identity with it.
- **Error details.** `InspectionError` carries an optional `details` object, which the
  session, MCP and CLI failure objects report as `details` when present (an additive
  field; errors without details keep their shape). Every component parameter error
  converted by `executeBatch` and the inspection queries carries
  `componentParameterErrorDetails`: `definition`, `body` (the placement the error names,
  so `LOCKED_ENTITY` from an override, a reset, a promotion or a definition edit names
  the locked placement), `parameter`, `driver`, `offset`, `excerpt`, `cycle`,
  `referrers` and, for a bounds `PARAMETER_DOMAIN`, `bound` (`min`, `max` or `step`),
  `limit` and `value` in SI units. `component_parameters.evaluate` returns the same
  fields inside its `error`.
- **`evaluate` target type.** R093.d's optional `type` already checks an expression
  against a parameter type, which covers every driver kind (stretch, position and
  repeat spacing are lengths, rotation an angle, visibility a boolean, repeat count a
  count, child parameter the child parameter's type from `describe`), so no `for`
  field was added. One gap remains: `type: "choice"` checks a choice result against
  the options of the evaluated definition's own choice parameters, so a child
  parameter of a choice type is checked against its options only by the 300 ms dry run
  of `component_parameter.drive` (§3.5).
- **Dry-run geometry.** `previewBatch` and `previewAmend` had no cap on the bodies
  they return, so no separate `regenerated` section was needed. Both now return
  `geometry` only for changed bodies that are shown (a body or ancestor `hidden` is
  not drawn) and add `removed`: ascending IDs of bodies shown before that the dry run
  deletes or hides, which covers removed repeat copies and members a visibility
  driver hides.
- **History labels.** `Viewport::commitCommands(commands, shared, historyLabel)`
  passes a non-empty label as `history: {label}`; existing callers are unchanged.
- **Persistence.** The ADR 0108 "Schema 28" section has an addendum for the field.
  The `component-parameters-v28.sketchyup` fixture was regenerated once, with the room
  definition carrying `sketchyup-tests/room` version 1; the fuzz seeds built from the
  same fixture carry it, and a malformed identity is a rejected `native-json` seed.
  `docs/api/native-format-v1.json` describes versions and features only, so it did not
  change.

## Implementation notes (R094.c)

R094.c built §1 in the desktop. These points settle what the ADR left open; where they
differ from §1, these notes govern.

- **Widgets.** `ComponentParametersSection` (`src/app/component_parameters_section.*`) is a
  `CollapsibleSection`; `ParameterValueField` (`src/app/parameter_value_field.*`) is one row
  (label column, control, reset button and error line). `EntityInfoPanel` puts both sections
  in one scroll area with no horizontal scroll bar. The Entity info frame stays the panel's
  focus proxy; while Entity info is collapsed the proxy is its header. Tab order is set
  explicitly: section header → each control → its reset (override rows only) → Reset all →
  Edit definition….
- **Layout.** A row is a box layout that puts the label column (label and caption) beside
  the value column while the label fits on one line next to at least 16 characters of value,
  and above it otherwise, in place of `QFormLayout`'s `WrapLongRows` (whose label column did
  not take the height a wrapped caption needs). The section stack takes the Tray's width
  (horizontal policy `Ignored`) and reports its height for a width as its box layout places
  the sections, so the scroll area never squeezes wrapped text. A long bounds range may break
  after its dash. Reset all and Edit definition… stack when they do not fit side by side.
  Two existing Entity info controls (the coordinate frame combo box and "Inspect problem
  geometry") are wider than a 290 px Tray at 200 % text and clip there, as they did before.
- **Section header.** `CollapsibleSection::setDetail` adds the muted count beside the header
  ("2 overridden", omitted at zero) and appends it to the header's accessible description
  ("Expanded, 2 overridden"). Headers elide their title in a narrow Tray instead of widening
  the section; the accessible name keeps the full title.
- **Refresh.** The section rereads `values` and `describe` through `inspectDocument` when the
  document, revision, placement, units or precision change, and runs whether or not the Info
  tab is shown, so the Measurements box works with any Tray tab. Rows are rebuilt only when
  the definition's parameter descriptions change; otherwise they update in place, so a
  commit keeps focus in its field.
- **Captions and names.** A caption joins the source ("Override · default *x*", "Default",
  "Derived · *expression*", "From *parent*") and the bounds ("400–2400 mm", sharing one unit
  when both ends have it; "at least" or "at most" with one bound). The field's accessible
  description is the same text joined by commas, with "Invalid: *sentence*" first while it
  has an error, and a `DescriptionChanged` event is sent. A check box reads "Shown" or
  "Hidden"; the count's − and + buttons are named "Decrease *Label*" and "Increase *Label*".
  The section keeps the design's note ("Values accept the same units as the Measurements
  box. Enter applies, Esc restores. Each change is one Undo step.").
- **Entry.** Lengths go through `parseLength` (document unit by default, `'` and `"`),
  angles through `parseAngle` (degrees by default, `rad`), areas take `mm²`, `cm²`, `m²`,
  `in²`, `ft²` (or `2`) and otherwise the document unit squared, numbers and counts the
  locale decimal or C locale; a count must be whole. A field sends a JSON number in SI
  units. The count spin box has a range of ±999,999 so the field never clamps; a step (keys,
  wheel or the buttons) commits at once and a step past a bound is refused like a typed value.
- **Messages.** Until R094.e moves them to `parameter_messages`, the section writes the §5
  sentences that apply to value fields: the bounds sentences ("2.6 m is above the maximum,
  2400 mm. Nothing was changed."; below the minimum; not a step of), "At *value* the shape
  can't be regenerated. Nothing was changed." for `PARAMETER_REGENERATION`, "The model
  changed. Try again.", "The placement was removed." and "This placement can't hold
  values."; any other refusal shows the core message followed by "Nothing was changed.". The
  core message is the error's tooltip. A text that cannot be read shows "Type a length, such
  as 1200 mm or 3'11"." (and the equivalents per type) at once.
- **Commit path.** The section builds the batch itself (`executeBatch`, or `executeAmend`
  for a Measurements re-entry) with `history: {label}`, then refreshes the viewport and emits
  `Viewport::changed`, as `Viewport::organize` does. `Viewport::commitCommands` is private
  and the placement is always top level, so no component scope is needed. Each batch is
  checked with `evaluate` first, so the core refusal is a race only. Labels are "Change
  *Label*", "Reset *Label*" and "Reset all parameters", the label cut to 64 UTF-8 bytes with
  "…".
- **Measurements box.** With the Select tool and the section showing an editable placement,
  the window shows the parameter's label in the Measurements step label, its stored value in
  the box while the box is not focused, and names the box "*Label*, Measurements in …".
  Enter sets the parameter; re-entry on the same placement and parameter amends while the
  document has not changed since. Typing digits in the canvas with the Select tool still
  triggers the view shortcuts (1, 2, 3 …); the box is reached with F6, Tab or a click, so
  the canvas does not claim digits from those shortcuts.
- **Status text.** While focus is in the section the status bar shows the design's hint; it
  returns to the previous text when focus leaves.
- **Edit definition….** Opens the placement's context as Enter does. A placement with
  overrides passes through a marked hook in `ComponentParametersSection::editDefinition`
  where R094.e opens `ParametersScopeDialog`; until then it opens the context as today.
- **Nested placements.** Every row is read-only (check boxes and combo boxes disabled, line
  edits read-only and focusable), Reset all is disabled and the note is hidden.
- **Theme.** `ThemeColors::information` and `informationSurface` (light `#2c5aa0` /
  `#eef3fb`, dark `#b3cdf8` / `#1f2b3d`) style override captions, borders and fills;
  invalid fields use `failure` and `failureSurface`; focus on an override field shows the
  accent border. `theme_input_tests` checks the text pairs at 4.5:1 and the borders at 3:1 in
  both themes.

## Implementation notes (R094.d)

R094.d built §§2 and 3 in the desktop. These points settle what the ADR left open; where they
differ from §§2 and 3, these notes govern.

- **Widgets.** `ComponentDefinitionSection` (`src/app/component_definition_section.*`,
  "Parameters · definition") and `ComponentDriversSection` (`src/app/component_drivers_section.*`,
  "Drivers") are `CollapsibleSection`s in the Info tab's stack after "Parameters · this
  placement". They show while any component context is open (parametric or not, so the first
  parameter or driver can be added). `CollapsibleSection::addHeaderWidget` puts "+ Parameter"
  and "+ Driver" in the header row after the header (accessible names "Add parameter" and
  "Add driver"). `FormulaField` (`src/app/formula_field.*`) is every default, formula and
  spacing field. Shared helpers (`authoring::query`, `dryRun`, `publish`, `memberOf`,
  `sceneOf`) are in `component_definition_section.hpp`. Each commit is one batch through
  `executeBatch` with `history: {label}`, as R094.c's section commits; a dry run is
  `previewBatch`.
- **Parameter tree and editor.** Rows are ordered by `position`: Name ("Width" over "width"),
  Type (with the bounds, or "derived"), Default or formula (the default's display, or "= " and
  the formula). Cells do not wrap (Qt's delegates cannot size a wrapped cell to its column): a
  narrow Tray elides them and each cell's tooltip has its text; a row's accessible text is
  "Width, width, Length, 400–2400 mm, 900 mm". The editor (Label, Name, Type and Mode in create
  mode, Default or Formula, Bounds, Step, Options, Used by, the re-baseline note) has no button:
  Enter commits and Esc cancels or restores, as §3.2 says. Bounds and Step are typed in
  document units (R094.c's parsing, now the free function `parseParameterText`). Options are
  typed as `oak = Oak; ash = Ash`. A name is checked at the field ("Use lowercase letters,
  digits and _, starting with a letter.", "*name* is a reserved word.", "*name* is already a
  parameter."). One Enter that renames and edits commits `update` then `rename` as one
  "Edit parameter *Label*" entry; a rename alone is "Rename parameter *name*" (the old name).
  Delete refused while referenced lists the referrers by label ("Still used by Leg
  position · Stretch Top."). Used by names drivers "*Kind* *member*". Tree keys (F2, Delete,
  Alt+Up/Down, Enter) are taken through `ShortcutOverride`, so they win over Entity info's F2.
- **Drivers.** Columns Kind, Target ("Top · x past 450 mm", "Leg left front · about z",
  "Shelf · z, 300 mm", "Table · width"), Formula, State. A Changed row's state cell is "⚠
  Changed" in the warning colour with the in-row Rebind (no focus); its accessible text is
  "Changed" and its description the sentence. Rebind shows while any driver is Changed and is
  enabled for a Changed selection; Rebind all shows while two or more are. Labels: "Add
  *Kind* driver", "Edit *Kind* driver", "Remove *Kind* driver", "Rebind driver", "Rebind *n*
  drivers". The form's button reads Add in create mode and Apply when editing a driver (the ADR
  names only Add). Repeat's Max count starts at the first count parameter's maximum, else 10;
  Child parameter's formula starts at the same-named parameter, else the first of the child
  parameter's type. Each axis shows a colour swatch and its letter; its accessible text is
  "x axis (red), the part's own frame" (or parent frame). A dry run of `drive` 150 ms after a
  form change shows refusals under the form; formula errors go to the field whose text holds
  the error's excerpt.
- **Add driver.** `component.add_driver` (Shift+D, Model scope, viewport `WidgetShortcut`) is
  in the Component menu and the palette. It is **enabled in any definition context** (not only
  with one member selected), so that Shift+D can say what to select: "Select a part inside the
  definition, then Shift+D", "The definition itself can't be driven; …" or "A repeat copy can't
  be driven; select the part it copies, then Shift+D". On success the Tray shows the Info tab
  and focus goes to the form's Kind. Kinds offered follow §3.3 (a repeat copy is a placement
  member that is not a definition member; the glue member and its ancestors lose Rotation, and
  Position's axes leave out the glue normal). The rail action is a separate, non-public
  `QAction` ("railAddDriver") after a separator, both visible only in a definition context.
  At 1280 × 900 the rail already overflows its height (Orbit and Pan are in its extension
  menu), so Add driver is reached there at that size.
- **Stretch plane.** `Viewport::setStretchPlane` draws a dashed quad (8 % margin past the
  member's bounds) in `selectionStroke`, the points past the plane with arrows, and the label
  "Stretch · *formula*". It is drawn on the context placement's scene record of the member,
  which is the template when the placement has no overrides (§4, R094.e). Dragging starts on
  the quad or within 8 logical px of its outline, before Alt-orbit; snapping and the "Plane"
  Measurements box follow §3.4. The default plane is the middle of the member's bounds on the
  longest axis, so the table's top gets exactly 0.45 m.
- **Formula fields.** The field refuses non-ASCII input and text over 1,024 characters with a
  sentence, keeping the previous text. The caret is a label positioned from the edit's cursor
  rectangle (`Qt::ImCursorRectangle`) and font metrics. A missing ")" points one past the end
  ("A closing ")" is missing at character 16."). The core reports a `-`/`+` dimension mismatch
  at the operator, not the literal, so the unit fix tries each bare number literal nearest
  that offset with the document's length unit (mm, m or in) and then `deg` (angles: `deg`
  first), and offers the first text that `evaluate` checks clean. Sentences ("Can't subtract
  a number from a length. Add a unit, for example 120 mm.", "No parameter is called depth.",
  the cycle by labels, division by zero, whole numbers, the stretch plane) are composed by
  `formulaErrorSentence` until R094.e moves them to `parameter_messages`; bounds use R094.c's
  `parameterErrorSentence`.
- **Context, banner and strip.** The breadcrumb names a parametric placement's context
  "*Definition* (definition)". The banner's parametric text carries `parametric="true"`
  (information fill, text and border; links in `information`) and the link Close definition,
  which returns to the placement's parent context. Entering a parametric context sets the
  status text to the design's footer hint. The Preview strip is placed **under the banner**,
  as §3.1 says (the design draws it at the canvas's foot), the banner's width, with the fields
  in a wrapping row and the note and Back to defaults below them. Quantities (counts too) are
  line edits, booleans check boxes, choices combo boxes. Its fields follow the banner in the
  focus chain. Esc in the strip is Back to defaults and never reaches the viewport. The strip
  rebuilds its fields only when the inputs change; any new revision refills the defaults and
  clears the ghost.
- **Ghost.** `Viewport::previewParameters` is `previewBatch` of
  `component_parameter.override {body: context placement, values}` (all strip values); it
  keeps the changed bodies' edges and outer face loops and draws them with dashed edges and a
  diagonal hatch in `selectionStroke` over the geometry, tied to the document and revision it
  was computed at. Values equal to the stored ones ("Batch has no committed changes") give no
  ghost and no error. Window grabs do not include the viewport's painter overlay;
  `grabFramebuffer` does.
- **Teardown.** `~Window` detaches both sections and stops the strip's timer before the
  document goes, as R094.c did for its section.
- **Tests.** `component_authoring_input_tests` defines the ADR 0165 table (eight parameters,
  six drivers and the retired one, the room's parameter and child driver) through the
  interface and compares `encodeComponentParameters` and the overrides byte for byte with
  `parameter_fixture::makeTable()`. It runs in native.yml's X11 and Wayland 1× and 2× lines
  and, with `component_parameter_input_tests`, in the sanitized Wayland step.
  `shortcut_input_tests` checks Shift+D; `accessibility_bridge_test.py` reads the sections,
  header buttons, trees, a parameter row and the strip field over AT-SPI.

## Implementation notes (R094.e)

R094.e built §§4 and 5 in the desktop. These points settle what the ADR left open; where they
differ from §§4 and 5, these notes govern.

- **Opening.** `Viewport::openContext` is the gesture path (double-click, Enter, Edit › Edit
  selected context, and Entity info's Edit definition…, whose R094.c hook now calls it). It offers
  the request to the window's opener, which shows `ParametersScopeDialog`
  (`src/app/parameters_scope_dialog.*`) when the placement does not show its template
  (`requireComponentTemplateScope` refuses it); otherwise the context opens directly.
  `enterContext` stays the raw path for the breadcrumb, programmatic callers and the Outliner's
  Enter (`organization_panel.cpp`, which R094.f owns), so the Outliner still opens such a
  placement without asking.
- **The dialog.** A `Settled<QDialog>` (R091.h): title and heading "*Placement* has its own
  values", the design's sentence with the differing values in display order ("Width 1200 mm,
  Shelves 3 and Finish Ash "natural" differ from the defaults. …"), Cancel, Make unique and
  edit, and Edit definition at defaults (the default, focused, `accentFill`), then the
  footnote. The dialog's accessible name is the title and its description the sentence. Each
  choice is checked first by a dry run of its batch; a refusal (or a revision that changed while
  it was open: "The model changed. Try again.") is shown in the dialog with the §5 mark, joins
  its accessible description, and the dialog stays open. A nested placement reads
  "*Width 1800 mm* differs from the defaults. Its values come from *Room*." and offers Cancel and
  Make unique and edit, which is then the accent-filled default.
- **Make unique and edit.** One batch labelled "Make *placement* unique":
  `component_parameter.promote` for a top-level placement. `promote` refuses a nested placement
  (R094.b), so a nested one gets `component.make_unique` followed by one
  `component_parameter.update` per parent-supplied value, setting the copy's default to that
  value, in the same entry. The copy's ID is the document's next definition ID
  (`makeComponentUnique` allocates the placement's copy first); a wrong prediction would only
  make the dry run refuse. The copy then shows its template, so its context accepts edits.
- **Edit definition at defaults.** One batch `component_parameter.reset {all}` labelled "Edit
  *Definition* at defaults"; the window keeps the placement, its overrides (SI JSON), its name
  and the `Document::AmendStamp` taken right after the reset. Each `sync` checks it: when the
  context no longer contains the placement, a stamp that `canAmend` still accepts means nothing
  was edited and the reset is undone (it moves to the redo branch); otherwise the values are
  checked with `evaluate`, those the definition no longer has or refuses are left out one by one
  (the status bar: "*Placement*'s values are restored except Width, which the definition no
  longer accepts"), and one batch "Restore *placement* values" restores the rest. When the
  placement shows its own values again while open (Undo of the reset, another client) the
  context closes with "*Placement* has its own values again; the definition was closed" and the
  restore is forgotten. The banner adds "*Placement*'s own values return when you close the
  definition." Every way of closing (Esc, Close definition, the breadcrumb, a click outside,
  Select #1) goes through the same check, because it watches the context, not the gesture.
- **PARAMETER_TEMPLATE_SCOPE.** Without a suspension, a context whose placement starts to show
  its own values while it is open closes with "*Placement* now has its own values. Close the
  definition and open it again." Only that transition closes it: a placement opened directly
  with values (Outliner, programmatic) stays open.
- **Message table.** `src/app/parameter_messages.*` holds every §5 sentence;
  `parameterErrorSentence` and `formulaErrorSentence` are thin wrappers, and R094.c's and R094.d's
  interim sentences (bounds, regeneration, stale, removed, can't hold values, the formula rows,
  "Text can't be stretched", "*Child* has no parameter *name*", referrers) moved there. It reads
  the error's `details` and the document (driver kind and member, placement names), never the
  sentence. Bounds use the document's display unit for both the value and the bound
  ("2600 mm is above the maximum, 2400 mm."; in metres "2.6 m is above the maximum, 2.4 m.").
  PARAMETER_REGENERATION names the driver: "At -200 mm Slab would pass its stretch plane
  (Stretch Slab). Nothing was changed." (other kinds: "… can't be regenerated (*Kind* *member*)");
  the referrers of a refused removal name drivers as "Stretch Top". `parameterRefusal` maps
  executeBatch's plain "STALE_REVISION" to its code. LIMIT_EXCEEDED quotes the core message and
  adds "Nothing was changed.".
- **LOCKED_ENTITY wording.** The ADR gives the definition-default sentence ("Changing the default
  Width would change *Table*, which is locked. Unlock it or keep the default."). Other definition
  edits read "Changing the definition would change *Table*, which is locked. Unlock it or keep
  the definition as it is.", and a value field "*Table* is locked. Unlock it to change its
  values.". The action reads "Select #1" when the name ends in "#1", else "Select *name*"; a value
  field offers none when the locked placement is the one it edits. A warning is amber
  (`warning`) with a padlock mark, not a red invalid border; its field description starts
  "Warning:" rather than "Invalid:".
- **Actions.** Value fields, formula fields, the definition editor, the driver form and the
  Preview strip each show the message's action as a normal Tab stop after the message; they
  emit `Viewport::parameterAction`, which the window handles: Show the driver opens the
  definition through §4 (asking when the placement has values) and selects the driver in
  Drivers; Select #1 enters the locked placement's parent context (closing a suspended one) and
  selects it, but a locked placement is not selectable in the canvas (`Selection::selectable`),
  so the Tray shows the Outliner with its row current and focused, where it can be unlocked;
  PARAMETER_TEMPLATE_SCOPE closes the context. Because a locked placement cannot be selected,
  its own value fields never show, and the value-field LOCKED_ENTITY sentence is reached only by a
  race. `ParameterMessageLabel` draws the
  crossed circle or the padlock in the text colour for the editor, form, strip and dialog lines.
- **Changed since bound.** The strip (`parameterStatusStrip`) is a `QFrame` on the viewport, the
  banner's width, under the Preview strip when that shows (else under the banner) and after it
  in the Tab chain. It compares the open definition's driver states before and after each new
  revision, so it reacts to every commit path (tools, sections, MCP): drivers that became
  Changed show it with "*History label*. **Repeat Shelf** now starts from a changed shape.
  Placements may not look as expected until you rebind."; any later revision (Rebind, Undo,
  another edit) or closing the context hides it. Rebind commits `component_parameter.rebind`
  for those drivers ("Rebind driver", "Rebind *n* drivers"); Undo triggers Edit › Undo and shows
  the key in the design's muted monospace. Its accessible description is the plain sentence and
  an `Alert` event is sent when it shows.
- **Theme.** The dialog's default uses the `accentFill` rule; warnings use `warning`, the strip
  `warningSurface` and `warningBorder` (the border is decorative: the mark and text carry the
  state).
- **Teardown.** `~Window` calls `detachParameterStates` first: the opener, the action
  connection and a pending restore are dropped before the document goes.
- **Tests.** `component_parameter_state_input_tests` covers the dialog and its three choices,
  undo when nothing changed, the two labelled entries and restored values, partial restore
  (by removing a parameter: a bound that excludes the value takes the same `evaluate` loop, but
  the fixture's nested room table at 1800 mm blocks lowering the maximum below the wide table's
  1200 mm), undoing the reset, promotion, the nested case, the §5 rows at their controls (bounds,
  regeneration with Show the driver, locked in the editor with Select, syntax, type with Use
  120 mm, name, domain, cycle, LIMIT_EXCEEDED in the editor, TEMPLATE_SCOPE and
  STALE_REVISION in the dialog), the rows the desktop cannot produce from its own thread
  (NOT_FOUND, INVALID_TARGET, a locked value field, driver targets) through the table, and the
  strip with Rebind, Undo and Drivers' Rebind all. `native_accessibility_audit` has the dialog
  in the dialog-keyboard inventory ("parameters-scope", Enter on a placement with its own values),
  `visible_focus_input_tests` sweeps it and probes each button, and
  `accessibility_bridge_test.py` reads the dialog, its description and buttons over AT-SPI before
  editing at defaults.

## Implementation notes (R094.g)

R094.g settled these points against the R082 viewport and inference code. They refine
§7; [the verification record](../verification/R094g-variant-instancing.md) has the
measurements and commands.

- **Sharing key.** The viewport resolves a placement body's sharing source as
  `variant->members.at(member)` when `Document::componentVariant(owner)` is non-null,
  and as the definition member otherwise. References are excluded the same way, from
  `variant->references`. That record keys the shared tessellation, the placement mesh
  (`PlacementKey.source`) and, in the inference index, the local-space index. Equal
  values share one `VariantPtr`, so their driven members share one record. Undriven
  members are the template's own `BodyPtr`, so they share with default placements. The
  displayed-content check is unchanged, and a body whose record differs from the
  variant member still draws its own world-space mesh. The body cache also keeps the
  variant pointer, so a body re-resolves its source when its placement's variant
  changes (override, reset, parent value, undo). A document without parameters has no
  variants and takes exactly the previous path. Its benchmark hashes, counters and
  payloads match the base build.
- **Suite.** `variant_instancing_input_tests` uses a window definition with stretch,
  position, repeat and visibility drivers. It has default placements, equal-value
  placements, a second variant and a mirrored variant placement. Every coordinate,
  driver delta and translation is a small dyadic rational, so the shared path and the
  per-body path round identically. The framebuffer and picking hashes are therefore
  required to be equal with sharing on and off, in ten states. R082.dd's prism fixture
  differs by 3–4 pixels, so it reports pixel differences instead of asserting them.
- **Benchmark family.** `real_model_benchmark N variant` gives the R082 prism a width
  parameter (default 2 m) with a stretch driver. It overrides the first placement to
  2.001 m and copies it as a row and then the row as a grid, at the repeated family's
  positions. Every placement then shows one variant with one driven member record. The
  family is saved and reloaded (schema 28) like the others. The 1 mm override keeps the
  rasterized area of the repeated family, so software frame times compare the
  instancing path, not llvmpipe fill. An earlier 2.5 m override drew prisms 25 % wider.
- **Measurements.** The variant family's body-cache and GPU payloads equal the default
  family's at 1,000 and at 10,000 placements, for both the suite's window and the
  benchmark prism. That meets the 10 % criterion with a ratio of exactly 1.
  - In a Release build with R082.jj's single-CPU container controls, the 1 mm variant
    family is within run-to-run noise of the repeated family for complete-frame and
    edit-to-feedback p95 at 10,000 placements, in two interleaved rounds of three runs
    per family. The medians are within +10 % for the frame and −29 % to −3 % for the
    edit, inside a spread of up to 2.9× between repeated runs. A round with the 2.5 m
    override was 20–43 % slower, which is consistent with its wider prisms' raster area.
  - Software rendering does not meet the absolute M9 budgets for either family. One
    llvmpipe CPU takes 0.9–4.3 s per frame at a million triangles, against the 33.3 ms
    and 100 ms targets.
  - This is acceptable for R094.g. The budgets are hardware budgets, met on the AMD
    machine by R082.jj. The AMD session is waived ([ADR 0161](0161-m9-hardware-evidence-waiver.md)),
    and the release criterion is parity with the repeated family, which holds.
- **Remaining per-edit cost (R093.b code, not changed here).** Per-phase thread CPU
  time shows `Document::apply` of a placement move costs 1.1–1.5 ms in the variant
  family against 0.4–0.6 ms in the repeated family at 10,000 placements. At 1,000
  placements the costs are 0.37–0.53 ms against 0.33 ms. The cause is
  `checkUniqueGeometry` (`src/core/model.cpp`), which every apply runs. While any
  placement shows a variant, it loops over `ComponentVariantIndex::placements` and
  calls `variantDirect`, a walk of the variant's member records, once per variant
  placement. The cost is O(variant placements × members per variant). Keeping
  per-variant placement counts in the variant index, or caching `variantDirect` per
  variant, would make it O(distinct variants). `Viewport::refresh` is equal within
  noise.

## Implementation notes (R094.f)

R094.f built §6 in the core, the CLI and the desktop. These points settle what the ADR left
open; where they differ from §6, these notes govern.

- **Sources.** `resources/assemblies/*.json` (`format` `"sketchyup-assembly-source"`,
  `version` 1, `license` `"MIT"`, `assembly`, `name`, `description`, `labels`, `units`,
  `commands`) and a README stating the licence, the format and how to build. Each file is one
  batch of at most 100 public commands applied to a new document; its IDs are the ones a new
  document allocates in order. Members are boxes (`geometry.face` and `geometry.push_pull`)
  with plain-colour materials; no third-party models, textures or fonts.
- **Parameter sets** (Open item 7's default; all quantity inputs are bounded):
  - Interior door: width 600–1200 mm, height 1900–2400 mm, hinge side (Left, Right: two
    leaves with `visibility`; the right leaf's frame is turned 180° about z so one stretch
    and a `position` on width keep it hinged on the right jamb), leaf open 0–90°. The frame
    is a ring with a threshold whose front face is the glue face, so the opening is the
    full width × height.
  - Casement window: width 600–2400 mm, height 600–2000 mm, panes 1–4 (the sash is
    stretched to `(width - 100 mm) / panes` and repeated at that spacing), frame depth
    60–200 mm (a stretch of the glue member's back face along the glue normal, which leaves
    the glue face in place), opening angle 0–90°.
  - Dining table: width 1200–2400 mm, depth 700–1200 mm, height 700–780 mm, legs (Square,
    Tapered: two leg sets with `visibility`), apron (boolean). The leg spacing is derived
    inline (`width - 140 mm`, `depth - 140 mm` in the legs' `position` drivers), not a named
    parameter.
  - Bookshelf: width 400–1200 mm, height 800–2400 mm, depth 200–450 mm, shelves 1–8 (a
    `repeat` whose spacing `(height - 36 mm) / (shelves + 1)` is derived inline).
  - Base cabinet: width 300–1200 mm, height 700–950 mm, depth 300–650 mm, doors 1–2, shelves
    0–3, toe-kick height 50–150 mm.
  - Straight stair: total rise 700–4000 mm, going 220–350 mm, width 600–1500 mm, riser count
    (derived, `round(rise / 175 mm)`), nosing 10–50 mm. **The pitched frame works**: the
    Flight group is pitched by a `rotation` driver about y, the Step group (tread, nosing,
    riser) counter-rotates by the same Δ and is repeated `risers` times along the flight at
    the step's diagonal `sqrt((rise / risers) ^ 2 + going ^ 2)`. No core change was needed.
  The card's count is the definition's parameters (inputs and derived), which gives the
  design's 4, 5, 5, 4, 6 and 5; the values form lists the inputs only.
- **`--build-assembly`.** `sketchyup-cli --build-assembly <source> <output.sketchylib>` is a
  standalone mode; the output must be a new path. The build lives in
  `src/io/assembly_source.{hpp,cpp}` (`buildAssemblyBundle`), so the headless tests build
  bundles without the CLI. It applies the batch, stamps the identity with
  `setComponentAssembly` on the one top-level placement's definition and encodes the bundle.
  **Byte-identical builds** need a fixed document identity: `captureLibraryComponent` and
  `encodeComponentBundle` take an optional identity, and an assembly's is 32 hex digits of
  SHA-256 over its `id` and `version`. The thumbnail (320 × 240 PNG) is the measured-drawing
  hidden-line pass (`captureMeasuredDrawing`, the R078/R092 export path) of an orthographic
  view from the front left, painted with QPainter on a QImage; it draws no text, so it does
  not depend on installed fonts.
- **Build tree and package.** CMake builds each source into
  `<build>/share/sketchyup/assemblies/` (target `sketchyup_assemblies`, in `ALL`, only with
  the CLI) and installs them to `share/sketchyup/assemblies/`. The executables sit at the
  build root, not in `bin/`, so the desktop looks in `applicationDirPath()/../share/
  sketchyup/assemblies` (the package) and then `applicationDirPath()/share/sketchyup/
  assemblies` (the build tree). `verify-package.sh` checks the six installed bundles, that the
  package owns them, that each is byte-identical to a rebuild from the packaged source with
  the packaged CLI, and that removal deletes the folder.
- **`placeLibraryAssembly(Document&, bundle, AssemblyPlacement, overrides, label)`.** The
  placement is a transform (free) or an `AssemblyHostFace` (`host`, `face`, host-local
  `anchor` and `tangent`, `inset`: `component.attach`'s parameters). Overrides are
  `component_parameter.override` literals (a `QJsonObject`) and go through that command's
  builder, so refusals keep the core's codes. Reuse picks the lowest-ID definition with the
  bundle's identity; a bundle without one is inserted as before. Every step runs on one
  `readSnapshot` draft and the document receives one edit labelled "Place *Name*"; a refusal
  leaves it unchanged. `libraryAssemblyIdentity` and `findLibraryAssembly` are the helpers.
- **Components tab** (`src/app/components_tab.*`, tab 12, focus proxy the Show box, and a
  View → Components action like every other Tray tab). "Components" is a
  `CollapsibleSection`; the cards are a `QListView` in icon mode with a painted card
  delegate, two columns sized from font metrics (names and captions wrap), one Tab stop and
  arrow keys. A card's accessible text is "Interior door, cuts opening, 4 parameters" (",
  in this model" when the model has the assembly); its caption adds "In this model" too.
  Enter or a click starts placing. All components adds the user library folder's component
  bundles (caption "Component"; decoded when activated). The values form uses
  `ParameterValueField` (no reset buttons: the form has Use defaults), shows defaults and
  bounds in the model's units, checks each value with `component_parameters.evaluate` on the
  bundle's document 150 ms after typing and at once on Enter, and stores only values that
  differ from the defaults. Esc anywhere in the tab, or Cancel, ends placement; the form
  closes when placement ends. After placing, the Tray shows the Info tab.
- **Colours.** Cards use `input` with a `muted` border (the design's `#c9d4c5` is 1.5:1
  against the card fill, below the 3:1 for borders), the pressed (current) card `selected`
  with a 2 px `accent` border, and with focus in the list a 2 px `ink` border. The name is
  `ink`; the caption is `muted`, and `ink` on the pressed card, where `muted` on `selected` is
  below 4.5:1. The note, Show label and "Esc" key are `muted`. Both themes come from
  `ThemeColors` through `Window::applyTheme`.
- **Placing** (`src/app/viewport_assembly.cpp`). Placement is a mode over the Select tool;
  the status bar shows the design's text ("Placing Interior door · Click a wall face to place ·
  the opening is cut in the same step · Esc cancels"; "… · Click to place · Esc cancels").
  The ghost is the assembly at the form's values from a dry run of
  `component_parameter.override` on a private copy of the bundle's document (all visible
  members, not only the changed ones that `previewBatch` returns), redrawn under the pointer
  with the parameter ghost's dashed edges and hatch. A gluing assembly snaps to the hovered
  face of ordinary geometry through `componentPlacementOnFace` with a tangent horizontal
  along the face (so a door stands upright), draws the opening outline, the anchor and the
  opening width in the information colour, and 150 ms after the pointer rests runs
  `placeLibraryAssembly` on a private snapshot, whose cut outline (or refusal) is shown. A
  free assembly goes onto the hovered face's plane, else the ground. A host anchor from the
  pointer or a face's centre moves in whole millimetres across the face (from its first corner
  along the tangent): the core refuses about one opening in seven cut from anchors with
  single-precision low-order bits, as pointer rays carry, with "Opening requires validated
  closed geometry: self_intersection" (measured over 770 anchors for widths 800–1200 mm;
  none with millimetre anchors). That core defect also affects Shift+H and is left to a core
  slice. The glue anchor of the
  door and the window is the opening's centre at the defaults, so Enter with a face selected
  centres the opening on the face. The Measurements box takes host-local anchor coordinates
  or an inset (gluing) or `[x,y,z]` (free); after placing, re-entry amends the entry
  (`Document::amendLast`) until another edit or a tool change. While placing or amending, the
  box is the placement's, not Entity info's parameter routing (§1.3).
- **Tests.** `assembly_bundle_tests` builds every source twice and compares the bytes with each
  other and with the CLI-built bundle, decodes each, checks the parameters, identity, glue
  and the 320 × 240 thumbnail, and places each at its defaults, every input at its minimum
  and maximum, every choice option and boolean, and all inputs at their minimum and at their
  maximum; the door and window cut through a fixture wall. `assembly_placement_tests` covers
  one entry that inserts, overrides and cuts a 1000 mm opening, the re-cut on a width change
  in one entry, reuse by identity, undo and redo, refusals that leave the document unchanged,
  a unique copy losing the identity, and a bundle without one. `assembly_library_input_tests`
  covers the tab, the filter, the cards by keyboard, the form, the ghost, the door on a wall
  in one entry and its re-cut from Entity info, keyboard placement on a selected face with
  reuse and amend, `[x,y,z]` with amend, and the window, table, bookshelf, cabinet and stair
  at their defaults. `keyboard_panel_tabs_tests` gains the Components tab and its action;
  `library_input_tests` is unchanged.
