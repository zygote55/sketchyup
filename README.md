# SketchyUp

A planned native Linux 3D modeler with SketchUp-style editing, AI-assisted model
creation and modification, and optional Blender rendering. Arch Linux with
Omarchy is the primary desktop target.

## Project status

The native application described here is a proposal, not a released product.
An early local prototype uses Electron and Three.js under the name Formline;
that prototype is not included in this documentation change. Neither native
feature parity nor AI and Blender integration has been implemented by this PR.

## Build documentation

- [PR roadmap](docs/PR_ROADMAP.md): 108 sequenced implementation/research entries,
  dependencies, acceptance criteria, and coverage of every scope item.
- [UX design and mockups](docs/UX_DESIGN.md): native window layout, drawing
  interactions, assistant previews, rendering, and document safety.
- [Build plan](docs/BUILD_PLAN.md): product goals, architecture, milestones,
  dependencies, validation, packaging, risks, and release gates.
- [Scope and acceptance matrix](docs/SCOPE.md): the complete proposed feature
  inventory, target milestones, and observable completion criteria.
- [AI modeling contract and guide](docs/AI_MODELING.md): proposed tools,
  transaction semantics, model inspection, editing instructions, examples,
  provider integration, and evaluation requirements.

These documents use **SketchyUp** as the working name. Application licensing,
final branding, geometry dependencies, and compatibility commitments are
explicit decisions in the build plan.
