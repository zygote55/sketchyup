# R082.bb — Responsive retained tooltip file progress

2026-10-08. The public progress candidate at `626479ba904bfdc542163ec01fe82c3c2e382ebf`
passes the original six AMD 100 MB file cases: all 18 maximum heartbeat gaps
are below 50 ms, and every operation is below five seconds. The publication
contains exactly the same 478 runtime and 301 test input bytes, with additional
qualification documents. The full release and final integrated-source gates remain open.

The retained `QDialog` now uses a `Qt::ToolTip` surface with the existing
nonactivation request flags. It remains application modal and noncancellable.
The 150 ms delayed show, immutable worker, 10 ms completion polling, edit fences,
stale-save behavior, shutdown join and synchronous hide/reset are unchanged.
The original 5 ms heartbeat, trailing callback, first-frame, canonical-byte,
history, revision and recovery predicates are unchanged.

Separate lifecycle tests wait for actual exposure, then enforce application
modality, Escape/close rejection and hidden/inactive completion. They report Qt
and native focus separately. Compositors may focus this surface despite the
request flags; this change makes no universal focus-prevention claim. The strict
nonfocus assertions in earlier AR2/AR3/AS4 attempts were an added hypothesis,
not a roadmap responsiveness requirement. Those failures remain retained.

## Verification

- Three normal I/O/snapshot regressions and four Wayland software native controls
  (warm and verified cold, scales 1 and 2) pass. Rebuilding the b3 retained-dialog
  parent fails the new tooltip-type assertion.
- The same three I/O regressions and four native controls pass ASan/UBSan with
  leak detection enabled, the original 60 s native deadline and qualified
  Wayland client library. All tracked cache inputs are restored afterward.
- Six physical AMD RX 6800 XT/X11 cases use the original 100,671,415-byte fixture,
  three warm and three verified-cold runs. Cold source residency is zero before
  opening. Every source hash, file/undo/recovery/edit-fence oracle and worker
  resource bound is checked. No extra swap or private phase instrumentation.

| Case | Cache policy | Worst heartbeat gap (ms) | Longest operation (ms) |
| --- | --- | ---: | ---: |
| warm-1 | warm | 20.660 | 347.216 |
| verified-cold-1 | verified-cold | 22.761 | 601.940 |
| warm-2 | warm | 21.618 | 346.168 |
| verified-cold-2 | verified-cold | 33.006 | 338.548 |
| warm-3 | warm | 30.670 | 353.291 |
| verified-cold-3 | verified-cold | 19.709 | 351.399 |

The worst heartbeat gap is 33.005558 ms. The longest operation is 601.939976 ms;
its complete operation/first-frame accounting is 601.940036 ms. Open samples
include fresh framebuffer readback; save samples retain the original accounting.

The [bound results](R082bb-public-tooltip-progress-result.json) identify the actual
compiled source, binaries, fixture, controllers, report hashes and per-operation
measurements. [Normal regressions](R082bb-normal-regressions.txt) retain all three
results. Earlier [retained-dialog results](R082ak-public-progress.md), including
17/18 heartbeat failures, are unchanged. Minimal private Qt probes and partially
passing AS4 rows are diagnostic history and are not counted as acceptance.

The broad R082 gate still requires the current public capacity profile, complete
Intel/AMD viewport/inference/history/memory campaigns and final source integration.
This record closes the measured AMD file-responsiveness predicate for this source;
it does not mark R082, M9 or the release accepted.
