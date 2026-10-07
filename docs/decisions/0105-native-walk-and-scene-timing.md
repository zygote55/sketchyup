# Native walking and scene transition timing

R072.b, 2026-10-07. Camera offers Look around and Walk. Look rotates around a
fixed eye; dragging or arrow keys changes heading/pitch. Scroll changes its lens.
Walk uses WASD/arrows for horizontal travel, Q/E or Page Down/Up for eye height,
Shift for triple speed, and scroll for speed. Camera motion is independent of
model geometry, dirty state and undo history. Escape returns to Select.

Held movement is bounded by elapsed time, normalized for diagonals, and stopped on
release, focus loss, window deactivation or tool change. Control/Alt/Meta shortcuts
remain available. Walk enters perspective. Mouse and trackpad scrolling use the
active navigation tool's speed/lens operation; Alt trackpad scroll still turns.
No collision detection or terrain following is implied.

Scenes exposes transition duration from zero to ten seconds and remembers the
preference. Zero duration and reduced motion recall immediately. Otherwise the
camera uses the shared shortest-yaw interpolation; projection changes cut at the
saved endpoint. Model/visibility state retains existing scene-recall semantics.

Native acceptance checks fixed eye, horizontal motion, eye height, key release,
focus loss, shortcut handling, unchanged document/history, immediate timing and
existing animated scene recall on Wayland and X11 at both display scales.
