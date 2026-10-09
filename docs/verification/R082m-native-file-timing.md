# R082.m — Native 100 MB file timing and verified OS-cache-cold reads

2026-10-07. Six actual Intel/Wayland runs pass the existing file-worker correctness
checks on the unchanged 100,671,415-byte R082.c fixture. Three fresh processes use a
warm copied input; three first synchronize and advise eviction of only their own
private copy, then use `mincore` to require all 24,578 file pages absent from the OS
page cache before opening. Storage-controller caches are not cleared or claimed cold.
[Reports, hashes, limits and regression logs](R082m-native-file-timing.json).

| Group | Open through first framebuffer readback | Current-state durable save | Largest open heartbeat gap |
| --- | ---: | ---: | ---: |
| Warm, three runs | 674.82–703.28 ms | 1058.71–1144.11 ms | 254.73–282.17 ms |
| Verified OS-cache-cold, three runs | 706.88–737.90 ms | 1072.16–1113.13 ms | 186.43–209.02 ms |

Saves that receive an intervening edit take 1031.57–1485.00 ms. They retain that
newer edit as dirty and preserve exact captured bytes, as before. All timings are
within the initial five-second open/save target on this machine. The initial fixture
read/hash/copy and cache preparation are outside the measured interval. Open includes
model replacement and UI synchronization; its separate first-frame figure also
includes explicit framebuffer readback. Later save timing excludes verification
reads and includes the normal UI action's snapshot, durable write and synchronization.

A precise five-millisecond timer records callback intervals and the final synchronous
tail, including first-frame readback for open. Its p95 is reported per operation;
these short traces are not statistical confidence intervals. Callbacks run throughout
both open and save, but the **282 ms maximum opening gap is still a UI-latency finding**.
Save gaps peak at 58 ms. This does not establish a uniformly nonblocking GUI or accept
all of R082. Recovery checkpoints and other import/export workflows are separate.

The fixture is storage/metadata-heavy: 1,000 square faces and four opaque local
16 MiB assets. It does not model an architectural scene or texture decoding. Its
canonical SHA-256 remains
`d74a68105014d648676afbfb37998fd3e08fd95a9148a36f0d8b29d03dd60f37`.
The GPU is Intel Arc integrated graphics, Mesa 26.2.2, kernel 7.2.5-3-omarchy,
Qt 6.11.2, native Wayland under Hyprland with effective widget scale 1.6. Reports
retain actual viewport dimensions. Displays are already awake and no desktop
configuration changes. Every worker has eight CPUs, 4 GiB RAM, zero additional swap
and private disk-backed temporary/config/data/cache directories; heavy local work is
serialized. This is local evidence, not acceptance for every reference SSD or GPU.

## Regression and CI investigation

Four headless Release checks pass (warm/cold at Wayland 1×/2×). Twelve further
ASan/UBSan checks pass with leak detection enabled and the **same private Wayland
1.26 client patch used by CI**: those four combinations and eight additional warm
2× repetitions. The helper builds that private library, runs its Meson suite and
both destroyed-proxy leak probes. Meson 1.12.1, Ninja 1.13.2 and Wayland 1.26.0 are
recorded. The library is not installed over the host or compositor libraries.

[PR238's CI run](https://github.com/zygote55/sketchyup/actions/runs/37679503237)
timed out at the 60-second file-worker case on Wayland 2×. The new stage logs cover
open, saves, replacement and close so another timeout can be located. The local
CI-client repetitions do not reproduce it; they do not prove its cause or waive
required CI. One failed-job retry was requested after these checks. The timeout
and full-release acceptance remain unresolved, and the deadline is unchanged.

The captured Release and sanitizer fixture source is `d7085e8`, using renderer
runtime `cd1950e`. The existing package/native sanitizer caches are overlaid only
with the recorded files and restored afterward. Local compilation uses two jobs
inside the same eight-CPU / 4 GiB bounds; Release linking uses two LTO workers per
link. The subsequently stacked shared-limit refactor changes no numeric values;
these measurements are not relabeled as a fresh build of that later merge.
