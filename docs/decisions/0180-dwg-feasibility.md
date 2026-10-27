# 0180 — DWG import: feasibility and isolation

**Status: Proposed (2026-10-26).** This is the feasibility design for [R106](../PR_ROADMAP.md#r106) ("Deliver DWG support only after feasibility passes"), scope row C05, which [SCOPE](../SCOPE.md) keeps at "Investigate". It is research and design only: no product code, no dependency added, and no compatibility claim. Throwaway probes ran outside the tree under `.local/` and are not committed. **Verdict: import through a sandboxed helper that links LibreDWG, read-only, for AC1015 to AC1032, in the tiers of §4: AC1015, AC1018 and AC1024 supported, the rest best effort. This is conditional on counsel's answer to the GPL questions in §2 and on the owner's acceptance. No code starts before then. Until then, DWG stays unsupported (ADR 0003), and the user-mediated conversion path is documented as a conversion, not as DWG support.** The owner decides on that verdict and on the open questions in [Owner decisions](#owner-decisions). If accepted, this ADR supersedes [ADR 0003](0003-compatibility-gaps.md) for DWG import only. ADR 0003 stands until then. Writing DWG is out of scope.

## Verdict in brief

- **A native import is feasible on Linux without a paid SDK.** [LibreDWG](https://www.gnu.org/software/libredwg/) is a GPL-3.0-or-later C library. Its own manual says it can read "all DWG formats from r1.2 to r2018 for 99%" (0.13.4 manual, 17 March 2026). Its repository is actively maintained (last push 2026-10-22). It is not in Arch's official repositories, so the project must vendor a pinned commit, as it does for OpenSKP.
- **The reader is not production-grade yet.** LibreDWG calls itself "in beta development stage". The NVD lists 15 LibreDWG CVEs published since 2024, most of them out-of-bounds reads or heap overflows in the decoder. The newest is CVE-2026-111167 (published 2026-10-15), and I did not verify a fix for it. On LibreDWG's own 141-file test corpus, `dwg2dxf` exits 0 on every file, yet its text output shows 363 skip messages for the R2013 files alone, with 37 buffer-overflow errors. Its writer produces R2004-and-later files that read back with no entities (§4.1).
- **Isolation must be better than the existing helpers.** The SKP, IFC and STEP helpers are resource limits only. They have no network denial (ADR 0179 §6 said so, and I confirmed it in `src/io/`). The DWG helper should add a seccomp-bpf filter that denies sockets and exec, and a Landlock ruleset that confines it to a private directory. Both are fail-closed. I ran both on this kernel: a filtered child gets EPERM on `socket()` and on `execv`, and EACCES on reads outside its directory. An unfiltered control succeeds on all four checks (§3.4).
- **The Open Design Alliance SDK is rejected for 1.x.** Its commercial tier costs USD 3,000 in year one and USD 2,250 a year after that, with "limited commercial use". Distribution rights end when the subscription ends. The Linux downloads are members-only (search snippet, not verified on a primary page). The ODA File Converter is free for the user, but its licence is revocable and forbids distribution "as part of any commercial application". It cannot be bundled.
- **Several alternatives fail on licence, platform or maturity.** libdxfrw (GPL-2.0-or-later) reads DWG as "experimental" and publishes no version list. acadrust (MPL-2.0) is a 0.x Rust crate with self-declared coverage. RealDWG is Windows-only according to a search snippet. §1 has the table.
- **Recommendation:** approve the conditional path (§11). Ask counsel the questions in §2 once, before R106.a's gate. Keep DWG unsupported in the support matrix until R106.d.

## Context

- **ADR 0003 (2026-10-03) found DWG unsupported.** It gave three reasons, quoted from its DWG section: "No membership agreement, redistributable SDK selection, version matrix or round-trip corpus has been established here." In short: the ODA route needed membership and a redistribution right, no Linux SDK route was established, and there was no version matrix or corpus. This ADR answers each one:
  - *Redistributable SDK selection:* LibreDWG is redistributable under GPL-3.0-or-later, and needs no membership. The GPL question is in §2.
  - *Version matrix:* §4, with measured results from a probe.
  - *Round-trip corpus:* §8. Only synthetic fixtures are committed. The owner's files stay local, as for `.skp`.
  - *Membership agreement:* not needed for the recommended library. The ODA route is rejected in §1.
- **Existing DXF import (ADR 0120, 0121, 0123; R077).** A bounded, in-process ASCII DXF parser captures a 2D subset. It accepts AC1009, AC1015, AC1018, AC1021, AC1024, AC1027 and AC1032 (`src/io/dxf_source.cpp:316–317`). It rejects "binary DXF, DWG and unknown versions" (ADR 0120). The subset is LINE, ARC, CIRCLE, LWPOLYLINE and 2D POLYLINE. `importDxf` (`src/io/dxf_import.cpp`) turns that into bodies, tags and analytic curves, in one undoable edit. The DXF report is its own sheet, `dxfReport` (`src/app/window_dxf.cpp:89`). ADR 0123 says "No full DWG claim is made." R106 says to keep the DXF feature report distinct, and SCOPE's C05 row says the same.
- **The `.skp` precedent (ADR 0176).** A pinned, vendored, MIT parser runs in a helper, `sketchyup-skp-worker`. The package is a bounded binary with a checked decoder. The helper ships in the main package, with notices and a source record. ADR 0176 describes the helper as "sandboxed as in ADR 0171 §1.6". ADR 0179 §6 found that this means resource limits only.
- **The IFC helper (ADR 0171).** It is LGPL-linked and optional. It ships in its own package, `sketchyup-ifc`, so the main package stays MIT. ADR 0171 §1.3 gives the engineering reading of the LGPL position, and says it is "no legal opinion".
- **Alternatives considered:**

  | Route | Licence (read 2026-10-26) | DWG versions read / written | Linux distribution | Verdict |
  |---|---|---|---|---|
  | **LibreDWG**, vendored and pinned, in a helper | GPL-3.0 (GitHub API `spdx_id`; README: "version 3 (or at you option any later version)") | Read: "all DWG formats from r1.2 to r2018 for 99%" (manual, 0.13.4). Write: r2000 and older by default; R2004 write added in 0.14 (NEWS); R2007 not implemented; R2010+ "an ongoing effort" | Not in the official Arch repositories (search returned "0 matching packages found"). AUR `libredwg` 0.13.3-1 (search snippet). Source build with autotools | **Recommended, conditional on counsel (§2)** |
  | ODA Drawings SDK (Teigha) | Commercial licence through ODA membership | Read and write AC1009 to AC1032 (ODA datasheet) | Linux libraries are members-only (search snippet; not on a primary page) | **Rejected for 1.x.** Membership fee; distribution rights end with the subscription; redistribution limits (§2.5) |
  | ODA File Converter, user-installed | ODA Community User Agreement, dated 2025-09-05: revocable, non-sublicensable; no distribution "as part of any commercial application" | Converts between DWG and DXF versions. The version list is not on its page | RPM, DEB and AppImage; glibc 2.28 or later | **Not bundled and not invoked.** A user-mediated conversion path only (§6.3) |
  | libdxfrw (LibreCAD) | GPL-2.0-or-later (README; GitHub API `spdx_id` GPL-2.0) | DWG read and write marked "experimental"; no version list. "DXF-first" (README) | Not in the official Arch repositories (search returned "no matching packages") | **Rejected.** Experimental DWG, no version matrix, and the README says DWG coverage is partial |
  | acadrust (Rust crate) | MPL-2.0 (the repository's `LICENSE` file, read 2026-10-26; the crate metadata has no licence field) | README table: read and write for AC1009 to AC1032; "Entity availability varies by file version" | crates.io 0.6.7; pushed 2026-10-26; 145 stars | **Second choice, not chosen.** A 0.x crate with self-declared coverage, no independent oracle, and a Rust toolchain in the packaging. Kept as the fallback if counsel rules a GPL helper out |
  | Autodesk RealDWG | Commercial, through Tech Soft 3D (search snippet) | All | Windows 10 and 11 only, per the 2026 system requirements (search snippet; the primary page was not fetched) | **Rejected.** No Linux build |
  | ACadSharp | MIT (search snippet only) | "Most DWG and DXF formats", basic objects (search snippet) | .NET runtime | **Rejected.** A .NET runtime in the packaged product. Not evaluated further |
  | ezdwg (Python, PyPI 0.17.0) | Not stated in the registry metadata | R14 to AC1032 read (search snippet) | Python | **Rejected.** Licence unknown; Python runtime |
  | LibreDWG's `dwg2dxf` used as a converter | GPL-3.0 | As LibreDWG | As LibreDWG | **A conversion workaround.** Not used by the application (§6.3) |
  | Keep DWG unsupported (ADR 0003) | — | — | — | Current state, until the owner accepts this ADR |

## 1. The library and the evidence

**What LibreDWG is.** A C library to read and write DWG. Its homepage calls it "in beta development stage" and says its decoder "is done, just some very advanced R2010+ entities fail to read and are skipped over" (homepage, read by curl 2026-10-26). The manual (0.13.4, dated 17 March 2026) says it "provides the API/ABI version 1", that coverage is "approximately 99%", and that it reads "all DWG formats from r1.2 to r2018 for 99%". It lists the revisions it knows: r9, r9c1, r10, r11b1, r11b2, r11, r13b1 to r13c3, r14, r2000b to r2000i, r2002, r2004a to r2004c, r2004, r2007a to r2007, r2010b to r2010, r2013b to r2013, r2018b and r2018.

**Release history.** From the NEWS file (read 2026-10-26):

| Release | Date | Relevant change |
|---|---|---|
| 0.14.1 (beta) | 2026-07-25 | Eleven CVE and GHSA fixes, mostly R2004/R2007 decompression, header parsing and object handling; heap overflows, use-after-free, double-free |
| 0.14 (beta) | 2026-06-27 | R2004 write support; R2007 output falls back to R2010; fixes CVE-2026-62254 and CVE-2026-54610 (as the NEWS names them) |
| 0.13.4 (beta) | 2026-03-18 | R2000 output improved; fixes a heap overflow-by-one and several double-frees |
| 0.13.3 (beta) | 2023-02-26 | Three OSS-Fuzz reports |

The repository's tags (`git ls-remote --tags`, 2026-10-26) are numbered in the `0.14.86xx` series, and the NEWS file names 0.14.1 as the newest beta. So the pin must name a commit, not a release tag. The probe used commit `a8ccb1f3` (2026-10-22).

**Activity and maintenance.** The GitHub repository (read 2026-10-26) shows 1,642 stars, 145 open issues and a last push on 2026-10-22. Its licence is GPL-3.0 by SPDX. Its README calls it "GNU General Public License version 3 (or at you option any later version)" [sic].

**Write support, a conflict between sources.** The 0.13.4 manual says, "We write as default in the r2000 or older formats only", that R2007 writing "is not implemented", and that R2004+ writing "is an ongoing effort. You need to patch the code". The GitHub page (2026-10-26) says "The writer is good enough for everything but R2007" and "R2010-R2018 writing leads to CRC errors still". The 0.14 NEWS says R2004 write support was added. The newest source is the NEWS, but the manual and README contradict it. **Measured (§4.1): this build (`0.14.8625`) writes R2004 and later files that read back with no entities, so the NEWS claim does not hold for this build.** This ADR does not depend on writing beyond R2000.

**Security history.** NVD keyword search for "libredwg" returns 99 results across all years (read by curl, 2026-10-26). The first page holds the 2018 to 2020 entries, most of them heap over-reads, NULL dereferences and double frees in decode paths, with CVSS 6.5 to 9.1. The entries published since 2024 are these:

| CVE | Published | CVSS 3.x | Summary (NVD, one line) |
|---|---|---|---|
| CVE-2023-26157 | 2024-01-02 | 5.5 | Out-of-bounds read involving `section->num_page` before 0.12.5.6384 |
| CVE-2025-61154 | 2026-03-12 | 6.5 | Heap buffer overflow in v0.13.3.7571 to v0.13.3.7835 |
| CVE-2026-9500 to 9503, 9504, 9529, 9530 | 2026-05-25 to 05-26 | 3.3 to 5.3 | Out-of-bounds read and null dereference in `decode.c`, `dwggrep.c` and `Dwgbmp`, all "up to 0.14" |
| CVE-2026-9605 | 2026-05-27 | 7.3 | `bit_read_RC` in `bits.c`, `Dwgbmp`, "up to 0.13.4.8160" |
| CVE-2026-15182, 15184 | 2026-07-09 | 5.3, 3.3 | `dwg_bmp` and `dwg_next_entity` in `src/dwg.c`, "up to 0.13.4" |
| CVE-2026-15520 | 2026-07-13 | 5.3 | `decompress_R2004_section`, "0.13.4-154" |
| CVE-2026-90622 | 2026-09-14 | 3.3 | `DWG_TABLE` in `src/dwg.spec`, "0.13.4" |
| CVE-2026-111167 | 2026-10-15 | 6.1 | Global out-of-bounds read in the AcDs decoder `acds_private()`, "through 0.14.8612". **Fix status not verified.** |

Most of the 2026 entries describe themselves as "A vulnerability was found in…", which reads like a third-party numbering authority's wording. Their CVSS scores are low, but they are all in the decoder, which the DWG helper must run. The NEWS also names CVE-2026-62254 and CVE-2026-54610 as fixed in 0.14. Neither appeared in the NVD keyword results I read, so I have not confirmed them.

For `libdxfrw`, the NVD lists six entries, including CVE-2026-110606 (published 2026-10-14). For ODA's SDK, I found no public CVE list (not searched further).

**Fuzzing.** The NEWS credits many OSS-Fuzz and GitHub fuzzer findings. The repository includes `llvmfuzz_standalone` scripts (GitHub page, 2026-10-26). These show that fuzzing has been used, but I did not measure coverage.

**API stability.** LibreDWG declares API/ABI version 1 (manual, 0.13.4) but is pre-1.0 in practice, so the pin must include its own patches and a re-check per upgrade. The `libdxfrw` README says 2.0.0 "is an ABI boundary". acadrust is 0.x. ODA's SDK publishes an API but is not open.

**Measured on this machine (2026-10-26).** LibreDWG was built from commit `a8ccb1f3` in the probe directory (`.local/r106-probes/`), with `./configure --disable-bindings --disable-python --disable-werror`. The build ran under a 10 GiB systemd scope, and the binary reports `LibreDWG 0.14.8625`. The probe's results are in §4.1.

- **`dwg2dxf` exits 0 when it skips objects.** On the 141-file corpus it exited 0 every time. Its stderr shows `Unknown object, skipping` (438 lines in total) and `ERROR: bit_read_TU buffer overflow` (59 lines). It also writes a warning for each object of an "unstable" class, such as MATERIAL, DIMASSOC and TABLESTYLE. These are text messages, not counts. **So the helper must read each object's status through the library's API. An exit code cannot show success, and the stderr text cannot be the source of the report's counts.**

## 2. Licence analysis (what needs counsel)

**I have no legal opinion. This section is an engineering reading for the owner and counsel to confirm.** It follows ADR 0171 §1.3, which uses the same caveat.

### 2.1 The shape

- The helper, `sketchyup-dwg-worker`, links LibreDWG and is licensed GPL-3.0-or-later. The application and `sketchyup-cli` do not link LibreDWG. The helper is a separate executable that the application starts with a fixed argv and no shell (the pattern of ADR 0171 §1.6).
- The helper and the application exchange a bounded, one-way file: a package written by the helper and read by the application through a checked decoder. The protocol has no shared memory, no callbacks and no shared data structures. The helper's only inputs are the file, the argv and the cancel signal.
- **GPLv3 §5 (read 2026-10-26, <https://www.gnu.org/licenses/gpl-3.0.txt>).** An "aggregate" is "a compilation of a covered work with other separate and independent works, which are not by their nature extensions of the covered work, and which are not combined with it such as to form a larger program, in or on a volume of a storage or distribution medium". Including a covered work in an aggregate "does not cause this License to apply to the other parts of the aggregate."
- **The GPL FAQ (footer dated 2026/01/13; read 2026-10-26, <https://www.gnu.org/licenses/gpl-faq.html>).** "By contrast, pipes, sockets and command-line arguments are communication mechanisms normally used between two separate programs. So when they are used for communication, the modules normally are separate programs. But if the semantics of the communication are intimate enough, exchanging complex internal data structures, that too could be a basis to consider the two parts as combined into a larger program." It also says that "where's the line between two separate programs, and one program with two parts? This is a legal question, which ultimately judges will decide."
- **Our reading.** A bounded data file, with a fixed schema that is checked on read, is closer to "separate programs" than to "complex internal data structures". That is an engineering judgement and a question for counsel (Q1).

### 2.2 Consequences if the helper is accepted as a separate program

- The optional package `sketchyup-dwg` carries GPL-3.0-or-later. Its `license` field says so. The main package stays MIT, with no GPL binary (§7).
- Source obligations. The source of LibreDWG (the pinned, patched vendored subset) ships in the source archive, and `DWG-SOURCE.txt` gives the written offer, as `IFC-SOURCE.txt` and `SKP-SOURCE.txt` do (ADR 0171 §1.3, ADR 0176 §1). The GPL text ships as `LibreDWG-COPYING` (GPLv3 §4, §6). Counsel should confirm that this is enough for a binary package.
- GPLv3 §6 requires "Installation Information" only for a "User Product". A desktop application installed from an Arch package is not one, on our reading. Counsel should confirm (Q2).
- GPLv3 §11 includes an express patent licence from each contributor. Nothing in LibreDWG's README suggests a patent claim. Not reviewed further.

### 2.3 If counsel says the helper and the application form one work

- Then the application itself would need to be GPL-3.0-or-later, or DWG import cannot ship with an MIT application at all. That outcome is not compatible with the current MIT licence (`LICENSE`), and the owner would need to decide it.
- The fallback is acadrust (MPL-2.0), a file-level copyleft licence. It is written in Rust, so it needs its own helper and toolchain. It is pre-1.0. It is the only candidate found that is not GPL.

### 2.4 Trademark and naming

- The LibreDWG manual says: "DWG is the native and proprietary file format for AutoCAD® and a trademark of Autodesk, Inc. LibreDWG is neither associated with Autodesk, nor with the Open Design Alliance." (Manual, read 2026-10-26.)
- The user-facing text is "Import DWG (.dwg)…". No product, package or executable name uses "AutoCAD". The user guide carries the same non-affiliation note as ADR 0176 §8.

### 2.5 Reverse engineering and the ODA terms

- **The DWG format and LibreDWG's method.** The manual says "the DWG file format is proprietary and was created in the 1970s" and that its specification "had to be" obtained by reverse engineering (text cut off in the fetched page). Counsel must decide the status of that work.
- **US law (read 2026-10-26, <https://www.law.cornell.edu/uscode/text/17/1201>).** 17 U.S.C. §1201(f) allows a person who has "lawfully obtained the right to use a copy of a computer program" to circumvent a technological measure "for the sole purpose of identifying and analyzing those elements of the program that are necessary to achieve interoperability". It is written for computer programs, not file formats, and it concerns circumvention. Whether it covers reading a file format, and whether a drawing password is a "technological measure", are questions for counsel (Q3).
- **EU law.** Article 6 of Directive 2009/24/EC covers decompilation for interoperability. I did not read its text (the EUR-Lex fetch failed on 2026-10-26). Counsel should confirm it, and the national implementation if the owner is in the EU (Q3).
- **Password-protected DWG.** A search snippet quotes an Autodesk moderator saying the drawing password option was removed from AutoCAD 2016-based products (forum, 2026-10-26; not verified on a primary page). Older files can still carry a password. The recommended stance: refuse, do not attempt decryption, and say why (Q4). This matches ADR 0177 decision 5, which refused circumvention.
- **ODA File Converter (Community User Agreement, dated 2025-09-05; read as a PDF by `pdftotext`, 2026-10-26).** Its definition of "Community Application" is "any free end-user software application made available by the ODA through its online resources including, but not limited to, Open IFC Viewer, Open STEP Viewer, and other free stand-alone utilities". The converter is not named, so whether it falls in this class is for ODA or counsel to confirm (Q5). The scope of use covers "viewing, exploring, and converting supported file formats", but "may not directly or indirectly resell or distribute a Community Application, or any part thereof, as part of any commercial application". The grant is "limited, non-exclusive, non-transferable, non-sublicensable, revocable". It forbids reverse engineering the application and copying it except as needed for use. It may be terminated, and on termination the user "will immediately cease using all Community Applications". I found no clause that permits bundling or automated pipelines.

### 2.6 Questions for counsel

Ask once, before R106.a's gate:

- **Q1.** Is a GPL-3.0-or-later helper, in an optional package, that communicates with an MIT application through a bounded one-way package file, an "aggregate" under GPLv3 §5, so that the application is not covered? Does the answer change if the helper is in the main package?
- **Q2.** Is the source offer in §2.2 (the vendored source in the source archive, `DWG-SOURCE.txt` and the GPL text) enough for a binary package? Is GPLv3 §6 installation information needed for a desktop Linux package?
- **Q3.** Does reading the DWG format by reverse engineering raise an issue under 17 U.S.C. §1201(f), under Article 6 of Directive 2009/24/EC, or under Autodesk's terms (none read)?
- **Q4.** Is the stance "refuse password-protected DWG, never decrypt" correct?
- **Q5.** May SketchyUp describe, but not bundle or invoke, a user-installed ODA File Converter, given the Community User Agreement's wording? Is the converter a "Community Application"?
- **Q6.** Are the naming and non-affiliation wording in §2.4 adequate?

### 2.7 ODA SDK membership

Rejected for 1.x. The pricing page (fetched 2026-10-26, <https://www.opendesign.com/pricing.md>; no page date shown):

| Tier | First year (USD) | Renewal (USD) | Commercial use |
|---|---|---|---|
| Commercial | 3,000 | 2,250 | "limited commercial use" |
| Sustaining | 7,500 | 4,500 | "unlimited commercial use" |
| Founding | 37,500 | 18,000 | "unlimited commercial use with full source code" |

The page says ODA SDKs are "licensed through an annual subscription model", and that when a subscription ends "you lose the right to distribute the ODA-based product, even if it was developed during the validity of the license". A search snippet gives a redistribution limit of 100 copies at the Commercial tier. The fetched page does not state it, so I have not used it. The non-commercial tier "cannot commercialize your product". The Linux SDK is members-only (search snippet, 2026-10-26). The ODA datasheet (read by `pdftotext`, 2026-10-26) says the SDK supports "file versions from AutoCAD 12 (AC1009) through AutoCAD 2025 (AC1032)". It does not mention Linux.

## 3. Isolation design (decision 2)

### 3.1 What the existing helpers do

The SKP, IFC and STEP helpers run in a new session (`setsid`) with `PR_SET_PDEATHSIG` (`src/io/skp_worker.cpp:86–87`, `src/io/ifc_worker.cpp:162–163`, `src/io/step_worker.cpp:260–261`). The application's supervisor sets their resource limits. They have the user's file access and the network. Nothing in `src/` installs a seccomp filter, applies Landlock or creates a network namespace. (A grep for the names returned only unrelated words, such as "unshared", 2026-10-26.) ADR 0171 §1.6 says so directly: "This contains accidents and resource exhaustion; it is not a sandbox."

ADR 0176 decision 2 says the SKP helper is "sandboxed as in ADR 0171 §1.6". That describes limits, not a sandbox. This ADR does not edit ADR 0176. §3.6 proposes a separate decision for the other helpers.

### 3.2 The DWG helper's layers

The helper has four layers, applied in this order:

1. **Launch (ADR 0171 §1.6, as the SKP helper does).** A private 0700 copy of the input, with its SHA-256 checked. A scrubbed environment and a fixed argv, with no shell. `setsid` and `PR_SET_PDEATHSIG`. `PR_SET_NO_NEW_PRIVS`. `RLIMIT_AS` 4 GiB, `RLIMIT_CPU` 480 s and `RLIMIT_FSIZE` 512 MiB. A wall-clock deadline of 120 s enforced by the parent, with cooperative cancel. The `P`/`R` line protocol.
2. **Seccomp-bpf filter, before the first byte of DWG is read.** The helper installs it itself. Its architecture is checked first (x86_64 and aarch64), and the process is killed on a mismatch. These calls return `EPERM`:
   - network creation: `socket`, `socketpair` (`connect`, `bind`, `listen` and `accept` are then unreachable);
   - execution: `execve`, `execveat`;
   - process and thread creation: `fork`, `vfork`, `clone`, `clone3`, since LibreDWG needs no threads in this design;
   - `ptrace`.

   The probe (§3.4) used a deny-list. **R106.b should use an allow-list**, built from `strace` runs over the fixtures and the probe corpus. A deny-list fails open on any syscall it forgot.
3. **Landlock ruleset (ABI 1 or later), applied after the filter and before the parse.** Filesystem access is restricted to the private directory: read for the input copy, write for the output package. The Landlock docs say the feature "was first introduced in Linux 5.13 but it must be configured at build time", that no special privilege is needed, and that "for unprivileged processes, setting the no_new_privs attribute is required by Landlock" (kernel docs, read 2026-10-26, <https://docs.kernel.org/userspace-api/landlock.html>). ABI 4 adds TCP bind and connect rules, but seccomp already removes sockets, so the helper does not rely on ABI 4.
4. **Fail closed.** If `PR_SET_NO_NEW_PRIVS`, the seccomp filter or the Landlock ruleset cannot be installed, the helper exits with a named refusal before it parses anything. There is no fallback to limits alone. §3.5 covers the owner's choice on Landlock.

### 3.3 What this does not stop

- **Memory corruption inside the allowed calls.** A compromised parser can still corrupt its own memory, read its input and write its output. It can return a crafted package. The application's checked decoder (range checks, bounded counts and chunk SHA-256s, as in ADR 0176 §3) and fuzzing of the decoder are the defence for that. This is the reason R106.b includes both.
- **Denial of service within the limits.** A hang is stopped by the deadline, a memory exhaustion by `RLIMIT_AS`, and a CPU loop by `RLIMIT_CPU`. The 2026 CVEs in §1 are mostly out-of-bounds reads, so they usually crash the helper. The sandbox contains the crash. It does not prevent it.
- **The kernel.** Seccomp and Landlock depend on the kernel. A kernel bug is outside this design.
- **Files the helper can already reach.** Landlock limits access to the private directory, but the helper's own shared libraries are loaded before the restriction is applied.
- **Filter gaps.** A deny-list, and any allow-list drawn from a short trace, can miss a call. §8 (W6) tests the denials directly.

### 3.4 Measured on this kernel (throwaway probe, 2026-10-26)

`.local/r106-probes/scripts/sandbox_probe.py` forks a control child and a filtered child. The filtered child installs `PR_SET_NO_NEW_PRIVS`, a seccomp filter that returns `EPERM` for `socket`, `socketpair`, `execve` and `execveat`, and a Landlock ruleset that allows only a temporary directory. The kernel is `7.2.8-9-omarchy`.

| Check | Control (no filter) | Filtered |
|---|---|---|
| `socket(AF_INET, SOCK_STREAM)` | allowed | **EPERM** (errno 1) |
| read a file inside the allowed directory | allowed | allowed |
| read a file outside the allowed directory | allowed | **EACCES** (errno 13) |
| `execv("/bin/true")` | exec succeeded | **EPERM** |

- The kernel offers the mechanisms: `/sys/kernel/security/lsm` lists `landlock` (read 2026-10-26), and `/proc/sys/kernel/seccomp/actions_avail` lists `kill_process kill_thread trap errno user_notif trace log allow`.
- The seccomp man page (man-pages 6.19, footer 2026-06-05, <https://man7.org/linux/man-pages/man2/seccomp.2.html>) says the filter needs `no_new_privs` or `CAP_SYS_ADMIN`, and gives its history as "Linux 3.17". It lists the actions, and says filters can inspect syscall arguments. It does not say that pointer arguments cannot be inspected, so I have not claimed that.
- The probe ran with `python3 -I` and printed one `DeprecationWarning` about the layout of the Landlock struct. The warning does not change the result.

### 3.5 Network namespaces: not relied on

- ADR 0179's V4 test uses `unshare -n`. In this session `unshare -n true` failed with "Operation not permitted". The kernel's settings allow the feature: `/proc/sys/kernel/unprivileged_userns_clone` is 1 and `/proc/sys/user/max_user_namespaces` is 125570. So the failure is probably the session's own sandbox, but I did not establish the cause.
- Distribution policy on unprivileged user namespaces varies. I did not check the target distributions. The DWG helper therefore uses seccomp for network denial, and namespaces are only an extra layer where they work.

### 3.6 Retrofit to the other helpers (owner decision 8)

Applying the same filter to `sketchyup-skp-worker`, `sketchyup-ifc-worker` and `sketchyup-step-worker` would make ADR 0176's wording true. It is a separate change with its own ADR and its own tests. It is not part of R106.

### 3.7 Refusals

Each refusal below is named and leaves the document unchanged:

- `notDwg`: the file does not start with an AC code (checked by the application before the helper runs);
- `tooLarge`: over the input budget (§9, checked before the helper runs);
- `unknownVersion`: an AC code not in §4;
- `encrypted`: a password flag, where the header shows one (detection to be confirmed in R106.b);
- `sandboxUnavailable`: a filter or Landlock layer cannot be installed (fail closed);
- `parse`: the library fails;
- `helperStopped`: a crash, a deadline or a memory limit.

## 4. Versions (decision 5)

| AC code | Release | Files | Skip messages | Buffer-overflow errors | Files with error lines | Tier |
|---|---|---|---|---|---|---|
| AC1009 | R11, R12 | 3 | 0 | 0 | 0 | Best effort; three files are too few to promote |
| AC1012 | R13 | 1 | 3 | 0 | 1 | Best effort, labelled |
| AC1014 | R14 | 4 | 7 | 0 | 2 | Best effort, labelled |
| AC1015 | 2000 | 24 | 12 | 0 | 5 | **Supported** |
| AC1018 | 2004 | 22 | 20 | 3 | 5 | **Supported**, with notices |
| AC1021 | 2007 | 19 | 16 | 0 | 19 | Best effort: every file has page-map or preview errors ("Invalid num_pages", 19 lines; "Preview overflow", 18 lines) |
| AC1024 | 2010 | 19 | 10 | 0 | 4 | **Supported** |
| AC1027 | 2013 | 20 | 363 | 37 | 9 | Best effort: the most skips and overflows in the corpus |
| AC1032 | 2018, and AutoCAD 2025 per the ODA datasheet | 21 | 7 | 22 | 5 | Best effort: 22 buffer-overflow errors |
| AC1003, AC1004, AC1006, "AC1.40", "AC2.10" | R1.x to R2.x | 8 | — | — | — | Refused: before R12 |
| Any other code | — | — | — | — | — | Refused: `unknownVersion` |

The counts come from the corpus (§4.1). "Skip messages" are lines of `dwg2dxf`'s text output, not object counts.

- **"Supported" means import, read-only, with a notice for every object the library skips.** It does not mean that every entity is read. The helper must count skipped objects through the library's API (§1), not from the text output. If the API cannot report skipped objects for a version, that version drops to best effort.
- **AC1032 is the newest code named in the sources.** The ODA datasheet names it for AutoCAD 2025. AutoCAD 2026 and later may use the same code or a new one. I did not verify that. A new code is refused, not guessed.
- **The DXF version gate (`src/io/dxf_source.cpp:316–317`) is a different set.** It includes AC1009 for DXF and has no AC1012 or AC1014. DWG's set is wider, because DXF's is a text format and DWG's is not.
- **No DWG export in 1.x (decision 6).** This build writes R2000 with a clean read-back. It writes R2004 and later files that read back with no entities, and it does not write R2007 at all (§4.1). An export would produce files that Autodesk software may reject. The ODA SDK writes every version, but it is rejected for other reasons (§2.7).

### 4.1 Probe: LibreDWG on its own test corpus and a write round trip (2026-10-26)

**The corpus.** LibreDWG's `test/test-data` at commit `a8ccb1f3`: 141 DWG files. `dwg2dxf -y` wrote a DXF for every file, with exit status 0. The slowest file took 0.19 s, including the DXF write (`Dynblocks.dwg`, AC1032). The probe measured only what the library wrote. It does not show what a reader would import.

- The DXF output carries `$INSUNITS` in 125 files: inches in 110, millimetres in 13, metres in 1 and unitless in 1. The other 16 files have none.
- Entities written, across all files: LINE 2,094, SPLINE 366, ARC 290, LWPOLYLINE 205, INSERT 225, DIMENSION 187, TEXT 103, CIRCLE 70, ELLIPSE 64, MTEXT 49, POLYLINE 51, HATCH 13.
- The stderr messages are the source of the skip and error counts in §4. "Unstable Class" warnings appear in every version from R2000 on. They are one line per object, for example 756 for MATERIAL.

**The write round trip.** A nine-entity DXF was generated with ezdxf 1.4.5 (in a probe-only virtualenv; ezdxf's licence not checked). It is R2000, with `$INSUNITS` 4, four layer states (on, off, frozen, locked), a LWPOLYLINE with a bulge of 0.5, an ARC, a CIRCLE, a TEXT and four LINEs. Each version was written with `dxf2dwg -y --as <version>` and read back with `dwg2dxf`.

- **R2000 (AC1015):** write exit 0. The read-back has all 8 entities (LINE 4, ARC, CIRCLE, LWPOLYLINE, TEXT), with the same layer kinds and states, `$INSUNITS` 4, the bulge of 0.5 and the arc's angles. The writer logged two "Duplicate handle" errors and still exited 0. ezdxf reported two problems in the read-back DXF: a non-unique handle and an ENDBLK without a BLOCK.
- **R2004 (AC1018):** write exit 0, output 1.60 MB, read back 0 entities. The reader logs `BLOCK_HEADER *Model_Space first_owned_entity missing`, and `dwgread -v1` still prints `SUCCESS`.
- **R2007 (AC1021):** write exit 1. The writer's help says "Encoding currently does not work for r2007".
- **R2010 (AC1024), R2013 (AC1027), R2018 (AC1032):** write exit 0, read back 0 entities. R2010 wrote 0.57 MB and R2018 0.30 MB. Only R2018's error text was read, and it shows the same Model_Space message.

So this build's R2004+ writer does not round-trip. That contradicts the 0.14 NEWS entry for this build. The fixtures use R2000 only (§8.1).

**The sandbox.** The mechanism probe is in §3.4.

## 5. Mapping onto the existing DXF import (decision 4)

The DWG helper does not produce DXF text, and the application does not run the DXF parser on DWG. The helper writes a bounded package (a new format, provisionally `SKDWGP01`, with the same envelope as `SKSKPP01` in ADR 0176 §3). The application decodes it with a checked decoder into the `DxfSource` structures (`src/io/dxf_source.hpp`: `DxfEntity`, `DxfSegment`, `DxfLayer`). It then calls `importDxf` unchanged. Bodies, tags, locked state, analytic curves, the caps and the one-undo edit are therefore shared with DXF.

**Rejected alternative: the helper writes DXF text and `parseDxf` reads it.** It is simpler and reuses a tested parser. But the result is a DXF conversion, which is the thing R106 says to keep apart. It would also inherit DXF's 64 MiB text limit and lose the DWG-native provenance that §6 needs.

| DWG source | `DxfSource` result | Notes |
|---|---|---|
| LAYER table: name, on or off, frozen, locked | `DxfLayer.hidden` (off or frozen), `locked` | As DXF (ADR 0123) |
| Layer name | Native tag | 256 UTF-8 bytes, as ADR 0120 |
| LINE | Segment with sweep 0 (wire) | |
| ARC, CIRCLE | Segment with centre and signed sweep (analytic curve, ADR 0121) | A circle has sweep 2π |
| LWPOLYLINE (bulges, closed flag) | Segments converted from bulges by the helper, with endpoints checked by `importDxf` | As ADR 0120 |
| 2D POLYLINE with VERTEX and SEQEND | As LWPOLYLINE | As ADR 0120 |
| Extrusion other than (0, 0, 1), or nonzero thickness | Omitted, reason `extrusion` | As DXF. Converting arbitrary OCS is a later decision (owner decision 12) |
| ELLIPSE, SPLINE, TEXT, MTEXT, DIMENSION, HATCH, INSERT, 3DFACE, SOLID, 3DSOLID, MESH and the rest | Omitted, counted by DWG type name | ELLIPSE and SPLINE are outside the DXF subset too. INSERT blocks are not expanded, as in DXF |
| Paper-space entities | Omitted, counted | As ADR 0120 |
| Blocks, objects and other sections | Counted | As ADR 0120 |
| Objects the library skips | Counted under `unreadable`, with the object's handle as the sample | From the library's object-level status, not its text output (§1, §4.1) |

- **Units.** `$INSUNITS` maps as in DXF: inches, feet, millimetres, centimetres and metres. A missing, unitless or other value needs a caller scale, with the same bounds (1e-6 to 1e6 metres per unit). The probe found `$INSUNITS` in the DXF that LibreDWG writes for 125 of 141 corpus files, and the round trip kept millimetres (§4.1). The probe did not read the DWG header's own variable directly. R106.b must confirm it through the library's API.
- **Caps.** The same as DXF: 10,000 entities, 100,000 segments, 1,024 layers, 256-byte names, 128 distinct omitted keys.
- **Coordinates.** The helper converts object coordinates to world coordinates before writing the package. The application checks that they are finite and within the native range, as DXF does.

## 6. DWG versus conversion: reports and tests (decision 4 and the roadmap's Verify line)

### 6.1 The report

- A new sheet, `dwgReport`, titled "DWG import report". The DXF sheet (`dxfReport`) is unchanged, and the two never share a title.
- The report's JSON carries `"format": "dwg"`, `"reader": {"library": "LibreDWG", "version": "<pinned commit>", "helper": "sketchyup-dwg-worker <version>"}`, `"dwgVersion"` (for example `"AC1027"`) and `"release"` (for example `"AutoCAD 2013"`). It also carries `"intermediate": "none"` and `"externalConverter": false`, the object counts read and skipped by DWG type name, and the DXF-shaped counts.
- The banner says: "Read directly from the .dwg file by the built-in helper. No external converter ran."
- Its notices are its own. They do not repeat DXF's wording.

### 6.2 Tests that tell them apart

- **T1, canary.** The test harness puts fake `dwg2dxf`, `dxf2dwg`, `dwgread` and `ODAFileConverter` programs on `PATH`. Each records its invocation. The DWG import test asserts that none ran, and that the helper was the only child process. This is also check W6(a).
- **T2, provenance.** A DWG import's report has `format` `"dwg"` and carries `dwgVersion` and `helper`. A DXF import's report has `format` `"dxf"`, and carries none of the DWG fields.
- **T3, the wrong-format workaround.** A DWG file renamed `.dxf` is refused by the DXF parser (ADR 0120 rejects binary input). A DXF saved as `.dwg` is refused by the helper's header check.
- **T4, counts from the fixture's generator.** For each fixture, the expected counts come from the generator's own record, not from `dwg2dxf`. DWG is judged on what the generator wrote.
- **T5, differential, local only.** On the owner's machine, the DWG counts are compared with a conversion made by a user (`dwg2dxf` or the ODA converter), recorded as numbers. A difference is a finding to triage, not a failure. Nothing in CI depends on it.
- **T6, the dialogs.** The File menu's DWG item and DXF item open different sheets with different titles (the dialog suite, as `skp_import_dialog_tests`).
- **T7, the documents.** The support matrix row for DWG is separate from the DXF row. The DXF row keeps "no full DWG claim" (ADR 0123).

### 6.3 The conversion workaround, documented as such

The user guide gets a page "Converting a DWG yourself". It describes `dwg2dxf` and the ODA File Converter, says that the result is imported through the DXF import and reports as DXF, and says that this is not DWG support. SketchyUp does not install, bundle or invoke either program (§2.5, Q5).

## 7. Packaging and notices (decision 3)

- **A new optional package, `sketchyup-dwg`.** It carries `/usr/lib/sketchyup/sketchyup-dwg-worker`, linked statically with the pinned LibreDWG subset. The license field is `GPL-3.0-or-later`. The main package stays MIT, with no GPL binary. This follows the IFC split (`packaging/arch/PKGBUILD`, the `sketchyup-ifc` component, lines 69 to 80). The main package's license array (line 20) does not change.
- **Why not the main package.** The `.skp` helper ships in the main package because it is MIT. A GPL helper there would make the main package's licence depend on counsel's answer, and the DWG feature should not put that risk on every install. Counsel may still decide that the optional package is also unacceptable (§2.3).
- **Notices.** `share/licenses/sketchyup/LibreDWG-COPYING` (the GPLv3 text) and `share/doc/sketchyup/DWG-SOURCE.txt`, which records the pinned commit, the archive SHA-256, the patches, the licence, the written source offer and the non-affiliation note. The CMake install lines follow `CMakeLists.txt:1628–1630`, which install the OpenSKP notices.
- **Vendoring.** `third_party/libredwg/`, one pinned commit, the subset the reader needs. The writer, the Python and SWIG bindings, the tests, the JSON input (jsmn) and the `dwgfilter` tools are left out unless the reader needs them. `VENDOR.txt` records a SHA-256 for each file. `scripts/vendor-libredwg.py check` rebuilds the tree from the commit and fails on drift, as `scripts/vendor-openskp.py` does. The subset's size is to be measured in R106.b.
- **Records.** `dependencies.json` gains a LibreDWG entry: version or commit, linkage "static, linked only by sketchyup-dwg-worker", licence GPL-3.0-or-later, source, patches, and this ADR.
- **Linkage check.** `scripts/verify-dwg-library.py`, like `scripts/verify-skp-library.py`: L1, only `sketchyup-dwg-worker` carries LibreDWG symbols; L2, its dynamic dependencies are the C runtime and nothing else; L3, the notices and the source record are installed.
- **Not the AUR.** The AUR package `libredwg` (0.13.3-1, per a search snippet, last updated 2024-02-26) is not an option. ADR 0171 rejected AUR dependencies, and the official repositories do not carry the package (2026-10-26).

## 8. Fixtures and verification (decision 9)

### 8.1 Fixtures

- **Committed, for CI: R2000 (AC1015) only.** The probe's round trip (§4.1) shows that LibreDWG's writer produces R2000 files that read back exactly. A generator script, committed with the files, writes a DXF (its licence checked before use), converts it with `dxf2dwg -y --as r2000`, and records each fixture's SHA-256. The content is the probe's: a box, an arc, a circle, a bulged LWPOLYLINE, the four layer states, `$INSUNITS` set to millimetres, and one ELLIPSE and one TEXT for the omission path. Expected counts come from the generator, not from the reader.
- **The limit of self-written fixtures.** A fixture written and read by the same library proves regressions, not fidelity. That is the same rule as ADR 0176 §7. Fidelity is checked against files the library did not write.
- **No committed R2004 to R2018 fixtures.** LibreDWG's writer cannot produce readable ones (§4.1), and a licence-clean source would be needed for real ones. I found none. The search for a permissive DWG corpus (2026-10-26) returned only LibreDWG mailing-list threads from 2010 and 2011, which show that licences for sample DWG files were never settled. Those threads are historical leads only.
- **The owner's files, local only.** AutoCAD saves from 2007, 2010, 2013 and 2018, a textured or annotated drawing, and one large model for timing. These are never committed. Only numbers are recorded, as for `.skp` (ADR 0176 §7).
- **LibreDWG's test corpus.** The repository's `test/test-data` holds 141 DWG files (2026-10-26, commit `a8ccb1f3`). They are used only for the probe and, on the owner's machine, as fuzzing seeds. They are never committed or vendored. Their individual licences are not stated.
- **ODA-produced reference DXF.** Local only, if the owner agrees (Q5). Only numbers are recorded.

### 8.2 Checks (W1 to W9)

- **W1, header and version gate.** AC1015 to AC1032 accepted; AC1002 and AC1006 refused; an unknown code refused; an encrypted header refused.
- **W2, accounting.** Layers, and entities by DWG type, satisfy imported + omitted = counted, as in ADR 0176 §4.
- **W3, units.** Each `$INSUNITS` value maps as §5 says. A unitless drawing needs a caller scale.
- **W4, bounding box.** Within 1e-6 m of the reference the generator recorded.
- **W5, the distinction.** T1 to T3 and T6 pass.
- **W6, sandbox.** (a) `socket` and `execve` return EPERM inside the helper. (b) A read outside the private directory returns EACCES. (c) A killed helper leaves the document unchanged. (d) A killed helper's temporary directory is removed at the next start, as the IFC helper's is.
- **W7, fuzzing.** The package decoder (`parser_fuzz_dwg_package`) and a libFuzzer target on the reader, both under ASan and UBSan in the sanitizer shard, with no crashes in a fixed run count.
- **W8, refusals.** Each refusal in §3.7 is named and leaves no partial model (as ADR 0176 §5). The helper's fault injection (`--test-fault crash|abort|hang|alloc:N`, as in the IFC helper) is covered.
- **W9, the owner's files.** The counts and the bounding box, recorded as numbers, and the owner's file is never copied into the tree.

## 9. Budgets (proposed; R106.b measures)

| | Budget | Rationale |
|---|---|---|
| Input file | **128 MiB**, checked before the helper starts | DWG is binary and denser than DXF text. IFC's limit is 128 MiB, and `.skp` has 256 MiB because of textures. The DXF limit is 64 MiB of text, which is not comparable |
| Helper | 4 GiB address space, 120 s deadline, 480 s CPU, 512 MiB output | The `.skp` limits (ADR 0176 §6) |
| Package | 32 MiB manifest, 64 MiB total | Smaller than `.skp`, because the package carries only the planar subset |
| Entities and layers | 10,000 entities, 100,000 segments, 1,024 layers | The DXF caps (ADR 0120) |
| Parse time | Measured in R106.b | Probe: at most 0.19 s per corpus file for `dwg2dxf`, including the DXF write, on this machine (§4.1) |
| Application memory during import | At most 1 GiB | The R082.w budget |

## 10. Risks

1. **Legal (highest).** The GPL helper (§2); the reverse-engineered format (§2.5); the Autodesk trademark (§2.4); the ODA terms (§2.5, §2.7).
2. **Parser security.** The CVE history in §1 is in the decoder that the helper runs. The most recent entry, CVE-2026-111167 (2026-10-15), has no fix I could verify. The mitigation is the sandbox (§3), the checked decoder (§3.3), fuzzing (W7), and a pin re-check before each release, as ADR 0176 §9 does for `.skp`.
3. **Silent loss.** `dwg2dxf` skips objects and still exits 0 (§4.1), so an exit code cannot show success, and a report built from its text would be wrong. The helper must read each object's status through the library's API. If the API cannot report skipped objects for a version, that version is best effort.
4. **Beta status.** LibreDWG calls itself beta, with 145 open issues on GitHub (2026-10-26). The pin must be explicit, and each upgrade is a reviewed change.
5. **Write support.** This build's R2004+ writer produces files that read back with no entities, and its R2007 writer fails (§4.1). The 0.14 NEWS says otherwise. The fixtures use R2000 only, so this does not block the design, but any future export would need the writer fixed upstream.
6. **Fixture fidelity.** Self-written fixtures prove regressions, not fidelity. Real-file coverage depends on the owner's corpus (§8.1).
7. **Unverified sandbox assumptions.** The probe ran on one kernel. The mechanisms are documented as available since Linux 3.17 (seccomp) and 5.13 (Landlock). The target distributions are not checked. `unshare -n` failed in this session (§3.5).
8. **The conversion path looks like support.** A user converts with `dwg2dxf` and imports the result. The report and the documentation must keep it apart (§6).
9. **Coverage gap.** Objects outside the DXF subset (ELLIPSE, SPLINE, TEXT, hatches, blocks) are omitted and counted. Users will read that as a failure. The support matrix must say so.
10. **Scope creep.** Users will ask for export, blocks and annotation first. Each is a separate decision.
11. **The ODA route returns.** If a later owner decision picks the ODA SDK, its membership terms, redistribution limits and Linux availability need review again.

## 11. Implementation split (conditional; no slice starts before counsel's answer)

- **R106.a — Decisions and counsel (no code).** Ask Q1 to Q6 once. Settle owner decisions 1 to 4. Gate: counsel's answers to Q1 to Q3 are recorded.
- **R106.b — Reader and helper.** Vendor the pinned subset. Build `sketchyup-dwg-worker` with `--probe` (version, container, units, counts, skipped objects) and `--import` (the package). Add the sandbox (§3), an allow-list derived from traces, the package `SKDWGP01` and its checked decoder, the generator and the committed fixtures, and the fuzz targets. Verify W1, W2, W4, W6, W7 and W8. Record parse times and package sizes. Gate: those checks pass in CI, and the owner's corpus numbers are recorded.
- **R106.c — Mapping and application.** Decode into `DxfSource` and call `importDxf` unchanged. Add the `dwgReport` sheet. File › Import DWG (.dwg)…, with the reused R097.e1 import screens and no options dialog, as `.skp` (ADR 0176 §9, R102.c). `sketchyup-cli --import-dwg` with `--dwg-deadline` and `--dwg-worker`. Verify W3 and W5 with T1 to T6. Gate: the dialog suite and the CLI tests pass.
- **R106.d — Packaging and publication.** The `sketchyup-dwg` component, the notices and `DWG-SOURCE.txt`, `verify-dwg-library.py`, the sanitizer and fuzz lines, the user guide (§6.3), the support matrix row, and the pin re-check against upstream before release. Gate: the package acceptance passes in the pinned image.
- **Optional, owner decision 8: the retrofit.** The sandbox layer for the SKP, IFC and STEP helpers, under its own ADR.

## Owner decisions

1. **Verdict: conditional import through a sandboxed LibreDWG helper** (§11). *Recommended: yes.* The other verdicts are "user-mediated conversion only", which is the documented fallback, and "not viable for 1.x", which applies to the ODA SDK.
2. **Library: LibreDWG (GPL-3.0-or-later), vendored and pinned.** *Recommended: yes.* acadrust (MPL-2.0) is the fallback if counsel rules out a GPL helper. The ODA SDK is rejected for 1.x (§2.7).
3. **Packaging: a separate optional `sketchyup-dwg` package, not the main package.** *Recommended: yes.* The main package stays MIT, and the GPL risk stays out of the default install. Counsel may still decide that the optional package is also unacceptable.
4. **Counsel.** *Recommended: ask Q1 to Q6 once, before R106.a's gate* (§2.6). The GPL aggregate question and the reverse-engineering question matter most.
5. **Versions.** *Recommended: AC1015, AC1018 and AC1024 supported. AC1009, AC1012, AC1014, AC1021, AC1027 and AC1032 best effort, labelled. Codes before R12, and unknown codes, refused.* The corpus numbers are in §4. A version moves up only after the owner's corpus and R106.b's fixtures measure it.
6. **Import only.** *Recommended: no DWG export in 1.x.* This build's writer fails for R2007 and produces unreadable R2004+ files (§4.1).
7. **Sandbox fails closed.** *Recommended: yes, for the seccomp filter and `PR_SET_NO_NEW_PRIVS`, and for Landlock.* The Landlock requirement means a kernel with Landlock enabled in its boot LSM list. If the owner prefers a degraded mode, it must show a named notice, not a silent weaker run.
8. **Retrofit the sandbox to the SKP, IFC and STEP helpers** (§3.6). *Recommended: yes, as a separate ADR.* ADR 0176's "sandboxed" wording should match what runs.
9. **Fixtures.** *Recommended: committed fixtures are written by LibreDWG's own writer and generated by a committed script. Owner files stay local. LibreDWG's test corpus is never committed or vendored.* This follows ADR 0176 §7.
10. **Conversion workaround.** *Recommended: document `dwg2dxf` and the ODA converter as user-mediated conversions, labelled as such. SketchyUp never bundles or invokes them (§6.3, Q5).*
11. **Password-protected DWG.** *Recommended: refuse, and never attempt decryption (Q4).*
12. **Extrusion.** *Recommended: keep the DXF rule (world XY only, +Z, zero thickness) in R106.b. Converting arbitrary OCS is a later decision.*
13. **Naming and budgets.** *Recommended: "Import DWG (.dwg)…", with the non-affiliation note (§2.4), and the 128 MiB input budget (§9).*

## Sources

Every source below was read on 2026-10-26 unless noted. Where a page carries a date, it is given. Items marked "search snippet" come from search results and were not verified on the page.

- LibreDWG repository: <https://github.com/LibreDWG/libredwg> (WebFetch, read 2026-10-26); GitHub API metadata <https://api.github.com/repos/LibreDWG/libredwg> (curl, 2026-10-26).
- LibreDWG NEWS: <https://raw.githubusercontent.com/LibreDWG/libredwg/master/NEWS> (WebFetch, 2026-10-26).
- LibreDWG manual, version 0.13.4, dated 17 March 2026: <https://www.gnu.org/software/libredwg/manual/html_node/Overview.html> (curl, 2026-10-26).
- LibreDWG homepage: <https://www.gnu.org/software/libredwg/> (curl, 2026-10-26).
- LibreDWG repository tags: `git ls-remote --tags https://github.com/LibreDWG/libredwg` (2026-10-26); the probe clone at commit `a8ccb1f3` (2026-10-22).
- NVD CVE data for "libredwg" (99 results; the 2024+ entries): <https://services.nvd.nist.gov/rest/json/cves/2.0?keywordSearch=libredwg> (curl, 2026-10-26).
- NVD CVE data for "libdxfrw": <https://services.nvd.nist.gov/rest/json/cves/2.0?keywordSearch=libdxfrw> (curl, 2026-10-26).
- libdxfrw: <https://github.com/LibreCAD/libdxfrw> (WebFetch) and <https://raw.githubusercontent.com/LibreCAD/libdxfrw/master/README.md> (curl), both 2026-10-26; GitHub API <https://api.github.com/repos/LibreCAD/libdxfrw> (2026-10-26).
- ODA pricing: <https://www.opendesign.com/pricing.md> (WebFetch, 2026-10-26; no page date shown).
- ODA Community User Agreement, dated 2025-09-05: <https://www.opendesign.com/agreements/2025/en/ODA%20Community%20User%20Agreement%2009-2025.pdf> (saved PDF, text extracted with `pdftotext`, 2026-10-26).
- ODA Drawings datasheet: <https://www.opendesign.com/files/2024-10/Drawings%20Datasheet.pdf> (saved PDF, text extracted with `pdftotext`, 2026-10-26; no date shown).
- ODA File Converter page: <https://www.opendesign.com/guestfiles/oda_file_converter> (WebFetch, 2026-10-26; no date shown).
- ODA membership and distribution (search snippets, 2026-10-26, not verified on primary pages): <https://www.opendesign.com/oda-membership.md>, <https://www.opendesign.com/pricing/individual.md>.
- Arch official repositories: <https://archlinux.org/packages/?q=libredwg> ("0 matching packages found") and <https://archlinux.org/packages/?q=libdxfrw> ("no matching packages"), both WebFetch, 2026-10-26. AUR `libredwg` (0.13.3-1, last update 2024-02-26): a search snippet only. The AUR page itself returned an access challenge.
- Autodesk DXF reference, `$ACADVER` mapping: <https://help.autodesk.com/cloudhelp/2024/ENU/AutoCAD-DXF/files/GUID-A85E8E67-27CD-4C59-BE61-4DC9FADBE74A.htm> (WebFetch, 2026-10-26).
- ezdxf's ODA File Converter add-on: <https://ezdxf.readthedocs.io/en/stable/addons/odafc.html> (WebFetch, 2026-10-26). The statement that ezdxf cannot convert to DWG is from a search snippet of the ezdxf manual.
- acadrust: crates.io API <https://crates.io/api/v1/crates/acadrust> (curl, 2026-10-26; crate 0.6.7, updated 2026-10-24, no licence field); repository `LICENSE` <https://raw.githubusercontent.com/hakanaktt/acadrust/main/LICENSE> (MPL 2.0, curl, 2026-10-26); README <https://raw.githubusercontent.com/hakanaktt/acadrust/main/README.md> (curl, 2026-10-26); GitHub API <https://api.github.com/repos/hakanaktt/acadrust> (2026-10-26).
- ezdwg: PyPI JSON <https://pypi.org/pypi/ezdwg/json> (curl, 2026-10-26; licence field empty).
- RealDWG (search snippets, 2026-10-26, not verified on primary pages): <https://aps.autodesk.com/developer/overview/realdwg-oem>.
- AutoCAD drawing passwords (forum snippet, 2026-10-26, not verified): <https://forums.autodesk.com/t5/autocad-forum/autocad-protected-files/m-p/8950634>.
- GNU GPL FAQ, footer dated 2026/01/13: <https://www.gnu.org/licenses/gpl-faq.html> (curl, 2026-10-26).
- GNU GPLv3 text: <https://www.gnu.org/licenses/gpl-3.0.txt> (curl, 2026-10-26).
- 17 U.S.C. §1201: <https://www.law.cornell.edu/uscode/text/17/1201> (curl, 2026-10-26).
- EU Directive 2009/24/EC, Article 6: <https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32009L0024>. **Not read**: the fetch returned an empty page on 2026-10-26.
- Landlock kernel documentation: <https://docs.kernel.org/userspace-api/landlock.html> (WebFetch, 2026-10-26).
- seccomp(2), man-pages 6.19 (footer 2026-06-05): <https://man7.org/linux/man-pages/man2/seccomp.2.html> (WebFetch, 2026-10-26).
- Kernel LSM list and seccomp actions: `/sys/kernel/security/lsm` and `/proc/sys/kernel/seccomp/actions_avail` (2026-10-26); user namespace settings `/proc/sys/kernel/unprivileged_userns_clone` and `/proc/sys/user/max_user_namespaces` (2026-10-26).
- Probe: `.local/r106-probes/scripts/sandbox_probe.py` (not committed; the output is in `.local/r106-probes/sandbox-probe.out`).
- LibreDWG build and probes (not committed; under `.local/r106-probes/`): the build at commit `a8ccb1f3` (2026-10-22), `scripts/dwg_corpus_probe.sh` with its output in `corpus/`, and the write round trip in `write-probe/`. ezdxf 1.4.5 from PyPI, installed into `venv-ezdxf/` on 2026-10-26; its licence was not checked.
- Repository sources (read in this worktree, 2026-10-26): `docs/decisions/0003-compatibility-gaps.md`; `0120-bounded-planar-dxf-source.md`; `0121`; `0123-native-dxf-interchange-workflow.md`; `0171-ifc-exchange-subset.md` (§1.3, §1.6); `0176-skp-import.md`; `0177` and `0178` and `0179` (on their branches, `docs/r103-ruby-feasibility`, `docs/r104-dc-lc-feasibility` and `docs/r105-hosted-assets-feasibility`); `docs/PR_ROADMAP.md` (R005, R077, R089, R106); `docs/SCOPE.md` (C05); `docs/SUPPORT_MATRIX.md` (line 83); `src/io/dxf_source.cpp:316–317`; `src/io/dxf_import.cpp`; `src/app/window_dxf.cpp:89`; `src/io/skp_worker.cpp:86–87`; `src/io/ifc_worker.cpp:162–163`; `src/io/step_worker.cpp:260–261`; `CMakeLists.txt:1628–1630`; `packaging/arch/PKGBUILD`; `dependencies.json`; `scripts/verify-skp-library.py`.
- **Not verified**: the ODA Linux SDK's platform list and membership terms on primary pages; RealDWG's platforms; the acadrust and ezdwg licence beyond the files above; the NVD status of CVE-2026-62254 and CVE-2026-54610; a fix for CVE-2026-111167; the EU text of Article 6; the password-protection status of LibreDWG's reader; whether the DWG header itself carries `$INSUNITS` (the probe read the DXF that LibreDWG writes); whether the R2004+ writer failure is fixed upstream after commit `a8ccb1f3`; and the R2007 write path, which the writer refuses.
