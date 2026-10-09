# Reclaim completed normal-build artifacts before sanitizers

The previous full workflow for PR243 failed while the native sanitizer file test tried to save: No space left on device. Its original metadata, raw log hash and excerpt remain recorded. This backports the exact helper qualified in PR247, retaining five later fractional-scaling executables, both workers and CTest evidence at their existing paths before removing only the completed normal build.

The four bounded helper checks previously verified byte preservation and fail-before-mutation behavior for missing workers, a preexisting staging directory and a symlinked build. The helper is byte-exact here, and this branch's remaining normal consumers match its retained set. All other workflow steps, deadlines, application and test bytes remain unchanged. Full current-head CI is required; this evidence does not waive the original failure or assert release acceptance.

[Input identities and original failure](CI-PR243-artifact-backport.json).
