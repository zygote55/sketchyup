# Native section controls, drawing and selection

R064.e, 2026-10-06. View → Section planes opens an undoable Sections panel.
Create/edit dialogs name a plane, choose its editing context, enter a local normal
and a unit-aware distance along that normal, and control fill/color/cut edges.
The normal points into the retained half-space. Edited directions normalize;
untouched coordinates and colors retain their original precision. Changing context
keeps the displayed local values and deactivates the old context. Creating and
activating together publishes one shared command batch. Activate, context off,
reverse and delete use the same commands as automation. Stale drafts reject inline;
cancel and equal submissions preserve model history. Missing-context records remain
inspectable and locally editable.

Drawing applies the model-to-body ancestor cuts in world coordinates. Derived
triangles/caps/edges are cached with source geometry, placement, active records and
presentation policy. Native records, topology identities and triangulation remain
unchanged. Original-triangle barycentric weights interpolate both physical sides'
UVs and smooth lighting after clipping, including reflected/sheared contexts.
Generated caps use their plane's fill color and respect body opacity/X-ray mode;
wireframe omits cap fill. Cut edges use model edge color and global edge visibility.
Profiles trim their original strokes to the retained region. Open or overlapping
boundaries retain cut edges and report unavailable fill. A bounded-kernel failure
reports an unavailable section view and omits that body's geometry instead of
silently drawing an uncut substitute.

GPU selection draws the same clipped native triangles. A generated cap selects
its owning editing context, never an invented face. Native face tools treat filled
caps as occluders and return no editable face there. CPU edge picking trims native
edges and also respects cap occlusion. Turning fill off exposes retained interior
faces/edges. Selection face/edge overlays and window-selection containment use
derived retained geometry; context bounding boxes remain editing extents, trimmed
against their own ancestor cuts. Guides and inference point/occlusion policies
use the same half-spaces. Filled caps block native inference through the interior;
they do not create fake topology references. Inference waits for the current
derived frame when section state changes.

Saved scenes capture and recall named activation through decision 0075. The legacy
free clipping plane remains a temporary view modifier, applied after named cuts;
its established behavior does not create cap fill. This layer does not introduce
destructive geometry cuts. Export integration is the next R064 layer and must use
the same derived geometry/provenance rules before R064 delivery is accepted.
