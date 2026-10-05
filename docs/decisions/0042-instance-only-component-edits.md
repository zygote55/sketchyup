# 0042 — Instance-only geometry edits

Status: implemented; live manual-target acceptance passes locally, CI pending.

The first live M5 manual-window trial correctly rejected the recipe because the
native drawing had no recipe metadata. The assistant could inspect vertices and
transform geometry, but canonical component members require an explicit definition
edit. Offering that command only behind shared-definition permission prevented an
ordinary selected-instance edit even when the user requested make-unique.

`component.edit_instance` accepts a placed `body` and a bounded `commands` array.
It makes that placement and its enclosing component ownership path unique, then
runs ordinary geometry/member edits through the existing scene-ID component scope.
The original shared definitions and sibling placements remain intact. The command
uses inspected scene body/entity IDs and the selected placement's world frame;
it does not require guessed canonical IDs, a definition ID, recipe metadata, or
permission to change shared definitions. All member edits should be batched into
one invocation: each invocation deliberately creates a fresh unique definition
(and ownership path), using the existing make-unique semantics.

The nested subset is geometry commands, entity position/dimensions/properties,
and material assignment/color. It excludes nested command wrappers, component
operations, document settings, material resource edits and other global changes.
Existing scope mapping rejects outside scene-body references. Geometry validation,
resource limits, locks, staged revision checks and recursive assistant command
authorization remain authoritative. Failure rolls back the unique definition and
all earlier commands; publication remains one transaction/Undo entry. The existing
shared `component.edit` command and its explicit permission are unchanged.

The command is discoverable through CLI, session, MCP and the native assistant's
routine allowlist. Command schemas now retain their human descriptions, and the
vertex transform schema explains column-major matrices and translation indices.
Assistant guidance distinguishes authored recipes from ordinary topology edits,
requires inspection, and forbids inventing recipe metadata to bypass validation.
Bounds scaling is not offered as proof of preserving frame thickness.

Deterministic scope tests cover flat and nested shared components, outside-target
and global-edit rejection, rollback, unchanged source definitions and siblings,
exact geometry, read-only preview, one-entry Undo/Redo and coherent persistence.
The command registry's executable-case suite and recursive assistant authorization
suite cover the new command as well. Live evidence will determine the M5 gate;
implementation alone does not establish natural-language modeling reliability.

Assistant-owned draft lifetime defaults to the remaining task time, capped at
300 seconds; an explicit shorter lifetime is retained. This does not extend the
task deadline. The first live use of ordinary scoped edits reached staged
verification but exceeded the dispatcher's unrelated 60-second default. A
confirmed `TRANSACTION_EXPIRED` releases the assistant's ownership marker because
the dispatcher has retired that draft, allowing a bounded fresh attempt. Tests
use an injected clock and the real dispatcher to verify expiry, replacement,
remaining-time limits and no implicit publication.
