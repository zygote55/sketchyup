# Native materials separate shared assignments from document swatches

R036.d, 2026-10-04.

The Model panel's Materials tab lists document swatches, including an explicit
Original face color entry for material ID zero. Front, Back and Both sides are
independent assignment choices. Selecting a swatch changes the current brush;
it does not edit the document. Apply targets selected faces or a geometry
record's own faces, ignoring selected edges/guides. Empty groups and closed
components must be opened before their contents can be painted.

B activates Paint and opens Materials. A click assigns the current swatch to the
chosen side; Alt-click samples the visible physical side, including reflected
placements, without changing the model. Sampling ID zero selects the original
face color setting rather than inventing a swatch from a legacy RGB value.
Painting in an opened component uses the canonical shared-definition command
scope and one undo step. Swatch edits and resource replacement remain explicitly
document-wide, including when initiated inside a component edit context.

New material offers a bundled local library (White, Concrete, Terracotta, Steel
and translucent Glass) as editable starting swatches. Creating one gives it a
normal document-owned identity; the library introduces no external dependencies.
The material editor accepts a name, RGB color, opacity and a stored-resource
binding. It retains validation errors and rejects a stale document snapshot or
revision. Renaming preserves untouched floating-point color/opacity channels.
Used swatch deletion rejects through authoritative reference validation.

Attach file reads an explicit bounded local file and atomically imports/binds
its bytes. Replace file also resolves missing records with their existing IDs,
updating all material references. File choice cancellation is read-only; a
changed document rejects a delayed choice. Detaching retains the owned resource
for reuse or undo; Clean files removes only assets unused by any swatch, in one
undoable batch. Files and missing states are visible in the panel; image mapping
is not represented as available functionality.

The tab scrolls when its content exceeds the narrow tray. Buttons retain readable
heights, and the file chooser uses the platform's normal chooser in production.
Native tests use Qt's widget chooser and real filename-field input under isolated
X11/Weston, with native focus synchronization before modal input.
