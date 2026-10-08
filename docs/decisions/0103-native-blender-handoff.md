# Native one-way Blender handoff

R071.b, 2026-10-06. Each retained result offers Open scene in Blender and a visible
one-way notice: external edits do not update the native model; a later import is a
separate, potentially lossy operation. The save dialog chooses a .blend destination
and explicitly offers Save and open. Pending render work/device checks finish first.

The action reopens that result's immutable capture and worker options. A modeless
progress dialog exposes cancellation while the ordinary modeling window remains
usable. Canceling or failing scene creation publishes no destination file. The image
result and native document/history are unaffected.

A verified scene is saved using QSaveFile; its modern header, bound, hash and size
are rechecked before publication. Destinations require a .blend suffix and cannot be
symlinks, preventing accidental native-file replacement. Only a successful atomic
save permits an explicit detached Blender launch. Arguments are passed directly,
with --disable-autoexec and the absolute chosen scene path. Launch failure retains
the saved scene and explains how to open it manually.

The external editing process is intentionally independent of SketchyUp. No watcher,
automatic import or native-document overwrite is installed. The embedded transfer
manifest and packed assets travel with the saved file. Core tests check atomic byte
publication and invalid destinations; native tests observe a disposable launch after
publication, simulate an external edit and confirm unchanged model/history.
