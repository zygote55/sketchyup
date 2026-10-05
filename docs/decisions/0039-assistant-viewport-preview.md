# ADR 0039: immutable assistant changes in the native viewport

The viewport may retain the exact immutable `Document::PreparedEdit` acquired
from a sealed native assistant transaction. It cannot publish that handle.
`Document::canApply` checks the source session and revision when the handle is
installed and on every rendered frame. Human edits or replacement clear the
preview before drawing another frame. The panel remains responsible for task
expiry, cancellation, native lock-policy changes and unknown-outcome transitions.

The live model remains the base scene. Added bodies/faces receive green diagonal
hatching, removed bodies/faces red hatching, and updated body geometry amber
outlines in its proposed position. This is a change overlay, not a replacement
of the live model or proof that Apply occurred. Hatching is evaluated in logical
screen pixels in the existing OpenGL shader. Depth testing, clipping and native
hidden-state policy remain active; opaque surfaces occlude preview geometry.
Picking continues to target the live model. The assistant panel must distinguish
its proposal entity list and measurements from live selection.

The CPU display buffers share a 600,000-vertex limit. Per-body face complexity is
checked before tessellation. Exceeding display limits clears the overlay and
reports a failure; it does not publish or invalidate the underlying durable task.
The GPU buffers follow the viewport's existing context creation and destruction
lifecycle. Changes in temporary visibility rebuild display geometry. No user
model, history, saved bytes or material record is modified for coloring.

A host-selected preview entity can show before/after world-bounds dimensions.
Those dimensions are measured from the live document and exact proposed snapshot,
then cached when the inspected entity changes; they are not computed from model
prose or repeatedly measured on every camera frame. Missing sides are shown as
absent. These world-axis dimensions are not automatically called an opening width,
member thickness or other semantic recipe measurement.

This layer covers geometry overlays. Non-geometric metadata, guide and material
changes need explicit details in the panel; a lack of colored geometry is never
proof that the proposal has no changes. The banner and Apply/Discard controls,
proposal inspection list and provider setup belong to the panel integration.
