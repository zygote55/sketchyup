# Native extension management and guarded actions

R080.d, 2026-10-07. Extensions → Manage extensions provides native installation,
explicit enable/disable, removal, command-capability inspection, action selection
and typed parameters. Installed packages start disabled. Standard Qt controls,
plain-text metadata and a scrollable parameter form support keyboard use and
bounded layouts. File installation copies a validated local package into the
registry; source packages remain unchanged. No package runs on installation,
enablement, registry loading or document opening.

Run starts the application's declarative worker on a QThread and keeps the dialog
responsive. Package controls are disabled during the attempt; Cancel or closing
the dialog interrupts and joins its worker. Closing cannot leave a callback
referencing destroyed dialog state. Worker errors return to the GUI thread and
persist the extension's disabled/error state. Explicit enable clears a recorded
failure; cancellation leaves the installed enabled state intact.

Before applying returned commands, the GUI reloads the registry and verifies the
package is still enabled with the same source bytes. It then compares the document
save stamp and revision captured before the attempt. A changed model rejects the
result while preserving intervening user edits. Valid commands prepare one private
public-API batch and publish it through the shared prepared-edit viewport path.
Geometry or resource validation failure discards the entire draft. Successful
actions get an extension-labelled undo item. Actions operate at model context;
users must close ordinary-group/component editing before starting an action.

The public `capabilities` response now includes extension support. Standalone
`--extension-capabilities` outputs the installed versioned catalog
`docs/api/extensions-v1.json`; mixed modes reject. The public interface remains
declarative command packages with typed scalar parameters, with the compatibility
and trust limits stated in ADRs 0134–0136. It does not claim compatibility with
existing Ruby or native-code extensions.

Native acceptance covers actual file installation, disabled defaults, command
inspection, explicit enable, parameters, the shipped panel sample's measured
geometry, one-step undo/redo, cancellation, an intervening model edit, invalid
geometry and removal. All eight normal/sanitized Wayland/X11 scale runs and
installed/helper/source-package checks are required before R080 acceptance.
