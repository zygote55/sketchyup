# SketchyUp user guide

This guide describes SketchyUp 1.2.0. Check the [support matrix](SUPPORT_MATRIX.md)
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

Drag the left edge of the sidebar to make it wider or narrower; with the Tray and
Assistant side by side, the handle between them shares width between the two.
**Ctrl+Alt+]** and **Ctrl+Alt+[** (View → Widen sidebar / Narrow sidebar) do the same
in 40 px steps, and double-clicking a handle or View → Reset sidebar width restores the
default. The sidebar keeps at least 240 px, leaves the viewport at least 400 px, and
remembers its width in both Model and Sheets and on the next launch.

In a window narrower than 1100 px (a tiled half screen, for example) the sidebar
collapses to a strip of icons at the right edge: Info, Outliner, Materials, Tags,
History, Assistant and **More panels** (all Model panel tabs). Click an icon, or use
its View command or shortcut (**Ctrl+J** for the Assistant, **Ctrl+Shift+T** for More
panels), to open that panel in a sheet over the viewport; keyboard focus moves into it.
**Esc**, clicking the open panel's icon again or clicking the viewport closes the sheet
and returns focus to where it was. Drag the sheet's left edge to resize it; it shares
its width with the 1100–1500 px side panel. The menu bar becomes one **Menu** button
with the same menus (Alt+F and the other menu keys still open them), and every
shortcut keeps working. Widening the window past 1100 px restores the docked sidebar
on the same tab at its remembered width.

Below 800 px the tool rail shows one group of tools at a time. Its first item is a group
button ("Draw ⌄"); open it (click, or **Enter**, **Space** or **Down** when it has
focus) to choose Select, Draw, Modify, Measure or Camera. Choosing a group shows its
tools and does not change the active tool, and picking a tool by shortcut (**L** for
Line, for example) or from command search switches the rail to that tool's group.
Widening the window past 800 px shows the whole rail again with the same active tool.

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
screen-reader session has been run (waived for 1.0, 1.1 and 1.2, [ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)),
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
were not run for 1.0, 1.1 or 1.2 ([ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)); AMD discrete-GPU
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
with documentation sheets is schema **27** (see [Documentation sheets](#documentation-sheets)),
and a model with configurable components is schema **28** (see
[Configurable components](#configurable-components)). The CLI
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
   is erased, and when an edit gives the geometry new identities without saying
   where it went (merging contexts, explode, make unique). Splitting an anchored edge
   and making the geometry into a component (or a group, from a selection) keep
   dimensions and labels attached, and so do the dimensions and labels drawn in the
   model itself; only an ambiguous case, such as geometry copied to two places at
   once, still breaks them. Select a broken item and choose **Reattach** in Entity info: the nearest point
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

## Configurable components

Configurable components are new in SketchyUp 1.2.0
([ADR 0165](decisions/0165-constrained-component-parameters.md) for the parameters and
formulas, [ADR 0166](decisions/0166-configurable-component-interface.md) for the
interface). A component definition can have named **parameters**, such as a width or a
shelf count, and **drivers** that tie them to its parts. Each placement keeps its own
values, and its geometry is regenerated from the definition. At the default values a
placement shows exactly the definition's own geometry. A wider Tray helps here: drag
the sidebar's edge or press **Ctrl+Alt+]** (see
[Navigate and adapt the interface](#navigate-and-adapt-the-interface)).

**Change a placement's values.** Select one placement of a configurable component.
**Parameters · this placement** in the Tray's Info tab lists its values; the header
counts the overridden ones. Each row says where its value comes from ("Override ·
default 900 mm", "Default", "Derived · width - 60 mm", "From *parent*") and its bounds.
Type a value as in the Measurements box (`1.2m`, `1200`, `3'11"`; the document unit when
none is given). It is checked as you type and applied with Enter or when focus leaves
the field; Esc restores the stored value. A value outside its bounds is refused with a
sentence such as "2.6 m is above the maximum, 2.4 m. Nothing was changed."; it is never
clamped. **↺** next to an overridden value, or **Ctrl+Backspace** in an unchanged field,
resets that value; **Reset all** resets them all. Each change is one Undo step. With the
Select tool, the Measurements box shows and sets the parameter you last focused; reach
it with F6, Tab or a click, because digits typed in the canvas are view shortcuts.
Derived values and the values of a nested placement are read-only.

**Author the definition.** Open the component (Enter, double-click, Edit → Edit selected
context or **Edit definition…**). The banner says you are editing the shared definition
at its default values, and **Close definition** (Esc) leaves. The Info tab then adds:

- **Parameters · definition**: **+ Parameter** adds one, choosing its type (length,
  area, angle, number, count, yes/no or choice) and whether it is an input with a
  default or a derived formula. Select a row to edit its label, name, default or
  formula, bounds, step and options; Enter applies and Esc cancels. F2 renames, Delete
  removes (refused while a formula or driver still uses it), Alt+Up and Alt+Down
  reorder. Changing a default reshapes the definition and keeps each placement's own
  values.
- **Drivers**: select a part inside the definition and press **Shift+D** (Add driver,
  also in the Component menu and as **+ Driver**). Only the kinds that part accepts are
  offered: **Stretch** (the points past a plane move by the change in value),
  **Position**, **Rotation**, **Visibility**, **Repeat** (copies along an axis, up to
  1,000) and **Child parameter** (a nested component's value). Each axis shows its
  colour and its letter. For a stretch, drag the dashed plane in the viewport (it snaps
  to the part's points; Alt turns snapping off) or type **Plane at**; the form counts
  the points that will move. Enter or **Add** applies.
- **Formula fields** accept the definition's parameter names, numbers with `mm`, `cm`,
  `m`, `in`, `ft`, `deg` or `rad`, arithmetic, comparisons, `and`, `or`, `not`, and
  functions such as `if`, `min`, `max`, `clamp`, `round` and `sqrt`. They are checked as
  you type; an error marks the character and says what is wrong, and a missing unit
  offers a fix ("Use 120 mm"). The model keeps the last valid formula until you fix it.
- **The Preview strip** under the banner takes other values and shows the regenerated
  shape as a ghost. Nothing changes until you apply a value elsewhere; **Back to
  defaults** clears it.

A change to a default, formula or driver regenerates every placement in the same edit
and keeps the open context, the selection and materials. If you edit a part that a
driver acts on, an amber strip says the driver now starts from a changed shape:
**Rebind** accepts the new shape and **Undo** reverts the edit. The Drivers list keeps
the **Changed** state until you rebind. Painting a part changes no shape, so its drivers
stay bound.

**A placement that has its own values.** Opening a placement whose values differ from
the defaults asks how to continue:

- **Edit definition at defaults** (the default) resets the placement for as long as the
  definition is open, so you edit the shared shape, and restores its values when you
  close it. If you changed nothing, closing leaves no History entry; otherwise there are
  two ("Edit *Definition* at defaults" and "Restore *placement* values"). A value the
  changed definition no longer accepts is left out, and the status bar names it.
- **Make unique and edit** makes a copy of the definition for this placement alone,
  with its current values as the copy's defaults, and opens the copy.
- **Cancel** (Esc) changes nothing.

A nested placement whose values come from its parent offers only Make unique and edit.

**Place an assembly.** The **Components** tab in the Tray (View → Components) lists
six assemblies that ship with SketchyUp: interior door, casement window, dining table,
bookshelf, base cabinet and straight stair. Choose one (arrow keys and Enter, or a
click), set its values in the form (**Use defaults** resets them), and click to place
it; the status bar says what to click, and Esc cancels. Click a wall face to place a
door or window: it snaps to the face under the pointer, moving in whole millimetres, its
opening is cut in the same step, and changing its width later re-cuts the opening. With
a face already selected, Enter places it at the face's centre. The Measurements box
places by typing instead: `[x,y,z]` for furniture, or, for a door or window on the
selected (or hovered) face, an anchor `[x,y,z]` in the wall's own coordinates or one
inset length. Typing again right after placing moves it within the same step. Placing an assembly that is already in the model
reuses its definition, so every door is one definition with its own values. The
assemblies are MIT-licensed project content with no third-party models, textures or
fonts.

**Limits.** Formulas are one-way: editing geometry never changes a parameter, and a
formula reads only its own definition's parameters. SketchUp Dynamic Components are not
converted into parameters; their attributes are read as read-only properties of a `.skp` import (see
[Importing SketchUp (.skp) files](#importing-sketchup-skp-files)). The in-app assistant cannot change component parameters; MCP
clients and the transaction APIs can (`component_parameter.*` operations and
`component_parameters.*` queries in the [tool reference](TOOL_REFERENCE.md)).

**Files with parameters and older releases.** A model with no configurable component is
saved as schema **26**, or **27** with sheets, which SketchyUp 1.1.x opens. Adding a
parameter saves it as schema **28**. SketchyUp 1.1.x and 1.0.x refuse a schema-28 file
with "Unsupported container field" and leave it unchanged. Removing every parameter and
driver saves the model as schema 26 or 27 again. Schema 28 is fixed from 1.2.0, the first
release that writes it; files with parameters from a development build before 1.2.0 may
need to be re-created.

## Use the assistant

Open **View → Assistant** or press **Ctrl+J**. Preferences offers the implemented
OpenAI connections and an experimental numeric-loopback Ollama adapter. The
supported provider for 1.0, 1.1 and 1.2 is the remote OpenAI connection: choose **Continue with
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
with drawing sheets or component parameters in 1.2: the sheet and component parameter
operations in the tool reference are available over MCP and the transaction APIs, not
in the assistant panel. The retained local
CPU profile is experimental, with no quality guarantee, automatic model download
or cloud fallback.

## Exchange and present a model

Use File import/export commands for GLB/glTF, OBJ/MTL, STL or the documented 2D
DXF subset. SketchUp (.skp) files import too, as described in [Importing SketchUp (.skp) files](#importing-sketchup-skp-files). Review conversion notices before relying on geometry, units, axes,
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

## IFC import and export

IFC support covers the building subset of three schemas for import, and IFC4 for export. Both
directions are limited to what the report states: read the report for every file. The
[format matrix](SUPPORT_MATRIX.md#exchange-boundaries) has the claimed schemas and directions.

**File → Import IFC…** reads an `.ifc` file of schema `IFC2X3`, `IFC4` (IFC4 ADD2 TC1) or
`IFC4X3_ADD2` (IFC 4.3.2.0). It keeps the spatial structure (project, site, building, storey and
space), the elements that have a body, their properties and quantities, materials and colours, and
the object classification with its URI. The result is a new unsaved model; the source file is not
changed, and the whole import is one undo step. Before the import starts, the dialog states the
unit, the spaces choice, the georeference choice and the maximum deviation.

- **Units.** A file must declare a length unit: a metre with an SI prefix, an inch, a foot, a yard or a
  US survey foot. A furlong is refused. A file without a length unit is refused unless you choose one
  in the dialog (or pass `--length-unit`); the choice is reported.
- **Georeference.** A map conversion with no rotation sets the site origin, and the report says so.
  A rotated conversion keeps the project axes, unless the georeference choice in the dialog (or
  `sketchyup-cli --import-ifc ... --ifc-georef grid`) turns the model to the map grid. A map unit that
  is not the metre (US survey feet, for example), several conversions and several model contexts are
  reported and not georeferenced.
  A world coordinate system that is not the identity refuses the file.
- **Limits.** A file over 128 MiB, or with more than 2,000,000 entities, is refused with the limit
  named. A model over the document limits (100,000 unique vertices or faces, 10,000 bodies) imports
  partly, and the report names the elements left out. Geometry more than 1,000,000 m from the
  origin is refused.
- **Not imported.** Advanced breps, NURBS, CSG, swept solids, revolutions and parametric profiles may
  produce geometry, but they are not claimed; check the report. IFC 4.3 infrastructure (alignments,
  linear placement, rail, road, bridge and tunnel) is reported, not imported. Elements whose placement
  cannot be composed, and elements in a cycle, are not imported and are located in the report.
- **What the report names.** The report counts every entity as converted to geometry, kept as
  structure and metadata, or omitted. The command line's report lists the omitted entities by IFC
  class, and the elements not imported by class and reason. For example, `IfcGrid` and `IfcRelNests`
  are counted as omitted, and `IfcAnnotation` and `IfcVirtualElement` are not imported, with the reason
  `unsupportedClass`.
- **Loss.** Geometry arrives as meshes: planar faces are merged where they are flat, and curved or
  tessellated surfaces keep their facets. Openings the library could not cut are shown hidden, on
  the tag `IFC Opening`, and the report says so. Relationships are kept where the report says so: the
  spatial tree, groups, systems and zones, materials, layers and classification. Everything else is
  counted as omitted. Check the report before you rely on imported dimensions or materials.

**File → Export IFC…** writes an IFC4 (IFC4 ADD2 TC1) file of the open model. No other IFC schema is
written, and the file is not a buildingSMART certified view: it declares no view definition.

- **What is written.** The spatial structure (project, site, building, storey and space), groups as
  assemblies, bodies as tessellated face sets, materials and colours, properties, and hidden state.
  A site origin is written as the map conversion only when its label is an authority code such as
  `EPSG:32760`; otherwise the export says the georeference is not written.
- **What is not written.** The dialog lists each kind before anything is written. Terrain, reference
  images, guides, loose edges, curves, annotations, sections, scenes, sheets, component parameters and
  textures are not written. Native history and topology do not appear in IFC.
- **Loss.** Importing the exported file gives back the same bodies, with vertices within 1e-9 m, the
  same hierarchy and the same properties. Face counts can differ by a few faces on a second import.
  No other program's reading is promised.
- **The file is new.** Export never replaces an existing file; choose a new name.

## STEP import and export

**File → Import STEP…** reads a STEP file into a new, unsaved model, and **File → Export STEP…**
writes the open model to a new STEP file. The source file is never modified, and an export never
replaces an existing file: a name that already exists is refused before anything is written. The
command line does the same work: `sketchyup-cli --import-step FILE.step --output MODEL.sketchyup`
imports (without `--output` it only checks the file and prints the report), and
`sketchyup-cli --input MODEL.sketchyup --export-step FILE.step --step-schema ap203` exports (AP214 is
the default). Both run through the optional `sketchyup-ifc` package. Without it, the menu item says
the helper is not installed, and nothing is read or written.

**Schemas.** Import reads AP203 and AP214. AP242 is read on a best-effort basis: the geometry the
translator reads comes across, and the product and GD&T content it does not read is counted in the
report and not imported. Export writes AP214 (the default) and AP203. AP242 is never written; a
request for it is refused.

**Units.** A unit is never guessed. A file that declares its length unit is converted, and the
report names the unit and the factor. A file that declares no unit is refused unless you choose one
(the Unit field of the import dialog, or `--step-length-unit` with mm, cm, m, in or ft). An export
writes millimetres and says so in its report.

**What comes across.**

- Both ways: the assembly structure as groups; names (a name longer than 2,048 bytes is cut, with a
  notice on import); flat colours; layers as the layer name; and hidden parts, which stay hidden.
- Import: a product that is repeated becomes one component with several placed instances. A layer
  becomes the `step.Layer` property on each body, and a body the file hides comes in hidden. A
  product with no colour takes the default grey, with a notice. Each body records the file's digest
  and schema, so it can be traced back to its source.
- Export: elements that share a mesh, a name, a layer and a visibility become one part with several
  occurrences. A face whose boundary does not close is written one triangle at a time, and the
  report counts it.
- Not carried either way: a layer's own on/off switch (the bodies on it keep their own hidden
  state), material names and opacity, textures, guides, text, dimensions, sections, scenes, sheets,
  edges and curves that are not part of a face, and the site origin with its coordinate system. The
  export report lists what was not written, with counts where the count is known.

**Loss of editability.** Imported B-rep faces become planar faces. Each solid is tessellated into a
mesh of flat faces, not B-splines or parametric surfaces. A curved (NURBS or B-spline) surface cannot
be edited as a curve in SketchyUp: you edit the flat faces it becomes, as you edit any mesh. An
export writes the mesh only, so the original NURBS or parametric definition is not kept in the round
trip. The import dialog and the import report say the same.

**Limits.** Import refuses a file over 128 MiB before the helper starts. The helper may run for 120
seconds by default (`--step-deadline` changes that, up to an hour). Import converts at most 1,000,000
triangles in total and 100,000 per element. A document holds at most 100,000 faces of unique
geometry, and a curved surface becomes one face per triangle, so a curved model fills the document at
about 100,000 triangles; the bodies that would go over it are left out and reported as not imported. An export
writes into a 256 MiB file and is refused above it. Measured: about 2.4 KB per triangle face, so a
model made of triangle faces reaches the limit at about 110,000 faces; large flat faces take about
0.9 KB per triangle, so the limit is reached at about 280,000 triangles. The export dialog's size
note appears above 100,000 triangles, before a model of triangle faces reaches the limit.

## Importing SketchUp (.skp) files

**File → Import .skp file…** opens a SketchUp model as a new, unsaved SketchyUp model. Choose
the file; there are no options to set. The file is read by a separate helper process,
`sketchyup-skp-worker`, with a progress sheet and Cancel. A file that cannot be imported is refused
and nothing is created. The report lists what came across, what did not and anything that needs
your attention; **Show .skp import report…** shows it again. The .skp file itself is never
changed, and Save chooses where the new model goes.

On the command line, `sketchyup-cli --import-skp FILE.skp` validates the file and prints the report
as JSON. Add `--output NEW.sketchyup` to write a native file, and `--skp-deadline SECONDS` (1 to 3600,
default 120) to allow the helper more or less time.

### Versions

| SketchUp that saved the file | Import |
|---|---|
| 2021 and later | **Supported.** Tested with a SketchUp 2024 model. A version newer than the ones tested is read and labelled "newer than tested" in the report |
| 2013 to 2020 | **Best effort**, labelled as such in the report. These files use an older container that the reader handles less completely. Some of them cannot be read yet, because the reader fails on certain text labels in them (OpenSKP issue 442). A file it cannot read is refused with the advice below; it is never imported in part |
| Before 2013 | **Refused:** "This SketchUp file is from before 2013. Save it from SketchUp 2021 or later and try again." |

If a file is refused, save it again from SketchUp 2021 or later and import that copy.

### What comes across

- Faces with their holes, with the same outline and orientation as in SketchUp, and their front and
  back materials.
- Edges with their hidden, soft and smooth flags. A loose edge comes across as a wire.
- Components and their instances. Instances of one component share one definition, so editing the
  component changes every instance.
- Layers, as tags of the same name. Hidden layers stay hidden. The default layer (Layer0) becomes
  the Untagged tag.
- Materials with their colour and opacity, and their textures. A texture image in PNG or JPEG form is
  kept; any other image format is converted to PNG.
- The names of components, groups, instances and materials.
- Attributes, as properties named `skp.<dictionary>.<key>`. Only text, number and yes/no values come
  across.
- Dynamic Component attributes (the `dynamic_attributes` dictionary), at instance and definition level,
  as read-only properties. A length is stored in metres. A definition's values reach each of its
  placements, and a placement's own value for the same key wins. Formulas and the `onClick` script come
  across as text: they are shown, never evaluated or run.

Geometry is converted from inches to metres. The file's unit setting is shown in the report, and it
does not change the size of the model.

### What does not come across

- **Groups** come across as components that are used once. The file format does not mark a group in
  the data the reader gets, so the result is a component, not a native group.
- **Scenes, styles, section planes, text, dimensions, guides (construction lines and points) and the
  camera** are not imported. The report counts what was left out.
- **Dynamic-component formulas and behaviour** are not run. A component with `Copies` comes across as one
  placement. The report counts each formula, each `onClick` script and each placement with `Copies`.
- **Live Components.** The import makes no call to any Trimble service, and a Live Component's
  configuration is not available offline. The file does not mark a Live Component, so the report does not
  identify one. Its geometry and attributes come across like any other component's.
- **A face the native model refuses** is left out and counted in the report under "Faces the native
  model refuses". These are faces that are not flat, that cross themselves, that collapse, or whose
  outline touches itself.
- **Attributes** that are not text, number or yes/no values, or that are over the property limits.
- **Export.** SketchyUp does not write .skp files.

### Limits

- The file: 256 MiB at most. A larger file is refused before the helper starts.
- The helper: 4 GiB of address space, 480 seconds of CPU time, 512 MiB of output, and the time
  allowance above. A helper that goes over one of these limits stops, and nothing is created.
- The model: a component of up to 100,000 faces, 100,000 vertices and 300,000 edges. A component over
  these limits is left out and reported, and the rest of the model comes across. The document also
  takes at most 10,000 distinct geometry records, 600,000 faces after placement and 1,024 materials.
  Anything beyond them is left out and reported.
- Textures: an image over 4,096 by 4,096 pixels is not imported, and the report says so.
- Nesting: components nested more than 64 levels deep are left out and reported.

### Trademarks

SketchUp is a trademark of Trimble Inc. SketchyUp is not affiliated with or endorsed by Trimble Inc.
The reader is OpenSKP, an independent open-source implementation (MIT licence) that was built by
studying the file format. It is not affiliated with or endorsed by Trimble either, so some files may
not import exactly as they look in SketchUp.

Dynamic Components and Live Components are Trimble's terms. SketchyUp is not affiliated with or endorsed by
Trimble for them either.

**File → Asset catalogs…** lists the `.sketchycat` catalogs in the catalogs folder, and any catalog
you open with **Open catalog file…**. Pick entries and **Insert** places them at the model's origin,
as one undo step. An entry that cannot be inserted says why: a template opens as a new model, and no
command does that yet; an entry whose texture is missing or does not match its checksum is refused,
and nothing from that catalog is inserted with it. A refused insert opens a report that names the
entry and the dependency. **Choose folder…** changes the folder the dialog lists, and the choice is
remembered. The MCP server and the command line still read only the default catalogs folder.
**File → Export asset catalog…** writes the components, materials and style you choose to a new
`.sketchycat` file. An existing file is never replaced, and the model is not changed.

## Getting models from online libraries

SketchyUp does not connect to any online model library. It does not search one, sign in to one or
download from one. To use a model from a library, download the file yourself in a browser, then
import it. The import makes a new, unsaved model, and the downloaded file is never changed.

### The 3D Warehouse website

The 3D Warehouse website is run by Trimble Inc. SketchyUp is not affiliated with or endorsed by
Trimble Inc. The 3D Warehouse website is named here only to say where to find models.

1. Open the model's page on the 3D Warehouse website in your browser. Sign in to your own account if
   the download needs it. The Download Policy on the 3D Warehouse website sets a daily download limit for
   each plan.
   SketchyUp does not set it, and it does not count your downloads.
2. If the page offers a choice of formats, choose one in the table below. The download may arrive as
   a ZIP archive.
3. If it is a ZIP archive, extract it first, in your file manager. SketchyUp refuses a ZIP archive and
   does not extract it for you. Then import the model file inside it.
4. Import the file from the File menu entry for its type.

| File | In this version |
|---|---|
| `.skp` | Not released yet. When it is, the versions in [the .skp section](#importing-sketchup-skp-files) apply. |
| `.glb`, `.gltf` | Imported with a conversion report, from File › Import GLB/glTF… |
| `.obj`, `.stl`, `.dxf`, `.ifc`, `.step` | Imported with their own reports, from their File › Import entries. |
| `.zip` | Refused, with the advice to extract the archive and import the file inside it. |
| `.dae` (COLLADA) | Refused, with the reason. Where the page offers a newer `.skp` version of the model, download that instead. The `.skp` import is not released yet. |
| `.usdz`, `.fbx` | Refused, with the reason. Where the source offers a glTF or GLB version of the model, download that instead. |

Every import shows a report. A `.skp` report gives the format version and container, the file's
SHA-256, the units, the counts of what came across, the notices, and a plain line for a file read
best effort. A glTF or GLB report gives the triangle, instance and camera counts, the unit and axis
conversion, what was kept, and a notice for each loss. Nothing is dropped without a notice.

**Trademarks.** SketchUp is a trademark of Trimble Inc. SketchyUp is not affiliated with or endorsed
by Trimble Inc. The 3D Warehouse website is named in this section only to say where to find models.

### Licences and credit

- Each model has its own licence. Models on the 3D Warehouse website are licensed under the General
  Model License, which its Terms of Use set out. The Terms we read did not require credit to the uploader. They do
  forbid claiming to own a model you did not create, and removing its copyright or trademark notices.
  Keep those notices in the file.
- The Terms bar selling a model on its own or as a stand-alone item, and gathering models into a
  collection for redistribution.
- Read the Terms of Use on the 3D Warehouse website before you publish or sell work that uses a model.
  This section is not legal advice.

### Poly Haven and Sketchfab

Some sites offer glTF or GLB files, for example Poly Haven and Sketchfab. Check the format on each
asset's page. Each asset has its own licence, shown on its page. That licence governs the asset's use,
and SketchyUp does not grant one. Poly Haven and Sketchfab are named here only to say where to find
glTF and GLB files. SketchyUp is not affiliated with or endorsed by Poly Haven or Sketchfab, and it
does not connect to either site.

### What this is not

This is not a service integration. SketchyUp sends no request to an online library for this, and it
does not search, sign in, download or keep a copy. Only the downloads you make in your browser leave
your computer. The import reads the file on your computer, and no network request is made for it.

## Terrain and site tools

**File → Import terrain…** adds a terrain to the open model from a DXF with contours
(LWPOLYLINE with elevation, 2D and 3D POLYLINE, LINE) or a CSV, TXT, XYZ or PTS point file. Choose
the units first: Import stays disabled until you do (a DXF that declares its units offers them).
Give each layer or description code a role (contour, breakline, boundary, hole, ground points or
ignore) with the Role list under the table; for point files choose a column preset or a meaning
per column above the sample rows. The site coordinates block shows the source extents, the
proposed origin (the data centre rounded to 100 m, editable while the model has no site
origin), the conversion formula and the local extents. The preview on the right shows the
sources or the triangulated surface; drag or use the arrow keys to pan, + and − to zoom and
Shift+Z to fit. **Show report** lists refusals, merges and notices; choose a location to zoom to
it, and copy or save the report. A refused import changes nothing. Import is one undoable edit,
and the first terrain also sets the model's site origin.

**Working with a terrain.** Select a terrain to see its **Terrain** section in the Info tab:
name, triangles and tiles, source file and digest, sources by role, elevation range, generator
and the flat triangles the triangulation left. **Terrain settings…** changes the boundary mode,
maximum edge length, flat-triangle flips and Steiner fill, shows the result of the change before
you apply it, and applies it as one undoable edit. If a terrain's tiles no longer match the mesh
generated from its sources, an amber **Changed since generated** chip offers **Regenerate**
(one undoable edit; it asks first when paint on individual tiles would be lost). **Explode
terrain…** shows what it does to the 100,000-face modelling budget and the 250,000-triangle
terrain budget; Cancel is the default, and Explode is unavailable when the faces would not fit.
To make a terrain from drawn contours, select their edges or curves, right-click in the view (or
press the Menu key or Shift+F10) and choose **Create terrain from selection**; edges inside
groups work too. Constant-height edges become contours and others breaklines; a refusal or any
notices open the report. When the selection includes a closed loop or lies under more than one
tag, a roles step opens first: it lists each tag (Untagged included) with Automatic (contours
and breaklines by height, as above), Contour, Breakline, Boundary, Hole or Ignore, in the same
role table as File → Import terrain…. A boundary must be one closed loop and holes closed
loops; problems are named by tag and count, and Create is enabled once the command's own check
passes. Other selections create the terrain in one click. Modelling tools such as Push/pull, Offset and the drawing tools show a
not-allowed cursor over a terrain, and the status bar offers **Explode terrain…**; Select,
Paint, tags, hiding and locking, and moving the whole terrain work as usual. The **Terrain** menu
(between Draw and Camera) holds the terrain tools and, under the selected terrain's name,
Terrain settings…, Regenerate terrain, **Reset edits…** (asks first; the terrain is rebuilt from
its sources as one undoable edit) and Explode terrain…, then Create terrain from selection.
Right-clicking a terrain offers its tools, Terrain settings… and Explode terrain…. All terrain
actions are also in command search (Ctrl+K).

**Sculpt and smooth a terrain.** Choose **Terrain → Sculpt** (**Shift+S**), **Smooth**, or the
rail's **Terrain** slot after Tape, which starts the last terrain tool used. A strip across the top
of the view holds the tool switcher and the options: **Brush** or **Region**, **Radius**,
**Falloff** (smooth, linear or constant), **Amount** for Sculpt, or **Strength** (0.05–1) and
**Passes** (1–8) for Smooth. Its fields take the Measurements box's syntax and units (`350cm`,
`12'6"`); **F6** moves between the view, the strip and the Measurements box, and Escape in a field
returns to the view. The brush lies on the surface at the pointer: a solid ring at the radius, a
dashed ring at half strength, a dot on every vertex that will move (larger and darker where it
moves more) and a label with the amount, the radius, the number of vertices and the tiles. A brush
that catches no vertex turns amber and dashed and says so; **]** enlarges it and **[** shrinks it
(10 % a press). **Press and drag** paints a stroke at the strip's Amount: each vertex moves once,
by its distance to the whole stroke. The changed surface is shown in place, outlined by a black and
white band, and **releasing** applies it as one undoable edit ("Sculpt terrain"); **Escape**
before releasing withdraws it and changes nothing. **Ctrl** lowers and **Shift** smooths while
held. A **click** without a drag anchors the brush instead: move the pointer up or down to set the
amount, then click or type it. With Smooth, a click smooths one spot. After an edit the
Measurements box reads **Amount** (or **Strength**): typing a new value revises the same edit, as
long as nothing else changed the model. With **Region** scope, a face or closed loop that is
selected when the tool starts is the region; otherwise click points on the terrain and close the
polygon on its first point. The dashed outline outside it is the **Falloff width**. A click on the
terrain or Enter applies it. Pads (stamped building platforms) never move, and their vertices get
no dots. Without a pointer, type `[x, y]` (model metres) or `@E, N` (site coordinates) in the
Measurements box to place the brush, then the amount (or strength) and Enter; in Region scope,
type the points one by one and the first point again to close. Over anything but a terrain the
cursor is not-allowed and the status bar says to point at a terrain; with no terrain in the model
it says how to import one.

**Edit contours.** Choose **Terrain → Edit contours** (or **Contours** in the strip). The
terrain's source contours are drawn solid, breaklines dash-dot and spot points as small crosses,
while the display contour lines fade. Hovering a source highlights it and names it ("Contour
105.00 m · source 17"); when several lie under the pointer, **Tab** chooses another. Click a source
to select it: its vertices get square handles and its segments round handles at their middles, and
the strip shows its **Role** (Contour or Breakline) and **Elevation**. **Drag a square handle** to
move a vertex (a contour keeps its elevation) or a **round one** to insert a vertex there; the
re-triangulated surface is shown in place, outlined by a black and white band, and releasing
applies it as one undoable edit ("Edit contour"). A move that would cross another source, leave the
terrain's outline or enter a pad is refused at the pointer instead: the moved part turns red and
dashed, an × marks the first conflict, a label says what it crosses ("Crosses contour 106.00 m ·
Release to cancel, or move back"), and the status bar gives the place in site coordinates; releasing
then changes nothing. With a source selected, **type a number** to set its elevation, press
**Delete** to remove the focused vertex and **Shift+Delete** (or **Remove**) to remove the source.
**Add contour**, **Add breakline** and **Add spot** draw new sources on the terrain as the Line tool
does: click points, press **Enter** to finish (click a contour's first point to close it, and
**Backspace** removes the last point). A new contour takes its first point's elevation unless you
type one first; a spot point takes the elevation you type, or the surface's when you press Enter.
**Promote** turns the display contour under the pointer into a source contour at its level, which
is how a terrain made from points becomes editable. A pad's outline and daylight line, and a draped
line, belong to their change: select them to see which, and change them in Terrain edits. Without
a pointer, type `[x, y]` or `@E, N` to select the source nearest that position, **Tab** between its
handles, and type a position to move the focused one.

**Stamp a building pad.** Select a horizontal face, a group or component (its lowest horizontal face
is used) or a closed loop of edges, and choose **Terrain → Stamp pad** (**Pad** in the strip); or
start the tool, point at a face, group or component, whose footprint is highlighted, and click.
The strip names the footprint. The pad starts at the footprint's height less the **Pad offset**
(0 m at first; type a slab build-up such as `15cm` once and it is remembered). Move the pointer up
or down, or type the pad's elevation, and press **Enter** or click to stamp it as one undoable
edit ("Stamp pad"). The preview shows the graded surface in place: **cut** tinted brown with a
diagonal hatch, **fill** teal with dots (the strip's legend names both), the pad flat and light,
the slope rays as thin lines and the dashed **Daylight** line where the slopes meet the ground.
**Daylight** grading (the default) takes **Cut** and **Fill** slopes as rise:run (`1:2`; `0.5` and
`26.6°` also work) and a **Max distance** (100 m); **Fixed width** takes a **Width** instead. A pad
that cannot be stamped (a slope ray that leaves the terrain or finds no ground within the maximum
distance, a slope that overlaps another pad) is marked in red with an × and its place, and Enter
does nothing until you change it. Right after stamping, typing a new elevation (`106.2`) or slope
(`1:3`, `26.6°`) revises the same edit. Without a pointer, type the footprint's position as
`[x, y]` or `@E, N`, then its elevation.

**Drape lines and paths.** Select edges, curves or faces (in groups too, above or below the terrain)
and choose **Terrain → Drape**, or right-click and choose **Drape onto terrain**. Point at the
terrain: the lines are shown lying on the surface, a painted face's region in its material when
**Paint regions with their material** is on, and parts outside the terrain red and dashed, counted
in the strip ("1 edge clipped"). Click the terrain or press **Enter** to drape, as one undoable edit
("Drape on terrain"). **Imprint** (the default) leaves the terrain's shape unchanged and adds the
lines to it as breaklines, so later sculpting and pads respect them, and paints the regions on every
rebuild; afterwards the terrain is selected. **Overlay** instead makes ordinary geometry in a new
group in the open context, lifted by **Lift** (0.01 m), and selects that group. The tool then
returns to Select.

**Terrain edits.** With a terrain selected, the **Terrain edits** section follows the Terrain section
on the Info tab. It lists the terrain's changes in the order they apply, each with its kind, its
title ("Pad · Building A"), its key values ("106.00 m · daylight, cut 1:2, fill 1:2") and its number;
the header counts them and the caption gives the use ("5 of 4,096 changes"). Selecting a change
outlines its region in the view with a label; double-click or Enter frames it. **Remove** (or
Delete) removes the change and replays the ones after it; **Edit pad…** changes a pad's elevation (or
its offset below the footprint), grading, slopes or width; **Restamp** re-reads a pad's footprint
from its building (unavailable, with the reason, when the building is gone or the pad was stamped
from edges or a polygon); **Reset edits…** asks first and rebuilds the terrain from its sources.
Each is one undoable edit, replayed in the background while the status bar shows its progress
("Replaying 3 changes · 64 %") with **Cancel**, which leaves the model as it was. A change that no
longer applies after a replay stays in the list, marked **⚠ Skipped** with the reason, until you
remove it or undo the replay. The notices of the last change or replay (changes skipped, paint
removed from rebuilt tiles, sources clipped) show under the list with **Show report**, which opens
the full report.

**Terrain display.** A new terrain uses the **Terrain** material and draws as one smooth
surface: the seams between its tiles and its interior edges are not drawn (Show hidden geometry
shows them dashed). Under **Display** in its Terrain section, **Show contour lines** draws
contours at the **Interval** (metres; every fifth line, a multiple of five intervals, is a
heavier index line), and **Shading** colours the surface by its material or by ten **Elevation
bands** from green through sand to brown. Contour lines are display only: they are not
geometry, and selection and snapping ignore them. Each change is one undoable edit that does not
mark the terrain as changed since generated. An interval that would need more than 500,000 line
segments draws none and says so.

**Site coordinates.** **File → Site settings…** shows the site origin (the easting, northing and
elevation of the model origin, in metres), its CRS label and where it came from (the source file
and its SHA-256). The first terrain import sets the origin; without one, the dialog explains this
and offers **Set origin…**. **Re-anchor…** moves the origin: all geometry moves by the
difference, so every site coordinate stays the same, as one undoable edit. It is unavailable
while a top-level entity is locked or when geometry would pass the ±1,000 km model limit, and
the dialog says which. With a site origin, Entity info adds a **Site** row (the origin, or an
edge's two endpoints with their model coordinates), and in the Measurements box a leading `@`
types an absolute site position for any tool that takes a point: `@512310, 5234510, 100`
(without Z the point lies on the drawing plane). The viewport marks the site origin with a
labelled crosshair while a terrain is selected or Site settings is open; **View → Site origin**
shows it all the time, and the choice is remembered.

**Site coordinates in DXF.** With a site origin, **File → Export 2D DXF…** offers **Write site
coordinates** (off by default; disabled without an origin). It adds the origin back to X
(easting) and Y (northing) in the chosen unit, so coordinates match the survey; Z is not written.
On the command line use `--dxf-site-coordinates`. STL, OBJ and the other exports keep model
coordinates, and their reports say so.

**Command line, MCP and the assistant.** `sketchyup-cli --import-terrain FILE` imports a terrain
into `--input` (or a new model) and, with `--output`, writes a new native file. Options include `--terrain-units` (required for point
files), `--origin E,N[,Z]`, `--terrain-crs`, `--terrain-roles` and the point-file options; on a
terminal it shows the check's stages on stderr. A terrain source file can be up to 64 MiB. Automation sessions and MCP expose every site and
terrain command and query, among them `terrain.import`, `terrain.create_from_edges`,
`terrain.sculpt`, `terrain.edit_source`, `terrain.stamp_pad`, `terrain.drape` and
`terrain.describe` (see the [tool reference](TOOL_REFERENCE.md)). The in-app assistant can read a
terrain but not change one: it sees only `site.describe`, `terrain.describe` and
`terrain.elevation_at`.

## Quantities, reports and schedules

**View → Quantities** opens the Quantities section of the Tray and puts the focus on **Group by**. It
shows the totals for the whole model: contexts, instances and definitions, the area, the solid volume,
and the solids whose volume could not be checked. Each total carries its unit and the revision of the
model it was computed from. The count runs off the interface, so the section stays usable while a large
model is counted. **Cancel** stops a count and keeps the last complete result, labelled with its own
revision.

**Grouping and filters.** Group by None, Kind, Tag, Material or Definition. **Include hidden** (the
default) counts hidden groups, hidden bodies and content on hidden tags; **Visible only** leaves them out.
A material filter limits the area rows only, because counts and volumes ignore it. **Selection only**
counts the selected bodies and is available while something is selected.

**Solids that are not counted in volume.** A body that is not a closed solid has an area, which is
counted, but no volume. Each such body is listed under its reason, such as "Not a solid: open boundary",
and the volume total says how many bodies it leaves out. Terrain has an area but no volume.

**Export.** **Export CSV…** and **Export JSON…** write the report to a new file. An existing file is never
replaced. The CSV is the file of record. Its first lines are `#` lines that state the document and its
revision, the units, the precision, the metrics, the grouping and the filters. The JSON carries the same
figures. A report over 256 MiB is refused, and nothing is written.

**Schedules.** **Save as schedule…** keeps the current grouping and filters under a name, for the open
model only. Schedules are stored on this computer, not in the model file, so saving one does not change
the model. Run a schedule from the list, rename it or delete it. A schedule saved for one model is refused
for another, and a schedule does not use the selection. It keeps the precision it was saved with.

**Command line.** `sketchyup-cli --input MODEL.sketchyup --report OUT.csv` writes a report. Add
`--metrics counts,areas,volumes` (or `all`), `--group-by tag`, `--visible`, `--precision full` (or a number
of decimals) and `--format csv` or `json`. `--report OUT.csv --schedule NAME` runs a saved schedule;
`--save-schedule NAME` saves the flags as a schedule; `--list-schedules` and `--delete-schedule NAME`
manage them. The command line never changes the model and never replaces a file.

**What is not counted.** Sheets, views and items are listed in the file's metadata, not in any total. The
in-app assistant cannot run reports or schedules. Quantities, reports and schedules are not in the 1.3.0
declaration; the [support matrix](SUPPORT_MATRIX.md#exchange-boundaries) gives the claimed limits.

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
