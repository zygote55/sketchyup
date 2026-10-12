# M9 hardware session runbook

> **Status (2026-10-12): the session ran partially and the rest is waived.** On
> 2026-10-11/12 the preflight, the section 2 AMD campaigns and a partial Wayland
> hotplug run (section 3) were done, at revision `db68cd14`. The owner then decided
> to skip the remaining checks and test them in daily use, so sections 1 (Intel), 3
> (Xcb hotplug), 4, 5, 6, 7, 8, 9 and 10 are waived for the 1.0 gate. See
> [ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md). Results:
> [R082.ii](verification/R082ii-amd-hardware-session.md) (AMD, with the JSON summary) and
> [R085.r](verification/R085r-hotplug-owner-attested.md) (hotplug, owner-attested). The
> runbook below is kept unchanged as a procedure for future use, for example if the
> application gains other users.

This runbook covers the M9 checks that need the owner's hardware and the owner in
person: physical outputs, suspend, a real mouse and keyboard, input methods, a
screen reader, and the reference GPUs. It is a procedure, not a result. Each
section names the roadmap entry and evidence record its result feeds; the
[M9 acceptance procedure](M9_ACCEPTANCE.md) decides what counts at the release
candidate (RC).

Every helper script asks for a typed `yes` before it acts, refuses to run without
a terminal, and writes to a new evidence directory under
`~/sketchyup-verification/m9-hardware-session/` (override with
`SKETCHYUP_SESSION_ROOT`). Apps started by the scripts use private settings, data
(including the recovery journal), cache and temporary directories in that
evidence directory, so the owner's own SketchyUp settings are not touched.
Screenshots capture only the SketchyUp window.

| Machine | Role |
| --- | --- |
| Laptop: Omarchy/Arch, Hyprland 0.56.2 (Lua config), Intel Core Ultra 7 155H with Arc graphics (Meteor Lake) | Intel campaigns and every physical check. eDP-1 2880×1800 at scale 2, position 2400x0; DP-7 3840×2160 at 1.6, position 0x0 (configured in `~/.config/hypr/monitors.lua` as `desc:Sony SONY TV`). |
| `sketchyup-gpu`: CachyOS QEMU guest, XFCE/X11, AMD Navi 21 (Mesa reports RX 6800 XT) | AMD campaigns, driven over SSH as in [R085.g](verification/R085g-current-discrete-gpu.md). |

## Preflight (laptop, about 30 min attended; the build runs the day before)

**P0. Install what is missing (owner, before the session).** A read-only
`pacman -Q` on 2026-10-11 found `fcitx5`, `fcitx5-qt`, `fcitx5-gtk`,
`at-spi2-core`, `grim`, `jq`, `inotify-tools`, `wtype` and `xorg-xwayland`
installed. These are missing:

```sh
sudo pacman -S --needed orca speech-dispatcher espeak-ng   # section 9
sudo pacman -S --needed fcitx5-configtool fcitx5-mozc      # section 7 (or fcitx5-chinese-addons for Pinyin)
```

ibus is not installed and is not needed; fcitx5 is already the session's input method
framework (`QT_IM_MODULE=fcitx`, `XMODIFIERS=@im=fcitx`). On the AMD machine,
`glxinfo` (mesa-utils) and `xset` must exist; both were used by the earlier runs.

**P1. Name the source revision.** Use the RC if one is named, otherwise current
`main`, and record it:

```sh
cd ~/projects/sketchyup
git fetch -q origin
SESSION_REV=$(git rev-parse origin/main)   # or the RC commit
git worktree add --detach .local/wt-session "$SESSION_REV"
echo "$SESSION_REV" | tee ~/sketchyup-verification/m9-hardware-session.rev
```

