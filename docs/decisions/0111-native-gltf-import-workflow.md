# Native and CLI glTF import workflow

R074.c, 2026-10-07. File → Import GLB/glTF accepts `.glb` and `.gltf` through the
[bounded package reader](0109-bounded-gltf-packages.md) and
[native conversion contract](0110-native-gltf-conversion.md). Conversion completes
privately before any unsaved-document replacement prompt. Malformed or unsupported
input leaves the current document unchanged. Canceling replacement also preserves
its document bytes and history.

A successful import replaces the workspace with an unsaved native model, clears its
save path, resets recovery and selection, and fits the viewport. A native report
shows triangle/instance/camera counts, metre and axis conversion, preserved features,
and human-readable notices for every recorded loss. Imported geometry is editable
through ordinary component tools; edits undo normally. First Save chooses a native
destination. External changes do not update the model.

`sketchyup-cli --import-gltf FILE [--output NEW.sketchyup]` is a standalone operation.
Without output it prints a structured version-1 conversion report. With output,
it verifies a complete native container before atomic no-replacement publication.
Existing files, source paths and symlinks reject, including a competing writer at
publication. The parent directory must exist. Data and directory synchronization
follow the native migration policy; uncertain post-publication durability is
reported without deleting the complete copy. This shares the migration publisher,
not ordinary save/backup replacement behavior.

Images are decoded sequentially with the existing 4,096-pixel/64-MiB per-image bound
and an additional 256-MiB aggregate decoded-image budget, preventing many small
compressed inputs from requesting unbounded total decoding work. Managed payloads
remain subject to the 16-MiB individual and 64-MiB aggregate byte limits.

Exchange remains intentionally asymmetric. GLB export uses the existing immutable
render snapshot, hierarchy, mesh sharing, camera, sided appearance, embedded image
and hash-bearing manifest contracts. It publishes `scene.glb`, optional environment
bytes and `manifest.json` in a new directory; it does not export a sidecar-based
`.gltf` variant. Export permits up to 256 MiB whereas import capture permits 128 MiB;
therefore very large native exports may exceed this bounded importer. Neither path
claims native history, topology, annotations, analytical records, arbitrary PBR or
animation round-trip fidelity. Import never trusts private native extras as editable
records; its explicit loss report applies even to SketchyUp-produced GLB.

CLI tests check new publication, source/destination/symlink preservation and exclusive
mode validation. Native tests check corrupt/canceled replacement, readable notices,
new-file save/reopen, source byte preservation and component editing/undo on Wayland
and X11 at both display scales. Actual Blender interoperability and sanitizer checks
remain the conversion acceptance gates.
