# Immutable saved-scene animation capture

R072.c, 2026-10-07. An animation freezes a history-free document snapshot and an
explicit ordered list of 2–32 saved camera scenes. Timing is 1–60 frames/second,
1–600 transition steps and 0–600 hold steps; the complete plan cannot exceed 240
frames. The first pose and every endpoint are exact. Intermediate cameras use the
shared shortest-yaw, eased interpolation. Projection changes cut at the keyframe.

Each frame applies its outgoing saved scene to a private document copy. At the next
keyframe, visibility, named sections, model style and solar settings switch to that
scene. Properties omitted by a scene use the frozen source document's values.
Temporary body/face hiding is included; edges/guides do not contribute surfaces.
Camera conversion matches native projection conventions. The live document is never
recalled or edited and later edits/deletions do not change captured frames.

Derived view edits may increment the private copy's revision. Export provenance
therefore stores the original document identity/revision separately and records the
saved `sceneView` identifier in the GLB manifest. GLB extras and manifest identify
that original capture, not the private working revision. These additions leave
ordinary render capture metadata unchanged.

Missing scene references, temporary free clipping planes and show-hidden editor
overlays are rejected explicitly. Existing render snapshot and GLB resource bounds
apply. A frame needs visible surface geometry for the Blender export adapter.
Individual frame preparation may run on a worker with only the frozen value.