R082.gg (#298, the public 10,000-placement limits) is on `main` as of 2026-10-11, so
the capacity case (section 1d) uses the same revision as every other section. Any
`main` revision from `d4dcac19` on contains it.

**P2. Build the binaries (day before).** Earlier records built Release binaries in
the pinned build image and ran the copied executables on both machines. Use the
bounded container runner from the session worktree. It enforces one CPU, 4 GiB and
no swap, so expect a long build:

```sh
cd ~/projects/sketchyup/.local/wt-session
run=hw-session-$(date +%Y%m%d)
scripts/run-container-check.sh "$run" sketchyup-build:20261002 bash -c '
  set -euo pipefail
  cmake -S /source -B /work/package/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSKETCHYUP_NATIVE_FIXTURES_IN_ALL=OFF
  cmake --build /work/package/build --target sketchyup sketchyup-text-worker \
    sketchyup-extension-worker real_model_benchmark native_persistence_benchmark \
    file_operation_input_tests viewport_tests
  cd /work/package/build
  cp sketchyup sketchyup-text-worker sketchyup-extension-worker real_model_benchmark \
    native_persistence_benchmark file_operation_input_tests viewport_tests /capture/
  cd /capture && sha256sum sketchyup* real_model_benchmark native_persistence_benchmark \
    file_operation_input_tests viewport_tests > binaries.sha256'
export SKETCHYUP_SESSION_BUILD=~/projects/sketchyup/build/local-checks/$run/capture
```

If the image is unavailable, build on the host under the same eight-CPU and 4 GiB
ceiling used by R082.i/j (cmake is in `~/.local/bin`):

```sh
cd ~/projects/sketchyup
systemd-run --user --wait --pipe --collect --same-dir --setenv=PATH="$PATH" \
  -p MemoryMax=4G -p MemorySwapMax=0 -p CPUQuota=800% -p TasksMax=256 \
  /usr/bin/taskset -c 0,1,3,6,8,10,12,13 bash -c '
    cmake -S .local/wt-session -B build/hw-session-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSKETCHYUP_NATIVE_FIXTURES_IN_ALL=OFF &&
    cmake --build build/hw-session-release -j2 --target sketchyup sketchyup-text-worker \
      sketchyup-extension-worker real_model_benchmark native_persistence_benchmark \
      file_operation_input_tests viewport_tests'
export SKETCHYUP_SESSION_BUILD=~/projects/sketchyup/build/hw-session-release
```

Record which path was used: the two builds are not byte-comparable.

**P3. Prepare the 100 MB fixture** ([R082.c](verification/R082c-native-persistence.md)):

```sh
mkdir -p ~/sketchyup-verification/m9-hardware-session/fixture
"$SKETCHYUP_SESSION_BUILD/native_persistence_benchmark" --prepare \
  ~/sketchyup-verification/m9-hardware-session/fixture/canonical.sketchyup
export SKETCHYUP_SESSION_FIXTURE=~/sketchyup-verification/m9-hardware-session/fixture/canonical.sketchyup
stat -c %s "$SKETCHYUP_SESSION_FIXTURE"; sha256sum "$SKETCHYUP_SESSION_FIXTURE"
```

R082.m used 100,671,415 bytes, canonical SHA-256 `d74a6810…0f37`. Schema 26
([R082.cc](verification/R082cc-instanced-storage.md)) may change the bytes; if
so, record the new size and hash and use the same file on both machines.

**P4. Disk, memory and load.** At least 6 GiB of available memory before each
campaign (the scripts check it), and at least 5 GiB free on `/home` for evidence,
fixture copies and the build (`df -h ~`; 1.4 TiB was free on 2026-10-11). Stop
local model runtimes, container checks (`docker ps`) and builds.

**P5. Record the outputs and back up settings.** These are read-only except the copies:

```sh
B=~/sketchyup-verification/m9-hardware-session/preflight-$(date +%Y%m%d)
mkdir -p "$B"
hyprctl -j monitors all > "$B/monitors.json"; hyprctl monitors all > "$B/monitors.txt"
cp ~/.config/hypr/monitors.lua "$B/"; sha256sum ~/.config/hypr/monitors.lua > "$B/monitors.lua.sha256"
cp -a ~/.config/fcitx5 "$B/fcitx5-config" 2>/dev/null || true
cp -a ~/.config/SketchyUp "$B/sketchyup-config" 2>/dev/null || true
{ cat /sys/power/mem_sleep; systemd-inhibit --list --no-pager; systemctl --user is-active hypridle; } > "$B/suspend.txt"
gsettings get org.gnome.desktop.interface toolkit-accessibility > "$B/a11y.txt" 2>&1 || true
hyprctl -j clients | jq '[.[] | {class, title, workspace: .workspace.name, monitor}]' > "$B/windows.json"
```

**P6. AMD machine.** Wake its display, then from the laptop:

```sh
R=/home/keith/sketchyup-verification/m9-session-$(date +%Y%m%d)
ssh sketchyup-gpu "mkdir $R && mkdir $R/bin $R/scripts $R/fixture && pacman -Q qt6-base mesa && glxinfo -B | grep -E 'Accelerated|renderer'"
pacman -Q qt6-base
rsync -a "$SKETCHYUP_SESSION_BUILD"/{real_model_benchmark,file_operation_input_tests,sketchyup-text-worker,binaries.sha256} sketchyup-gpu:$R/bin/
rsync -a scripts/session-lib.sh scripts/session-benchmark-campaign.sh sketchyup-gpu:$R/scripts/
rsync -a "$SKETCHYUP_SESSION_FIXTURE" sketchyup-gpu:$R/fixture/
ssh sketchyup-gpu "cd $R/bin && sha256sum -c --ignore-missing binaries.sha256 && sha256sum $R/fixture/*"
```

This is R085.g's method: build once on the laptop from the named commit, copy,
and verify the transferred bytes. It relies on matching Qt (6.11.2 on both on
2026-10-07). If the copied binaries fail to start there, build on the AMD machine
instead from a source archive of the same commit (`scripts/package-source.sh`,
copy the archive, then the host build command from P2 there) and record that the
binaries differ. The host build in P2 writes no `binaries.sha256`; create it with
`sha256sum` in the build directory before copying.

## 1. R082 Intel campaigns (about 80 min, unattended; 10k case adds about 45 min)

**Purpose.** Re-measure complete frame, first edit frame, picking and inference on
the five supported fixtures, rendered history and memory, and the 100 MB
open/save heartbeat on Intel Arc under native Wayland at the session revision.
This addresses D-004 (Intel misses 16.7 ms under one CPU in
[R082.an](verification/R082an-public-memory.md), but passed under eight CPUs in
[R082.j](verification/R082j-viewport-reuse.md)) and D-005 (Intel's 282 ms
open heartbeat gap in [R082.m](verification/R082m-native-file-timing.md) has not
been rerun since the [R082.bb](verification/R082bb-public-tooltip-progress.md)
progress fix). It gives the evidence either way for the reference-configuration
decision that D-004 needs.

**Harness.** `real_model_benchmark [count] [scenario] [history-cycles]` and
`file_operation_input_tests FIXTURE` with `SKETCHYUP_TEST_FILE_CACHE_POLICY`.
`scripts/session-benchmark-campaign.sh` runs the case lists and resource controls
of the earlier controllers, one transient `systemd-run --user` unit per case,
records the cgroup limits each worker actually received, and flags values above
the [BUILD_PLAN targets](BUILD_PLAN.md#initial-performance-budgets).

| Run | Matches | Controls |
| --- | --- | --- |
| `intel-8cpu frame` | R082.i/j: 5 × 25-placement smokes, 15 × 1,000 (three per scenario) | 4 GiB, no swap, `CPUQuota=800%`, `TasksMax=256`, CPUs 0,1,3,6,8,10,12,13, 300 s, Wayland, fullscreen 1920×1080 |
| `intel-1cpu history` | R082.an: 5 smokes, 5 × 1,000, five history cycles | 4 GiB, no swap, `CPUQuota=100%`, `TasksMax=128`, CPU 13, 900 s |
| `intel-8cpu file` | R082.m: 3 warm and 3 verified-cold 100 MB cases | as `intel-8cpu`, 120 s |
| `intel-1cpu capacity-10k` | R082.an's 10,000-placement row, now accepted by the public path (R082.gg) | as `intel-1cpu` |

All runs set `QT_QPA_PLATFORMTHEME=generic`, `QT_SCALE_FACTOR=1`, one OpenMP/BLAS
thread and private TMP/XDG directories, as before. The compositor scale still
applies (1.6 on DP-7). Hypridle respects systemd idle inhibitors, so run each
campaign under one; it ends with the command, so there is nothing to restore:

```sh
cd ~/projects/sketchyup/.local/wt-session
scripts/session-benchmark-campaign.sh intel-8cpu frame --dry-run   # prints cases and controls
systemd-inhibit --what=idle --why='SketchyUp M9 campaign' scripts/session-benchmark-campaign.sh intel-8cpu frame
systemd-inhibit --what=idle --why='SketchyUp M9 campaign' scripts/session-benchmark-campaign.sh intel-1cpu history
systemd-inhibit --what=idle --why='SketchyUp M9 campaign' scripts/session-benchmark-campaign.sh intel-8cpu file
```

**1d (10,000 placements, R082.gg).** The public path now accepts the 10,000-placement,
million-triangle fixture ([R082.gg](verification/R082gg-placement-limits.md), #298), so
this case is a measurement, not a rejection check. The earlier state
(`Component placement exceeds document editing limits` at the first 10,000-placement
attempt in R082.an, D-003) no longer applies; a rejection now is a failure to report.
Measure it against the [R082.w v2](verification/R082w-memory-budget-v2.md) budget, not
the v1 3.5 GiB experimental class:

```sh
systemd-inhibit --what=idle scripts/session-benchmark-campaign.sh intel-1cpu capacity-10k
```

The script's `capacity-10k` list runs `repeated` at 10,000 placements three
times, then `deep`, `textures` and `far` once each (`10000 deep 5`, `10000 textures 5`,
`10000 far 5`; added for the 2026-10-11 session). R082.gg's remaining list also names
the sharing-off control, which is run by hand with
`SKETCHYUP_BENCHMARK_PLACEMENT_SHARING=0` and the same controls. R082.gg
measured deep and textures at 10,000 with a test build that raised the limit scale,
and only `repeated` with the public build, so a public-build rejection of another
family is possible and is a finding (none occurred on AMD). The script flags
`viewportGpuCompleteP95Ms` above 16.7 ms for every case; for 10,000 placements the
target is 33.3 ms ([BUILD_PLAN](BUILD_PLAN.md#initial-performance-budgets)), so judge
that figure by hand and do not count a 16.7–33.3 ms flag here as a miss.

Do not use the laptop while a campaign runs; the benchmark window is fullscreen.
If a display sleeps, the script stops before the next case. Resume with
`SESSION_FROM=<label>` in a new evidence directory.

**Expected for 1d.** Every case exits 0 and the fixture hash is `5a0d89c3…` as in
R082.gg. Own-process peak RSS stays below the v2 ceilings: 1.25 GiB for repeated, deep
and textures, and 3 GiB for the sharing-off control (software: 614 MB, 624 MB, 884 MB
and 1.93 GB). Resident growth over five cycles stays below 64 MiB (software: 6.6 MB)
and retained history below 64 MiB (software: 1.6 MB). A pass accepts the v2 ceilings
for that GPU; a ceiling above them is a finding and reclassifies D-003 to Major.

**Expected.** Every case exits 0 with no `MISS` for picking, inference or edit
feedback. The frame row is the open question: in R082.j, all 1,000-placement runs
met 16.7 ms with eight CPUs; in R082.an, all five missed with one CPU. The file
cases are under 5 s; D-005 closes if every maximum heartbeat gap is below 50 ms,
as AMD achieved in R082.bb.

**Capture.** The evidence directory: per-case report (`.out`), stderr, exit status,
limits, monitors, `summary.tsv`, `controls.txt` and binary/fixture hashes. Note
the fixture hashes; Intel and AMD must agree per scenario.

**Pass/fail.** Pass: all cases exit 0, fixture hashes agree across machines, and
no `MISS` appears outside the Intel 1-CPU complete-frame row. That row is reported
as measured for D-004, not relabeled. Fail: any nonzero exit, hash mismatch or
other `MISS`.

**Feeds.** [R082](PR_ROADMAP.md#r082) (D06, N01); D-004, D-005 and D-003 in
[R089.a](verification/R089a-defect-triage.md) (D-003 is Minor, "fixed in software,
hardware acceptance pending", until this section reports); the M9 R082 row. Write a
new R082 record that cites R082.i/j/m/an/bb and R082.gg/w v2 as the earlier conditions.

## 2. R082/R085 AMD campaigns (about 70 min unattended; run during sections 3–10)

**Purpose.** The same fixtures on the discrete GPU at the same revision, matching
[R085.g](verification/R085g-current-discrete-gpu.md), R082.an (AMD) and R082.bb.

**Commands.** After P6, run in a laptop terminal (SSH needs `-t` for the prompt).
The script borrows `DISPLAY` and `XAUTHORITY` from the running `xfce4-session`, as
the earlier runners did:

```sh
R=/home/keith/sketchyup-verification/m9-session-$(date +%Y%m%d)
for kind in frame history file; do
  ssh -t sketchyup-gpu "SKETCHYUP_SESSION_BUILD=$R/bin SKETCHYUP_SESSION_ROOT=$R/evidence \
    SKETCHYUP_SESSION_FIXTURE=$R/fixture/canonical.sketchyup $R/scripts/session-benchmark-campaign.sh amd $kind"
done
rsync -a sketchyup-gpu:$R/evidence/ ~/sketchyup-verification/m9-hardware-session/amd/
```

Controls: X11 (`QT_QPA_PLATFORM=xcb`); benchmark cases use 4 GiB, no swap,
`CPUQuota=100%`, `TasksMax=128`, the highest-numbered CPU (11 on the 12-CPU
guest) and 900 s; file cases use `CPUQuota=800%`, CPUs 0–7, 90 s, Fusion style and
`QT_IM_MODULE=compose`, as R082.bb. The 10k case (as 1d, including the
script's `repeated`-only case list and the hand-run families and control) is
`... session-benchmark-campaign.sh amd capacity-10k`; it measures the same fixture on
the discrete GPU against R082.w v2.

The guest's XFCE power manager may blank the display during a long run. The
script checks `xset q` before every case and stops rather than waking it. If it
blanks, enable XFCE presentation mode for the run, and add it to the restore list.

**Expected.** All targets met, as in R082.an and R082.bb (AMD frame p95 about
2.3 ms; worst file heartbeat gap 33 ms).

**Capture.** As section 1, plus `glxinfo.txt`. Record the guest load and that the
hypervisor load is unmeasured.

**Pass/fail.** Pass: every case exits 0, no `MISS`, fixture hashes equal to the
Intel ones. Fail otherwise.

**Feeds.** [R082](PR_ROADMAP.md#r082), [R085](PR_ROADMAP.md#r085) (supported GPU
configurations); the M9 R082 and R085 rows.

## 3. R085 output hotplug (about 20 min)

**Purpose.** Unplug and replug DP-7 while SketchyUp runs with a model and an
unsaved edit, on native Wayland and on the Qt X11 fallback (XWayland). R085.h/i
cover output moves on stable outputs only; hotplug is open in R085.

**Commands.**

```sh
scripts/session-hotplug-check.sh wayland "$SKETCHYUP_SESSION_FIXTURE"
scripts/session-hotplug-check.sh xcb     "$SKETCHYUP_SESSION_FIXTURE"
```

Without a model argument the script uses the demo. It moves only the SketchyUp
window to DP-7's active workspace (after asking), then waits for the unplug and
replug, and records `hyprctl monitors` before, unplugged and replugged, the window
position, `app.log` (with `qt.qpa.screen` logging), window screenshots, and core
dumps since the start.

**Expected.** No crash. After the unplug, the window is on eDP-1 and the viewport
renders sharply at scale 2. After the replug, it renders at 1.6 wherever Hyprland
places it (workspace rules put workspaces 1–9 back on the TV). The unsaved edit
survives and saves with Save As.

**Pass/fail.** Pass: app alive at both points, viewport correct, edit present in
`after-hotplug.sketchyup`, no core dump. Fail: any crash, blank or wrongly scaled
viewport that does not recover on the next redraw, or a lost edit.

**Feeds.** [R085](PR_ROADMAP.md#r085) (N01, N04); a new R085 record next to
[R085.h](verification/R085h-physical-outputs.md) and
[R085.i](verification/R085i-assisted-output-transitions.md).

## 4. R085 suspend and resume (about 25 min)

**Purpose.** Suspend the laptop during a long file operation and confirm that the
GL context, viewport, document and recovery journal are correct after resume.

**Commands.**

```sh
scripts/session-suspend-check.sh wayland save "$SKETCHYUP_SESSION_FIXTURE"
scripts/session-suspend-check.sh wayland open "$SKETCHYUP_SESSION_FIXTURE"
scripts/session-suspend-check.sh xcb     save "$SKETCHYUP_SESSION_FIXTURE"
```

In `save` mode the app opens a private copy of the fixture; you make an edit, the
script waits 40 s so the 30 s recovery checkpoint covers it and lists the journal,
then arms an `inotifywait` watcher. You press Ctrl+S, and the script runs
`systemctl suspend` when the save creates its first file beside the target. In
`open` mode the app starts with the demo and the watcher fires when the fixture
is opened (Ctrl+O). If the file chooser reads the file for a preview, the watcher
fires before the open starts; the recorded event shows this, so repeat the run. After you wake and unlock the machine and press Enter, the
script records the `PM: suspend entry/exit` journal lines, monitors, window,
screenshot, GL-related log lines, the journal listing and model hashes. It also
compares inhibitors and hypridle with their state before.

**Expected.** The viewport redraws after resume (new context or preserved context,
with no black area). The save or open completes, or reports a clear error; the
progress window is gone. After a completed save the file has new bytes and a
`.bak`. After a failed save, the original bytes are unchanged and the edit is kept
unsaved. The recovery journal exists and File > Recover work shows nothing
unexpected.

**Pass/fail.** Pass: all of the above, suspend lines present in the journal, no
core dump. Fail: crash, permanently broken viewport, corrupt or truncated model,
lost edit, or a missing or unreadable journal.

**Feeds.** [R085](PR_ROADMAP.md#r085) (N01); [R083](PR_ROADMAP.md#r083) for
the file and journal outcome (D02, D04).

## 5. N04 physical 150% scale (about 15 min)

**Purpose.** R085's Verify line: the 640 px and wide layouts keep their actions
and Measurements at fractional scale, on a physical output at 150%, with picking,
text and overlays aligned (N04).

**Commands.**

```sh
scripts/session-fractional-scale.sh DP-7 1.5
```

The config uses Lua (Hyprland 0.56), so the script uses
`hyprctl eval 'hl.monitor({...})'` instead of `hyprctl keyword monitor`. Before
any change it records `hyprctl -j monitors all` and `monitors.lua` (with its hash),
and writes `restore-monitors.sh` with one `hl.monitor()` call per output. DP-7 at
1.5 is 2560 logical pixels wide, so eDP-1, which sits directly to its right at
x=2400, moves to x=2560. On exit, including Ctrl+C or an error, it restores both
outputs and verifies they match the recording (name, mode, position, scale,
transform). It never writes to `~/.config/hypr`. It stops and restores if the
scale does not take effect. `hyprctl eval` for monitors has not been tried on this
machine: that path is used for the first time in the session.

**Expected.** Wide: every action, panel tab and Measurements is visible and
usable; the inference marker sits under the pointer; text is crisp. 640 px: the
same actions are reachable (Commands button, tool rail or palette) and
Measurements accepts a value.

**Capture.** Monitors before, scaled and restored; window records and screenshots
`wide` and `narrow`; the result.

**Pass/fail.** Pass: both layouts meet the expectations and the restore verifies.
Fail: a hidden or unreachable action, a hidden Measurements, misaligned picking or
overlays. A restore mismatch is a session problem to fix before continuing
(`hyprctl reload`), not a product failure.

**Feeds.** [R085](PR_ROADMAP.md#r085) Verify line; N04; the M9 R085 row.

## 6. N03 physical pointer capture and focus (about 20 min)

**Purpose.** Drag operations that leave the window and release outside it, and
keyboard focus through tool changes, with a real mouse on Hyprland. Headless
fixtures cover these with synthetic events only; D-007 records Wayland focus
races in those fixtures.

**Commands.**

```sh
scripts/session-manual-check.sh pointer wayland
scripts/session-manual-check.sh pointer xcb
```

The script prints steps P1–P7: push/pull, line, orbit and selection drags
released outside the window or over another application, re-entry, a drag on the
other output, and L/R/P/M/Space tool changes with typed Measurements and F6. Type a
step id to take a screenshot and note an observation.

**Expected.** No stuck drag or rubber band, at most one undo entry per drag, the
first click after re-entry handled normally, digits always reaching Measurements,
and a visible focus ring.

**Pass/fail.** Pass: every step as expected on both platforms. Fail: a stuck
capture, a swallowed click, a wrong focus target or a lost edit.

**Feeds.** N03 under [R084](PR_ROADMAP.md#r084) and [R085](PR_ROADMAP.md#r085);
D-007 in R089.a if a real-device race appears.

## 7. R085 input methods (about 30 min)

**Purpose.** Dead keys, Compose and an IME in Measurements and text fields, on
Wayland and X11. The user guide says that physical layouts and input methods are untested.
[R085.p](verification/R085p-keyboard-layouts.md) (#291, merged) covers us, de and fr
layouts with injected physical keycodes on X11 and nested Weston; this section covers
composition, not layouts on a real keyboard.

**Setup (owner).** In `fcitx5-configtool`, add "Keyboard - English (US, intl., with
dead keys)" and Mozc (or Pinyin) as extra input methods. The profile was backed up
in P5 and is restored afterwards.

**Commands.**

```sh
scripts/session-manual-check.sh input-method wayland                      # QT_IM_MODULE=fcitx
SESSION_IM_MODULE=wayland scripts/session-manual-check.sh input-method wayland   # text-input protocol
scripts/session-manual-check.sh input-method xcb
```

Steps I1–I6: dead `'` and `"` in imperial Measurements, accented names in the
Outliner and tags, Compose, IME preedit and commit in a text field, an IME active in
Measurements, and tool shortcuts with the IME on.

**Expected.** The exact composed text is committed once, with the preedit shown in
the field. Measurements either accepts the value as typed or rejects it clearly,
and never applies a different length. Escape cancels the preedit before the
dialog. Shortcuts behave as noted.

**Pass/fail.** Pass: no lost, duplicated or misapplied input on any platform.
Fail otherwise; record the exact keystrokes.

**Feeds.** [R085](PR_ROADMAP.md#r085) (input methods; N03); the user guide's
"Known gaps" paragraph.

## 8. D-020: hidden window during a long open (about 10 min; longer if Hyprland crashes)

**Purpose.** Weston 15.0.1 crashed when a progress popup was created for a
minimized window ([R083.f](verification/R083f-file-worker-hang.md)). Hyprland
has no minimize; moving the window to an unshown special workspace is its
equivalent.

**Commands.**

```sh
scripts/session-d020-check.sh "$SKETCHYUP_SESSION_FIXTURE"
```

The script starts the app with the demo and polls `hyprctl clients` every 20 ms
for the "Open model" dialog. When you confirm the copied fixture and the dialog
closes, it moves only the SketchyUp window to `special:sketchyup-d020`, without
following it. That happens within a few tens of milliseconds, before the
progress popup's 150 ms delay ends. After 10 s it checks Hyprland and the app,
moves the window back and records everything. An optional save variant is
printed at the end (hide the window by keybinding right after Ctrl+S).

**Expected.** Hyprland and SketchyUp keep running; the model opens; the window
returns intact.

**Pass/fail.** Pass: no compositor or app crash and no protocol error in
`app-wayland-lines.txt`. Fail: either crashes (Hyprland's core dump is then in
`coredumpctl list Hyprland`), or a protocol error.

**Feeds.** D-020 in R089.a, under [R085](PR_ROADMAP.md#r085). If it passes, D-020
stays external to Weston, with Hyprland tested.

## 9. R084 screen reader with Orca (about 30 min)

**Purpose.** The first interactive screen-reader session. Can a user select an
entity in the Outliner, hear its name, open Entity info and hear the measured
values, following the user guide's "Accessibility and keyboard use" section?
[R084.l](verification/R084l-linux-accessibility-bridge.md) checked the same data
through AT-SPI without speech.

**Commands.**

```sh
scripts/session-orca-check.sh wayland
scripts/session-orca-check.sh xcb
```

The script refuses to run if Orca is already running. It tests speech with
`spd-say` and starts `orca --replace --debug-file=<evidence>/orca-debug.txt`. It
then starts the demo with `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1` and prints steps
S1–S7. Type a step id to note what you heard (a screenshot is taken). At the end
it stops the Orca it started and extracts `SPEECH OUTPUT` and `BRAILLE LINE`
lines into `spoken.txt`. If this Orca version rejects `--debug-file`, start it
with `orca --replace --debug` from the evidence directory instead (it writes
`debug-*.out`, which the extraction also reads).

**Expected (checklist).**

- [ ] S1 window title spoken
- [ ] S2 "3D model viewport" and "No selection"
- [ ] S3 Outliner entity name and role spoken when chosen
- [ ] S4 selection summary changes after choosing
- [ ] S5 Info tab reached by keyboard (note how); 8 × 6 × 0.15 m, 100.2 m², 7.2 m³ spoken
- [ ] S6 command palette results spoken
- [ ] S7 undo announced or summary updated

Orca's own keyboard commands may be limited on Wayland compositors. If they do not
work under Hyprland, rely on focus-change speech and record that limit. The xcb
run is the comparison.

**Pass/fail.** Pass: S3 and S5 are spoken with the right values on at least one
platform, and every other result is recorded. Fail: the entity name or measured
values cannot be reached by speech. That is an accessibility finding, not a crash.

**Feeds.** [R084](PR_ROADMAP.md#r084) Verify ("verify … screen-reader
alternatives"); N05; the user guide's statement that no Orca session has been
validated; the M9 R084 row.

## 10. R085 automatic X11 output placement (about 10 min)

**Purpose.** R085 lists automatic X11 output placement as open. The harness exists:
`viewport_tests --screens` requests each output for its own fullscreen test
window. Without `SKETCHYUP_TEST_OUTPUT_HELPER` it relies on Qt's own placement,
which failed twice in [R085.h](verification/R085h-physical-outputs.md) (D-009).
With the helper it passed in R085.i. No new script is needed.

**Commands.** Both open fullscreen test windows on each output for a few seconds:

```sh
E=~/sketchyup-verification/m9-hardware-session/x11-placement-$(date +%H%M%S); mkdir -p "$E"/{cfg,data,cache}
common=(env XDG_CONFIG_HOME="$E/cfg" XDG_DATA_HOME="$E/data" XDG_CACHE_HOME="$E/cache" QT_QPA_PLATFORMTHEME=generic)
# Automatic (Qt only): the open question
"${common[@]}" QT_QPA_PLATFORM=xcb timeout 120 "$SKETCHYUP_SESSION_BUILD/viewport_tests" --screens > "$E/xcb-automatic.txt" 2>&1; echo "exit $?" >> "$E/xcb-automatic.txt"
# Assisted control (R085.i) and native Wayland control
"${common[@]}" QT_QPA_PLATFORM=xcb SKETCHYUP_TEST_OUTPUT_HELPER="$PWD/scripts/test-hyprland-output.py" \
  timeout 120 "$SKETCHYUP_SESSION_BUILD/viewport_tests" --screens > "$E/xcb-assisted.txt" 2>&1; echo "exit $?" >> "$E/xcb-assisted.txt"
"${common[@]}" QT_QPA_PLATFORM=wayland timeout 120 "$SKETCHYUP_SESSION_BUILD/viewport_tests" --screens > "$E/wayland.txt" 2>&1; echo "exit $?" >> "$E/wayland.txt"
```

**Expected.** Wayland and assisted X11 pass, as in R085.i. Automatic X11 is
expected to fail the requested-output assertion again. If it passes, record the
compositor and Qt versions.

**Pass/fail.** Report automatic X11 placement as measured. The two controls must
pass; if either fails, the session environment differs from R085.i and needs
investigation.

**Feeds.** [R085](PR_ROADMAP.md#r085); D-009 in R089.a; the M9 R085 row
(known unsupported configurations).

## Durations

| Section | Owner attention | Elapsed |
| --- | --- | ---: |
| Build (P2, day before) | none | 1–2 h (one CPU in the container) |
| Preflight P0–P6 | attended | 30 min |
| 1. Intel campaigns (frame, history, file) | start, then hands off | 80 min |
| 1d. Intel 10k | hands off | 45 min (more for the added families) |
| 2. AMD campaigns (overlaps 3–10) | start only | 70 min (10 min attended) |
| 3. Hotplug, two platforms | attended | 20 min |
| 4. Suspend/resume, three runs | attended | 25 min |
| 5. 150% scale | attended | 15 min |
| 6. Pointer capture, two platforms | attended | 20 min |
| 7. Input methods, three runs | attended | 30 min |
| 8. D-020 | attended | 10 min |
| 9. Orca, two platforms | attended | 30 min |
| 10. X11 placement | attended | 10 min |
| Restore checklist | attended | 10 min |
| **Total on the day** | | **about 4 h 40 min** (5 h 25 min with the Intel 10k run; the AMD 10k run overlaps) |

The total assumes section 2 runs on the AMD machine during sections 3–10. Run
section 1 first, while the laptop is otherwise idle.

## Restore checklist

- [ ] **Monitor scale and positions:** `hyprctl -j monitors all` matches
  P5's `monitors.json` (eDP-1 2880×1800 at 2400x0, scale 2; DP-7 3840×2160 at 0x0,
  scale 1.6). If not, run the section 5 evidence's `restore-monitors.sh`, then
  `hyprctl reload` if still wrong. Optionally run `hyprctl reload` once at the end
  to drop the runtime monitor rules the scale check added.
- [ ] **Config unchanged:** `sha256sum -c` P5's `monitors.lua.sha256`.
- [ ] **DP-7 plugged in and awake;** workspaces 1–9 back on the TV and 10 on eDP-1
  (`hyprctl workspacerules`).
- [ ] **Layouts:** no SketchyUp windows left; `special:sketchyup-d020` empty
  (`hyprctl -j workspaces`); other windows where P5's `windows.json` had them.
- [ ] **Suspend settings:** `systemd-inhibit --list` has no leftover session
  inhibitors; `systemctl --user is-active hypridle` is active; `/sys/power/mem_sleep`
  unchanged (compare P5's `suspend.txt`). The session changes no suspend settings.
- [ ] **Input methods:** restore `~/.config/fcitx5` from P5 if methods were added
  (`fcitx5 -r` to reload); keep or remove the installed packages.
- [ ] **Orca:** not running (`pgrep -x orca`); toolkit accessibility as in
  `a11y.txt`; no new Orca autostart entry in `~/.config/autostart`.
- [ ] **AMD machine:** no `sketchyup-m9-*` units left
  (`ssh sketchyup-gpu systemctl --user list-units 'sketchyup-m9-*'`); presentation
  mode off if it was turned on; evidence copied back.
- [ ] **SketchyUp settings:** `~/.config/SketchyUp` unchanged (scripts use private
  directories).
- [ ] **Evidence:** review screenshots and logs for private content before
  publishing; publish hashes of private logs as earlier records did. Keep
  `.local/wt-session` until the records are written.
