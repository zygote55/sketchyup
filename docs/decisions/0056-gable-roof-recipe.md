# 0056: Editable gable roof recipe

Status: implemented for the R060.b roof subset.

`assembly.roof` creates a standalone continuous roof slab as a named ordinary
group containing one closed native solid. The ridge runs parallel to local Y.
Its footprint is local X=0…width, Y=0…depth; `origin` places that coordinate frame
in world space. It preserves all pre-existing scene records and resources.

| Parameter | Default | Allowed values / meaning |
| --- | --- | --- |
| width, depth | 6 m, 4 m | 2–100 m building footprint |
| plateHeight | 2.7 m | 0.5–20 m; underside height at X=0 and X=width |
| pitchDegrees | 30° | 5–75° from the horizontal |
| overhang | 0.3 m | 0–2 m beyond each of the four footprint edges |
| verticalThickness | 0.15 m | 0.02–1 m, measured vertically |
| origin | [0, 0, 0] m | Explicit world-space translation within model coordinate bounds |

For angle θ, the underside ridge height is `plateHeight + width/2 × tan(θ)`.
The eave height is `plateHeight − overhang × tan(θ)` and must remain positive.
The top ridge is one vertical thickness higher. Perpendicular slope thickness is
`verticalThickness × cos(θ)`. Both the input origin and every translated bound
must fit the model's coordinate limits.

## Construction and postconditions

A six-corner cross-section describes the two connected sloping slabs and their
vertical end thicknesses. Ordinary `geometry.face` and `geometry.extrude_isolated`
commands produce a native prism of length `depth + 2 × overhang`. Ordinary grouping,
translation, material and property commands complete the assembly. The reported
bounded `expandedCommands` are executable engine commands, with no opaque mesh or
external geometry generator. A shared recipe builder retains the existing
100-command, 64 KiB expansion and 128 KiB report bounds.

The final expansion batch sets authored metadata and uses `assert.measurement`
for volume, area, dimensions, minimum and maximum bounds. For roof span S, length L,
vertical thickness T and half-slope length H:

- Material volume = S × L × T.
- Surface area = (4H + 2T) × L + 2S × T.
- Unique edge length used to bound area error = 8H + 4T + 6L.

Bounds use 1e-6 m absolute tolerance. Area and volume tolerances are explicitly
reported and scale with the geometry kernel's linear tolerance times edge length
or surface area, respectively (eight times those products, with a 1e-7 floor).
Independent fixtures inspect actual ridge/eave vertices and closed-solid orientation.

The receipt exposes actual IDs, dimensions, authored pitch/thickness/origin, measured
material volume and completion assertions. Their `evaluation: "recipe_completion"`
is distinct from an explicit outer-batch `assert.measurement`: later outer commands
may intentionally move or reshape the roof. Authored metadata records the creation
parameters; it is not a persistent constraint system. Current measurements remain
authoritative after manual edits.

Publication is one outer batch/transaction Undo item. Invalid fields, out-of-range
parameters, nonpositive eaves, coordinate overflow or a failed geometry assertion
reject the entire batch, including earlier edits. Existing room/window, material,
component and host records are preserved. Roof placement is explicit; existing
walls remain separate editable objects, and gable infill/trusses are separate work.

## Executable study

Prompt: “Add a gable roof for a 6 × 4 m footprint, with its underside at 2.7 m at the
wall plates, 30-degree pitch, 300 mm overhang and 150 mm vertical thickness.”

The [versioned recipe](../../examples/roof-recipe-v1.json) starts from an empty
model, creates the default room and roof in one transaction, measures the roof,
previews, commits and saves it. Typed references use actual returned roof IDs.
For an existing room, the shared command alone adds the roof at the explicit origin.
The [starting room](../../examples/m6-roof-before.sketchyup) and
[editable roof result](../../examples/m6-roof-after.sketchyup) are native fixtures.

The default roof has 4.554 m³ material volume. Its underside eave and ridge heights
are approximately 2.526795 m and 4.432051 m; the top ridge is 4.582051 m. Its two
slopes meet in one closed material solid. Invalid pitch/thickness, an overhang that
lowers the eave below the origin plane, unknown fields and translated coordinate
overflow are executable rejection fixtures. Native Move and Undo demonstrate that
the saved assembly remains ordinary editable geometry.
