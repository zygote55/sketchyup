# R059.g — Native hosted-component authoring and placement

Date: 2026-10-06 UTC. Depends on R059.f in the review stack.
[Hosted-component contract](../decisions/0054-hosted-components.md).

The native Edit menu now authors a component's canonical glue face, anchor,
alignment direction and explicit opening-cutting behavior. The dialog identifies
shared placement scope and rejects invalid values without publishing an edit.
Placement options expose rotation in degrees, signed XYZ scale and host-normal
inset. Opening the options cancels a pending preview; the instructions explicitly
restart placement afterward.

Select the whole component and Ctrl-click its host face, then use Shift+H. Pointer
motion previews the component and host cut; click or Enter commits, and Escape
cancels. Measurements accepts host-local coordinates or one inset and revises the
same Undo item. Bind retains the current pose with an explicit inset. Detach
restores the opening, Bake retains geometry and releases relationships, and clearing
shared glue releases affected attachments. These actions use the same authoritative
commands and editor-lock checks as the scripted path.

The native fixture verifies:

- Canonical glue authoring on a ring face, including a glue anchor in its central
  hole; an off-plane anchor and singular placement scale remain in their dialogs
  with an error and no model edit.
- Actual Ctrl-click addition of a host face, Shift+H, a private cut preview,
  pointer/click placement and Escape cancellation. Releasing the shortcut's Shift
  key no longer substitutes an unrelated drawing inference cursor for the host ray.
- Actual Measurements Return, exact anchor and inset amendment, one history item,
  rejection of off-plane input and prevention of committing an invalid preview.
- Exact container save/reopen and one Undo restoring geometry, topology, transforms,
  appearance and component/host records. Revision and allocation counters remain
  monotonic rather than reusing retired identities.
- Bind without motion, detach restoring the full 100 m³ host, bake preserving its
  96 m³ cut result, and shared glue removal/Undo.
- A reflected component with nonuniform scale and 90-degree rotation leaves 98 m³
  of host material. Rehosting by native pointer ray onto a reflected, nonuniform
  affine host preserves independent component dimensions and exact host-local
  amendment. Independent solid analysis verifies both host volumes; one Undo
  restores both openings and the original attachment.
- Intervening model edits reject stale previews, and a local editor lock on the
  former host rejects detachment without changing the document.

The complete final fixture passes X11 and Wayland at DPR 1 and 2. Wayland DPR 2
also passes ASan, UBSan and leak detection. Existing native component, inference
constraint, tool-lifecycle and responsive-shell suites pass on X11. The desktop
CTest suite passes. The application and all these targets build without warnings.
CI includes the four native variants and the Wayland sanitizer fixture.

The [native OpenGL framebuffer capture](images/R059g-hosted-placement.png) shows
the reflected ring component, through-opening and reveals. SHA-256:
`aa5d7f5f47a4b206a5ad4ac675039af2f1ede3ade1a6ba244b39cd52b2444e92`.

Copy/array attachment policy, legacy recipe adoption and the M6 acceptance gate
remain separate work. This layer does not close R059 or M6.
