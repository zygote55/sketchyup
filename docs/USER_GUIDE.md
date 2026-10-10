# SketchyUp development workflow guide

This guide describes the native implementation in the current review stack.
SketchyUp is still a development build. Check the [support matrix](SUPPORT_MATRIX.md)
before choosing an exchange format or relying on a platform configuration.

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
semicolons between coordinate/dimension values. Invalid input stays selected
with an explanation. Enter completes accepted numeric input and returns focus
to modeling; Escape cancels the active operation.

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

The current document schema is **24**, inside container version **2**. The CLI
can validate an older file and migrate it to a new destination without replacing
the original:

```
sketchyup-cli --validate-native old.sketchyup
sketchyup-cli --migrate-native old.sketchyup --output migrated.sketchyup
```

The destination must be new. Unknown future formats reject explicitly. See the
[native format contract](decisions/0108-public-native-format.md).

## Use the assistant

Open **View → Assistant** or press **Ctrl+J**. Preferences offers the implemented
OpenAI connections and an experimental numeric-loopback Ollama adapter. Choose
an explicit model from the configured connection. Credential setup uses the OS
credential facility; ordinary modeling and saving work without a provider.

Review the context disclosure before sending a model. **Preview first** shows
proposed changes and host-measured information. Check the target objects and
dimensions, then Apply or Discard. Applying is one undoable edit. A manual edit
makes an older proposal stale. An unknown commit outcome must be reconciled
before further editing or saving.

Model prose is not the measurement oracle. The [agent guide](AI_MODELING.md),
[tool reference](TOOL_REFERENCE.md) and [frozen provider corpus](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/R086a-frozen-provider-corpus.md)
describe the supported commands and evaluation boundary. The retained local
CPU profile is experimental, with no automatic model download or cloud fallback.

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
