# SketchyUp user guide

This guide describes SketchyUp 1.1.1. Check the [support matrix](SUPPORT_MATRIX.md)
before choosing an exchange format or relying on a platform configuration, and the
[release notes](RELEASE_NOTES.md) for upgrade notes and known limitations.

## Build a measured solid

Start a new model and focus the viewport. The following keyboard workflow is
covered by the [measured construction fixture](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/R084d-keyboard-modeling.md).
These are default shortcuts; saved custom bindings take precedence.

1. Press **R** for Rectangle. Use **F6** to reach Measurements, type `[0,0,0]`,
   and press Enter to set the first corner.
2. In Measurements enter `2m,3m` and press Enter. The rectangle has area **6 m²**.
3. Press **Ctrl+K**, search for the new face, select its result and press Enter.
   Press **P** for Push/Pull, then enter `4m` in Measurements.
4. Inspect the resulting body in Entity info: its envelope is **2 × 3 × 4 m**
   and its volume is **24 m³**. **Ctrl+Z** undoes the extrusion;
   **Ctrl+Shift+Z** restores it. Save with **Ctrl+S**.

Explicit suffixes override the model's display units. Comma-decimal locales use
semicolons between coordinate/dimension values. The numeric keypad's decimal key
always types your locale's decimal separator, whatever its keycap shows, in
Measurements and in numeric fields such as Entity info; the main keyboard's `.`
and `,` are unchanged. Invalid input stays selected
with an explanation. Enter completes accepted numeric input and returns focus
to modeling; Escape cancels the active operation.

**File → Document units** chooses metres, millimetres or feet and inches, and a
display **Precision**. Each precision choice shows a sample in the selected
units. **Full** is the default. It shows the exact trimmed values used before
this setting existed. A fixed precision shows that many decimal places, keeping
trailing zeros (`1.20 m`). For feet and inches it applies to the inches
(`4' 0.6"`). Areas and volumes use the same number of decimals. Changing units
resets precision to Full. Precision changes only what is displayed; geometry,
typed values and editable fields keep their exact values. It is saved with the
model and can be undone. Decimal imperial entry works with any units: `2.5in`,
`1.25ft`, `1.5'` and `3' 4.5"`, or a bare `2.5` (feet) in a feet-and-inches model.

## Navigate and adapt the interface

**F6 / Shift+F6** cycles window regions. **Ctrl+K** finds commands, objects and
recent files. **Ctrl+Shift+T** toggles the Model panel; in its text controls,
**Ctrl+Alt+Shift+T** creates 3D text. These two actions have different shortcuts.
Outliner supports keyboard navigation, renaming, visibility/locking actions and
undo. Entity info exposes host-computed measurements for the current selection.

Use **View → Interface text size** for 75–200% text independently of display
scaling. Light, Dark and System themes are under View. Navigation preferences
include trackpad gestures, field of view and reduced motion. The
[text-size](decisions/0151-interface-text-size.md) and
[keyboard Outliner](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/R084k-keyboard-outliner.md) records identify
the exact checked workflows and their limits.

Find **Keyboard shortcuts** with Ctrl+K. Filter the command list, select an action,
enter the new sequence and choose **Assign**, then **Save**. Conflict messages
explain unavailable combinations. Cancel preserves existing bindings; Reset
defaults is a draft until saved. See [shortcut behavior](decisions/0149-native-shortcut-editor.md).

## Accessibility and keyboard use

