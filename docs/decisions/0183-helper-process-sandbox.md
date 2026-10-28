# 0183 — Helper process sandbox

**Status: Implemented (S0–S6).** The owner accepted decisions 1 to 11 as recommended on 2026-10-27 and assigned roadmap row **R109** (decision 12). The shared slice (S0), one slice per helper (S1 to S5) and CI (S6) are all merged; the implementation notes below record what each built, and where it differs from the design. S1's `tooLarge` fix was merged first (PR #470). Originally proposed 2026-10-26. This ADR decides how the file-parsing helpers are sandboxed. It also lists the corrections owed to ADRs 0171, 0176 and 0136, which call those helpers "sandboxed" or describe them loosely, and to ADR 0180, which needs two corrections of its own. The design was written from measurements, and it was not code. The owner decides on the verdict and on the [owner decisions](#owner-decisions). Before S0, the helpers had resource limits and, in some modes, `PR_SET_NO_NEW_PRIVS`. They kept the user's file and network access, as [ADR 0171 §1.6](0171-ifc-exchange-subset.md) says. The sandbox removes that access.

## Verdict in brief

- **Sandbox every helper that reads an untrusted file, before it opens that file.** The layers, in order: `PR_SET_NO_NEW_PRIVS`; a Landlock ruleset that allows only a private directory and the read-only system paths the helper needs; a seccomp-bpf allow-list whose default action kills the process. A few calls a helper may meet (sockets in Blender, fork in the IFC diagnosis path) are refused with an explicit `EPERM` entry instead of being left to the kill.
- **Namespaces and bubblewrap are not the primary mechanism.** In the pinned CI image, under Docker's default seccomp profile, `unshare(CLONE_NEWUSER)` and `unshare(CLONE_NEWNET)` return `EPERM` (measured). Namespaces also leave `exec`, `fork`, `ptrace` and socket creation open. They only cut off the network.
- **Fail closed.** If a layer cannot be installed, the helper refuses with a named `sandboxUnavailable` before it parses anything. Fail-open was measured too: it runs the parser with resource limits only and prints a warning.
- **Measured.** Every helper class parses to the same output under the kill allow-list as it does unsandboxed: 74 IFC and STEP fixtures, 12 SKP fixtures, the text worker (2 of 3 requests exact; the third varies between unsandboxed runs), 1 extension request, and 2 Blender probes plus 1 Cycles render. This ran on kernel 7.2.8 (Landlock ABI 10) and inside the pinned CI image. Negative controls are refused as designed.
- **The measurements found real defects.** A syscall that a trace did not show was refused: the extension worker's `fcntl`, which appears only when stdin is a pipe. Glibc's `isatty` path issues `ioctl` when stdout is `/dev/null`. Blender's startup opens a socket that a kill policy would kill it for. A refused `fork` silently changed the IFC diagnosis result. The Blender exec path needs the dynamic loader's own calls. The design below handles each of these.

## Context

- ADR 0180 (branch `docs/r106-dwg-feasibility`, commit `5c3edcf0`, Proposed 2026-10-26) proposes a seccomp and Landlock layer for the DWG helper. Its §3.6 and owner decision 8 ask for a separate ADR to retrofit the other helpers. This is that ADR.
- ADR 0171 §1.6 (line 409) says the helper "contains accidents and resource exhaustion; it is not a sandbox." ADR 0176 decision 2 (line 278) calls the SKP helper "sandboxed as in ADR 0171 §1.6". ADR 0136 says its child is "not an OS sandbox". Those three statements are the ones to correct.
- The supported platform is Arch Linux on x86_64 ([`docs/SUPPORT_MATRIX.md`](../SUPPORT_MATRIX.md), line 12). CI runs on a GitHub-hosted `ubuntu-latest` host, inside an Arch container pinned by digest ([`.github/workflows/native.yml`](../../.github/workflows/native.yml), lines 114–116). The pinned image is `sketchyup-ci-pinned:r097a-ifc`.
- A grep of `src/` on 2026-10-26 finds no `seccomp`, `landlock`, `unshare` or `CLONE_NEW*` call. The only isolation is `setsid`, `PR_SET_PDEATHSIG` and `setrlimit`, plus `PR_SET_NO_NEW_PRIVS` in some helper mains.

## 1. Current state, per helper

Line numbers are in this worktree at `d65a617d` (main, 2026-10-26).

| Helper | Binary | Launched by | Isolation today | Input and output |
|---|---|---|---|---|
| SKP | `sketchyup-skp-worker` | `src/io/skp_worker.cpp:69–88` | `setsid` and `PDEATHSIG` (86–87); rlimits as arguments (75–76); `PR_SET_NO_NEW_PRIVS` in `--import` only (`src/skp/worker_main.cpp:92`) | Private copy in, `SKSKPP01` package out |
| IFC and STEP import, export, check | `sketchyup-ifc-worker` | `src/io/ifc_worker.cpp:129–171` (import), `ifc_worker_run.cpp:16–28` (export, check), `step_worker.cpp:240–262` (STEP import) | `setsid` and `PDEATHSIG` (`ifc_worker.cpp:162–163`, `ifc_worker_run.cpp:26–27`, `step_worker.cpp:260–261`); rlimits as arguments; `PR_SET_NO_NEW_PRIVS` in import, check, export and STEP package modes (`src/ifc/worker_main.cpp:123, 138, 155, 219, 246`) | Private copy in, package out |
| IFC and STEP probes | same binary | `src/app/ifc_availability.cpp:44` runs `--probe` with no file | None for the probe modes: `--probe-api`, `--probe-step`, `--probe-write` and `--write-step` (`src/ifc/worker_main.cpp:158–203, 222–225`), and `--import-step` without `--output` (245) return before `PR_SET_NO_NEW_PRIVS` | Developer modes that read a named file |
| SKP probe | `sketchyup-skp-worker --probe` | Not called by the application | None: `--probe` returns at lines 70–73, before `PR_SET_NO_NEW_PRIVS` (92) and before any limit is applied; `runProbe` then reads up to 1 GiB (`src/skp/import.cpp:118`) | Developer mode |
| Text | `sketchyup-text-worker` | `src/text/text_worker.cpp:176–238`, from `src/automation/text_commands.cpp:191` | None. The child gets `systemEnvironment()` (`text_worker.cpp:188`), so `HOME` and `XDG_*` point at the user's home. No `setsid`, no `PDEATHSIG`, no rlimit, no `NO_NEW_PRIVS` | Protocol JSON on stdin and stdout; reads system fonts through fontconfig and Qt |
| Extension | `sketchyup-extension-worker` | `src/automation/extension_worker.cpp:72–140` | Environment scrubbed to `PATH` and `LANG` (`extension_worker.cpp:93–99`); the parent enforces the deadline. No `setsid`, `PDEATHSIG`, rlimit or `NO_NEW_PRIVS` | Protocol JSON on stdin; the manifest travels inside the request |
| Blender | `/usr/bin/blender` (external) | `src/integrations/blender_job.cpp:568–602` | Environment: `BLENDER_*`, `PYTHONPATH`, `PYTHONHOME`, `DISPLAY` and `WAYLAND_DISPLAY` removed; `BLENDER_USER_CONFIG` and `BLENDER_USER_SCRIPTS` set to the attempt directory (570–578). The child gets `PDEATHSIG` and a `getppid` check (597–601, Linux only). No `setsid`, rlimit or `NO_NEW_PRIVS`. `HOME`, `XDG_*`, `TMPDIR`, `ALSOFT_DRIVERS` and `--offline-mode` are not set | Scene snapshot in, `result.json` and PNG or video out |

Two more facts about the current code affect the design:

- **Crash handling reads a sandbox kill as a crash.** A helper killed by a signal is classed `crash` (`src/io/ifc_worker.cpp:281–282`) and enters the restart and element-exclusion path (`ifc_worker.cpp:391–440`). A policy violation would then be retried as if the parser had faulted.
- **The IFC diagnosis path forks.** When a geometry stop happens on several threads, the helper restarts on one thread and bisects the remaining tasks in child processes (`ifc_worker.cpp:391–431`; `src/ifc/geometry.cpp:393–401`). Each child calls `prctl`, `alarm` and `_exit`. Refusing `fork` is therefore a behaviour change, not only a tightening (section 6.5).

## 2. Threat model

**Assets.** The user's other files, including other documents and credentials. The network. Other processes of the user (signals, `ptrace`, sockets reachable from the user's session). The GPU device, for Blender only. The application's document, which the helper must not be able to alter outside its output package.

**Adversary.** A crafted `.skp`, `.ifc`, `.step`, font request, extension manifest or Blender snapshot. Its goal is to corrupt the helper's memory, then use that to read or write other files, open a connection, run a program, or reach another process.

**What the sandbox has to stop.** Those steps after the corruption: reading outside the private copy, writing outside the output directory, creating any socket or reaching the network, executing a program, using `ptrace`, `process_vm_*`, `io_uring`, `bpf`, `mount` and the other calls that cross process or privilege boundaries. The 2026 CVE history of the parsers (ADR 0180 §1) is mostly out-of-bounds reads. A sandbox turns many of them into a crash. Only a parser that can run code can use the sandbox's holes, so the sandbox is the second line of defence, behind the checked decoders and fuzzing (ADR 0176 §3, ADR 0180 §3.3).

**Out of scope.** Kernel bugs in Landlock or seccomp. Denial of service within the existing limits. Corruption of the helper's own output, which the application's checked decoder must reject. Attacks on the application process itself.

## 3. Mechanisms compared

