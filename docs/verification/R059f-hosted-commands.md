# R059.f — Shared hosted-component commands

Date: 2026-10-06 UTC. Depends on R059.e in the review stack. CI/dependency
merges remain pending. [Contract](../decisions/0054-hosted-components.md).

The shared catalog now exposes `component.glue`, `component.attach`,
`component.bind`, `component.detach`, and `component.bake_host`. Explicit canonical
member/face references define glue and cutting behavior. Placement uses host-local
anchor/tangent/inset, radians and signed scale; current-pose binding requires an
explicit inset. Metadata-only attachment changes commit normally. Hosted commands
inside canonical component-edit drafts reject rather than losing scene bindings.

Bounded inspection publishes canonical member bindings, canonical glue and current
host/face references, relative frames and opening identities. Receipts reflect final
placement after later commands in the same batch. Paged staging includes changed
host and attachment records and excludes structurally unchanged frozen neighbors.
The three command-bearing transport schemas are regenerated from the live catalog.

Native assistant policy advertises attach/bind/detach routinely, glue with shared
edit permission, and baking with destructive permission. Session locks protect
metadata-only attachments and glue edits during preview and publication, including
a new lock acquired after a preview is sealed. Provider instructions explain
explicit glue discovery, placement coordinates and automatic host regeneration.

The complete development build passes **92/92 CTest suites** in **51.63 seconds**,
including the actual Blender worker.

Validation:

- `hosted_command_tests` covers private preview and exact receipts, an attach/move
  compound batch, surviving reveal IDs, independent closed material volume,
  detach/bake/clear-glue with Undo, and exact native persistence. Bounded inspection
  supplies the actual canonical and typed scene references used by the commands.
- Alignment-only binding changes no bodies but creates one Undo item. Staging shows
  its metadata changes and excludes an unchanged neighbor sharing the same host.
  Missing/unknown fields, malformed stable IDs and glue records, stale revisions,
  locked hosts/placements, unsupported canonical scope, and a late off-plane move
  all reject without changing bytes or history.
- A simulated assistant drives the real native transaction session through begin,
  apply, preview and durable commit for metadata-only binding. Repeated Apply adds
  no duplicate history; one Undo removes the attachment. Native editor locks block
  a bind, a glue-only change and a commit after preview, without publication.
- The all-published-command suite executes all five new catalog entries, checks
  required/unknown fields and verifies ordinary Undo plus attachment restoration.
- ASan/UBSan with leak detection passes hosted commands, the complete command
  catalog suite and native assistant sessions. Final receipt and structural-diff
  regressions pass the rebuilt hosted-command and staging sanitizer suites.
- Native assistant panel tests pass on X11 DPR 1 and isolated Weston Wayland DPR 2,
  including advertised attachment permissions, preview/direct publication, Undo,
  consent, clarification, staleness, layouts and reconciliation. No credentials or
  live provider are used by these fixtures.
- A fresh temporary install matches all three published transport schemas. Its
  installed hosted-component example creates and moves a cutting placement, saves
  and reopens identical native bytes, and measures 96 m³ of wall material. Detach
  restores 100 m³. The check removes its owned temporary installation and models.

Native placement controls, copy/array attachment policy and explicit adoption of
existing recipe relationships remain separate R059 work. M6 acceptance remains
pending; this slice does not claim a new live-provider or native placement gate.
