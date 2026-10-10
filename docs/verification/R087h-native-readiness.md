# R087.h — Publish complete launch proof and await keyboard focus

The detached Blender-launch fixture previously created its completion path before
writing the requested file name. The waiting parent could observe the path while
it was still empty. The fake child now publishes the complete proof through
QSaveFile's atomic commit. This fixes a fixture race consistent with the retained
PR196 launch-proof failure; it does not prove that race was its only cause.

The keyboard-only shortcut fixture now waits for the shortcut dialog to become
active and own keyboard focus before beginning its original Tab sequence. A
visible dialog can precede the compositor's focus transfer from the command
palette. Navigation counts, assertions and native harness deadlines are retained;
the fixture does not force focus or change application behavior.

Frozen source `dd70eadebe1d429f5d0642b0d615e6ad7cadc195` passes the Blender job suite and eight
[normal native cases](R087h-normal-native.txt): render and shortcut input on X11
and Wayland at scales 1 and 2. It also passes the Blender job suite and four
[ASan/UBSan Wayland cases](R087h-sanitized-native.txt) at both scales with leak
detection enabled. The exact previously verified CI Wayland client is hashed and
its dynamic linkage asserted. [Inputs and byte hashes](R087h-native-readiness.json)
bind the checks to their frozen source. Temporary compiler-cache source overlays
are restored after validation.

Only the two test fixtures change. Existing application runtime sources, user
settings and provider selection are unchanged. These local results do not stand
in for exact-head remote CI, ordered merges, clean release packages or the
remaining R087/M9 release gates. Original failed CI logs remain retained.