This section records what was tested. It is not a conformance claim; accessibility
acceptance is still open (see the [support matrix](SUPPORT_MATRIX.md#runtime-and-services)).
Shortcuts are defaults; saved custom bindings take precedence.

**Without a pointer.**

- **Regions:** F6 / Shift+F6 cycles the Commands button, tool rail, viewport,
  model panel, assistant (when shown) and Measurements.
- **Tool rail:** the rail is one Tab stop that lands on the active tool. Up/Down (or
  Left/Right) move between tools and wrap, Home and End go to the ends, and Enter or
  Space chooses the focused tool. Tools that do not fit are under the last button,
  **More tools**, which opens a menu of them.
- **Commands and objects:** Ctrl+K searches commands, objects, recent files and
  faces typed as `face 1/2` (body/face). Choosing an object selects it.
- **Exact modeling:** choose a drawing tool by its letter and type digits in the viewport;
  they move to Measurements. `[x,y,z]` sets an absolute point, Enter commits and
  Escape cancels. With a first point, Right, Left and Up lock the red, green or blue
  axis, and a typed length then follows it ([fixture](https://github.com/zygote55/sketchyup/blob/4d71fcd0d5eedc612827b644d86cdaaab4bcd98e/docs/verification/R084d-keyboard-modeling.md)).
- **Selecting:** with the Select tool (Space) and viewport focus, Tab / Shift+Tab
  steps through visible entities, Ctrl+A selects all eligible geometry, Enter opens
  the selected group or component and Escape leaves or clears.
- **Outliner:** F6 to the model panel, then arrows or Home choose an entity; F2
  renames, Space hides/shows, Ctrl+Shift+L locks, Enter opens a group, Escape leaves it
  and Ctrl+Z / Ctrl+Shift+Z undo and redo ([workflow](https://github.com/zygote55/sketchyup/blob/4d71fcd0d5eedc612827b644d86cdaaab4bcd98e/docs/verification/R084k-keyboard-outliner.md)).
- **Model panel tabs:** inside a panel page, Ctrl+Tab and Ctrl+Shift+Tab switch tabs
  and move focus into the new page; from the tab bar (Shift+Tab from the page), Left
  and Right switch tabs. Every tab also has a View menu and Ctrl+K command, such as
  **Entity info** or **Outliner**, that shows the panel and focuses that page
  ([record](https://github.com/zygote55/sketchyup/blob/15bcf724be7b17545ca5b8ff562995bca455f9f6/docs/verification/R084s-keyboard-panel-tabs.md)).
- **Exact values:** Entity info (the **Info** tab) shows the selection's bounds, area
  and volume. The AT-SPI check read 8 × 6 × 0.15 m, 100.2 m² and 7.2 m³ for the demo
  slab ([bridge](https://github.com/zygote55/sketchyup/blob/4d71fcd0d5eedc612827b644d86cdaaab4bcd98e/docs/verification/R084l-linux-accessibility-bridge.md)).
- **Shortcuts:** Edit → Keyboard shortcuts…, or Ctrl+K, as described above.
- **Appearance:** View → Interface text size… (75–200%), View → Theme (System, Light,
  Dark) and View → Reduced camera motion. Measured text, selected-text, button, hint
  and breadcrumb-link contrast exceeds 4.5:1 in both themes; this is not a check of
  every state or dialog.
- **Focus:** every focusable control (text fields, buttons, combo and spin boxes,
  check boxes, lists, trees, tabs, the tool rail and the assistant panel) draws a
  1 px accent ring when it has keyboard focus, in both themes; the viewport draws
  a 2 px accent border.

**The 3D viewport.** The viewport is a rendered canvas. Assistive technology finds
it as "3D model viewport" and can read its selection summary, a count such as
"1 face, 2 edges" or "No selection", which updates when the selection changes. While
an assistant preview is focused it describes that preview's world bounds instead. The
canvas exposes no geometry, positions or hover targets, so a screen reader cannot
read the model from it. Names and measurements come from the panels: the
**Selection summary** label (its description adds the selected entity's name,
editing context and face area), the Outliner and Entity info. No interactive
screen-reader session has been run (waived for 1.0 and 1.1, [ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)),
so how a particular reader announces a change is unverified.

Some interactions remain pointer-first:

| Interaction | Keyboard alternative |
| --- | --- |
| Orbit, pan, zoom (drag, wheel, trackpad) | None for orbit, pan or zoom. **1** / **2** / **3** set perspective, top and front, and the View menu or Ctrl+K offers Right, Back, Left, Bottom and Isometric. **Shift+Z** fits the model. Camera → Walk (W A S D or arrows, Q / E height, Esc exits) and Look around (arrows) move the eye. |
| Picking by click or window drag | Tab selection, Ctrl+A, the Outliner and Ctrl+K as above. No keyboard rectangle selection. |
| Snapping to an existing endpoint, midpoint or edge | None. Enter the coordinates instead. Tab cycles inference candidates and Down locks parallel/perpendicular, but both depend on the pointer hovering the geometry. |
| Push/pull by dragging | Select a face, press **P** and type the distance. |
| Move, Rotate, Scale | Measurements accepts `[x,y,z]` pivots, displacements, angles and factors, but no keyboard-only test covers these tools. |
| Follow Me, Intersect, Solid tools, Face orientation | Select the inputs, then press Enter. Not individually tested without a pointer. |
| Paint tool | Clicking a face is required, and Paint disables Measurements. Edit → Set original face color… colors the selection. |
| Freehand | None. Line with typed coordinates draws polylines, not strokes. |

**Known gaps (M9).** These are the support matrix's limits: dialogs, keyboard
layouts and interactive screen-reader workflows are not established.
Tab traversal is checked for the Drawing plane, Keyboard shortcuts, Commands and
Assistant preferences dialogs ([record](https://github.com/zygote55/sketchyup/blob/4d71fcd0d5eedc612827b644d86cdaaab4bcd98e/docs/verification/R084p-dialog-keyboard.md)), not every
dialog. Physical keyboard layouts and input methods are untested, and no Orca,
speech or braille session has been validated. These physical checks, and the physical
checks for displays (a 150% output, hotplug on X11, suspend/resume) and Intel graphics,
were not run for 1.0 or 1.1 ([ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)); AMD discrete-GPU
results are recorded (R082.ii).

## Save, recover and migrate

Save named `.sketchyup` documents explicitly. Longer native opens/saves show a
progress dialog while file work continues. If edits arrive during a save, status
reports that newer edits remain unsaved; save again to record them. See the
[file-operation contract](decisions/0158-responsive-native-file-operations.md).
A successful replacement keeps the
previous valid file as `.sketchyup.bak`. Automatic recovery normally runs every
30 seconds and keeps separate copies. File → Recovery settings changes the
interval; File → Save recovery now retries after a reported failure.

File → Recover work lets you open a verified recovery copy, open the last saved
file, or discard selected recovery data. Recovered work first saves to a new
path. The status distinguishes a verified recovery copy from newer in-memory
edits. See [recovery semantics](decisions/0016-native-recovery.md).

The current document schema is **26**, inside container version **2**; a model
with documentation sheets is schema **27** (see [Documentation sheets](#documentation-sheets)). The CLI
can validate an older file and migrate it to a new destination without replacing
the original:

```
sketchyup-cli --validate-native old.sketchyup
sketchyup-cli --migrate-native old.sketchyup --output migrated.sketchyup
```

The destination must be new. Unknown future formats reject explicitly. See the
[native format contract](decisions/0108-public-native-format.md).

## Documentation sheets

Sheets are new in SketchyUp 1.1.0
([ADR 0163](decisions/0163-documentation-sheets.md) for the records and
[ADR 0164](decisions/0164-sheet-layout-interface.md) for the interface). A sheet
is a drawing page with views of the model's scenes, dimensions, labels, text and
a title block. Editing a page never changes the model. File → Export sheets…
writes the pages to a PDF and File → Print… prints them, both at actual size.

1. **Save the scenes you want to draw.** A plan is a Top orthographic scene, an
   elevation a Front, Back, Left or Right orthographic scene; a perspective
   scene gives an illustration that is not to scale.
2. **Switch to Sheets** with the **Model | Sheets** switch in the header, View →
   Sheets workspace, or **Ctrl+Alt+S**. Press it again to return to Model.
3. **Create a page** with **+ Page** below the canvas or **Ctrl+Shift+N**. A new
   page is A3 landscape. Ctrl+PgUp and Ctrl+PgDn change pages; the Pages
   section of the Tray lists them, and F2 there renames one.
4. **Add a title block** with Insert → Title block. It sits in the bottom-right
   corner of the margin. Select it, press Enter and type the project, then Tab to
   the drawing title; Enter finishes. Date and revision are in Entity info, with
   the page's sheet number. The page number and the scale fill in by themselves.
   Later pages made with + Page copy the current page's title block.
5. **Place a view.** Press **W**. The Tray shows **New model view**: choose a
   scene and a scale, then drag a frame on the page, or point and type a size
   such as `200,150` (millimetres) and press Enter. Move a view by dragging it or
   with the arrow keys (1 mm; Shift for 10 mm), crop it with its handles, and
   type `1:100` while it is selected to change its scale. Enter opens edit-view
   to pan and zoom what the frame shows; Done or Esc leaves it.
6. **Dimension.** Press **D**, click two model points in one view, then click
   (or type) the offset. The value is measured from the model at the view's
   scale. With nothing selected, Entity info sets the page's dimension unit and
   decimals; the default is whole millimetres without a unit.
7. **Label and text.** Press **K**, click a model point in a view (or anywhere
   on the paper), then click where the text goes and type; Esc finishes. Press
   **X** and click to add text. Enter edits a selected text in place.
8. **Accept changes.** When the model changes, affected views show an amber
   **⟳ Changed** chip; dimensions already show the new values. Select a view and
   press **U** to accept it, or choose **Accept all** in the References section
   or the Page menu. A view whose scene was deleted is **Broken**: its card offers
   **Choose scene…**, which relinks it and keeps its frame and scale.
9. **Reattach.** A dimension or label whose model point no longer exists turns red
   and reads "[Missing reference]". This happens when the anchored vertex or edge
   is erased, and also when an anchored edge is split or the geometry is made into
   a component. Select it and choose **Reattach** in Entity info: the nearest point
   in the view is proposed; press Enter to accept it or click another point.
10. **Export a PDF.** Choose File → **Export sheets…** (in Model too, once the
    model has pages). Pick **All**, the **Current page** or a **Range** such as
    `A-101–A-301` (sheet numbers, or page positions such as `1, 3–4`), and the
    file with **Choose…**. **Checked before publishing** lists what the export
    will do, and updates as you change the options or the model:
    - a **broken view** blocks the export. **Go to view** closes the dialog and
      selects it; or tick "Export it as an empty, labelled frame";
    - **changed views** block it too. **Review and accept** selects the first
      one, so you can look at it and press **U**; or tick "Export them as they
      are now", and the export report names them;
    - a **font that is not installed** names its replacement; **Choose another
      font** selects the text and its Font field;
    - the measured scales, and the perspective or rendered views, which are drawn
      as images at 300 dpi.

    **Export PDF** works once nothing blocks. It writes a new file and never
    replaces one; if it fails, nothing is written. Each page is its own paper size,
    text stays real (it can be selected and searched), and lines stay vector. The
    report that follows lists the file, the pages and any views exported as they
    are now or as empty frames. **Show the reconciliation report (JSON)** shows
    the full record, which is also attached to the PDF as
    `sheet-export-report.json`.
11. **Print.** File → **Print…** in Sheets runs the same checks. **Choose…** next
    to the printer opens the system print dialog for the printer, the paper and
    the pages. Every page prints at 100% from the paper's top-left corner and is
    never scaled to fit, so measurements on paper match the scale. A page larger
    than the paper is cut off at its edge, and the dialog warns before you print;
    choose larger paper to print it whole. Set the printer itself to print at
    actual size if it offers its own scaling.

Every page operation is one undo step in the same history as model edits, so
Ctrl+Z in Sheets can step back into a model edit; the History section shows
what the next undo is.
Without a pointer, the Pages tree selects views and items, Tab cycles them on the
canvas, typed values in Measurements place and move them, and Entity info edits
every value.

**Files with sheets and SketchyUp 1.0.x.** A model with no pages is still saved as
schema **26**, which SketchyUp 1.0.x opens. Adding a page saves it as schema
**27**. SketchyUp 1.0.x refuses a schema-27 file with "Unsupported container
field" and leaves it unchanged; it does not open the model without its sheets.
Deleting every page saves the model as schema 26 again. To share a model with
someone on 1.0.x, save a copy, delete its pages, and save the copy. Schema 27 is
fixed from 1.1.0, the first release that writes it; files with sheets from a
development build before 1.1.0 may need to be re-created.

## Use the assistant

Open **View → Assistant** or press **Ctrl+J**. Preferences offers the implemented
OpenAI connections and an experimental numeric-loopback Ollama adapter. The
supported provider for 1.0 and 1.1 is the remote OpenAI connection: choose **Continue with
ChatGPT** to sign in with a ChatGPT plan (the subscription that includes Codex;
SketchyUp does not use the Codex tool or its credentials). The local adapter is
experimental and is not a 1.0 requirement ([provider scope](decisions/0160-provider-scope-for-1-0.md)).
Choose an explicit model from the configured connection. Credential setup uses the OS
credential facility; ordinary modeling and saving work without a provider.

Review the context disclosure before sending a model. **Preview first** shows
proposed changes and host-measured information. Check the target objects and
dimensions, then Apply or Discard. Applying is one undoable edit. A manual edit
makes an older proposal stale. An unknown commit outcome must be reconciled
before further editing or saving.

Model prose is not the measurement oracle. The [agent guide](AI_MODELING.md),
[tool reference](TOOL_REFERENCE.md) and [frozen provider corpus](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/R086a-frozen-provider-corpus.md)
describe the supported commands and evaluation boundary. The assistant does not work
with drawing sheets in 1.1: the sheet operations in the tool reference are available
over MCP and the transaction APIs, not in the assistant panel. The retained local
CPU profile is experimental, with no quality guarantee, automatic model download
or cloud fallback.

## Exchange and present a model

Use File import/export commands for GLB/glTF, OBJ/MTL, STL or the documented 2D
DXF subset. Review conversion notices before relying on geometry, units, axes,
materials or hierarchy. Imported content becomes an unsaved native model; Save
chooses its native destination. Keep multi-file OBJ/MTL/image packages together.
Use the [format matrix](SUPPORT_MATRIX.md#exchange-boundaries) for losses and limits.

**File → Export measured PDF/SVG** exports an orthographic drawing with explicit
page size and scale. Technical lines produce vector geometry; current appearance
is identified as raster content. Print at actual size to preserve the requested
scale. The editing model and camera remain unchanged.

**Camera → Render** uses an optional installed Blender executable and an immutable
captured model. Results identify their source revision. Save image as writes the
PNG separately from model Save. Jobs supports cancellation, retained results and
explicit retries. Open scene in Blender produces a one-way `.blend` handoff.
Scene animation exports a bounded PNG sequence and timing manifest; it does not
encode a movie or resume an interrupted sequence automatically.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| A modeling shortcut does nothing | Focus the viewport; check saved shortcuts and the active command. Text fields retain their own typing behavior. |
| Assistant is unavailable | Open its Preferences and inspect connection/model status. Manual editing and Save remain available. Never put credentials into a model or a support report. |
| Apply is unavailable after an edit | The proposal is stale; start a fresh preview on the current model. |
| Commit outcome is unknown | Use Reconcile; do not assume success or repeat a mutation blindly. |
| Recovery status reports a write failure | Save a named file if possible and use Save recovery now after resolving the reported storage problem. |
| An interchange file rejects | Read the conversion report and check the explicit supported subset, units and resource bounds. |
| Blender is missing or rejects a device | Select an installed compatible executable and recheck its devices. Native editing does not depend on Blender. |

When reporting a problem, include the build/source version, Qt/display backend,
the operation and its visible error. A small synthetic reproducer is preferable
to a private model. Raw provider exchanges and credentials are not public support
artifacts.