| Mechanism | Denies | Needs | Unsupported | Measured here | Verdict |
|---|---|---|---|---|---|
| **seccomp-bpf, raw** (no library) | Any syscall, by number and by some arguments. Filters see the argument registers, not the memory they point to (an inference: the man page does not state it). | `no_new_privs` or `CAP_SYS_ADMIN`; Linux 3.17 or later ([seccomp(2)](https://man7.org/linux/man-pages/man2/seccomp.2.html), read 2026-10-26) | The `seccomp()` call fails with `EINVAL`, `ENOSYS` or `EPERM` | Works. Allow-list and deny-list both tested (§6.3) | **Primary** |
| **libseccomp** | As raw BPF, with an architecture table | A library (LGPL-2.1). Upstream 2.6.1 is dated 2026-07-01; Arch packages 2.6.0-1 ([libseccomp releases](https://github.com/seccomp/libseccomp/releases/tag/v2.6.1), [Arch core](https://archlinux.org/packages/core/x86_64/libseccomp/), both read 2026-10-26) | Same as raw | Installed on this host (2.6.0), not used | Alternative if the owner prefers the library (decision 2) |
| **Landlock** | Filesystem access by path (read, write, execute, create, remove, rename, truncate, refer). Later ABIs add TCP bind and connect (ABI 4), device ioctl (ABI 5), abstract UNIX sockets and signals (ABI 6), and more | Kernel 5.13 or later with `CONFIG_SECURITY_LANDLOCK` and `landlock` in the boot LSM list. `no_new_privs` for unprivileged use ([kernel docs](https://docs.kernel.org/userspace-api/landlock.html), read 2026-10-26) | `landlock_create_ruleset` returns `EOPNOTSUPP` when disabled at boot, `ENOSYS` on older kernels | ABI 10 on this kernel. Restricts reads, writes and execs (§6.2) | **Primary** |
| **User and network namespaces** | Network reachability (only loopback is present), and what the mount table shows | Unprivileged user namespaces. `unshare -n` without `-U` needs `CAP_SYS_ADMIN` and fails with `EPERM` (measured, §6.2) | `EPERM` under Docker's default profile (measured) | `unshare -Urn` works on the host. `connect` to 8.8.8.8 returns `ENETUNREACH` inside bwrap (§6.8). Does not deny `fork`, `exec`, `ptrace`, `io_uring` or socket creation (§6.8) | Not primary. Optional extra layer where available |
| **`PR_SET_NO_NEW_PRIVS`** | Setuid and file capabilities gaining privilege across `exec` | Nothing (Linux 3.5+) | Not applicable | Works. Required by seccomp for unprivileged use, and by Landlock | **Required, first** |
| **bubblewrap** (`bwrap` 0.13.1) | Mount visibility, namespaces (as above) | A user namespace when not root. No setuid mode any more ([README](https://github.com/containers/bubblewrap), read 2026-10-26) | Does not start where user namespaces are denied | Works on the host, with 3.0 ms median startup (§6.8). Not run in the container; the namespace calls it needs return `EPERM` there | Not primary. A new external dependency for little extra |

Two facts decide the mechanism choice. The first is that CI cannot use namespaces. The second is that namespaces leave the calls that matter most open. Landlock and seccomp are available in the pinned image under the default profile, and that is what the design needs.

## 4. Design

### 4.1 Order and place

1. **In the helper, before the first input byte is opened.** The helper's `main` calls one function that installs the layers in this order: `PR_SET_NO_NEW_PRIVS`; the Landlock ruleset; the seccomp filter. Landlock is installed before seccomp so that the ruleset's own syscalls are allowed to run. Every mode that opens a user file is covered, including the probe modes (decision 7).
2. **In the parent, for an external program (Blender).** The job's `setChildProcessModifier` runs in the forked child, before `exec`. Only async-signal-safe calls are allowed there, so the parent builds the BPF program and opens the Landlock path descriptors before the fork. The child then issues the `prctl`, `landlock_restrict_self` and `seccomp` syscalls and calls `exec`. The `exec` happens under the filter (§4.4).
3. **Glibc startup and the dynamic loader are outside the first layer.** They run before `main`. The measured preload runs also began after the constructor. For helpers that install their own filter, that is correct. For the Blender exec, the loader's calls must be on the allow-list (§4.4, §6.6).

### 4.2 Filesystem rules

- The helper's private directory is read and written. It holds the input copy and the output package. It is never a system path. The mode is `0700`, as ADR 0171 §1.6 already requires.
- System paths are read-only. Executable library directories are read and execute. The helper's own plugin directory counts as a library directory (IfcOpenShell loads its kernels and plugins from it during the parse).
- No rule grants write access to anything outside the private directory. `/dev/null` is granted read and write for the helpers that redirect to it (measured for Blender).
- Landlock's `LANDLOCK_ACCESS_FS_IOCTL_DEV` (ABI 5) is not used. The Blender GPU path needs `ioctl` on `/dev/dri`. Device `ioctl` is therefore not filtered by this design.

### 4.3 Syscall policy

- **Allow-list, default `KILL_PROCESS`.** An unlisted call kills the helper. A missing entry becomes a visible failure, not a silent one. The measurements show why this matters (§6.5).
- **Explicit `EPERM` entries** for calls the helper may legitimately make and must not complete: `socket`, `socketpair`, `bind`, `connect` and friends for helpers that need no network. Blender's `urllib3` import probes IPv6 with `socket` and `bind`. Under a kill default that probe kills Blender, so those two calls return `EPERM` (§6.4).
- **Process creation.** A `clone` with `CLONE_THREAD` is allowed. A `clone` without it returns `EPERM` (fork-like). `clone3` returns `ENOSYS`, so glibc falls back to `clone` (measured: `pthread_create` succeeds under the policy, and Docker's default profile also returns `ENOSYS`). glibc's `fork` and `posix_spawn` use `clone`, so they get `EPERM`. The raw `fork` (57) and `vfork` (58) are not on the list, so the default action applies: the kill in production, `EPERM` in the errno test. The IFC policy allows fork-like `clone` (decision 5). `execve` and `execveat` are not on any list except Blender's, and there only for its own binary.
- **Always allowed:** `exit`, `exit_group`, `rt_sigreturn`.
- **Never allowed for any helper:** `ptrace`, `process_vm_readv`, `process_vm_writev`, `io_uring_*`, `bpf`, `mount`, `umount2`, `pivot_root`, `chroot`, `unshare`, `setns`, `perf_event_open`, `keyctl` and `add_key`. The allow-list omits them, so they are killed.
- **Architecture check.** The filter kills the process if the architecture is not `AUDIT_ARCH_X86_64`. The platform is x86_64 only (section 1). An aarch64 table is needed only if that platform is supported.

### 4.4 The Blender exec path

Blender is started by `fork` and `exec`, so the policy is also in force for the program's own startup. Three findings from the measurements set the list:

- The dynamic loader and libc startup make calls that a trace started after the constructor never sees: `open`, `brk`, `writev`, `arch_prctl`, `set_tid_address` and `getrandom` (§6.6). The list adds them, with `execve`, to the 51 calls measured in the trace.
- Blender links `libsycl` from the Intel oneAPI tree under `/opt/intel`. The loader needs read and execute there, and the trace could not show that because the loader ran before the constructor.
- Landlock limits the `exec` itself. With `rx /usr/bin/blender` as the only executable rule, `exec` of `/bin/sh` returns `EACCES` (measured, rc 127). In a run without seccomp, Blender's start-up tried a shell, which failed on the `/dev/null` redirection (the rule was missing). Under seccomp no such message appeared, which fits a refused fork-like `clone`. The spawner was not identified.

### 4.5 Deriving the allow-lists

Each allow-list is the union of the calls seen in traces of the helper, taken under every I/O setup the helper meets in production:

- standard input as a pipe (the application's `QProcess`), as a file and as a terminal or `/dev/null`;
- standard output and standard error as pipes, as files and as `/dev/null`;
- each input class in the corpus and the fault paths (for IFC, `--diagnose-from` with `--test-fault`).

The design named a test-only record filter (it would trap each call, log its number and path argument, and run it through a trampoline) and a test-only observe mode (it would log every call outside the list and run it, so the run finishes). **Neither was built.** The library has no record or observe mode, in the tests or in release builds (R109 S6, checked against the sources). A refused call is found in the kernel's audit log instead: a seccomp refusal that kills the process is a `type=1326` (SECCOMP) record, which names the process, its executable, the signal and the call number (`syscall=`). On a machine where the kernel log is readable, `journalctl -k | grep type=1326` lists them. The S6 notes say how the sanitizer checks used it.

Two gaps in trace-derived lists were found and are the reason for the rule. The first is that a list taken from one I/O setup misses calls that another setup makes (`fcntl` appeared only with a pipe on stdin). The second is that a trace which starts after the constructor misses the loader and libc startup. Both are covered by the union rule and by the CI check in §8.

### 4.6 Failure reporting

A sandbox kill is reported as `sandboxViolation`, a named reason separate from `crash`. The IFC restart path does not treat it as a parser fault, so it does not restart with element exclusions (§1). The SKP launcher and the Blender job should treat it the same way (proposed). The message names the helper and the layer, not the syscall, to keep the report bounded.

## 5. Per-helper policy

Filesystem paths are the rules measured in the prototype. `{private}` is the helper's private directory. The syscall counts are the allow-list sizes that were measured, before the CI check and the corpus extension in §8.

| Helper | Filesystem | Network | Exec | Process and threads | Syscalls (allow-list) |
|---|---|---|---|---|---|
| SKP (`--import`, `--probe`) | `rw {private}` (input copy, output package). Nothing else | None: no `socket` entry | None | Single thread in the traces. Thread `clone` allowed, fork-like `clone` `EPERM` | 19 measured: read, write, close, fstat, lseek, mmap, brk, rt_sigaction, rt_sigprocmask, writev, madvise, prctl, futex, clock_nanosleep, exit_group, openat, set_robust_list, prlimit64, rseq |
| IFC and STEP import (`--import`, `--import-step`) | `rx /usr/lib/sketchyup` (helper and its IfcOpenShell plugins, in production); `rx /usr/lib` (OCCT libraries, opened during the parse); `ro /etc`, `ro /proc`, `ro /sys`; `rw {private}` | None | None | Threads (`--threads` is at most 4). Fork-like `clone` allowed for diagnosis children, which inherit the policy and do not exec | 31 measured (25 for import, plus 6 for diagnosis: alarm, getpid, wait4, getppid, gettid, tgkill) |
| IFC and STEP export and check | Same as import, plus the export package read. Not measured (§6.9) | None | None | As import | Not measured. Expected to be the import list, plus any calls the export path uses |
| Text | `rx /usr/lib/qt6`; `ro /usr/share/qt6`, `/usr/share/fonts`, `/usr/share/fontconfig`, `/etc/fonts`, `/var/cache/fontconfig`, `/etc/xdg`, `/etc/ssl`, `/usr/lib/locale`, `/usr/share/locale`, `/usr/lib/gconv`; `rw {private}` as `HOME`, `XDG_CACHE_HOME`, `XDG_CONFIG_HOME` and `TMPDIR` | None | None | Threads (Qt) | 27 measured (adds eventfd2, getrandom, statx, newfstatat, fstatfs, readlink, getdents64 and others to the SKP-style set) |
| Extension | `ro /usr/lib/locale`, `/usr/lib/gconv`, `/usr/share/locale`, `/etc/ssl`. No write rule | None | None | None observed | 17 measured (read, write, close, fstat, lseek, mmap, brk, getpid, fcntl, getuid, geteuid, gettid, futex, exit_group, openat, eventfd2, statx) |
| Blender (CPU and GPU, render or probe) | `rx /usr/lib`, `rx /opt/intel`, `rx /usr/bin/blender`; `ro /usr/share`, `/etc`, `/sys`, `/proc`, `/dev/dri`; `rw /dev/dri/renderD128`, `rw /dev/null`; `rw {private}` as `HOME`, `XDG_*`, `TMPDIR` and `BLENDER_USER_*` | `socket` and `bind` `EPERM`. `--offline-mode` passed (§6.6) | `execve` allowed only for `/usr/bin/blender` by the Landlock rule; `/bin/sh` `EACCES`. Fork-like `clone` `EPERM` | Threads (`--threads 4`) | 58 measured in the exec flow: the 51 from the trace, plus execve, open, brk, writev, arch_prctl, set_tid_address and getrandom |

## 6. Measured results

All measurements ran on 2026-10-26 on this machine: kernel `7.2.8-9-omarchy`, x86_64, Omarchy 4.0.6 on Arch. Docker 29.9.2 ran the pinned image `sketchyup-ci-pinned:r097a-ifc`, which has Arch packages for OCCT 7.9.3, Qt 6.11.2, GCC 16.2.1 and Blender 5.2.2. The IfcOpenShell prefix (0.9.0, pinned commit `7ba70197`) was built from a clean `git archive` of `d65a617d` inside that image. The prototype was throwaway and is not committed. Its scripts are described here, not published. Its size is listed in §6.10.

### 6.1 Method

- A preload library (`libsbox.so`, about 420 lines of C, not committed) applies the policy named in an environment variable from a constructor, before `main`. It implements the record filter, the allow-list and deny-list filters, the observe mode, the Landlock rules, the failure policy and the launcher used in §6.6.
- Helpers ran unmodified. Each input was copied into one private directory, reused by every configuration, so the output packages record the same source path.
- Seven configurations per input: unsandboxed baseline; `PR_SET_NO_NEW_PRIVS` only; seccomp allow-list only; Landlock only; allow-list with errno default plus Landlock; allow-list with kill default plus Landlock; deny-list (ADR 0180 style) plus Landlock.
- Output identity is a SHA-256 of the package or of the normalised JSON. The text worker's `documentId` is random per run, so it is removed before hashing.

### 6.2 Mechanism facts

| Check | Host | Pinned image, default profile | Pinned image, `seccomp=unconfined` |
|---|---|---|---|
| Landlock ABI (`landlock_create_ruleset`, version flag) | 10 | 10 | 10 |
| `clone3(NULL, 0)` | `EINVAL` (reached the kernel) | `ENOSYS` | `EINVAL` |
| `unshare(CLONE_NEWUSER)` | ok | `EPERM` | ok |
| `unshare(CLONE_NEWNET)` | ok | `EPERM` | ok |
| `seccomp(GET_ACTION_AVAIL)` | ok | ok | ok |
| `PR_SET_NO_NEW_PRIVS` | ok | ok | ok |
| `socket(AF_INET)` | ok | ok | ok |

Also on the host: `unshare -n true` fails with `EPERM`, and `unshare -Urn true` succeeds. This settles the question ADR 0180 §3.5 left open: the failure was not the session's sandbox. `unshare -n` needs `CAP_SYS_ADMIN` in the initial user namespace. The pinned image has no `/sys/kernel/security`, so its LSM list cannot be read from inside. The CI check has to use the Landlock ABI call (§8).

### 6.3 Helper matrices

Each cell is "identical output as the unsandboxed baseline / inputs run".

| Configuration | SKP (12) | Text (3) | Extension (1) | Blender (2 probes and 1 render) | IFC and STEP (9) |
|---|---|---|---|---|---|
| Baseline | 12 / 12 | 3 / 3 | 1 / 1 | 2 / 2, render ok | 9 / 9 |
| NNP only | 12 / 12 | 3 / 3 | 1 / 1 | 2 / 2, render ok | 9 / 9 |
| Seccomp allow-list only | 12 / 12 | 2 / 3 * | 1 / 1 | 2 / 2, render ok | 9 / 9 |
| Landlock only | 12 / 12 | 3 / 3 | 1 / 1 | 2 / 2, render ok | 9 / 9 |
| Allow-list, errno default, + Landlock | 12 / 12 | 2 / 3 * | 1 / 1 | 2 / 2, render ok | 9 / 9 |
| **Allow-list, kill default, + Landlock** | **12 / 12** | **2 / 3 ***  | **1 / 1** | **2 / 2, render ok** | **9 / 9** |
| Deny-list (ADR 0180), + Landlock | 12 / 12 | 2 / 3 * | 1 / 1 | 2 / 2, render ok | 9 / 9 |

\* The third text request (mixed Latin, CJK and Greek, which triggers font fallback) produces different output between two unsandboxed runs, so its baseline is not reproducible. It returns rc 0 in every configuration. The other two requests match in every configuration.

Blender's render output is a PNG that differs between two unsandboxed runs of the same scene. The render therefore counts as successful when it exits 0 and writes a PNG.

**IFC and STEP coverage.** All 74 committed fixtures (60 IFC, 14 STEP) run under the kill allow-list with Landlock. Every return code and every package matches the unsandboxed baseline: 52 IFC succeed, 6 IFC are refused, 2 IFC crash with SIGSEGV in both runs, 13 STEP succeed and 1 STEP is refused. The 9-input matrix above used the allow-list derived from the trace of those 9. The coverage run is a stronger test than the matrix alone.

**Extension worker.** Its first list was missing `fcntl`. With stdin as a pipe, the trace shows `fcntl`. A trace taken with a file on stdin did not. The kill default killed the worker, while the errno default passed silently, because the call returned `EPERM` and the worker did not depend on it. The corrected list passes in every configuration.

### 6.4 Negative controls

- **SKP without the Landlock rule.** The helper refuses with rc 3, and the message is "The file could not be opened" under the code `tooLarge` (the failed read's message is set at `src/skp/import.cpp:30`, and `src/skp/import.cpp:228` reports every failed read as `tooLarge`). The denial works, and the reason is reported wrongly (§6.5).
- **SKP without `openat` on the allow-list.** Refused the same way, rc 3, `tooLarge`.
- **Blender under the kill default without the explicit `socket` and `bind` entries.** The process dies with SIGSYS (shell status 159). The urllib3 probe is the cause: the trace shows `socket` and `bind` immediately before urllib3 is imported (the link to urllib3's IPv6 check is an inference). With the entries, Blender completes.
- **Attack probe in the pinned image.** Under an errno allow-list with Landlock, inside the pinned image and with Docker's default profile: `socket(AF_INET)`, `socket(AF_UNIX)`, `socketpair`, `fork`, `ptrace`, `io_uring_setup` and `execve` all return `EPERM`. Reading `/etc/hostname` succeeds, and creating a file in `/tmp` returns `EACCES`. Creating a file in the granted private directory succeeds.
- **Exec of `/bin/sh` through the Blender launcher.** `EACCES`, rc 127 (`execv: Permission denied`).

### 6.5 Defects the method found

1. **The extension worker's `fcntl`** (§6.3). A trace under one I/O setup missed it.
2. **`ioctl` through `isatty` when stdout is `/dev/null`.** Glibc calls `isatty` for a character device, which issues `ioctl(TCGETS)`. A regular file or a pipe does not. This was found by comparing outputs, not by the trace.
3. **IFC diagnosis changes its result when `fork` is refused.** With `--test-fault crash:42` and `--diagnose-from 0`, the unsandboxed run excludes element 42 and writes 4156 bytes. A run with `fork` refused, and with the other calls allowed, completes and writes 4527 bytes, which includes element 42. The helper does not stop, so the culprit is silently kept. The kill policy without `fork` killed it (SIGSYS), and the list needed `alarm`, `getpid`, `wait4` and `getppid` (from the diagnosis trace), and `gettid` and `tgkill` (from the observe run). With fork allowed and the 31-entry list, the output matches the unsandboxed run exactly.
4. **The SKP refusal code.** An open refused by the sandbox is reported as `tooLarge`: `src/skp/import.cpp:228` gives every failed read that code. The denial is correct and the message is wrong. An open failure should be `unreadable` or `sandboxViolation`.
5. **Blender's startup socket.** Under a kill default, the urllib3 IPv6 probe at import kills the process (§6.4).
6. **Blender's loader and libc startup.** `open`, `brk`, `writev`, `arch_prctl`, `set_tid_address` and `getrandom` were not in the trace, because the trace began after the constructor. The exec flow needs them (§6.6).
7. **Blender's `libsycl` under `/opt/intel`.** The same cause: it is read by the loader before any constructor runs (§6.6).

### 6.6 Blender through the launcher (apply, then exec)

The production flow applies the policy in the forked child and then `exec`s Blender. The prototype's launcher does the same. It applies the policy from a constructor and then calls `execv`.

- Policy: 58 allow entries (the 51 from the trace, plus `execve`, `open`, `brk`, `writev`, `arch_prctl`, `set_tid_address` and `getrandom`), `deny 41`, `deny 49`, `deny 57`, the kill default, and the Landlock rules in §5.
- Result under the **kill** default: `blender --background --offline-mode` runs the CPU probe and writes `result.json` (rc 0). Run twice, the output hash is `d25616d7e90c33be`, the same as the unsandboxed probe. The Cycles render also completes under the same policy, and writes a 3055-byte PNG (the same size as the unsandboxed render).
- The launcher's `exec` of `/bin/sh` returns `EACCES` under this policy.
- Blender's trace under the errno default, taken with gdb (which is outside the filter), shows exactly which calls the loader and libc startup need. gdb catches each syscall entry, so the kill policy is not needed for the trace. Seven calls are added to the 51 traced ones: `execve`, `open`, `brk`, `writev`, `arch_prctl`, `set_tid_address` and `getrandom`. The gdb trace shows the first five and the last two in the loader and libc start-up.

### 6.7 Failure policy

The four cases were run in the pinned image. The layer's failure was simulated by a custom seccomp profile: Landlock syscalls return `ENOSYS`, or `seccomp()` returns `EPERM`.

| Case | Result |
|---|---|
| Landlock unavailable, fail closed | rc 70. `sandboxUnavailable: landlock (Function not implemented)`. No parse. |
| Landlock unavailable, fail open | rc 0. Warning: "running with limits only". Output as the baseline. |
| `seccomp()` refused, fail closed | rc 70. `sandboxUnavailable: seccomp filter (Operation not permitted)`. |
| `seccomp()` refused, fail open | rc 0. Warning: "running with limits only". |

Fail-open is the choice that runs a parser with no sandbox while the user sees nothing but a warning. The measurements show that it is cheap to make closed (one exit code), and that the closed path can be tested in CI (§8).

### 6.8 Namespaces and bubblewrap

- `bwrap --unshare-all` (bwrap 0.13.1, host) with the probe: `socket(AF_INET)`, `socket(AF_UNIX)`, `socketpair`, `fork`, `ptrace` and `io_uring_setup` all succeed. The probe's `execve` printed no error, so it was not refused. Files outside the mounts return `ENOENT`. `connect` to 8.8.8.8:53 returns `ENETUNREACH`, and to 127.0.0.1:9 returns `ECONNREFUSED`.
- Startup: `bwrap --unshare-all ... /usr/bin/true` has a median of 3.0 ms over 50 runs.
- In the pinned image, `unshare(CLONE_NEWUSER)` returns `EPERM` under the default profile (§6.2). bwrap needs a user namespace when it is not root, so it cannot start there. This was inferred from the syscall result, and bwrap itself was not run in the container.

### 6.9 Not measured

- IFC and STEP export and check modes. The same policy is expected to apply, with a list to be derived.
- Blender video encoding (`encode`) and the handoff operation. Only the probe and the render were run.
- Landlock ABI 5 and 6 features. Device `ioctl` and abstract-socket scoping were not tested.
- `aarch64`. Not supported (section 1).
- Larger inputs. The largest SKP fixture is 10 KB (`attribute_reporting.skp`) and the largest IFC is about 1 MB (`bsi-ifc4-landscaping.ifc`, inside the 74-fixture coverage run). Allow-lists may grow on larger files (mremap, munmap, mprotect were absent from the SKP trace).
- Ubuntu 24.04 and 26.04 hosts. See §10.

### 6.10 Prototype size

About 309 MB at the end of the work, mostly build output (IfcOpenShell 110 MB, the host build 89 MB, a source copy 93 MB, the run outputs 17 MB). It was deleted after the measurements. It was never committed.

## 7. Failure policy

1. **Fail closed, for every layer.** A layer that cannot be installed causes a named refusal before any input is read: `sandboxUnavailable`, with the layer name and the errno. The helper exits with a dedicated code (the prototype uses 70).
2. **The refusal goes to the application as a failed import or run that leaves the document unchanged.** That is the existing contract (ADR 0171 §1.6, ADR 0136), now with a named reason.
3. **A sandbox kill is `sandboxViolation`, not `crash`.** It does not trigger the element-exclusion restart (§4.6, §1).
4. **Fail open is not offered in 1.x** (decision 1). If the owner wants it, it must be a visible per-installation setting, shown as a named notice, and it cannot be the default.
5. **Blender's job fails before the process starts** when the child policy cannot be prepared. It reports `PREPARE_FAILED` with the layer named.
6. **A probe that cannot run sandboxed** reports itself unavailable. It does not run unsandboxed.

## 8. Tests and the CI check

Two tiers, matching ADR 0170 and the CI time directive in the owner's notes (merge on the single `CI verdict` check; full tier at release and nightly). The fast tier is seconds. The full tier is minutes.

**Fast tier (in the pinned image, default profile):**

- `sandbox_available`: calls `landlock_create_ruleset` with the version flag, requires ABI 1 or more, and checks `seccomp(GET_ACTION_AVAIL)` and `PR_SET_NO_NEW_PRIVS`. A failure is a named refusal, and the job is red. It does not read `/sys/kernel/security/lsm`, which the container does not have.
- `sandbox_attack_probe`: the attack set in §6.4, run under the errno policy. It requires `EPERM` for socket, socketpair, fork, ptrace, io_uring and execve, and `EACCES` for files outside the granted paths, while granted paths still work.
- `sandbox_one_per_helper`: one fixture per helper under the kill allow-list, identical output to the unsandboxed run. Text: the Latin request only. Blender: the CPU probe through the launcher.

**Full tier (nightly and release):**

- Every corpus file for each helper under the kill allow-list (74 IFC and STEP; 12 SKP; the text Latin requests; the extension panel; the Blender probes and the render), identical output and identical return codes.
- The IFC diagnosis case (`--test-fault crash:<id>`, `--diagnose-from 0`), identical to the unsandboxed run.
- **Trace coverage.** Designed as observe-mode runs over the corpus, under each I/O setup in §4.5, that report no call outside the allow-list. Not built: observe mode does not exist (§4.5). The suites cover the same ground in another way. A call outside the list kills the helper, and each corpus run compares its output with the unsandboxed run, so a stale list fails its suite for any call the suite makes. The `fcntl` and `isatty` cases were found by comparing outputs (§6.5), not by a trace.
- **Negative controls.** Blender's kill policy without the socket entries must die with SIGSYS, which shows the kill default is active. A missing Landlock rule must refuse.

**Kernel requirements, pinned image.** The image provides Landlock, seccomp, `no_new_privs` and the `clone3` fallback. It does not provide user namespaces. Landlock is a kernel feature: the runner's kernel must have `CONFIG_SECURITY_LANDLOCK` and `landlock` in its boot LSM list. That is not verified for `ubuntu-latest` (§10).

**Where the checks run.** The fast tier runs in the pinned image, which has Blender 5.2.2 and libseccomp, so the Blender checks run in CI as well. The Blender binary is at `/usr/sbin/blender` in the image, so the rule path in §5 is `/usr/sbin/blender` there.

## 9. Budgets

| Item | Measured | Proposed budget |
|---|---|---|
| Install, per helper process (preload, `/bin/true`, median) | Empty filter with NNP: 0.67 ms, against 0.44 ms with no preload. 51-entry allow-list with NNP: 0.84 ms. Three Landlock rules with NNP: 0.59 ms | ≤ 1 ms, measured in CI |
| Per syscall, allowed call (`getpid`, 2M iterations) | 106–116 ns without a policy, 127–137 ns with the 51-entry list (+~20 ns) | ≤ 50 ns added per call |
| BPF program size | About two instructions per allowed call (8 bytes each). The 58-entry Blender list is about 1 KB | Below the kernel limit of 4096 instructions, with a CI check |
| Wall time on the corpus | SKP: 5 ms with and without. Blender probe: 0.7–0.9 s with and without (noise). IFC import: 0.33 s baseline, 0.37 s kill (inside Docker, so noise is large) | No more than 3% on the full corpus, within run-to-run noise |
| Landlock ruleset | One to twelve path rules, opened as `O_PATH` in the parent | Negligible; the rule count is checked in CI |

The per-syscall cost matters most for helpers that make many calls, such as the IFC import with four threads. At +20 ns per call, a helper making one million calls pays about 20 ms, which is small beside the 120-second deadline.

## 10. Risks

1. **An allow-list is incomplete for a path the corpus does not exercise.** The effect is a SIGSYS, which is visible, and the CI trace check (§8) is meant to catch it before merge. Corpus coverage is still limited: 12 SKP files, 9 IFC files in the matrix (74 in the coverage run), 3 text requests, 1 extension request, and 3 Blender tasks.
2. **The CI host kernel may not have Landlock.** `ubuntu-latest` is moving from Ubuntu 24.04 to 26.04. The runner-images issue says the rollout begins on 19 October 2026 and completes by 19 November 2026 ([actions/runner-images#14748](https://github.com/actions/runner-images/issues/14748), read through the GitHub API on 2026-10-26). Neither image's `CONFIG_SECURITY_LANDLOCK` or LSM list has been checked. The required check is the way to find out.
3. **The Docker default profile may change.** The profile (moby, `profiles/seccomp/default.json`, read 2026-10-26 through the research pass) allows the Landlock syscalls, and has done since a commit dated 2022-01-31 (`0d25e436f8`). Future changes could block them. The fast tier fails in that case, and it does not fall back.
4. **Landlock ABI differences.** The design uses ABI 1 rules only. The scoping, TCP and device rules that later ABIs add are optional and were not measured.
5. **Device ioctl is not filtered.** Blender's GPU path opens `/dev/dri/renderD128` and calls `ioctl`. A bug in a driver can be reached through that. Landlock's device ioctl right (ABI 5) would limit it, and is a candidate for a later change.
6. **The sandbox does not stop memory corruption.** A corrupted parser can still write its output and return a crafted package. The checked decoders (ADR 0176 §3), the bounded packages and fuzzing remain the defence.
7. **Seccomp is per thread.** The filter must be installed before any helper thread is created. The designs in §4.1 install it before the threads start. A thread created earlier would not have it.
8. **The text worker's output is not reproducible for mixed-script text.** Its tests must use Latin requests for byte equality. The mixed-script request checks only the return code.
9. **Blender's own children.** The policy refuses fork-like `clone`, so Blender cannot start a shell or helper program. That is intended, but a Blender feature that needs a subprocess will then fail. The probe and render do not use one.
10. **Export and check modes are not measured.** The IFC export and check paths call the same binary with the same restrictions expected. Their allow-lists are not derived.
11. **Developer probe modes ship in release binaries.** Under this design they are sandboxed (decision 7). Removing them from release builds is the other option.
12. **Unknown Ubuntu default for the AppArmor userns restriction.** Not relevant to this design, which uses no user namespace. It would matter if bubblewrap were adopted. The Ubuntu 24.04 release notes say the restriction was "not enabled by default" in 23.10 ([release notes](https://discourse.ubuntu.com/t/noble-numbat-release-notes/39890), read 2026-10-26). Its default for 24.04 was not verified.

## 11. Implementation split

One slice per helper, after a shared slice. Each slice records its measured numbers in an implementation-notes section of this ADR, as ADR 0176 does.

- **S0, shared library (`src/sandbox/`).** NNP, the Landlock ruleset builder and ABI probe, the raw BPF assembler with the x86_64 table, the allow-list and explicit-entry model, the kill default, clone and clone3 rules, the failure reporter (`sandboxUnavailable` and `sandboxViolation`) and `sandbox_available` (built in S6). The record and observe modes named here were not built (§4.5). Gate: the attack probe and `sandbox_available` pass in the pinned image and on the host.
- **S1, SKP (`src/skp/worker_main.cpp`).** Apply before `runImport` and `runProbe`. Allow-list of 19, `rw {private}`. Fix the `tooLarge` code for a failed read (`src/skp/import.cpp:228`). Gate: 12 fixtures identical under kill; probe modes sandboxed.
- **S2, IFC and STEP (`src/ifc/worker_main.cpp`, `src/io/ifc_worker.cpp`).** Apply before every mode that opens a file: import, export, check, STEP import and export, and the probe modes. Allow-list of 31, with fork-like `clone` for diagnosis. `rx` on the plugin directory and `/usr/lib`. Classify a sandbox kill as `sandboxViolation` (`ifc_worker.cpp:281–282`). Gate: 74 fixtures identical; diagnosis case identical; export and check measured.
- **S3, text (`src/text/worker_main.cpp`, `src/text/text_worker.cpp`).** Apply in `main` before `QGuiApplication`. Set `HOME`, `XDG_*` and `TMPDIR` to the private directory (`text_worker.cpp:188–190`). Allow-list of 27. Gate: Latin requests identical; the mixed request returns 0.
- **S4, extension (`src/automation/extension_worker_main.cpp`).** Apply at the top of `main`. Allow-list of 17, no write rule. Gate: panel fixture identical under all three stdio setups.
- **S5, Blender job (`src/integrations/blender_job.cpp`).** Build the BPF program and the Landlock descriptors before the fork. The child modifier issues only syscalls, then `exec`s Blender. Environment: `ALSOFT_DRIVERS=null`, `HOME`, `XDG_*` and `TMPDIR` to the attempt directory (`blender_job.cpp:568–578`). Add `--offline-mode` (line 583–586). Allow-list of 58 and the rules in §5. Gate: probes identical and the render writes a PNG through the launcher path; `/bin/sh` refused; the negative control dies.
- **S6, CI.** Built: the required `sandbox_available` check, the per-helper selection in the path map, and the sanitized `sandbox` suite. The nightly trace check was not built; the nightly runs the full tier (see Implementation notes (R109 S6)). The 24.04 and 26.04 behaviour is still to be measured on those runners.

No slice starts before the owner accepts this ADR. Slices S1 to S5 can run in any order after S0. S6 needs at least S0.

## Corrections owed

These corrections were listed at acceptance and not made in this record. Each item below says where it was made, or why it is moot (R109 S6 checked them all). ADR 0180 is listed as well, because the measurements contradict two of its statements.

**ADR 0171, §1.6 and decision 19 (`docs/decisions/0171-ifc-exchange-subset.md`). Done (S2, and S6 for the last pointer).**
- Line 410: "A real sandbox … is possible on current kernels with a fallback to resource limits only." The fallback to limits is the fail-open choice. This ADR proposes fail closed (decision 1). Replace the sentence with a pointer to this ADR, and say the fallback is not part of the design.
- Lines 404–406: the list of isolation layers, including `PR_SET_NO_NEW_PRIVS`, is true for import, check, export and STEP package only. The probe modes and `--import-step` without `--output` do not set it (`src/ifc/worker_main.cpp:85–88, 158–203, 245`). Say so.
- Line 409, "it is not a sandbox": correct. Keep.
- Line 1062 ("until the Landlock and seccomp hardening of decision 19 exists") and lines 1161, 1249 and 1354 ("later hardening"): add the status, "ADR 0183, proposed".

**ADR 0176 (`docs/decisions/0176-skp-import.md`). Done (S1, PR #470 for the refusal code).**
- Line 104 (§1): the list of isolation layers is right for `--import`, but not for `--probe`, which runs with no `PR_SET_NO_NEW_PRIVS` and no limits (`src/skp/worker_main.cpp:70–73`; `src/skp/import.cpp:118`). Add that.
- Line 278 (decision 2): "run the parser only in a new `sketchyup-skp-worker` helper (sandboxed as in ADR 0171 §1.6)". The helper is not sandboxed. Replace "sandboxed" with "resource-limited, as in ADR 0171 §1.6, and not a sandbox until ADR 0183 is implemented".
- The import refusal for a sandbox-denied open is reported as `tooLarge` (`src/skp/import.cpp:228`). The reason code needs to change when S1 lands. This is a defect, not only a wording change.

**ADR 0136 (`docs/decisions/0136-bounded-extension-worker.md`). Done (S4).**
- Lines 24–27: "The child uses a minimal environment … This is a process-failure and command-validation boundary, not an OS sandbox for the installed application binary." The environment scrub limits what the child inherits: credentials and plugin configuration. It does not limit files or sockets. The child keeps the user's file and network access. It has no rlimit, `setsid`, `PDEATHSIG` or `NO_NEW_PRIVS` (`src/automation/extension_worker.cpp:90–131`). Say that plainly.
- Lines 10–17 (protocol, bounded reads, deadline): accurate; no change.

**ADR 0180 (branch `docs/r106-dwg-feasibility`, commit `5c3edcf0`). Moot until DWG is reopened.** ADR 0180 is deferred indefinitely (2026-10-27). Its §3.2, §3.3 and §3.5 corrections are not made, and a line in ADR 0180 says so.
- §3.3 (line 171): "the helper's own shared libraries are loaded before the restriction is applied." True for libraries loaded at start-up. Not true for plugins loaded during the parse: the IFC helper opened `/build/ifc-prefix/lib` and `/usr/lib/libTK*.so.7.9` after start-up (§6.3 trace). The DWG helper needs a read and execute rule for any library it loads lazily.
- §3.5 (line 191): the cause of `unshare -n` failing is not known in that ADR. It is now known: `unshare -n` without `-U` needs `CAP_SYS_ADMIN`, and `unshare -Urn` works (§6.2).
- §3.2 point 2 (lines 156–162): "Its architecture is checked first (x86_64 and aarch64)". The supported platform is x86_64 (`docs/SUPPORT_MATRIX.md`, line 12). An aarch64 table is needed only if that platform is added.

## Owner decisions

Each has a recommended default. The owner accepts or changes each.

1. **Failure policy: fail closed for every layer, in every helper, with no fail-open setting in 1.x.** *Recommended: yes.* (§7, §6.7.)
2. **Mechanism: `PR_SET_NO_NEW_PRIVS`, a Landlock ruleset, and a seccomp-bpf allow-list written as a raw BPF program in-tree, with no new library.** *Recommended: yes.* The alternative is libseccomp (LGPL-2.1), which removes the per-architecture table but adds a licensed dependency to the helper's build and package notices. (§3.)
3. **Namespaces and bubblewrap are not used.** *Recommended: yes.* They are unavailable in the CI container under the default profile (§6.2) and they do not deny exec or socket creation (§6.8). Keeping them out of 1.x also avoids the Ubuntu userns policy question.
4. **Kill default, with explicit `EPERM` entries only for the calls named in §4.3.** *Recommended: yes.* The alternative, errno default, passes silently when a call is missing (§6.3, extension worker).
5. **IFC allows fork-like `clone` for diagnosis children, which inherit the policy.** *Recommended: yes.* Refusing `fork` changes the IFC output (§6.5, item 3). Exec stays denied.
6. **Blender: sockets refused, `ALSOFT_DRIVERS=null`, `--offline-mode`, private `HOME` and XDG directories, and an exec flow that allows only Blender's own binary.** *Recommended: yes.* All of these were measured (§6.4 to §6.6).
7. **Developer probe modes (`--probe`, `--probe-api`, `--probe-step`, `--probe-write`, `--write-step`) are sandboxed too, not removed.** *Recommended: yes, and keep them.* The alternative is to remove them from release builds. That is a smaller binary and less surface, and the owner may prefer it.
8. **A sandbox kill is reported as `sandboxViolation` and does not trigger the crash-restart path.** *Recommended: yes.* (§4.6, §1.)
9. **CI: `sandbox_available` is a required check. It fails when the runner has no Landlock, and nothing skips it.** *Recommended: yes.* Test on both `ubuntu-latest` images as they roll out (§10, item 2).
10. **Retrofit scope: one shared slice (S0), then one slice per helper (S1 to S5), and CI (S6).** *Recommended: yes.* This is the split ADR 0180 §3.6 asked for.
11. **Landlock scope features (abstract UNIX sockets and signals, ABI 6) are added when available, and not required.** *Recommended: yes.* They were not measured here. The design does not depend on them.
12. **Roadmap ID.** No roadmap row exists for this work. *Owner to assign an ID (R10x) if the ADR is accepted.* This PR does not add one.

## Sources

Every source was read on 2026-10-26 unless noted. Dates in brackets are the page's own date where the page shows one.

- ADR 0180, `docs/decisions/0180-dwg-feasibility.md` on branch `docs/r106-dwg-feasibility`, commit `5c3edcf0`, Proposed 2026-10-26 (read with `git show`).
- ADR 0171, 0176 and 0136, and the code cited, in this worktree at `d65a617d` (main), 2026-10-26.
- `docs/SUPPORT_MATRIX.md` (line 12) and `.github/workflows/native.yml` (lines 62, 114–116, 38 and 47), this worktree, 2026-10-26.
- Landlock kernel documentation, <https://docs.kernel.org/userspace-api/landlock.html> [page dated 2026-08 by the research pass], read through the research pass and ADR 0180.
- seccomp(2), man-pages, <https://man7.org/linux/man-pages/man2/seccomp.2.html> [footer 2026-06-05, per ADR 0180], read through the research pass.
- Docker default seccomp profile, moby profiles, <https://github.com/moby/profiles/blob/main/seccomp/default.json>, pinned commit `6fe7deb1b9` [committed 2026-09-17], and the Landlock addition <https://github.com/moby/profiles/commit/0d25e436f8> [2022-01-31]. Read through the GitHub API by the research pass. The measured container results in §6.2 agree with it.
- GitHub runner images, Ubuntu 24.04 image readme, <https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2404-Readme.md> (Docker Server 28.0.4), read through the research pass.
- actions/runner-images issue 14748, <https://github.com/actions/runner-images/issues/14748>, read through the GitHub API, updated 2026-10-26.
- ArchWiki, Security, <https://wiki.archlinux.org/title/Security> (unprivileged user namespaces enabled by default on official kernels except linux-hardened), read through the research pass.
- bubblewrap, <https://github.com/containers/bubblewrap> (README and `bwrap.xml`), release v0.13.1 [2026-10-14], read through the research pass. The installed version here is 0.13.1.
- libseccomp, release v2.6.1 <https://github.com/seccomp/libseccomp/releases/tag/v2.6.1> [2026-07-01], licence LGPL-2.1, and Arch core package 2.6.0-1 <https://archlinux.org/packages/core/x86_64/libseccomp/> (read through the research pass). The installed version here is 2.6.0.
- Ubuntu 24.04 release notes, <https://discourse.ubuntu.com/t/noble-numbat-release-notes/39890>, read through the research pass. The default for 24.04 is not verified.
- Blender 5.2 manual, command-line options and preferences, read through the research pass (branch `blender-v5.2-release` of the manual repository, <https://projects.blender.org/blender/blender-manual>). `--offline-mode`, `--factory-startup`, `--disable-autoexec` and `--background` were then run on the installed Blender 5.2.2 LTS.
- Blender system requirements, <https://www.blender.org/download/requirements/>, read through the research pass.
- Measurements in §6 were taken on this machine on 2026-10-26, with the prototype described in §6.1 and §6.10. The prototype is not committed. The numbers are the only copy of its results.

## Implementation notes (R109 S0, S1)

Recorded 2026-10-27 on branch `feat/r109a-sandbox-skp`, based on `origin/main` at `04f80ca9`. This section covers the shared library (S0, `src/sandbox/`) and the `.skp` helper (S1). S2 to S6 are not covered here. The design above is unchanged except where a point below says so.

**Kernel and budgets (measured on this machine, kernel 7.2.8-9-omarchy).**

- Landlock ABI 10, read with the version flag (`landlock_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION)`). The runner's ABI is not known yet; the first `skp_sandbox` run on the runner records it, and S6 makes it a required check.
- The SKP policy compiles to 67 instructions, 536 bytes, from `buildProgram` (the program's length is read from the code, not counted by hand). That is far below the 4096 limit of §9.
- Install cost, the median of 300 forks: fork and exit 134.8 µs; fork, apply the SKP policy and exit 307.2 µs. The install costs about 172 µs, under the 1 ms budget.
- Per allowed call (`lseek`, 2 million calls): 59.9 ns without a filter and 66.5 ns with the SKP policy. That is +6.6 ns, under the 50 ns budget.

**The allow-list, re-derived (SKP).** The helper's calls are the calls made from `main` onward. The loader's calls before `main` are not filtered, because the filter goes in at the top of `main`.

- Traced with gdb, from `main`, on all 12 fixtures in each of the three modes (`--import`, `--probe`, `--dry-run`), with `--deadline-seconds 0`. The calls seen were `read`, `write`, `writev`, `close`, `fstat`, `lseek`, `openat`, `brk`, `prlimit64` and `exit_group`. The trace could not follow the watch thread: gdb's catch loop stops when the thread is created. So the thread's calls were not traced.
- The watch thread (the default deadline) was measured another way: a driver creates, sleeps and joins a thread under the policy, and each entry is removed in turn. Removing `mmap`, `madvise`, `mprotect`, `munmap`, `rseq`, `set_robust_list`, `futex`, `rt_sigprocmask`, `rt_sigaction` or `clock_nanosleep` kills the thread. `clone` (thread-only) and `clone3` (ENOSYS) are the thread's creation calls.
- The ADR's list of 19 differs from the final list in these ways. `prctl` is dropped: it runs before the filter, and no call after the filter uses it. `mprotect` and `munmap` are added, because the watch thread needs both; the ADR's list had neither. `clone` (thread-only) and `clone3` (ENOSYS) are added, because the watch thread creates a thread. `mremap` is added with no trace: no fixture reaches the size at which glibc uses it, so it is kept for large inputs (§6.9). `getpid` and `gettid` are added, and `tgkill` (below). The library always allows `exit` and `rt_sigreturn`.
- The final SKP list has 23 named calls allowed, and `clone` (thread-only), `clone3` (ENOSYS) and `tgkill` (own process only). There are no explicit EPERM entries: §5 says SKP has no network row, so `socket`, `execve` and a raw `fork` are killed (SIGSYS).

**A change to §5: `tgkill` is allowed for the helper's own process only.** glibc's `abort()` signals itself with `tgkill`, and reads its thread and process IDs with `gettid` and `getpid`. With the kill default alone, a parser's `abort()` died with SIGSYS. The host would then have reported a crash as `sandboxViolation`. The new action `ownProcessOnly` allows `tgkill` when its first argument is this process, and returns EPERM for any other process. This keeps the threat model's rule that a helper cannot signal other processes of the user (§2). The test checks both cases: an abort is SIGABRT, and a signal to another process is EPERM.

**Sanitizer builds.** Every sanitizer build (ASan and UBSan) and the fuzzer build set `SKP_WORKER_NO_ADDRESS_LIMIT`. Under those builds the policy adds four calls that the runtimes make after `main`: `pipe2` (UBSan's type cache, on a first vptr check in `readFile`), `sigaltstack` (ASan's stack for the watch thread), `sched_getaffinity` (ASan's thread bounds) and `fcntl`. A release build does not carry them. The sanitized helper also defines `__lsan_default_options` to return `detect_leaks=0`. The application starts the helper with an empty environment (`QProcessEnvironment` with only `LC_ALL`), so `ASAN_OPTIONS` never reached the helper. LeakSanitizer then checked for leaks at exit, using `ptrace`, which the sandbox refuses. CI's `detect_leaks=0` setting is now in the binary.

**Results.**

- Fixtures, release build: 12 of 12 for `--import` (package bytes and R line equal to the unsandboxed run of the same code, in this process), 12 of 12 for `--probe`, and 12 of 12 for `--dry-run`. The sanitized build matches the same tests. The clang ASan build of the fuzz tree also imports the five corpus fixtures that the fuzz step uses (5 of 5).
- Attack probes, under the SKP policy in a child: `socket` and `execve` are killed with SIGSYS; `fork` is EPERM; a read of `/etc/passwd` is EACCES; a write to a sibling of the private directory is EACCES; a write inside it works; the input file can be read; `abort` is SIGABRT.
- Fail closed, the real helper. Under a test filter that returns ENOSYS for the Landlock calls, the helper refuses with exit 70 and `sandboxUnavailable: landlock (Function not implemented)`. Under a filter that returns EPERM for `seccomp`, it refuses with `sandboxUnavailable: seccomp filter (Operation not permitted)`. Neither writes a package. The layer is simulated through the test filter, which needs no kernel change (method of §6.7). The in-process simulations give the same reasons for all three layers.
- The host: a helper killed by SIGSYS is `sandboxViolation`. QProcess reports the signal as the exit code of a crash (31), which is how the host tells it apart. A helper killed by SIGABRT is still a crash (`workerFailed`). A helper that exits 70 with `sandboxUnavailable` is a refusal.
- CI check on the release tree: `skp_package`, `sandbox`, `skp_import`, `skp_sandbox`, `skp_mapping`, `skp_library_install` and `skp_library_source` pass. `skp_import_real` and `skp_mapping_real` skip, because the owner's corpus file is not on this machine (exit 77, as before). The sanitizer set passes: `skp_package`, `skp_import`, `skp_sandbox`, `skp_mapping`.

**Contradictions with this ADR, and what was decided.**

1. §5 (SKP: no socket entry, so kill) and §4.3 (explicit EPERM for `socket` in helpers that need no network) disagree for SKP. The code follows §5: a socket attempt kills the helper, which the host reports as `sandboxViolation`. The owner should confirm which is wanted for SKP.
2. §4.3 and §5 do not mention `tgkill`. The own-process rule above is new.
3. §6.4's SKP negative control ("refused with `tooLarge`") no longer applies: the refusal-code fix (PR #470) is merged, and a denied open is now `unreadable`.
4. The refusal names the layer as `no_new_privs`, `landlock` or `seccomp filter`. §6.7 used the last two and did not name NNP.

**Not done here.** The corrections owed to ADRs 0171, 0176, 0136 and 0180 (see Corrections owed) are not edited in this record. The IFC and STEP, text, extension and Blender helpers (S2 to S5) and the required `sandbox_available` check (S6) are not started. The `sketchyup_sandbox` library has no runner-side Landlock result yet, and the mremap path is not measured (§6.9).

## Implementation notes (R109 S2)

Recorded 2026-10-27 on branch `feat/r109s2-sandbox-ifc`, based on `origin/main` at `6dcea44a`. This section covers the IFC and STEP helper (S2). The shared library and the `.skp` helper (S0, S1) are as recorded above. S3 to S6 are not covered here. Where a point below differs from the design above, it says so.

**Measurement setup.** The pinned image `sketchyup-ci-pinned:r097a-ifc`, kernel 7.2.8-9-omarchy (Landlock ABI 10). The helper is a Debug build (the test configuration; CMake's `Debug`), so the install and the wall times below are Debug numbers. The IFC prefix was the cached one for this pin. Release numbers are not measured.

**The allow-list, re-derived from a trace.** The helper's calls were traced from `main()` on (a ptrace tracer that starts at `main`, so the loader and the static constructors are not recorded). The traced runs were: every IFC and STEP fixture in each mode the application uses (import with one and four threads, check, the probe API, the STEP import with and without a package, the STEP probe, the probes, the corpus writers) with stdout and stderr as pipes, files and `/dev/null`; the existing Python suites (`ifc_worker_probe`, `ifc_georeference`, `ifc_step_worker`, `ifc_export_worker`, `ifc_import_worker`, and the fault corpus with 4 mutations per file); the diagnosis (`--test-fault crash:69` with `--diagnose-from`, whose children fork, and `abort`, `alloc` and `hang`); and the application's C++ tests (`ifc_import`, `ifc_worker_run`, `ifc_export`, `ifc_step`, `ifc_step_export`, `step_import`, `ifc_package`, `import_refusal`), with the helper replaced by the traced build. The traces record the path of every `openat`, `newfstatat`, `readlink`, `access` and `execve`, the request of `ioctl` and the option of `prctl`.

The allow-list (`src/ifc/sandbox_policy.cpp`) has 39 named calls, and the library always allows `exit`, `exit_group` and `rt_sigreturn`:

- 30 allowed: `read`, `write`, `writev`, `close`, `fstat`, `lseek`, `openat`, `newfstatat`, `statx`, `readlink`, `brk`, `mmap`, `mprotect`, `madvise`, `munmap`, `mremap`, `futex`, `set_robust_list`, `rseq`, `clock_nanosleep`, `rt_sigaction`, `rt_sigprocmask`, `alarm`, `wait4`, `getpid`, `getppid`, `gettid`, `getuid`, `uname`, `times`.
- `clone` with `forkOrThread`: threads, and fork-like children for the diagnosis; a clone that asks for a new namespace (`CLONE_NEW*`) is EPERM.
- `clone3` ENOSYS, so that glibc falls back to `clone`.
- `tgkill` with `ownProcessOnly`: abort signals its own process.
- `prctl` with `whenArgument`: only `PR_SET_PDEATHSIG`, which the diagnosis children set.
- `ioctl` with `whenArgument`: only request `0x802c542a` (`TCGETS2`), which glibc's `isatty` asks on a character device (stdout as `/dev/null` or a terminal).
- `socket`, `socketpair`, `bind` and `connect` with `denied` (EPERM).

The file rules are: the plugin directory (the directory of `libifcopenshell.plugin.so`, from `pinPluginDirectory()`) and `/usr/lib`, read and execute; five single files, read only (`/etc/ld.so.cache`, `/etc/localtime`, `/proc/self/maps`, `/proc/sys/vm/overcommit_memory`, `/sys/devices/system/cpu/online`); the input, read only; the output's directory, read and write. A trace's single file is granted, not its directory: `/etc`, `/proc` and `/sys` are not granted, so `/etc/passwd` and the NSS state under `/run` are out of reach.

Sanitizer builds add `pipe2` (UBSan's type cache), `sigaltstack`, `sched_getaffinity` and `fcntl` (ASan), behind `SKP_WORKER_NO_ADDRESS_LIMIT`. Only `pipe2` appeared in the sanitized traces made here; the other three were taken from S1 and were not re-checked one by one. The sanitized helper also defines `__lsan_default_options` (detect_leaks=0), as S1 does.

Compared with the list of section 5 (31 calls for the import, measured before S2): the policy adds `statx`, `uname`, `times` and `getuid` (found in the STEP export and the STEP import, not in the import trace), `mremap` (no trace reaches it: kept for large buffers, as S1 does), `alarm`, `wait4`, `getppid`, `gettid` and `getpid` (the diagnosis children), `tgkill` for the own process, and the `whenArgument` entries. It adds the four network calls as EPERM, which section 4.3 prescribes and section 5 did not list for this helper (see the contradictions). `clone3` ENOSYS and the `forkOrThread` rule are in section 4.3's design.

**Results.**

- Fixtures, the sandboxed helper against the unsandboxed code in this process (`ifc_sandbox`, which passes): all 66 IFC files (60 in `tests/fixtures/ifc` and 6 in `tests/fixtures/ifc/export`) imported with four threads give the same return code, signal and R line apart from its byte count, and imported with one thread the full R line and the same package bytes too (at four threads the package lists elements in the order the threads finish, ADR 0171 F8, so its bytes vary from run to run; the first version compared them and failed once in CI, on `openings-hard.ifc` in run 40601593697; the package's size can differ too, by a byte on `tree-cases.ifc` in run 40603185313, so the four-thread R line is compared without its byte count); all 66 checked give the same; all 14 STEP files give the same import package, the same metadata and the same probe. Section 6.3 counted 74 (60 IFC and 14 STEP); the committed set is 66 IFC and 14 STEP. The 3 `.skifc` packages in `tests/fixtures/ifc/packages` are inputs to the import tests, and this test does not use them.
- The helper's own outcome classes over the same fixtures (tally on the sandboxed binary): IFC import 58 succeed, 6 refused, 2 crash with SIGSEGV (the two that crash in both runs in section 6.3); IFC check 39 valid, 27 reported invalid; STEP import 13 succeed, 1 refused.
- The Python suites (`ifc_worker_probe`, `ifc_georeference`, `ifc_step_worker`, `ifc_export_worker`, `ifc_import_worker`, `ifc_worker_fault_corpus` with 64 mutations per file) and the application's `ifc_*` suites pass under the sandbox. The sanitized helper (ASan and UBSan) passes the six Python suites under the sandbox.
- The diagnosis case (`extrusion-profiles.ifc`, `crash:69`, `--diagnose-from 0`): the same return code, signal, progress lines and R line as the unsandboxed run. The host's restart sequence (`--diagnose-from 1`, as `ifc_worker_run` runs it) works under the sandbox: the child for element 69 dies, and the parent imports the rest.
- Attack probes, under the policy in a child (`ifc_sandbox`): an exec is killed with SIGSYS; `ptrace` is killed with SIGSYS; a socket is EPERM; a fork-like child runs, and its exec is killed with SIGSYS; a clone into a new user namespace is EPERM; an `ioctl` other than `TCGETS2` is EPERM, and `TCGETS2` answers ENOTTY; a `prctl` other than `PR_SET_PDEATHSIG` is EPERM; a read of `/etc/passwd` and of a sibling file is EACCES; a write to the sibling is EACCES; a write in the private directory and a read of the input work; an `abort` is SIGABRT.
- Fail closed (`ifc_sandbox`): a simulated failure of each layer is named and returns 70. The real helper under a filter that refuses Landlock's three calls (ENOSYS) refuses with exit 70 and `sandboxUnavailable: landlock (Function not implemented)`; under a filter that refuses `seccomp` (EPERM) it refuses with `sandboxUnavailable: seccomp filter (Operation not permitted)`; the probe refuses too. None writes a package.
- Host (`ifc_sandbox`): a helper killed by SIGSYS is `sandboxViolation` and runs once (not restarted); a refusal with exit 70 is `sandboxUnavailable`; a helper killed by SIGABRT is `workerFailed` (a crash). The STEP host does the same.
- Export and check (in-process, Debug; medians; the policy applied in the child before the work):

| Measure | Unsandboxed | Sandboxed | Change |
|---|---|---|---|
| Install (fork plus apply, against fork alone; 300 forks) | 630 µs | 1,149 µs | +520 µs (budget 1 ms) |
| Per call, `getpid` (2 million calls, 5 runs) | 77 ns | 80 ns | +1 to +5 ns (budget 50 ns) |
| IFC import, 60 files, four threads, 5 rounds, total | 2.408 s | 2.461 s | +2.2% (budget 3%) |
| Check, `bsi-ifc4-arch.ifc`, 20 runs | 18.6 ms | 19.2 ms | +3.3% |
| STEP export, AP214, one box, 20 runs | 7.3 ms | 8.1 ms | +11% (+0.8 ms) |
| STEP export, AP203, one box, 20 runs | 7.8 ms | 8.8 ms | +12% (+0.9 ms) |

The export rows are small operations: the install cost (about half a millisecond) is most of the change. The corpus is within the 3% budget. The export overhead is above 3%, and the owner should decide whether to accept it. The helper's wall time with its real process startup was not timed separately.

- BPF size: about 100 instructions (about 800 bytes), counted from the block sizes of section 4.3 (the library does not print the program); the kernel limit is 4096.
- Landlock: nine path rules at most (two directories, five files, one input and one output).

**Contradictions with this ADR, and what was decided.**

1. **The network calls are EPERM for the IFC and STEP helper, not a kill.** Section 5 gives this helper no network row, and section 4.3 says helpers that need no network refuse `socket`, `socketpair`, `bind` and `connect` with EPERM. The brief asked for `socket` to be killed, as S1 does for the `.skp` helper. The trace shows that OpenCASCADE's AP203 writer calls `gethostbyname` (through `OSD_Host::InternetAddress`) when it writes the header, so a kill would fail every AP203 export. The IFC and STEP helper follows section 4.3. The `.skp` helper still kills (S1). The two helpers now differ, and the owner should choose one rule.
2. **The AP203 header names 0.0.0.0, not the machine's address.** OpenCASCADE 7.9.3 does not check the `gethostbyname` answer, so an EPERM there makes the writer crash with SIGSEGV (seen under gdb). The helper answers that one call itself (`gethostbyname` in `worker_main.cpp`, returning the unspecified address), and the header reads `PERSON('IP0.0.0,' ...)`. Before S2 it read the machine's address (for example `IP172.17.0,` in the container). This is an output change for AP203 exports only, and it removes the machine's address from the exported file. The owner should accept it or choose another fix.
3. **The own-process rule covers the helper's process only.** `ownProcessOnly` compares `tgkill`'s process with the process id that was read when the policy was built. A diagnosis child is a different process, so its `raise()` is EPERM. The test fault `crash` therefore raises a real fault (a store to address 0) in `geometry.cpp`: the kernel delivers SIGSEGV, and the child still dies, which is what the diagnosis needs. An `abort()` in a child is EPERM for the `tgkill`, and glibc then ends the child with its abort instruction. The child still dies, so the diagnosis is unchanged, but the signal is not SIGABRT. A fork-aware own-process rule would need a change to the library (not made here).
4. **Section 5's IFC file rules are narrower.** Section 5 lists `ro /etc`, `ro /proc` and `ro /sys`. This policy grants five single files and no directory under those trees (the brief requires `/etc/passwd` to be denied). The trace shows which files each mode reads (see the list above).
5. **The `clone` rule goes beyond section 4.3 by one bit.** A fork-like clone runs (decision 5), and a clone with a `CLONE_NEW*` namespace flag is EPERM. Decision 5 did not say so. A namespace would give the child capabilities inside it.
6. **`TCGETS2`, not `TCGETS`.** The pinned image's glibc asks `isatty` for `TCGETS2` (request `0x802c542a`). The policy allows that request only.
7. **The two developer directories are created before the policy.** `--probe-step --round-trip-directory DIR` and `--write-step DIR` create `DIR` in `main()` before they apply the policy, because a Landlock rule does not grant a path that does not exist. The trace's `mkdir` is therefore not in the list.

**Not done here, and gaps.**

- The full fault corpus (`ifc_worker_fault_corpus_full`, 512 mutations per file) was not run locally; the full tier runs it in CI.
- `ifc_sandbox` is not built or run in the sanitizer configuration: the IFC sanitizer preset builds only the helper (no CLI). The sanitized helper runs the six Python suites under the sandbox instead.
- The STEP export's output is not compared byte for byte (OpenCASCADE writes the current time in the header; the AP203 header differs by design, see contradiction 2). The export tests of the application check the export's content.
- Release builds are not measured.
- The Landlock check on the CI runner's kernel is not done (the required `sandbox_available` check is S6).
- An existing issue, not caused by S2: `--probe-api --threads 1` and `--threads 2` on `tests/fixtures/ifc/tree-cases.ifc` do not finish (both run and time out at 60 s, with and without the sandbox). The developer probe is affected; no import is.

**Sanitizer-only calls of the four-thread import (recorded 2026-10-27 on branch `fix/ifc-sanitized-sandbox-calls`, based on `origin/main` at `7ff74c51`).** CI's `ifc-sanitize` job failed on PR #490 (run 40608341414): `ifc_worker_probe` ran `--probe-api bsi-ifc4-arch.ifc --threads 4` and the helper exited −31, which is SIGSYS from this policy's kill default. The release build of the same run passed. The call is `sched_yield`, which the sanitized policy did not allow. The sanitizer runtime makes it inside `libasan` (the stack of the call has no frame in the helper's code), and it does so when the four threads of the import contend. With the unchanged sanitized policy, the probe command was killed in 34 of 100 runs in the pinned image (shell status 159).

To list every refused call, the sanitized helper was built with a test-only trace that is not committed. A refusal traps to a SIGSYS handler that prints one line (the call number and the first three arguments) and returns the errno the policy names, so the run goes on. Over the 100 probe runs, `sched_yield` was refused in 37 runs (1,872 refusals), and no other call was refused. The other four-thread modes gave the same single call, over 20 runs each: `--import --dry-run` on bsi-ifc4-arch (7 runs, 9 refusals), bsi-ifc4-landscaping (20 runs, 2,423), bsi-ifc4-road (18 runs, 1,561) and openings (9 runs, 13), and `--import --output` on bsi-ifc4-arch (6 runs, 8) and bsi-ifc4-landscaping (19 runs, 890). The `.skp` helper was traced the same way, with its watch thread, over 100 runs in five modes (`--probe` and `--import --dry-run` and `--import --output` on three fixtures): no refusal. The `.skp` policy is unchanged.

The fix adds `sched_yield` to the sanitizer-only set in `src/ifc/sandbox_policy.cpp`, behind `SKP_WORKER_NO_ADDRESS_LIMIT`. The call takes no arguments, so it has no filter. The release policy is unchanged. This supersedes the statement above that only `pipe2` was re-checked: the trace found `sched_yield`, which that list lacked. The four sanitizer calls already in the set (`pipe2`, `sigaltstack`, `sched_getaffinity` and `fcntl`) were allowed in the traced build, so the trace cannot show whether any of them is needed.

Verification on the fixed sanitized helper: the probe command 100 times, all exit 0 (34 killed before the fix); 20 runs each of `--import --dry-run` on openings, bsi-ifc4-arch, bsi-ifc4-landscaping and bsi-ifc4-road, and of `--import --output` on bsi-ifc4-landscaping, all exit 0. `ctest` on `ifc-sanitize` (`ifc_worker_probe`, `ifc_import_worker`, `ifc_georeference`, `ifc_export_worker`, `ifc_worker_fault_corpus` and `ifc_step_worker`) passed 6 of 6. The release `ifc_sandbox` test passed.

**Heap-use-after-free (superseded: fixed by #495).** The sanitized helper reported a heap-use-after-free on some four-thread imports. The sandbox was not involved. On openings.ifc it appeared in 3 of 140 runs (1 of 20 with the trace, and 2 of 120 untraced on the fixed helper). The read is in `iteratePass` (`src/ifc/geometry.cpp`) on the main thread. The block was allocated and freed on a worker thread, in IfcOpenShell's `logger::append`. The probe fixture (bsi-ifc4-arch) showed no such error in the 266 probe runs that finished. Dated 2026-10-27: after the merge of `origin/main` with #495 (`461e2de8`), the rebuilt helper showed no heap-use-after-free in 200 `--import --dry-run` runs of openings.ifc at four threads (3 of 140 runs before the fix) and no ASan report in 100 `--probe-api` runs of bsi-ifc4-arch.ifc, so #495's fix removed the use-after-free in these runs, and all 300 runs exited 0. Verification on main with #495 and #496 (2026-10-28, ifc-sanitize helper in the pinned image): `--import --dry-run openings.ifc --threads 4` 200 runs, 0 ASan reports and 0 use-after-free (before #495: 3 of 140); `--probe-api bsi-ifc4-arch.ifc --threads 4` 100 runs, 0 SIGSYS and 0 ASan reports; the six CI ifc-sanitize tests 6/6. Zero in 200 would occur about 1 time in 70 at the old ~2% rate: strong evidence, not proof. This paragraph supersedes the earlier note on the use-after-free, which said it was not fixed here.

**Decisions recorded (parent, 2026-10-27/28).** The parent decided these on S2. They are recorded here so that the notes and the code agree.

1. **Socket calls are EPERM, for every helper.** This is decision 4 and §4.3 as written, and it is the rule for all helpers. The IFC and STEP helper refused `socket`, `socketpair`, `bind` and `connect` with EPERM from S2 on. The `.skp` helper (S1) killed on a socket call until R109 S6, and the text and extension helpers (S3, S4) had no socket entry, so they were killed too. R109 S6 changes those three to EPERM (see the S6 notes).
2. **The AP203 header's `0.0.0.0` is accepted.** The writer's host-address lookup now gets the unspecified address, so the file no longer names the machine (contradiction 2 above).
3. **A diagnosis child's `raise()` is refused, and the child still dies.** The own-process `tgkill` rule covers the helper's process only, so a child's `raise()` is EPERM, and the child then dies of the fault the test raised. The parent accepted this, because a fork-aware own-process rule cannot be written in classic BPF (contradiction 3 above).
4. **The STEP export's +0.9 ms is the fixed install cost, inside its 1 ms bound.** The Release figure is to be measured at release preparation.

## Implementation notes (R109 S3, S4)

Recorded 2026-10-27 on branch `feat/r109s34-sandbox-text-extension`, based on `origin/main` at `34fd58ff`, and merged with `origin/main` at `91a74fed` (S2 is merged, #487). This section covers the text helper (S3) and the extension worker (S4). The design above is unchanged except where a point below says so. S2 (IFC and STEP) has its own section above.

**Method.** Each allow-list is the union of the calls seen after `main` in gdb traces of the helper, under the stdio setups of section 4.5. For the text helper: stdin a pipe, a file, and a file with stdout a file, and stdout a pseudo-terminal, all on the Latin fixtures, plus a large request (24 lines, about 300 characters, 4 MB of output). For the extension worker: stdin a pipe or a file, stdout a pipe, a file, a pseudo-terminal and `/dev/null`, on the panel, plus the panel padded to 300 KiB. The trace starts at `main`, as the policy does. A trace of a run that fails in a different way is a trace of a different path: the locale checks below came from one.

**Kernel and budgets (measured on this machine, kernel 7.2.8-9-omarchy, release build, static libraries).**

- Install, median of 301 forks (the policy is built before the fork): text 301 µs, extension 255 µs. Building and applying in the child (the helper's order): 674 µs and 484 µs over 300 forks. The text policy's `QLibraryInfo` call and its list account for the difference. Budget: 1 ms.
- Per call, `getpid` in a loop of 2 million calls, 21 interleaved pairs: the paired difference's median is +8.1 ns (text) and +9.6 ns (extension); the medians are 71 and 73 ns without the policy and 80 ns with it. Budget: 50 ns.
- Whole process, median of 40 interleaved runs against an unsandboxed twin (the helper's own `main` with the policy call removed, linked with the helper's link line): extension 17.46 ms against 17.51 ms, the same 233 bytes of output; text 129.1 ms against 132.4 ms, the same output apart from the random `documentId`. Both differences are inside the run-to-run noise on this machine (other workers were building). The install is 0.3 to 0.7 ms per process: 2 to 4% of the extension worker's 17 ms, about the budget's limit, and under 1% of the text helper's 108 ms (whole-process medians of the sandboxed helpers, not of the twins). Budget: 3% on the corpus.
- BPF program: 91 instructions (728 bytes) for the text helper and 65 (520 bytes) for the extension worker, counted from the block layout in the policy source, not read from the kernel. Limit: 4,096.
- Syscall counts: the text policy names 35 calls (32 from S3, plus `mkdir`, `chmod` and `unlink`, which S4 denies) and the extension policy 21; each also allows `exit`, `exit_group` and `rt_sigreturn`. The sanitizer builds name three more in the text policy (`pipe2`, `sigaltstack`, `madvise`).

**The text helper's allow-list (S3).** Plain allow (27): `read`, `write`, `close`, `fstat`, `statx`, `newfstatat`, `fstatfs`, `lseek`, `openat`, `access`, `readlink`, `getdents64`, `brk`, `mmap`, `mprotect`, `munmap`, `mremap`, `getpid`, `gettid`, `getuid`, `geteuid`, `futex`, `sched_getaffinity`, `getrandom`, `eventfd2`, `rt_sigaction`, `rt_sigprocmask`. Thread creation (2): `clone` (CLONE_THREAD only, a fork-like clone is EPERM) and `clone3` (ENOSYS). Signals (1): `tgkill` for the process's own ID only. Argument-filtered (2): `fcntl` with F_SETFD, F_GETFL and F_SETFL; `ioctl` with TCGETS and TCGETS2.

Where each entry comes from: the pipe trace has `read`, `write`, `close`, `fstat`, `mmap`, `mprotect`, `munmap`, `brk`, `rt_sigprocmask`, `access`, `getpid`, `fcntl` (F_SETFD, F_GETFL, F_SETFL), `readlink`, `getuid`, `geteuid`, `fstatfs`, `gettid`, `futex`, `sched_getaffinity`, `getdents64`, `openat`, `newfstatat`, `eventfd2`, `getrandom` and `statx`. The file-stdin trace adds `lseek`. The large request adds `mremap`. The `/dev/null` stdout trace adds `ioctl` with TCGETS2 (`0x802C542A`, `_IOR('T', 0x2A, struct termios2)`, returning ENOTTY). The entries not in any trace are these, and each is from the ADR's design rather than a measurement: `clone` and `clone3` (the ADR's text row says Qt has threads; no trace made one), `tgkill` and `rt_sigaction` (abort must stay a crash; the S0 test's abort needs them), and `TCGETS` (the older request, which a C library may use). The filter returns EPERM for any other command or request, so a C library that asks for another one is refused, not killed.

Grants (read-only, nothing for writing): the Qt plugins directory, reported by `QLibraryInfo` (`/usr/lib/qt6/plugins` here); `/etc/fonts`; `/usr/share/fontconfig` (fontconfig's configuration, which the trace reaches through `/etc/fonts/conf.d`); `/usr/share/fonts`; `/usr/local/share/fonts` (named by fontconfig's default configuration; this machine has no such directory, so the rule is untested); `/var/cache/fontconfig`; `/usr/lib/locale`. A path that does not exist grants nothing.

**User font folders (R109 S4).** After S3, the text helper could not see the fonts in the user's own folders, and the desktop's font list still showed them: `QFontDatabase::families()` in `src/app/text_panel.cpp` reads the same fontconfig folders. A text command that named one of those fonts fell back or reported it missing. That is a regression from 1.3. The parent decided on 2026-10-27 to keep those fonts available, read-only. The helper's policy and the host's configuration are as follows.

- *Folders (host, `userFontFolders` in `src/text/text_worker.cpp`).* The folders are the ones the desktop's fontconfig reads: `$XDG_DATA_HOME/fonts` when `XDG_DATA_HOME` is an absolute path (a relative one is ignored, as the XDG specification says), otherwise `~/.local/share/fonts`; and `~/.fonts`. The host reads them from the user's environment, before it replaces HOME and the XDG directories. Only existing directories are kept, each once. The rule: a folder outside HOME is accepted only when it came from `XDG_DATA_HOME`, the user's own choice. The other two lie inside HOME by construction, so `XDG_DATA_HOME` is the only folder that can lie outside it. The helper cannot apply this rule itself, since its HOME is the private directory; it checks only what it is given.
- *Configuration (host, `writeFontConfiguration`).* A fontconfig file in the private directory: an `<include ignore_missing="yes">` of `/etc/fonts/fonts.conf`, one `<dir>` for each folder, and `<cachedir>` in the private directory. QXmlStreamWriter escapes the paths. The host sets `FONTCONFIG_FILE` to it. The cache directory is the private directory, which the helper cannot write, so fontconfig cannot keep a cache for these folders and scans them again on each call. The trace shows its attempts to write a cache (below), not the cost of the scan, which is not measured.
- *Arguments (helper, `parseTextFontArguments` in `src/text/sandbox_policy.cpp`).* `--font-dir=PATH` for each folder and `--font-config=PATH` for the file, checked before the sandbox is installed. A folder must be an absolute existing directory, the file an absolute existing file, folders and file come together or not at all, and any other argument is refused with a reply and exit 1 (not exit 70: the sandbox has refused nothing).
- *Grants (helper, `textSandboxPolicy`).* Each folder is granted read-only, and so is the configuration file, which lies in the private directory and needs its own rule. Nothing is granted for writing. A folder beside a granted one is not granted.
- *Refused calls (helper, measured).* With a folder present, fontconfig tries to write its cache: it makes a directory (`mkdir`, twice), sets the mode of a file (`chmod`) and removes a lock file (`unlink`). Each of these was a kill under S3's kill default, so the policy names them as denied: each returns EPERM and does nothing. The helper has no write rule, so Landlock would refuse each one as well. These three are the only calls added by S4, and they are the only ones the trace of the helper after the sandbox showed beyond S3's list.
- *With no user folder*, the host sets no `FONTCONFIG_FILE`, passes no argument, and the helper runs with the policy above and nothing more. This is the S3 behaviour.

The reason is the regression, not a wish to read more files: a font file is data that fontconfig and Qt read, and a read-only folder runs no code and keeps nothing. The grants do not reach any other file of the user's. The helper's `XDG_CONFIG_HOME` is the private directory, so a user file under `~/.config` is not read. This system's `/etc/fonts` does not include one, so the desktop does not read it either. A distribution whose `/etc/fonts` does include one would give the desktop rules the helper does not have. That was not checked here (contradiction 12).

**Measured (R109 S4).** The text sandbox suite (`text_sandbox_tests`) covers the change. A DejaVu Sans copy, renamed to a family that no installed font has (`UserFontTst`, the same length as `DejaVu Sans`, so the table offsets hold), is put in each of the three folder layouts: `$XDG_DATA_HOME/fonts` where `XDG_DATA_HOME` lies outside HOME and its name holds `&` and `<`, `~/.fonts`, and `~/.local/share/fonts`. In each, the helper resolves the family with no fallback and no substitution, a system font still resolves with the folder present, and without the folder the same request is reported missing. The folder is read-only: the helper reads a font in it; it cannot read a file beside it, open the folder's parent, write or create a file in it, make a directory in it, change a font's mode or remove a font (each EPERM, with the file unchanged). The helper refuses a folder that is not an existing directory, a relative folder, a configuration that is not a file, folders without their configuration, and any other argument. The repo ships no font, so the test copies the system's DejaVu Sans from `/usr/share/fonts`.

Other suites, on the Debug build of this branch on this machine (the user's `~/.local/share/fonts/warm-glass-icons.ttf` is present here, so `text_worker_tests` and `text_command_tests` ran with a real user folder): `text_sandbox_tests`, `text_worker_tests`, `text_command_tests`, `text_source_io_tests`, `sandbox_tests` and `skp_sandbox_tests` (S1) and `extension_sandbox_tests` (S4) all pass. The sanitized build (ASan and UBSan, under an 8 GiB cgroup cap) passes `text_sandbox_tests` and `text_worker_tests` with no sanitizer report. On this machine, the user's own `~/.local/share/fonts/warm-glass-icons.ttf` (family "Warm Glass Icons", one 0.8 MB file) resolves in the helper with `substituted` false. Its glyph for "A" is absent, so that glyph falls back, which is the helper's normal glyph fallback. A run with that folder took 31 ms on average and one without took 27 ms (means of 15 runs, Debug build, this machine): one folder with one font costs about 4 ms per call. Folders with many fonts were not measured.

**The extension worker's allow-list (S4).** Plain allow (18): `read`, `write`, `close`, `fstat`, `statx`, `lseek`, `openat`, `brk`, `mmap`, `munmap`, `getpid`, `gettid`, `getuid`, `geteuid`, `futex`, `eventfd2`, `rt_sigaction`, `rt_sigprocmask`. Signals (1): `tgkill` for its own process. Argument-filtered (2): `fcntl` with F_GETFL and F_SETFL; `ioctl` with TCGETS and TCGETS2. There is no thread creation: the ADR's row says none, and no trace made one, so `clone` is absent and a fork is killed with SIGSYS.

Where each entry comes from: the pipe trace has `read`, `write`, `close`, `fstat`, `statx`, `openat`, `brk`, `mmap`, `getpid`, `fcntl` (F_GETFL and F_SETFL), `getuid`, `geteuid`, `gettid`, `futex`, `eventfd2`. The file-stdin trace adds `lseek`. The padded request adds `munmap`. The `/dev/null` trace adds `ioctl` (TCGETS2). `rt_sigaction`, `rt_sigprocmask`, `tgkill` and `TCGETS` are in no trace, for the reasons given for the text helper.

Grant: `/usr/lib/locale` only. The host's environment is clean, with `PATH` and `LANG=C.UTF-8` and no `LC_ALL`, so glibc reads that locale from there. The gconv modules, the locale message catalogue (`/usr/share/locale`) and OpenSSL's configuration (`/etc/ssl`) are also opened, and they are not granted (see the measured necessity below).

**Measured necessity of the grants.** Each experiment changed one policy and ran the helper on the same inputs.

- `/etc/ssl` removed from both policies: the text and extension outputs are identical and both exit 0. OpenSSL continues without its configuration. Dropped from both.
- `/usr/lib/gconv` and `/usr/share/locale` removed from the extension policy: identical output. Dropped.
- `/usr/lib/locale` removed from both: both helpers are killed with SIGSYS. Glibc's fallback calls `access`, `QCoreApplication` then logs a warning for the locale, and Qt's journal logger opens a socket (`sd_journal_sendv`, traced with `bt`). The policy refuses the socket, correctly. The grant stays, and the host sets the locale (next point).
- The same path with `LANG=C` or with an uninstalled `LANG` (`xx_YY.UTF-8`): SIGSYS. A UTF-8 locale that exists (`C.UTF-8`, `en_US.UTF-8`) gives identical output. The hosts therefore set the locale to `C.UTF-8` for both helpers: `LANG` and `LC_ALL` for the text helper, whose environment is the user's, and `LANG` for the extension worker, whose environment is clean. The text host previously passed the user's environment, so a user with an uninstalled locale would have had every text command killed. This finding is not in the ADR.
- The Qt platform plugin loads with a read-only rule: the rule is not `rx`. Landlock checks execute on `execve`, and the plugin is mapped with `mmap` and `dlopen`, which the rule does not check. The fixtures pass with the read-only rule.
- The text helper's private directory has no write rule. All 227 `openat` calls in the trace are `O_RDONLY`. A write attempt there returns EACCES (the test checks this).

**Attack probes (both helpers, in the tests).** `socket` and `execve` are killed with SIGSYS. `abort` is SIGABRT. A glibc `fork` is EPERM for the text helper (its policy has the thread clone) and killed for the extension worker (no clone). `/etc/passwd` is EACCES. A file outside the directory, created or written, is EACCES. For the text helper, the private directory is not writable either. Reading the fontconfig configuration (text) and the C.UTF-8 locale (extension) succeeds.

**Fail closed.** Each layer, simulated in process (`ApplyOptions::simulate`), refuses with `sandboxUnavailable: <layer> (Function not implemented)` and writes no geometry or commands. For the real helper, a filter that refuses the layer's calls gives exit 70 and the same named reason in the reply (`{"ok":false,"error":"sandboxUnavailable: ..."}`). The host reports that reply as the error. The `seccomp()` failure gives `sandboxUnavailable: seccomp filter (Operation not permitted)`.

**Host classes.** A helper killed by SIGSYS is `sandboxViolation` (`QProcess` reports the signal as the exit code of a crash, 31). A helper that exits 70 with its reply is that refusal. SIGABRT is still a crash. The text host creates one empty private directory per call and sets `HOME`, the XDG directories and `TMPDIR` to it, and removes it after the call. The directory is in the ADR's design, and the host did not set those variables before this change (the ADR's line references, `text_worker.cpp:188–190`, were stale).

**Sanitizer builds.** Built and run locally (ASan and UBSan, the `sanitize` preset with the CI's CLI option): `text_worker_tests`, `extension_worker_tests`, `text_sandbox_tests` and `extension_sandbox_tests` pass, under an 8 GiB cgroup cap, since ASan cannot run under `ulimit -v`. The sanitized helpers make three calls that the release helper does not: `pipe2` (UBSan's type cache, as S1 found), `sigaltstack` (ASan's alternate signal stack, as S1 found) and `madvise` (an allocator call of the sanitizer runtime; the trace does not name its caller; S1's list did not need it). They are allowed only when `SKP_WORKER_NO_ADDRESS_LIMIT` is defined, as in S1. LeakSanitizer's exit check uses `ptrace`, so both helpers define `__lsan_default_options` as `detect_leaks=0`, as the `.skp` helper does.

**Contradictions with this ADR, and what was decided.**

1. §5 text, filesystem `rx /usr/lib/qt6` and `ro /usr/share/qt6, /etc/xdg, /etc/ssl, /usr/lib/locale, /usr/share/locale, /usr/lib/gconv`: the final rules are read only, the plugins directory from `QLibraryInfo`, and `/usr/lib/locale`. Not granted: `/etc/ssl` (measured unneeded); `/usr/share/qt6` and `/etc/xdg` (the trace finds no file there on this machine, ENOENT); `/usr/share/locale` and `/usr/lib/gconv` (the text trace does not open them, and the extension measurement shows them unneeded). Execute is not needed (above).
2. §5 text, `rw {private}` as `HOME`, `XDG_*` and `TMPDIR`: the directory is private and empty, and the policy has no write rule. The trace wrote nothing, so the rule would have granted nothing used.
3. §5 text, "27 measured": the final policy names 32 calls. The ADR's count and the trace's count differ in both directions. The trace has 26 plain calls and the two argument-filtered ones. The rest are the design entries listed above (threads, `tgkill`, `rt_sigaction`, `TCGETS`).
4. §5 extension, "17 measured, includes fcntl": the final policy names 21. It adds `ioctl` (TCGETS2 for `/dev/null` stdout, which the ADR's §6.5 item 2 found but its §5 list does not include), `munmap` (a large input), and the three design entries for abort. §5 says "none observed" for threads, and the policy agrees.
5. §6.5 item 2, `ioctl` through `isatty`: the request on `/dev/null` is TCGETS2 (`0x802C542A`), not the older TCGETS (`0x5401`). A pseudo-terminal stdout showed no `ioctl` in the trace, which I did not explain. The filter covers both requests.
6. §4.3 and the S1 precedent allow `fcntl` and `ioctl` without an argument test. The S0 library now has actions that test one argument: `whenArgument` (one value, from S2: `prctl` and `ioctl`) and `argumentOneOf` (S3: a list of values, for `fcntl`'s command and `ioctl`'s request). The list is needed because `F_SETOWN` lets the kernel signal another process, and because the text helper uses three `fcntl` commands and two `ioctl` requests. Both are changes to the shared library, tested in `sandbox_tests`.
7. §4.2 and §5, `/usr/local/share/fonts`: added, because fontconfig's default configuration names it. Untested here (the directory is absent on this machine).
8. New finding: a locale that is missing, or `C`, kills the helper with SIGSYS, through Qt's journal socket. The host sets `C.UTF-8`. A system without a C.UTF-8 locale would still see `sandboxViolation` for every helper run; that is a host installation problem, reported as a violation. Not measured on such a system.
9. The consequence the ADR does not state: the text helper now reads system fonts only. Fonts that are installed only in the user's home directory (`~/.local/share/fonts`, `~/.fonts`) are not visible to it. A text command that names such a font now fails as a missing font does (with substitution off) or falls back. Before this change it read them. This follows from the accepted design (private `HOME`, system paths only). **Resolved by S4 (item 12).**
10. §6.9 and §10: the text helper's Qt threads are not in any trace, so the thread entries are unmeasured. If a Qt path creates a thread, the entry allows it; if none does, the entry is unused.
11. §7 item 2 and the host: a `sandboxViolation` message names the helper and the layer, not the call (the ADR's §4.6). The text and extension hosts throw a plain `std::runtime_error` with that message, which the calling command passes on as its error text. I did not check how the desktop shows that text; no dialog was written for these two helpers in this change (the `.skp` import has one, `src/app/ifc_sheets.cpp`).
12. §5 text, and item 9: the user's font folders are granted read-only (R109 S4, under "User font folders" above). The §5 row lists none of them, and it does not list the fontconfig file the host writes into the private directory, which the helper also needs read-only. The parent decided on 2026-10-27 to keep user fonts working rather than accept the regression from 1.3. The helper's HOME is private, so the host applies the HOME rule and the helper checks only that each folder exists. S4 also adds three denied names (`mkdir`, `chmod`, `unlink`) for fontconfig's failed cache writes, so the text policy names 35 calls. The rest of the §5 design is unchanged: no write rule, no network, no exec. Not matched: a user file under `~/.config` (fontconfig's XDG_CONFIG_HOME) is not read by the helper. This system's `/etc/fonts` does not include one, so the desktop does not read it either; a distribution that includes one would give the desktop rules the helper lacks (not checked).

**Not done here.** The pinned CI image (`sketchyup-ci-pinned:r097a-ifc`) was not run; the runs above are on the host, Arch with kernel 7.2.8. The full tier, with the sanitized lines and the `CI verdict` check, runs in CI. The corrections owed to ADR 0136 are in that ADR. ADRs 0171 and 0176 are S2's. The Blender job (S5) and the required `sandbox_available` check (S6) are not started. The scratch traces, scripts and benchmark binaries are under `.local/r109s34-work` and are not committed.

## Implementation notes (R109 S5)

Recorded 2026-10-27 on branch `feat/r109s5-sandbox-blender`, based on `origin/main` at `34fd58ff`. This section covers the Blender job (S5, `src/integrations/blender_job.cpp`) and the two library changes it needs (S0, `src/sandbox/`). The sections above are unchanged except where a point below says so. Measurements were taken on this machine (kernel 7.2.8-9-omarchy, Blender 5.2.2 at `/usr/bin/blender`) while other builds ran, so the timings carry noise, given as ranges.

**What the job does now.** The parent builds the policy before the fork (`sandbox::PreparedPolicy::prepare`: the Landlock ruleset with its rule paths opened, and the BPF program). The child runs `enter()` (`PR_SET_NO_NEW_PRIVS`, `landlock_restrict_self`, `seccomp`), which issues only system calls, and then `exec`s Blender. A layer that cannot be prepared refuses the job before Blender starts (`sandboxUnavailable`, with the layer named). A layer that cannot be installed in the child exits with 70 and writes `sandboxUnavailable: <layer> (errno <n>)` to the job's output. A SIGSYS from the filter is `sandboxViolation` and is not retried on the CPU.

**The library change.** `apply()` is now `prepare()` followed by `enter()`, which the helpers use unchanged. `enter()` takes no lock and allocates nothing. The own-process immediate of a `tgkill` rule is filled in by the process that installs the filter. `FileAccess::readExecute` grants read and execute on one file (the Blender binary and the dynamic loader). `writeUnavailableLine()` is the refusal the child writes before it exits.

**The allow-list, re-derived from real runs.** The trace was taken with a ptrace tracer (not committed) over four real runs of this machine's Blender tests (the job's probes, Cycles and EEVEE renders, the handoff, the animation and the presentation encode). The calls of the Blender images and their threads are 77 names. Eight of them are socket calls, which the policy now refuses. The forked children (`/bin/sh` and `xdg-user-dir` through `popen`, and `nvidia-modprobe`) are not traced in the sandboxed run: their `clone` is EPERM and the Landlock exec rule refuses `/bin/sh`. The final policy names 94 calls (89 before the CI runner's five, see **CI runner** below):

- 76 allowed as they are (71 from the trace, and the five CI runner calls; memory, files, threads, signals, identity; the list is in `src/integrations/blender_sandbox_policy.cpp`);
- `clone`, allowed for threads only (CLONE_THREAD; a fork-like clone is EPERM), and `clone3`, which returns ENOSYS so that glibc falls back to `clone`;
- `tgkill`, allowed for the process's own ID only, so a Blender `abort()` is a crash and not a violation (the rule S1 added);
- 15 socket calls, EPERM: `socket`, `socketpair`, `bind`, `connect`, `listen`, `accept`, `accept4`, `sendto`, `sendmsg`, `recvfrom`, `recvmsg`, `getsockname`, `getsockopt`, `setsockopt` and `shutdown`.

Every other call, including `fork`, `vfork`, `ptrace`, `kill`, `execveat` and `io_uring_*`, is killed with SIGSYS.

**Differences from the ADR's list (§5 and §6.6, 58 calls).** The ADR's 58 are the 51 traced calls plus the 7 of the loader. The final policy adds these, and each has a reason:

- `fallocate` and `flock`: Mesa's shader cache, on a cold start. Every attempt is cold (HOME is private), and the EEVEE preview calls both. The warm trace of the user's own HOME did not show them. Landlock limits them to the attempt directory.
- `chmod`: the handoff saves its scene with mode 0600. Landlock ABI 10 has no right over `chmod`, so the call is not limited by path. It changes only the modes of files the user owns. This is a residual risk (see the risks below).
- `writev`: from the ADR's loader trace (§6.6). It is not in the four runs.
- `mremap`: glibc's `realloc` of a large block. No run reached it, and a killed render is worse than an allowed remap of the process's own memory. This follows S1.
- `tgkill` (own process only): the abort rule, as in S1.
- `sched_setscheduler` and `setpriority` are allowed, not refused, although `sched_setscheduler` returned EPERM in 10 of 48 unprivileged calls in the warm trace. Refusing it would change what Blender does, and the probes and renders pass with it allowed.

The eight observed socket calls, which the ADR's §5 would have refused as well, are refused with EPERM for every socket call, not only `socket` and `bind`. Blender's Wayland, D-Bus and PipeWire attempts then fail as they do without a network, and its IPv6 probe fails too.

**The Landlock rules.** Execute on the Blender binary and on the dynamic loader (resolved from `/lib64/ld-linux-x86-64.so.2`). Read on `/usr/lib` (libraries, the Python standard library, Blender's scripts), `/opt/intel` (the oneAPI runtime Blender links), `/usr/share/blender`, `/usr/share/locale`, `/usr/share/fonts`, `/usr/share/drirc.d`, `/usr/share/glvnd`, `/usr/share/egl`, `/proc`, `/sys`, `/dev/dri` and a set of named `/etc` files (`ld.so.cache`, `ld.so.preload`, `nsswitch.conf`, `localtime`, `drirc`, `glvnd`, `egl`). Read and write on `/dev/dri/renderD128` and `/dev/null`, and read and write on the attempt directory. Read on the render snapshot and on the frames of a video. Twenty-four path rules; twenty of the paths exist on this machine.

**Contradictions with the ADR, and what was decided.**

1. §5 gives the Blender filesystem as `rx /usr/lib`, `rx /opt/intel` and `ro /etc`. The loader and the libraries need only read (they are opened, not executed), so the policy grants read on the two library trees and execute on the two files above. `/etc` is granted file by file, which keeps `/etc/passwd` refused (the brief's attack probe). Blender then gets no passwd entry and uses `HOME`, which is set.
2. §7.5 says a child policy that cannot be prepared reports `PREPARE_FAILED`. The brief asks for `sandboxUnavailable`, which §7.1 uses for every layer. The job reports `sandboxUnavailable`, with the layer named.
3. §6.4's negative control killed Blender's IPv6 probe under the kill default. Here the kill is shown on a `socket` call of the sandboxed child (`blender_sandbox_tests`), with the policy's socket entries removed. Blender itself was not run without them.
4. §10 risk 9 expected Blender to need no subprocess. Blender's trace makes 50 forked children (`/bin/sh` and `xdg-user-dir` for `popen`, and `nvidia-modprobe`). Each `clone` is EPERM and the exec of `/bin/sh` is refused, so each fails and Blender carries on. No probe or render depends on them.

**The fake Blender under the same sandbox (decision 7 of the brief).** The fake runs under the same policy and the same launcher. A stand-in is a hard link to `blender_job_tests` in its test's directory, and its mode is in its name (`sketchyup-fake-<mode>-<n>`). A shell script would need `/bin/sh`, which the policy refuses, so the stand-ins are no longer scripts. The Qt stand-in made two kinds of call that Blender's trace does not contain, and it was changed to avoid them: its file I/O uses the standard library (Qt's file layer calls `statx`), and its PNG is written without `QImage` (Qt's logging configuration is read). Two test seams exist, and nothing in the application sets either. `setBlenderTestWritableDirectories` is process-wide, like `setPresentationVideoProbe`: it lets a stand-in count its launches in a test directory (the presentation export test does; a stand-in cannot write outside the attempt directory otherwise). `Options::sandboxSimulation` is per job: it makes a named layer fail, for the fail-closed tests. The presentation CLI test runs its stand-ins as "slow" (each launch waits three seconds) and waits for the first frame file, in place of launch counts, because the CLI cannot be given the seam.

**Measured numbers.** Against the ADR's §9 budgets:

| Item | Measured | Budget |
|---|---|---|
| Install in the child (fork, `enter`, exit, against fork and exit alone; median of 120) | +350 to +365 µs | ≤ 1 ms: met |
| `prepare()` in the parent (median of 31) | 91 to 173 µs | not budgeted; once per attempt |
| Per allowed call (`getpid`, 2 million calls, median of 5 children) | 67 ns plain, 72 ns sandboxed (+5 ns) | ≤ 50 ns added: met |
| BPF program | 195 instructions, 1,560 bytes | < 4,096 instructions: met, and checked by `prepare()` |
| Landlock ruleset | 24 path rules, 20 present | negligible (the rule count is not yet a CI check, S6) |
| Short render (128 by 128, 4 samples, Cycles CPU), through the job against the same Blender without a sandbox, median of 5 | 674 and 722 ms through the job, 682 and 746 ms directly: differences of −1.2% and −3.2% | ≤ 3%: met, within noise |
| Blender probe, sandboxed against unsandboxed (CPU and OpenGL) | identical results | identical output: met (`blender_sandbox_real`) |

**Tests and gates.**

- `blender_sandbox_tests` (fake, in CI): every allow-list name is in the call table; the policy prepares; the attacks are refused (a socket, a UNIX socket, `/bin/sh`, another program, a fork, `/etc/passwd`, a write outside the attempt directory), while the attempt directory and the input work; an abort is SIGABRT; a ptrace is SIGSYS. The negative control (no socket entries) is killed with SIGSYS. Fail-closed refusals: a simulated Landlock failure before the fork, and a simulated no_new_privs and seccomp failure in the child (exit 70, the layer named). Host classes: a stand-in killed by SIGSYS is `sandboxViolation` and runs once; an abort stays a crash; a layer that cannot be prepared is `sandboxUnavailable` before the fork; one that cannot be installed is `sandboxUnavailable` with exit 70.
- `blender_sandbox_tests --real` (with `SKETCHYUP_BLENDER_TEST`): the sandboxed CPU and OpenGL probes give the same result as Blender run without a sandbox.
- A render writes a PNG through the launcher: `blender_job_tests --real` (CPU and EEVEE renders, the solar and environment renders, the fallback and the cancellation), and `animation_export_tests --real`, `blender_handoff_tests --real` (the handoff), `presentation_export_tests --real` (the video encode) and `m7_presentation_tests --real`.
- The suites the brief names pass unchanged in their assertions: `blender_job_tests` (fake and real), `presentation_export_tests` (fake and real) and `render_input_tests` (offscreen). `presentation_batch_input_tests` does not exist in this repository; the presentation batch is covered by `presentation_export_tests`, `presentation_cli_tests` and `presentation_store_tests`.
- The helpers that already ran under the library still pass: `sandbox` (S0) and `skp_sandbox` (S1), against the refactored `apply()`.

**Residual risks (S5).**

- `chmod` is not limited by Landlock on this kernel. A parser that runs code could change the mode of a file the user owns. The call is needed for the handoff's 0600 file. A later kernel with a `SETATTR` right, or a Blender that does not chmod, would close it.
- `/proc` is readable (the ADR's row). A child can read the environment of other processes of the same user, which can hold the user's credentials. The child has no socket, so nothing can leave it except a file in the attempt directory. The directory is removed when the job ends.
- Qt's `QProcess` does its own work in the forked child before the modifier runs (the argument and environment copies). That work is Qt's, not the policy's, and it is as it was before this change. The modifier itself allocates nothing. The policy's own work is before the fork.
- The Landlock ABI and the seccomp behaviour of the CI runner are not yet checked (S6). The Blender interoperability job runs `blender_sandbox` and `blender_sandbox_real` on the image's Blender (`command -v blender`), which is the first runner check of this slice.
- The EEVEE preview on a cold Mesa cache needs `fallocate`, `flock` and the render node. The CI run of `blender_job_tests --real` covers the EEVEE path on the runner's graphics stack, which may differ from this machine's Intel Mesa.

**Not done here.** S6 (the required `sandbox_available` check and the runner-kernel measurements) is open. The corrections owed to ADRs 0171, 0176, 0136 and 0180 are not edited. The keyboard reachability test (`keyboard_reachability_tests`) fails on the offscreen Qt platform at its command-palette check, which needs window activation that offscreen rendering does not give; CI runs it under Xvfb. It was built and run here, and the failure was not investigated further.

**CI runner (added 2026-10-28).** The first CI run of `blender_sandbox_real` failed: both the CPU and the OPENGL probes were killed with SIGSYS on the runner, and passed on this machine (run 40609207535). Reproduced in the pinned image `sketchyup-ci-pinned:r097a-ifc`, whose Blender is the build CI installs (`extra/blender 17:5.2.2-2`, at `/usr/sbin/blender`; this machine has `-3`) and which has no GPU. The kernel's audit records gave the calls, one per run, and each was added in turn:

- `statx` (332) and `openat2` (437): the runner's Blender build makes them where this machine's did not. `statx` returns the metadata `newfstatat` already returns, and `openat2` opens a path under the same Landlock rules as `openat`.
- `memfd_create` (319), `ftruncate` (77) and `sysinfo` (99): with no GPU, Mesa falls back to llvmpipe, which sizes an anonymous memory file and reads the RAM size for its caches. This machine has `/dev/dri` and never took that path. An anonymous file has no path, so no Landlock rule grants it more than the process already holds. It cannot be executed: `execveat` is not named, so it is killed, and `execve` is limited by Landlock to the Blender binary and the loader.

The path `/usr/sbin/blender` was not the cause: the layout is already canonicalized. With the five calls, `blender_sandbox` and `blender_sandbox_real` pass in the image through `/usr/sbin/blender` and `/usr/bin/blender`, `blender_real` (`blender_job_tests --real`) passes there, and no new kill is logged. The test now prints a failed job's code, message, each attempt's exit code or signal and the tail of its output. The trace that found the original 71 calls ran one Blender build with a GPU; a different build or a software renderer can need calls it never saw, which the runner shows.

**Sanitized builds (added 2026-10-27).** CI's sanitized build failed the seven suites that run a Blender job through the sandbox: `blender_job`, `blender_sandbox`, `animation_export`, `blender_handoff`, `render_store`, `render_queue` and `presentation_export`. The local `sanitize` preset (`build/sanitize`, with `SKETCHYUP_BUILD_CLI=ON`) fails the same seven. The sanitized build links each library as a shared object, so the stand-in Blender (a hard link to `blender_job_tests`) carries the sanitizer runtimes and loads the libraries from the build tree. Three things were wrong, and only the first is a system call:

1. **Refused calls.** The kernel audit log has `type=1326` (SECCOMP) lines; grep for that, since they do not contain the word "seccomp". It named `sigaltstack` (131) in the sandboxed children of `blender_sandbox_tests`, and `readlinkat` (267) in the sanitized stand-in. The release policy names `readlink` (89), not `readlinkat`. Both calls are now in a sanitizer-only set in `src/integrations/blender_sandbox_policy.cpp`, behind `SKP_WORKER_NO_ADDRESS_LIMIT`. The other S1 and S2 calls (`pipe2`, `fcntl`, `sched_getaffinity`, `sched_yield`) are already in the release list, so they need no sanitizer entry.
2. **The stand-in's libraries.** The stand-in exited with status 127 (`libsketchyup_render.so: cannot open shared object file`) before its own code ran, with no SECCOMP line. The Blender policy grants no read of the build tree, and the stand-in's loader opens the libraries after the policy is in force. S1 and S2 do not meet this, because their sandbox is applied inside the helper, after its loader has run. Under `SKP_WORKER_NO_ADDRESS_LIMIT` the policy now grants read-only on `QCoreApplication::applicationDirPath()` (the build tree, which contains the sanitized libraries). Release links the libraries into each executable, and its policy has no such grant.
3. **LeakSanitizer.** Its exit check uses `ptrace`, which the policy refuses (SIGSYS). `__lsan_default_options` returning `detect_leaks=0` is in the test code of `blender_job_tests` (the stand-in is this binary) and of `blender_sandbox_tests` (its probe children exit under the policy). It is not in the product. As a result, leak checking is off for these two test binaries in sanitizer builds.

The `ptrace` and `socket` kills that remain in `blender_sandbox_tests` are the intended probes: the ptrace attack and the negative control. The stand-in's `ptrace` is its `sandboxViolation` mode. Results on this machine: the sanitize `ctest` of the seven suites passes 7 of 7; the release `blender_job` and `blender_sandbox` pass 2 of 2. The release policy's calls and grants are unchanged. After the merge of `origin/main` (S3 and S4), the S0 suite `sandbox` passes in release but fails under ASan: its forked children are killed by `sigaltstack`, as above. CI does not run `sandbox` sanitized (only `skp_sandbox`, `text_sandbox` and `extension_sandbox` are), so this is not a CI failure, and the S0 policy has no sanitizer-only set. (Superseded by R109 S6: `sandbox` runs sanitized now, and its policies have a sanitizer-only set.)

**The presentation batch suite (added 2026-10-27).** `presentation_batch_input` (R099.d3, #484) ran its fake Blender as a `/bin/sh` launcher. The Blender policy grants execute on the stand-in only, so the job's child failed to start: `START_FAILED`, "Child process set up failed: execve: Permission denied". The probe never answered, and the suite's 60 s wait failed. No `type=1326` line is logged for it, because Landlock refuses the exec with EACCES, not seccomp. The suite now makes its stand-ins with the helpers of `tests/blender_stand_in.hpp`, as the other suites do. Its unsaved-changes prompt finds the Discard button by its standard role: under the gtk3 platform theme (this machine) the button reads "Close without Saving", and a text match left the prompt open until the 900 s deadline. The policy is unchanged. A real Blender is an ELF binary, which the policy grants, so it is not refused.

## Implementation notes (R109 S6)

Recorded 2026-10-28 on branch `feat/r109s6-sandbox-ci`, based on `origin/main` at `6149f271` (S5, #494). This section covers the required check, the per-helper selection, the sanitized `sandbox` suite, the socket rule and the closing records. Where it differs from the sections above, it says so.

**The required check.** `tests/sandbox_available_tests.cpp` (CTest `sandbox_available`) checks, in order: the kernel and machine, which it logs; the Landlock ABI, read with the version flag; the seccomp actions the policies use (`kill_process`, `errno` and `allow`); `PR_SET_NO_NEW_PRIVS`, accepted and read back; and one whole install in a child, with a policy that allows only `openat`, after which `open("/")` must fail with `EACCES`. A layer that cannot be installed is named on stderr, and the child exits 70. The test never skips. It takes about 0.01 s.

CI runs it as the step `Sandbox availability (required)` in the `fast` job, in shard 1 of 5 (`strategy.job-index == 0`), before that shard's tests. The shard lines exclude it from their `--tests-regex`, so it runs once. A failure fails `fast`, which `CI verdict` requires. The full tier runs it in the `build` job's `ctest --preset dev`.

Choice: the `fast` job runs only when the plan sets `build`, which happens when a changed path maps to the build area. A pull request the plan sends to no fast tier (docs only) does not run the check, as no other fast-tier check runs there (ADR 0170). The owner may want it on every pull request; that costs one short job.

Measured here (Arch host, kernel `7.2.8-9-omarchy`, x86_64): Landlock ABI 10, all three seccomp actions, `PR_SET_NO_NEW_PRIVS` accepted, and the install refuses the open. The CI runner's kernel and ABI are not known yet. The step runs with `-V`, so its job log records both.

**Per-helper suites.** The path map's rule for `src/sandbox/**` and `tests/sandbox*` selects the build, native, blender, `job:ifc`, `job:skp` and `job:sanitize` areas (`ci-plan.py areas src/sandbox/sandbox.cpp`). `ci-plan.py check` holds, including the example "R109 S6: the sandbox library selects every helper's sandbox suite". The suites that run under those areas:

- `skp_sandbox`: the fast build, and the SKP job (release and sanitized);
- `ifc_sandbox`: the IFC job, in the pinned image;
- `text_sandbox` and `extension_sandbox`: the fast build and the sanitized build;
- `blender_sandbox`: the fast build (with stand-ins), the sanitized build and the Blender job, which also runs `blender_sandbox_real` on Blender itself.

**Nightly coverage (ADR §10).** Decision: no new nightly job. The nightly schedule (`cron: "23 6 * * *"`) runs the full tier, and the full tier already runs each helper's sandbox suite on its committed inputs (above), and the real Blender probes in the Blender job. The trace check that §10 asked for was not built, because the observe mode it needed does not exist (§4.5). Part of what it would have caught is covered: a call outside a helper's list ends the helper, so a stale list fails the suite for any call the suite makes. A call that no suite makes is not covered.

**The sanitized `sandbox` suite.** The S0 note above says this suite fails under ASan and is not in the sanitize line. That is no longer true. `sandbox_tests` is in the sanitize build and in the sanitize CTest line. The fixes are in test code only:

- `sanitizerRuntime()` in `tests/sandbox_tests.cpp` is added to every policy the suite builds, behind `SKP_WORKER_NO_ADDRESS_LIMIT`: `pipe2`, `sigaltstack`, `madvise`, `gettid` and `sched_getaffinity`. `fcntl` is not in that set, because the argument-filter checks need every other `fcntl` command refused.
- The thread-only check also allows `fcntl` with `F_SETFD`, `F_GETFL` and `F_SETFL`, the three commands the text helper allows. ASan's thread start calls `fcntl`.
- `sandbox_available` allows `sigaltstack` in its install check, behind the same macro.
- The calls were found in the kernel audit log, where each seccomp kill is one `type=1326` line with its syscall number: `gettid` (186), `sched_getaffinity` (204), `fcntl` (72) and `sigaltstack` (131). After these fixes the sanitized runs log only the intended kills: the `getpid` check of the kill default, and the `execve`, `fork`, `socket` and `ptrace` probes.

**`sched_yield` in the S3 and S4 helpers.** Not added. The IFC helper needed `sched_yield` when four threads contend (#496). In this branch's sanitized runs, the text and extension suites, and every other sandbox suite, logged no `sched_yield` (syscall 24) record. If a sanitized CI run logs one, add it behind `SKP_WORKER_NO_ADDRESS_LIMIT` to `src/text/sandbox_policy.cpp` and `src/automation/extension_sandbox_policy.cpp`, and record it here.

**The socket rule (S1, S3 and S4).** Socket calls are refused with EPERM in every helper (decision 1 above). The `.skp` policy changes from S1's kill to EPERM. The text and extension policies change too, from the kill default that S3 and S4 left them with. `skp_sandbox`, `text_sandbox` and `extension_sandbox` check the return value (EPERM, not a signal). An EPERM refusal is not logged; only a kill is.

**Record and observe modes (§4.5 and §11 S0).** These were designed and not built. The ADR text is corrected in §4.5, §8 and the S0 entry of §11. A refused call is found in the kernel's audit log: `journalctl -k | grep type=1326` lists each seccomp kill with the process (`comm`, `exe`), the signal and the syscall number. Only kills are logged, so a call the policy refuses with EPERM does not appear there.

**Corrections owed.** Checked in this branch and on `main`: ADR 0171 (§1.6, decision 19, and the later references to "later hardening") names ADR 0183 as the sandbox; ADR 0176 (§1, decision 2, and the `tooLarge` reason that #470 fixed) and ADR 0136 (lines 24–27) are updated in S1 and S4. ADR 0180 says its §3.2, §3.3 and §3.5 corrections are moot until DWG is reopened.

**Measured results (2026-10-28, local).**

- Release (`build/release`): 6 of 6 suites pass (`sandbox`, `sandbox_available`, `skp_sandbox`, `text_sandbox`, `extension_sandbox`, `blender_sandbox`), 3.7 s in CTest.
- Sanitized (ASan and UBSan, `build/sanitize`, under the 10 GB cgroup cap): the same 6 pass, 55 s in CTest, of which `text_sandbox` is 48 s.
- Pinned image (`sketchyup-ci-pinned:r097a-ifc`, the `ifc-app` preset, which is the `ci-fast` configuration CI uses): `ifc_sandbox` passes in 17.1 s. The IfcOpenShell prefix came from the cache for pin `7ba70197`, and `mold` was installed in the container with pacman, as CI does.
- `blender_sandbox` passes with its stand-ins. `blender_sandbox_real` is skipped here, because this host has no Blender. The Blender job runs it in CI.

**Gaps.**

- The CI runner's kernel and Landlock ABI are not known until the first fast-tier run on this branch. The step prints them.
- The 24.04 and 26.04 behaviour (the S6 bullet of §11) is not measured. There is no such runner here.
- `ifc_sandbox` ran in the `ifc-app` configuration only, which is how CI runs it. The IFC helper's sanitized build does not run it.
- The full-ci run was not waited for.
