# R059.h — Attached copies and arrays

Date: 2026-10-06 UTC. Depends on R059.g in the review stack.
[Hosted-component contract](../decisions/0054-hosted-components.md).

Whole-instance copies retain an explicit attachment to their original host and
create independently validated openings. A host copied with every attached instance,
including through an enclosing group, becomes an independent assembly. Host-only
copies retain baked geometry; a host with only some of its attachments rejects.
Unbound component placement remains unbound. Linear and radial arrays use the same
rules and existing aggregate limits in a private, atomic candidate.

Numeric re-entry allows new copy identities and explicit array count changes while
preserving the original host/face/inset and component-definition scope. Existing
unrelated records remain protected. Native preview and commit also check editor
locks on hosts and attached roots affected indirectly by the transform.

Core and shared-command fixtures verify:

- Attached copies preserve shared definitions, explicit inset, original reveals and
  one Undo item. Moving/detaching a copy changes only its opening. Reflection works.
- A host-only copy remains baked after the original window is detached. Incomplete
  assemblies reject. Complete reflected, nonuniformly transformed group copies
  remap host/root relationships and retain current reveal paint and identities.
- Linear count/spacing amendments use fresh identities, retire previous copies,
  preserve the original opening and remain one Undo step. Radial arrays produce
  independently measured material volumes. A late invalid copy rolls back all work.
- Fixed-count amendment rejects count changes; the explicit array policy accepts
  positive count changes. Attempts to substitute a different component definition,
  attach replacement copies to unrelated hosts, detach unrelated records, or discard
  every copied binding reject without consuming IDs, revision or history.
- Alignment-only copies add relationship metadata without changing host geometry;
  exceeding the aggregate attachment budget rejects atomically.
- Shared-command preview remains private, its changes agree with commit, numeric
  amendment retains one Undo, and native container save/reopen is exact.

The native fixture exercises actual Ctrl-copy and Measurements Return, mouse-move
preview events, `xN`, `/N`, exact spacing, copied-root selection, one Undo, persistence,
and both preview/commit rejection for indirect editor locks. Wayland uses the same
widget mouse-event delivery as the existing drawing and transform fixtures because
its test platform does not support a global cursor warp.

The full development build and **93/93 CTest suites** pass in **61.76 seconds**,
including the real Blender worker. The seven targeted core/IO/assistant sanitizer suites pass
with ASan, UBSan and leak detection (90.24 seconds). The native fixture also passes
Wayland DPR 2 with all three checks. The complete final fixture passes X11 and Wayland at DPR 1 and 2. Existing native
array and component suites pass on X11, and hosted placement passes on Wayland DPR 2.
CI includes all four new native variants and the Wayland sanitizer fixture.

The [native OpenGL framebuffer capture](images/R059h-hosted-array.png) shows the
original ring and three selected attached copies, with four independent through-cuts.
SHA-256: `bf6e3315ab8eabd956a337e406abd4977e9ba00cf8555287ae060729da23e2fd`.

Explicit recipe adoption and the M6 acceptance gate remain separate work. This
layer does not close R059 or M6.
