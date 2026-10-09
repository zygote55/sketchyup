# Native template and component library workflows

R079.e, 2026-10-07. File → New from template and the component library action open
a native searchable dialog with folder selection, refresh, thumbnails, plain-text
metadata and errors. Standard Qt fields, mnemonic labels and keyboard list activation
support selection without a pointer. Invalid entries remain inspectable with Create/
Insert disabled. Refresh remembers only the selected library folder in preferences.
The entry's SHA-256 is checked when selected, then its exact bytes are decoded again;
files changed since scanning require an explicit refresh.

Template opening constructs and validates a new document before the dirty-model
replacement prompt. Cancel preserves the existing document. Acceptance gives a fresh
unsaved identity, clears the file path and recovery context, and applies the default
saved scene's persistent settings before creating the history-free document. Its
camera/editor visibility is recalled immediately, without a transition from the old
model. Save therefore uses a native document destination rather than the library file.

Component selection accepts a world position in the destination's display units and
inserts into the current ordinary group or model context. Shared component editing
must be closed first. The native viewport calls the atomic insertion contract (ADR
0131), selects the new placement and reports reused resources/name adjustments.
Normal undo/redo remains available. Library files are never updated by model edits.

Save template/component opens metadata and default-view/definition choices, then a
new-destination file chooser. Names, labels, resources and bundle bounds are checked
before publication. The thumbnail is a 320×240 capture of the current model view,
explicitly described in the form; component payloads still contain only their selected
definition closure. Publication cannot overwrite any existing destination. Options or
file cancellation creates no output. Model identity, records, history and camera remain
unchanged by capture/save. Missing referenced embedded assets report an error.

Core tests cover default-scene initialization and changed catalog entries in addition
to resource, relocation and validation checks. Native tests exercise fresh template
identity/camera/history, dirty-model cancellation, keyboard search and insertion in
millimetres, one-step undo/redo, malformed entry rejection, save cancellation, actual
bundle publication and existing/source protection. Platform and sanitizer runs are
required before R079 native acceptance is recorded.
