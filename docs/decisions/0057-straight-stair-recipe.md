# 0057: Editable straight stair recipe

Status: implemented for the R060.c stair subset.

`assembly.stairs` creates a solid straight flight inside an ordinary editable group.
It ascends along local +X, spans local Y=0…width and starts at local Z=0. `origin`
places this frame in world space without baking the translation into its vertices.
The flight has a flat bottom at Z=0 and a closed, filled stepped cross-section.

| Parameter | Default | Allowed values / meaning |
| --- | --- | --- |
| stepCount | 12 | Integer 2–32; counts both risers and treads |
| width | 1 m | 0.4–5 m across the flight |
| totalRise | 2.4 m | 0.2–10 m from the base to the final tread |
| going | 0.28 m | 0.1–1 m horizontal depth of each tread |
| origin | [0, 0, 0] m | Explicit world translation within model coordinate bounds |

Every riser has height `totalRise / stepCount`, which must be 0.05–0.5 m. Every
tread has depth `going`. Derived-rise comparisons allow only double-precision
roundoff at the inclusive bounds. Tread i, counted from zero, spans X=i×going…(i+1)×going
at Z=(i+1)×rise. Thus the first tread is one rise above the base, and the last tread
ends at X=stepCount×going at Z=totalRise. There is no extra inferred top landing.
These are geometric bounds, not a claim of building-code compliance.

## Construction and verification

The recipe constructs a stepped planar profile with 2×stepCount+2 corners using
`geometry.face`, then extrudes it by the requested width using
`geometry.extrude_isolated`. Ordinary grouping, placement, material and property
commands finish the flight. No external mesh or opaque generated object is stored.
The bounded receipt exposes the actual IDs and executable expanded commands.

For N steps, width W, height H and tread depth G:

- Total run = N×G; equal rise = H/N.
- Material volume = W×G×H×(N+1)/2, the sum of the N filled step prisms.
- Surface area = G×H×(N+1) + 2W×(N×G+H).
- Unique edge length = 4×(N×G+H) + (2N+2)×W.

The final expansion verifies volume, area, dimensions and minimum/maximum bounds
with `assert.measurement`. Bounds use 1e-6 m tolerance; area/volume tolerances use
eight times kernel linear tolerance times edge length/surface area, with a 1e-7
floor. Measured values and explicit tolerances are retained in receipts with
`evaluation: "recipe_completion"`. Independent tests inspect each actual tread
and riser, closed-solid orientation, material volume and envelope.

The root stores original authored parameters and member identity as metadata,
not persistent constraints. Ordinary later Move, Scale and geometry edits remain
available. This recipe has no regeneration or inferred host/landing relationship.
Rails, stringers, hollow flights and winders require separate modeling commands.

The outer batch publishes one Undo item. Parameter errors, fractional step counts,
invalid derived rise or coordinate overflow reject the complete batch, including
preceding edits. Existing room, roof, window, host and material records are preserved.

## Executable study

Prompt: “Add a straight solid flight beside the room at [7, 0, 0] m, one metre wide,
with 12 equal rises reaching 2.4 m and twelve 280 mm deep treads.”

The [versioned recipe](../../examples/stair-recipe-v1.json) starts empty, creates
the room, roof and stair flight in one transaction, measures staged and committed
stairs through returned IDs, and saves the editable result. The default flight has
200 mm rises, 3.36 m total run and 4.368 m³ material volume.

The [starting scene](../../examples/m6-stairs-before.sketchyup) contains an adopted
room and roof. The [editable result](../../examples/m6-stairs-after.sketchyup) adds
only the stair task. Native Move/Undo and exact save/reopen check normal editability.
Executable rejection cases include invalid/fractional count, zero going, derived
rise outside its bounds, unknown fields and translated coordinate overflow.
