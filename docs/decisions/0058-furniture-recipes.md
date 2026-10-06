# 0058: Editable table and cabinet recipes

Status: implemented for the R060.d furniture subset.

`assembly.table` and `assembly.cabinet` create ordinary editable assembly groups.
Each material member is a closed native rectangular solid. Repeated members use
actual component definitions and instances; they are not disconnected mesh copies.
Both commands preserve existing scene records, resources and hosted relationships,
expand through ordinary commands, and publish as one outer task/Undo item.

## Table convention

| Parameter | Default | Allowed values |
| --- | --- | --- |
| width, depth | 1.2 m, 0.8 m | 0.3–5 m |
| height | 0.75 m | 0.3–3 m |
| topThickness | 0.04 m | 0.01–0.2 m |
| legSize | 0.05 m | 0.02–0.3 m; square leg section |
| legInset | 0.06 m | 0–0.5 m; outer footprint edge to the nearest leg face |
| origin | [0, 0, 0] m | Explicit world translation within model bounds |

The top occupies the full width/depth at Z=height−topThickness…height. Four shared
leg instances extend from Z=0 to the top's underside. Their minimum X coordinates
are legInset and width−legInset−legSize; minimum Y coordinates follow the same
formula using depth. Clear spans between opposing legs and leg height must be at
least 0.05 m, within kernel linear tolerance. This prevents overlap and inverted
members. The group envelope is exactly width × depth × height.

The default top volume is 0.0384 m³ and each leg is 0.001775 m³. The sum of the five
separately validated member volumes is 0.0455 m³.

## Cabinet convention

| Parameter | Default | Allowed values |
| --- | --- | --- |
| width | 0.9 m | 0.3–5 m |
| depth | 0.4 m | 0.2–3 m |
| height | 1.2 m | 0.3–5 m |
| panelThickness | 0.018 m | 0.01–0.1 m; applies to every panel |
| shelves | 2 | Integer 0–6; excludes the top and bottom |
| origin | [0, 0, 0] m | Explicit world translation within model bounds |

The cabinet is open at local Y=0, with its back at Y=depth. Both side panels span
full depth/height and share one definition. The top, bottom and interior shelves
share a second definition. These horizontal panels fit between the sides and end
at the back panel's inner face. The back spans full height between the sides.
Adjacent members touch at their boundaries; their interiors do not overlap.

For thickness T and K interior shelves, every clear vertical opening is
`(height − (K+2)×T)/(K+1)`. Shelf i (1…K) starts at Z=i×(clearOpening+T). The top
starts at height−T and the bottom at zero. Interior width, depth and each clear
opening must be at least 0.05 m, within kernel tolerance. There are no inferred
doors, hardware or hidden toe kicks.

The default cabinet has two side panels, four horizontal panels and one back panel.
Its clear openings are 376 mm high, internal width 864 mm and internal depth 382 mm.
The sum of separately validated member material volumes is 0.059705856 m³.

## Construction, assertions and later edits

Ordinary face/extrusion, material, grouping, component creation/placement, transform
and property commands create the members. A final batch checks each member's local
dimensions and closed-solid volume, plus the assembly's local dimensions and bounds.
There are at most 11 members and 25 assertions, within shared command and measurement
budgets. Receipts include actual assembly/member/material/definition IDs, role names,
authored local positions/dimensions and measured volumes. Completion assertions
are explicitly labeled `evaluation: "recipe_completion"`.

Member bounds use 1e-6 m tolerance. Volume tolerance is eight times kernel linear
tolerance times member surface area, with a 1e-7 floor. The root's authored parameters
record creation intent; they are not persistent constraints or regeneration rules.
Independent fixtures verify actual member positions, dimensions, outward orientation,
non-overlap, shared bindings and preservation of the starting scene.

`memberVolumeSum` means the sum of separately validated, non-overlapping solid
members. Whole-assembly `measure.entity` retains its conservative multiple-record
classification and does not claim the furniture is a single material solid.
Normal shared component edits and make-unique instance edits remain available.
Existing definitions are never reused or changed implicitly by recipe creation.

Invalid fields, fractional shelf count, impossible clearances or translated
coordinate overflow reject the entire batch, including any preceding commands.

## Executable studies

Table prompt: “Add a 1.2 × 0.8 m table, 750 mm tall, with a 40 mm top and four
50 mm square shared legs, inset 60 mm, at [0, −2, 0] m.”

Cabinet prompt: “Add an open 0.9 × 0.4 × 1.2 m cabinet with 18 mm panels and two
equally spaced shelves at [0, −2, 0] m. Keep repeated panels as shared components.”

The [table](../../examples/table-recipe-v1.json) and
[cabinet](../../examples/cabinet-recipe-v1.json) versioned recipes create the default
room, roof, stairs and requested furniture, inspect staged/committed geometry using
returned IDs, and save the result in one transaction. The
[starting scene](../../examples/m6-furniture-before.sketchyup) instead contains an
already adopted room, roof and stairs. Native
[table](../../examples/m6-table-after.sketchyup) and
[cabinet](../../examples/m6-cabinet-after.sketchyup) results add only their furniture
task to that baseline. Tests cover both size extremes, zero/six shelves, invalid
clearances, one Undo, exact reopen and instance-only edits that preserve siblings.
