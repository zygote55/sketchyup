# Camera motion and bounded scene timelines

R072.a, 2026-10-06. Shared core camera math separates fixed-eye look-around from
orbiting. A look change preserves eye position and distance while changing heading
and pitch; horizontal walking translates eye and target in the camera's yaw frame,
with an explicit vertical step. Neither operation edits model geometry/history.
Inputs are finite and bounded, with existing SceneCamera validation after movement.

Scene interpolation uses the shortest yaw arc, linear target/pitch/field of view and
logarithmic distance, eased with the existing cubic in/out policy. Both endpoints
are exact saved camera values. Different projection modes cut at the destination
keyframe rather than inventing a perspective/orthographic interpolation.

A camera timeline accepts two to thirty-two saved poses, 1–60 frames per second,
bounded transition/hold steps and at most 240 captured samples. It includes the
first pose, every transition endpoint and requested hold steps. Frame indices are
stable; each frame carries the saved-scene index supplying its non-camera state.
Visibility remains at the outgoing scene during interpolation and switches exactly
at the destination keyframe. Sample time is frame index divided by frame rate;
encoded sequences display each sample for one frame interval.

Core tests check fixed-eye motion, horizontal/right/vertical displacement, shortest
yaw, logarithmic distance, exact endpoints, projection cuts, frame counts, visibility
indices and invalid/budget-exceeding requests. Native walk controls, configurable
transition timing and immutable frame export follow in later R072 layers.
