# R038.b: native recovery workflow

Date: 2026-10-04. Local implementation-agent validation passed; CI pending.
Requires [PR #50](https://github.com/zygote55/sketchyup/pull/50).

The [native decision](../decisions/0016-native-recovery.md) records scheduling,
acknowledgements, errors, selection and close/save behavior. Automatic recovery is
now enabled for ordinary app launches, using the R038.a store.

`recovery_controller_tests` checks background completion after newer edits,
active-session ownership, adopted-copy replacement ordering, storage obstruction
and manual retry, bounded preferences, a real five-second automatic interval,
document replacement and cleanup after returning to saved state. Native recovery
tests cover preferences, recovered/saved selection, unreadable candidates, missing
resources, source-safe first Save, exact captured-revision status, close Cancel,
persistent save failure with independent recovery protection, Retry Save and
explicit discard close. Existing low-level recovery fault tests remain in CTest.

The native recovery suite passes on X11 and isolated Weston at DPR 1 and 2.
The [recovery dialog](R038-recovery-dialog.png) was captured and visually inspected;
its details area scrolls to keep controls reachable with many missing resources.
Existing X11 Formline import, Save/Open dialog and responsive shell regressions
also pass. The final development run passes 40/40, including the new
headless recovery coverage. Core sanitizer code is unchanged from
R038.a's 28/28 run.

The CLI suite checks listing, exact native recovery output, missing resources,
source and recovery-evidence preservation, direct/symbolic-link output rejection,
exclusive input options, active-session rejection and corrupt-candidate reporting.
These are synthetic implementation-agent checks, not independent human acceptance.
