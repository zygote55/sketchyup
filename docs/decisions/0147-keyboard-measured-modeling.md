# Precise modeling through native keyboard controls

R084.d, 2026-10-07. A selected editable face can start push/pull from a typed
Measurements distance while the tool is Ready. The viewport initializes the same
validated extrusion session used by pointer input, including world/local distance
conversion and component edit scope. Missing or uneditable selections reject.
Active/committed sessions retain their existing revision and amendment rules.
This does not silently restart a stale committed operation.

Accepting a command-palette result reactivates the parent window before invoking
the chosen action. A face-selection result can therefore restore viewport keyboard
focus after the modal dialog closes, including the supported Qt X11 fallback.
The activation follows an explicit user selection and changes no compositor settings.

The native fixture begins with viewport focus, then uses keyboard events exclusively:
R selects Rectangle; F6 traverses to Measurements; typed coordinates and unit-suffixed
dimensions create a 2×3 m face; Ctrl+K and a face query select it; P and a typed 4 m
distance create a 2×3×4 m solid; Ctrl+Z/Ctrl+Shift+Z undo/redo the exact geometry.
It measures area, bounds and volume independently and verifies the extrusion adds
one history entry. Record comparison excludes only monotonic geometry allocation
floors. No pointer event or direct focus assignment performs an operation after setup.

This representative workflow supplements existing pointer/units/session regressions.
It does not establish all-dialog or Outliner accessibility, assistive-technology
operation, text scaling, contrast, shortcut editing or complete R084/M9 acceptance.
