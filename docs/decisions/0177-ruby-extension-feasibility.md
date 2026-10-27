# 0177 — Ruby extension compatibility: feasibility and isolation

**Status: Accepted (2026-10-27): deferred indefinitely.** The owner deferred Ruby extension compatibility indefinitely on 2026-10-27. C02 stays unsupported, no slice of the implementation split is scheduled, and no counsel question is asked for R103. Decisions 2 to 8 below apply only if the owner ever reopens R103; decision 10 (public claims unchanged) holds. Decision 9's counsel review of the application's own name is not decided here: it remains with the other naming questions (ADRs 0178 to 0180). Originally proposed 2026-10-26. This is the feasibility and isolation design for [R103](../PR_ROADMAP.md#r103) ("Investigate and conditionally implement Ruby extension compatibility"), scope row C02, which [SCOPE](../SCOPE.md) keeps at "Investigate". It is research and design only: no code, no dependency and no compatibility claim. **Verdict: not viable for 1.x. Keep C02 unsupported.** The owner decides on that verdict and on the open questions in [Owner decisions](#owner-decisions). ADR 0134's exclusion of Ruby, ADR 0136's rule that no extension code runs, and ADR 0137's "no compatibility claim" all stand.

## Verdict in brief

- A Ruby layer is possible in principle. CRuby can be hosted on Linux under a permissive licence, and the existing transaction API has the begin, apply, commit and abort operations a Ruby operation needs. Two points remain unverified (§4.3).
- The subset that could work is small. None of the four extensions I could inspect fits it. Dialogs, observers, input tools, file access, native code and GPL licences are the common case.
- The parity test has no oracle on Linux. SketchUp runs only on Windows and macOS, so reference results must come from the owner's machine.
- The legal position for reimplementing the public API is not settled for this project. That needs counsel.
- Recommendation: keep C02 unsupported. Reopen only on the conditions in §5.3.

## Context

- **What exists today (native, declarative).**
  - [ADR 0134](0134-versioned-extension-manifests.md): packages are declarative `.sketchyext` command batches. "Existing extensions for other applications, native modules, Ruby, arbitrary scripts and executable entrypoints are outside this format's compatibility claim."
  - [ADR 0136](0136-bounded-extension-worker.md): `sketchyup-extension-worker` resolves one action per process. Requests are at most 384 KiB, responses 96 KiB, and the default deadline is 10 s (30 s maximum). It "receives no document snapshot", and "No extension-supplied executable code is run."
  - [ADR 0137](0137-native-extension-management.md): management UI. "It does not claim compatibility with existing Ruby or native-code extensions."
  - [AI_MODELING.md](../AI_MODELING.md): "Never claim Ruby/native-code extension support." The [support matrix](../SUPPORT_MATRIX.md) row says "not Ruby extension compatibility".
- **A correction to the brief.** [ADR 0003](0003-compatibility-gaps.md) covers SKP and DWG only. It does not mention Ruby or C02. The Ruby position lives in ADRs 0134, 0136 and 0137, in R080a's contract, and in the support matrix.
- **The scope row.** C02: "Inventory required APIs/runtime behavior and test representative extensions; a new scripting API alone is not compatibility."
- **The roadmap Verify line.** "Each supported extension runs its documented workflows without bypassing transactions; unsupported APIs are explicit. A new native extension API is not evidence of Ruby compatibility."
- **Routes considered:**

  | Route | Licence (verified) | Verdict |
  |---|---|---|
  | CRuby 3.2 or 3.4 (libruby) in a separate helper | 2-clause BSD or Ruby's own conditions ([COPYING](https://github.com/ruby/ruby/blob/master/COPYING)) | **Preferred if reopened** (§2) |
  | mruby in a separate helper | MIT ([LICENSE](https://github.com/mruby/mruby/blob/master/LICENSE)) | Rejected for v1: a second dialect (§2) |
  | TruffleRuby | EPL-2.0, GPL-2.0 or LGPL-2.1, a choice ([README](https://github.com/truffleruby/truffleruby)) | Rejected: embedding needs a JVM (§2) |
  | Run Ruby inside the existing extension worker | — | Rejected: breaks ADR 0136's rule and its bounds (§4.1) |
  | Embed Ruby in the application process | as CRuby | Rejected: no process boundary (§4.1) |
  | Keep C02 unsupported | — | **Recommended** (§5) |

## 1. Inventory

### 1.1 Method

This is a sample, not a survey. It covers what I could inspect by source on 2026-10-26. I shallow-cloned four public repositories and only read and searched them. Nothing was built or run. The clones sit in the git-ignored `.local/r103-research/` and are not vendored. Counts are occurrences of a token in the source, not calls.

### 1.2 Inventory

| Extension (source, read date) | Licence | Ruby code | Native code | Encrypted (`.rbe`/`.rbs`) | Main API families (occurrences) | Fits S1 (§5.2)? |
|---|---|---|---|---|---|---|
| **OpenCutList** (`lairdubois/lairdubois-opencutlist-sketchup-extension`, commit `1ef3e14c`, 2026-10-19) | GPL-3.0 (`LICENSE`) | 370 `.rb` under `src/`, with vendored rubyzip and a UI library (kuix) | None | None | `Geom::Point3d` 258, `Geom::Transformation` 185, `start_operation` 39 and `commit_operation` 39, `add_observer` 24 (`AppObserver` 12, `MaterialsObserver` 7), `UI::HtmlDialog` 11, `UI.messagebox` 15, `UI.savepanel` 9, `File.open` 26, `Dir.mkdir` 13, `write_default` 24 and `read_default` 14, `Sketchup.version_number` 43, `Sketchup.platform` 10. Its kuix library defines the view input callback `onLButtonDown` (`src/ladb_opencutlist/ruby/lib/kuix/kuix.rb:345`) | **No.** Dialogs, observers, input and file I/O. GPL-3.0 also rules out vendoring into an MIT tree |
| **sketchup-stl** (`SketchUp/sketchup-stl`, commit `0b83322e`, 2021-01-04) | MIT, copyright Trimble Navigation Ltd. 2014 | 34 `.rb` | None | None | `UI::WebDialog` 15, `UI.add_event` 13, `File.open` 6, `Sketchup::require` 5, `start_operation` 3, `Geom::PolygonMesh` 2, `Sketchup::Image` 2, `read_default` and `write_default`. It also ships its own widget toolkit, `SKUI` (its `Window`, `Control`, `Listbox` and `Label` are local classes, not SketchUp's API; `src/sketchup-stl/SKUI/`) | **No.** `WebDialog` UI. Reference only (owner decision 8) |
| **open-newer-version** (`Eneroth3/open-newer-version`, commit `46510175`, 2022-08-04) | MIT-style permission text, copyright 2018 Julia Christina Eneroth | 4 `.rb` | **Yes.** `cpp/main.cpp` (54 lines) calls the SketchUp C API (`SUInitialize`, `SUModelCreateFromFileWithStatus`, `SUModelSaveToFileWithVersion`, `SUModelRelease`, `SUTerminate`). The package ships `SketchUpAPI.dll` and `SketchUpCommonPreferences.dll`. Ruby runs the helper through `system` (`src/ene_open_newer_version/open_newer.rb:52` and `:103`) | No | `Sketchup.open_file`, `Sketchup.version`, `Sketchup.undo`, `UI.openpanel`, `UI.savepanel`, `Dir.glob` | **No.** A Windows binary, and it redistributes Trimble DLLs |
| **TT_Lib2** (thomthom; documentation generated 2017, [docs](https://www.thomthom.net/software/sketchup/tt_lib2/doc/index.html), read 2026-10-26) | MIT, except a Win32 component under Artistic 2.0 | Shared library code, used as a dependency by the author's extensions | **Yes.** A Ruby C extension, built with Visual Studio on Windows and `build.sh` on macOS | Not stated | Not counted. The source is on Bitbucket and the clone failed without credentials | **No.** Native |
| **SketchUp `ruby-c-extension-examples`** (official reference, not an extension; [repo](https://github.com/SketchUp/ruby-c-extension-examples), read 2026-10-26) | MIT | n/a (C++ examples) | Examples for Windows (VS 2022) and macOS (Xcode 14) | n/a | Ruby ABI settings for 1.8, 2.0, 2.2, 2.5, 2.7 and 3.2 in the `.props` files | n/a. It shows the native contract, which has no Linux target |
| CleanUp³-type paid utilities (Extension Warehouse) | — | Unknown | Unknown | Unknown | Unknown | **Not inspected.** Their paid-licence checks would use `Sketchup::Licensing` (§3.1) |

Other sample facts:

- Popularity: OpenCutList shows 616 stars on the [GitHub topics page](https://github.com/topics/sketchup) (read 2026-10-26). The same page lists `SketchUp/sketchup-stl` and `Eneroth3/open-newer-version`.
- The Extension Warehouse encryption help says encryption is the developer's choice. The root `.rb` is never encrypted, and `.rbs` was removed ([help page](https://help.sketchup.com/en/extension-warehouse/extension-encryption-and-signing), last updated 2026-09-22, read 2026-10-26).

### 1.3 What the inventory says

- Every inspected extension depends on at least one of: dialogs (`UI::HtmlDialog` or `WebDialog`), observers, input tools, file or preference I/O, or native code. A layer that covered only geometry would have covered none of the four.
- OpenCutList is the only large codebase. sketchup-stl is small, but it depends on its own bundled `SKUI` toolkit over `WebDialog`.
- None of the inspected extensions ships encrypted code. The encrypted case, which is commercial packages, is the one I could not check.

## 2. Platform and runtime

### 2.1 Where SketchUp runs

- The [Ruby API documentation](https://ruby.sketchup.com/) says the API "is available from within SketchUp, it cannot be used by itself." (read 2026-10-26)
- SketchUp's [system requirements](https://help.sketchup.com/en/sketchup/system-requirements) (last updated 2026-10-21; read 2026-10-26, first 100,000 of about 155,000 characters) cover Windows 11 and macOS only. The page says "SketchUp 2027.0 and any future releases will require Windows 11" and that 2027.0 "will require Apple silicon for macOS installation". The visible text has no Linux section. The German and Italian pages reportedly say that no Linux version exists. I saw those only in search snippets and did not fetch them. Some users run SketchUp under Wine. That is unofficial.
- The [C API documentation](https://extensions.sketchup.com/developers/sketchup_c_api/sketchup/index.html) (footer "Generated on Fri Oct 9 2026"; read 2026-10-26) ships as `SketchUpAPI.dll` on Windows and as `SketchUpAPI.framework` on macOS. It describes no shared library for Linux.

Consequence: there is no Linux SketchUp to compare against. Parity could only be checked against the owner's Windows or macOS SketchUp, outside CI (§6).

### 2.2 Which Ruby to target

- [Release notes](https://ruby.sketchup.com/file.ReleaseNotes.html) (read 2026-10-26): 2024.0 says "Upgrade Ruby to 3.2.2". The 2025.0 to 2026.2 entries name no change to the Ruby version. 2027.0 says "we have upgraded the version of Ruby that we ship with SketchUp from 3.2.2 to 3.4.9". I could not confirm the 2027.0 release date.
- A [staff post dated 2026-08-03](https://forums.sketchup.com/t/important-upcoming-ruby-update/348793) (read 2026-10-26) says Ruby 3.4 would ship first in a Labs release. It says: "Most will continue to work seamlessly, though a few might fail if they rely on deprecated features", and "All extensions using Ruby C extension binaries will need to be rebuilt to stay compatible."
- Target: Ruby 3.2 for 2024–2026 extensions, and Ruby 3.4 for 2027.0 and later. A layer would pin one version per release.

### 2.3 Runtime options

| Option | Licence (read 2026-10-26) | Hosting from C++ | Fit with a sandboxed helper | Size and start-up (local proxy only) | Verdict |
|---|---|---|---|---|---|
| **CRuby (libruby)** | "2-clause BSDL (see the file BSDL), or the conditions below" ([COPYING](https://github.com/ruby/ruby/blob/master/COPYING)). Some bundled files have other terms, listed in a `LEGAL` file | The public header declares `ruby_setup`, `ruby_init_stack`, `ruby_cleanup`, `ruby_finalize` and `ruby_options` ([C API docs](https://docs.ruby-lang.org/capi/en/master/d3/d97/interpreter_8h_source.html), read 2026-10-26). Their use for embedding is not verified here | Good in a helper. The API must run on one thread: the docs say "must be performed from the main thread" ([landing page](https://ruby.sketchup.com/)) | Arch Ruby 3.4.10 on this machine: `libruby.so.3.4.10` is 6,361,160 bytes. A bare `ruby -e` process has about 13.4 MiB RSS. Start-up was 40–47 ms over five runs | **Preferred if reopened.** Closest to SketchUp's language; permissive licence |
| **mruby** | MIT ([LICENSE](https://github.com/mruby/mruby/blob/master/LICENSE), read 2026-10-26) | README: "You can link and embed mruby within your application." ([README](https://github.com/mruby/mruby), read 2026-10-26) | Good; small | Not measured | **Rejected for v1.** The README calls it "the lightweight implementation of the Ruby language complying to (part of) the ISO standard". It is a second dialect. The inventory's extensions depend on CRuby's library surface, such as `File`, `Dir`, `StringIO` and rubyzip. Each would need porting |
| **TruffleRuby** | "any one of" EPL-2.0, GPL-2.0 or LGPL-2.1 ([README](https://github.com/truffleruby/truffleruby), read 2026-10-26) | "The minimum Java version for embedding TruffleRuby…"; it runs on GraalVM or the JVM | Poor: a JVM inside a Qt desktop helper | Not measured | **Rejected.** A JVM dependency, a large footprint, and "not 100% compatible with MRI 4.0 yet" |
| Existing `sketchyup-extension-worker` (ADR 0136) | — | — | Wrong shape (§4.1) | — | **Rejected** |
| CRuby in the application process | as CRuby | as CRuby | No isolation | — | **Rejected** |

The local numbers come from Arch's Ruby 3.4.10, not from SketchUp's embedded Ruby. SketchUp's build, its patches and its extension loader are not public, so they must be measured in R103.b.

## 3. Legal and trademark

This section records what is known and what needs counsel. It is not legal advice.

### 3.1 What the sources say

- **Trimble offering terms, version 4.1, last updated 2026-10-13** ([terms](https://www.trimble.com/en/legal/offering-terms/terms/english), read 2026-10-26; the page is truncated, and the restrictions are in §2.4). Section 2.4 says the Customer may not "(d) reverse engineer, decompile, disassemble, or seek to access the source code … or non-public API Tools"; "(f) remove or obscure any proprietary notices in the Offering"; or "(h) circumvent its access restrictions". Section 2.5: "Customer may use the API Tools to develop Customer Applications that interoperate with the Offering", and "Use of API Tools may be subject to additional terms and conditions."
- **The SketchUp Pro licence URL** ([sketchup.trimble.com/license/b/sketchup-pro](https://sketchup.trimble.com/license/b/sketchup-pro), read 2026-10-26) redirects to the offering-terms page. I could not read the Pro licence's own restriction clause. A search snippet quoted an older reverse-engineering clause; I did not verify it. Whether these terms govern the desktop product is for counsel.
- **Trimble APIs agreement.** A forum post says a separate Trimble APIs agreement PDF is linked from the Ruby docs, and that publishing on the Extension Warehouse needs a Developer Agreement. I found neither on [developer.sketchup.com](https://developer.sketchup.com/) or [developer.trimble.com](https://developer.trimble.com/docs/sketchup) (read 2026-10-26). **Not verified.**
- **Encryption.** `Sketchup.require` "is the only way to require one of our encrypted files" ([help page](https://help.sketchup.com/en/extension-warehouse/extension-encryption-and-signing), read 2026-10-26). So decryption belongs to SketchUp's own loader.
- **Licensing.** `Sketchup::Licensing.get_extension_license` returns a state: LICENSED, EXPIRED, TRIAL, TRIAL_EXPIRED or NOT_LICENSED. Since 2025.0 it "automatically tries to fetch a license from Extension Warehouse" when the device has none, and that works only when the user is signed in ([Licensing docs](https://ruby.sketchup.com/Sketchup/Licensing.html), read 2026-10-26).
- **Trimble trademark guidelines, V2.0, last updated 2023-07-05** ([guidelines](https://www.trimble.com/legal/trademark-guidelines), read 2026-10-26):
  - §1: "You may use Trimble Marks to refer to Trimble products or services, as long as such references are appropriate and consistent with these Guidelines."
  - §4: "You may not use, incorporate, or combine any Trimble Marks into your product names, business names, or trade names." Also: "You may not use, register, or apply to register any trademark that is the same as, or confusingly similar to" a Trimble mark. My fetch truncated that sentence.
  - §7: "Trimble Marks must not be used in a way that makes a party think that Trimble sponsors or manufactures … another party's product."
- **Copyright precedent (US only).** *Google LLC v. Oracle America, Inc.*, No. 18-956 (U.S. Apr. 5, 2021), read from the Court's syllabus ([PDF](https://www.supremecourt.gov/opinions/20pdf/18-956_d18f.pdf), read 2026-10-26). The syllabus says the Court "assumes for argument's sake that the copied lines can be copyrighted", and holds that Google's copying of the Java SE API "was a fair use of that material as a matter of law". The syllabus is not part of the opinion. The case concerns one set of facts and does not settle other jurisdictions.
- **Bundled Trimble binaries.** The C API documentation says the application "requires including SketchUpAPI.dll and SketchUpCommonPreferences.dll". open-newer-version ships both (§1.2). Whether that is allowed is not analysed here.

### 3.2 Positions for R103 (proposed, pending counsel)

1. **Clean-room only.** Any implementation would use only the public documentation, at a dated version. It would never decompile SketchUp, read its loader, or use a non-public API. This follows §2.4(d) of the offering terms.
2. **No decryption, no circumvention.** `.rbe` and `.rbs` packages are refused, and so is any attempt to read a `Sketchup.require` payload.
3. **No licence-state spoofing.** `Sketchup::Licensing` returns an explicit unsupported error. It never returns LICENSED or any other state the host did not verify.
4. **No Trimble code, headers or DLLs** in the tree or in any package.
5. **No third-party vendoring without a clear licence.** OpenCutList is GPL-3.0 and cannot be vendored into an MIT application. sketchup-stl is MIT but under Trimble's copyright, so it is a reference only.
6. **Naming.** No product, package or extension name may contain "SketchUp". Any reference follows §1 of the guidelines ("appropriate and consistent"). User text says the feature is not affiliated with or endorsed by Trimble, as ADR 0176 §8 does.
7. **The app's own name is outside this ADR, but counsel should check it.** "SketchyUp" is close to "SketchUp", and §4 bars confusingly similar marks. Every compatibility feature would sit under that name.

### 3.3 Questions for counsel

- Can a non-affiliated program implement the names and signatures of the documented public Ruby API, for interoperability? What does the answer depend on outside the US?
- Do offering terms §2.4 and §2.5 bind a user who runs a third-party host and uses only the public documentation?
- What do the Trimble APIs agreement and any Developer Agreement require? I could not retrieve either.
- May the owner run his own SketchUp to produce reference outputs for our tests? The EULA question is counsel's.
- Is the app's name acceptable under §4 of the guidelines?

## 4. Isolation design (if reopened; not recommended now)

### 4.1 Why the existing worker is not reused

- ADR 0136's worker runs one declarative action per process, with a 384 KiB request limit, a 96 KiB response limit and a 10 s default deadline (`src/automation/extension_worker.cpp` caps the timeout at 30 s). It "receives no document snapshot" and runs "no extension-supplied executable code".
- Ruby needs state across calls: observers, dialogs, and operations that span several calls. A one-shot process cannot hold that.
- A Ruby VM inside that binary would make "no extension code runs" false for every extension action.
- The shape is therefore a sibling helper, `sketchyup-ruby-worker`, with its own protocol. It would use ADR 0171 §1.6's launch hardening: `setsid`, `PR_SET_PDEATHSIG`, `PR_SET_NO_NEW_PRIVS`, `RLIMIT_AS`, `RLIMIT_CPU`, `RLIMIT_FSIZE`, a scrubbed environment, and a wall deadline enforced by the parent.
- Running Ruby in the application process is rejected. A Ruby crash, a runaway loop or a corrupted heap would take down the document and the transaction state.

### 4.2 Confinement beyond ADR 0171

ADR 0171 §1.6 says of its own limits: "This contains accidents and resource exhaustion; it is not a sandbox." Ruby runs arbitrary code, so the helper needs more.

- **Landlock.** The package is copied into a private directory and read from there. Writes go only to that directory. No user documents are reachable. The repo has no Landlock code today (grep of `src/`, 2026-10-26). This machine's kernel (7.2.8) lists `landlock` in `/sys/kernel/security/lsm` (read 2026-10-26). Other target distributions are not checked.
- **seccomp-bpf.** Deny `socket`, `connect`, `bind`, `fork`, `clone` (for new processes), `execve` and `ptrace`. The repo has no seccomp code either.
- **Fail closed.** If Landlock or seccomp is unavailable, the layer refuses to start. It does not fall back to rlimits alone.

### 4.3 The transaction bridge

The Ruby side has no pointer to `Document`. Every mutation goes through the transaction API, and nothing else.

| Ruby call | Bridge operation (names verified in `src/automation/transaction_catalog.cpp`) | Host behaviour |
|---|---|---|
| `Model#start_operation(name, true)` | `transaction.begin` (`expectedRevision`, `ttlSeconds` at most 300, `history`) | Opens one draft. If a draft is already open, refuse. SketchUp implicitly closes the first one; the layer does not, so a commit point is never hidden |
| A mutation (`Entities#add_face`, `add_group`, `set_attribute`, …) | `transaction.apply` (`operationId`, `expectedVersion`, `commands`) | Appended to the open draft. A mutation outside a draft is an error |
| `Model#commit_operation` | `transaction.commit` | Publishes one revision and one undo entry |
| `Model#abort_operation`, a Ruby exception, worker death, or the deadline | `transaction.abort`, or the draft expires | Nothing applied. ADR 0136's rule: "Process failure must leave an uncommitted action unapplied" |
| Reads (`entities.each`, `bounds`) | `transaction.inspect` and `transaction.describe` on the draft | Read-your-writes from the draft, with bounded results |
| `start_operation` with `transparent` set to true | Refused in v1 | SketchUp appends to the previous operation. Merging drafts is R103.d work |
| Observers | Post-commit only, never inside an operation | Unsupported in v1 |

The catalog includes `transaction.begin`, `apply`, `preview`, `commit`, `cancel`, `status`, `abort`, `describe`, `diff`, `inspect` and `reconcile`. The dispatcher's limits are in `src/automation/transactions.hpp`: 4 drafts, 128 MiB retained, and a 300 s maximum TTL.

**Two open technical points (R103.b):**

- Whether `transaction.apply` returns created entity IDs. Ruby code keeps a handle to a new face inside an operation, so it needs them. The batch API has a `CreatedIds` response mode (`src/automation/commands.hpp`), so this looks possible. I did not read the reply path.
- Whether the draft view is fast enough for the reads that extensions do in loops.

**Conflicts.** If the user edits while an operation is open, `expectedRevision` or `expectedVersion` fails the commit. The layer never merges.

**Deliberate differences from SketchUp:**

- A mutation outside an operation is an error. SketchUp allows it. SketchUp's 2026.0 release notes describe page-setting changes as belonging inside operations ([release notes](https://ruby.sketchup.com/file.ReleaseNotes.html)), so SketchUp's own direction is the same.
- A nested `start_operation` is an error, not an implicit close.
- `transparent` is refused in v1.
- The user sees one undo step per operation.

### 4.4 Refusals

A refused or unsupported API raises an exception that names the API. The exception's name must not collide with a SketchUp class. A silent no-op is never allowed.

## 5. Verdict and the supported subset

### 5.1 Verdict: not viable for 1.x; keep C02 unsupported

1. **The common extension is outside any useful subset.** None of the four inspected extensions fits (§1.2).
2. **There is no oracle on Linux.** SketchUp has no Linux build (§2.1). Parity can only be checked against the owner's Windows or macOS SketchUp, outside CI.
3. **The API position is unsettled** (§3). This is the largest risk, and it needs counsel before any code.
4. **The required infrastructure is new.** Landlock and seccomp are not in the repo, and the transaction bridge depends on two unverified points (§4.3).
5. **The reachable value is small** compared with that work. The subset in §5.2 covers only geometry scripts, and none of the inspected extensions is one.

### 5.2 The subset, defined so it can be measured (for a future reopen only)

**S1: pure-Ruby geometry.** A package is in S1 only if all five criteria pass:

- **M1, package.** Only `.rb`, `.json`, `.txt`, `.md`, `.png`, `.jpg` and `.svg` files. No `.so`, `.bundle`, `.dll`, `.dylib`, `.rbe`, `.rbs` or symlinks. Unpacked size at most 2 MiB and 500 files.
- **M2, static scan.** Every `Sketchup::`, `Geom::` and `UI` constant and method the package uses resolves to the allowlist. The allowlist is generated from the public docs at a pinned version. The package has zero hits in a denied family. The required stdlib is on a short allowlist, fixed in R103.c.
- **M3, parity.** For each documented workflow, the owner's reference run on SketchUp matches the layer's result on entity counts by type, bounding box (within 1e-6 m), and undo steps (one per operation). Only the numbers are recorded.
- **M4, atomicity.** Every injected failure (exception, worker kill, deadline) leaves the document revision unchanged.
- **M5, explicit refusals.** Every denied call refuses by name, and each refusal in the list below has a fixture.

**Allowed families (v1):** `Geom::Point3d`, `Point2d`, `Vector3d`, `Transformation` and `BoundingBox`; `Sketchup::Model` operations; `Entities` mutations for faces, edges, groups and component instances; `Face`, `Edge`, `Vertex`, `Group`, `ComponentDefinition`, `ComponentInstance`, and `Material` (read and assign); attributes limited to string, number and boolean values; `Sketchup.active_model`.

**Denied families (v1):** `Sketchup::Tool` and input callbacks; all observers; `UI::*` (HtmlDialog, WebDialog, menus, toolbars, message boxes); `Sketchup::Http`; `Sketchup::Licensing`; `Sketchup.require` outside the package; file and preference I/O outside the private directory; `Sketchup.send_action`; `Sketchup::Importer` and `Exporter`; LayOut; native code.

An extension is **supported** when every documented workflow in a named list passes M2 to M5. A partly passing extension is not supported, and the layer names the workflow that failed.

### 5.3 Conditions to reopen

Reopen only if all of these hold:

- counsel gives an answer to §3.3 that the owner accepts;
- the owner supplies at least three licence-clean extensions and reference runs from SketchUp on Windows or macOS;
- the R103.b spike passes: CRuby hosted in a helper under Landlock and seccomp; `geometry_box` run through begin, apply and commit; `transaction.apply` returns created IDs; a helper killed mid-operation leaves the revision unchanged; network and process creation denied, shown by test;
- the owner accepts the S1 coverage the spike measures.

## 6. Verification plan and fixtures (if reopened)

**Self-written fixtures** (MIT, in `tests/ruby_fixtures/`, committed with their generator):

- Parity: `geometry_box` (one operation, one undo step), `group_and_instance`, `attribute_roundtrip`.
- Atomicity: `abort_rolls_back`, `exception_rolls_back`, `worker_killed_mid_operation`, `deadline_exceeded`, `revision_conflict`.
- Refusals: `nested_operation_refused`, `transparent_refused`, `observer_unsupported`, `tool_unsupported`, `dialog_unsupported`, `licensing_unsupported`, `network_denied`, `process_spawn_denied`, `file_outside_sandbox_denied`, `memory_exceeded`, `native_refused` (contains a placeholder `.so`), and `rbe_refused` (contains a placeholder `.rbe`, which is not an encrypted file).

**Reference runs.** The owner runs the parity fixtures on Windows or macOS SketchUp and records counts, boxes and undo counts. No Wine.

**Real extensions.** None are vendored. The owner chooses which to test locally. The inventory repositories are references, not fixtures.

**CI.** Self-written fixtures only, with the sanitizer shard for the helper. The reference comparison runs locally.

## 7. Budgets (proposed; R103.b measures)

| Item | Budget | Basis |
|---|---|---|
| Package, unpacked | 2 MiB, 500 files | Proposal |
| Helper address space | 2 GiB (`RLIMIT_AS`) | Proposal. ADR 0176's parser has 4 GiB |
| Helper RSS | 512 MiB per session | Proposal. Local baseline 13.4 MiB |
| Start-up to ready | 250 ms | Proposal. Local proxy 40–47 ms |
| Call deadline | 10 s default, 30 s maximum | ADR 0136; `extension_worker.cpp` |
| Operation | 300 s | Dispatcher maximum TTL (`transactions.hpp`) |
| CPU | four times the wall deadline | ADR 0171 §1.6 |
| Files written | `RLIMIT_FSIZE` 16 MiB; 64 MiB in total | Proposal |
| Bridge request | 64 KiB | `transactionRequestBytes` |
| Bridge response | 272 KiB | `transactionResponseBytes` |
| Retained staging | 128 MiB | `TransactionDispatcher::Limits` |
| Open drafts | 4 | `TransactionDispatcher::Limits` |
| Entities | Native caps: 100,000 faces and vertices per body, 300,000 edges | ADR 0176 §4 |
| Network traffic | 0 bytes | seccomp test |

## 8. Risks

1. **Legal (highest).** API reuse, the Trimble terms, the missing APIs agreement, and the app's own name (§3).
2. **No oracle.** CI cannot check SketchUp's behaviour. Differential tests depend on the owner's machine. SketchUp's embedded Ruby may differ from CRuby in ways we cannot see.
3. **Ruby drift.** 3.2 moves to 3.4 at 2027.0. C extensions must be rebuilt, and deprecated features may break pure-Ruby code (staff post, §2.2).
4. **Security.** Arbitrary code runs in a helper. Landlock and seccomp are new work. CRuby needs a security-update process as a packaged runtime.
5. **Scope creep.** Users will ask for dialogs and observers first. Adding them piecemeal breaks the S1 boundary.
6. **Licence contamination.** GPL code (OpenCutList), Trimble's copyright (sketchup-stl) and Trimble DLLs (open-newer-version) must not enter the tree. CRuby's notices and its `LEGAL` list must ship.
7. **User expectations.** A "partly compatible" label invites failure reports. AI_MODELING's rule stands.
8. **Coverage gap.** The paid, encrypted extensions users most want are excluded by design (§3.2, item 2).
9. **Measurement gap.** The sizes and start-up times are for Arch's Ruby 3.4.10, not for SketchUp's runtime.

## 9. Implementation split (conditional; no slice starts now)

- **R103.a: close the open questions (no code).** Counsel's answer to §3.3. The owner supplies licence-clean reference extensions and reference runs. Confirm the `transaction.apply` reply path. Gate: owner decisions 1 and 2 are settled.
- **R103.b: feasibility spike (throwaway, not merged).** CRuby 3.2 hosted in a helper. `geometry_box` through begin, apply and commit. Measure size, start-up and RSS. Show by test that Landlock and seccomp deny network and process creation, and that a killed helper leaves the revision unchanged. Gate: all pass, and read-your-writes works for `add_face` and `add_group`.
- **R103.c: allowlist and refusals.** Generate the allowlist from the public docs at a pinned version. Write the static scanner for M1 and M2. Add the refusal fixtures.
- **R103.d: bridge and transactions.** The mapping in §4.3. Undo and conflict fixtures. Observers after commit only, if the owner wants them.
- **R103.e: packaging and publication.** Notices (COPYING and `LEGAL`), the support-matrix row, the user guide with the non-affiliation note, and the S1 rule in the manifest validator.

Verify line (§5.2): each supported extension runs its documented workflows without bypassing transactions, and unsupported APIs are explicit.

## Owner decisions

1. **Verdict: keep C02 unsupported for 1.x.** *Recommended: yes.* The common extension is outside the subset, there is no Linux oracle, and the API position is open.
2. **Counsel before any code.** *Recommended: ask once, using §3.3.* The main risk is legal, and the sources here are partial or secondary.
3. **Reopen conditions.** *Recommended: reopen only when all four conditions in §5.3 hold.* Without them, no parity claim can be tested.
4. **Runtime if reopened: CRuby, hosted in a separate helper.** *Recommended: CRuby 3.2 for 2024–2026 targets and 3.4 for 2027.0, not the extension worker, not in-process, and not mruby or TruffleRuby.* CRuby is the closest match to SketchUp's language, and its licence is permissive.
5. **Refuse encrypted, native and licence-bypass content.** *Recommended: refuse `.rbe`/`.rbs` packages, native binaries, and any licence answer other than an explicit error.* Decryption and faked purchase state would circumvent access restrictions.
6. **Subset S1 if reopened.** *Recommended: geometry only, as in §5.2, with no UI, observers, tools, network, licence checks or native code in v1.* Those are what block all four inspected extensions.
7. **Reference runs by the owner, not Wine.** *Recommended: the owner runs the parity fixtures on Windows or macOS and records only numbers.* Linux has no SketchUp, and Wine is unofficial with unknown licence status.
8. **No vendoring of third-party extensions.** *Recommended: none. The inventory repositories stay references only.* OpenCutList is GPL-3.0 and cannot enter an MIT tree, and sketchup-stl is under Trimble's copyright.
9. **Trademark and naming.** *Recommended: no "SketchUp" in any product or extension name, and counsel reviews the app's own name.* §4 of the guidelines bars confusingly similar marks, and the risk sits above every compatibility feature.
10. **Public claims unchanged.** *Recommended: keep AI_MODELING's rule and the support-matrix row.* No evidence yet supports a claim.

## Sources

Each page was read on 2026-10-26 unless noted. "Last updated" dates are the page's own.

- SketchUp Ruby API home: <https://ruby.sketchup.com/>
- Class list: <https://ruby.sketchup.com/class_list.html>
- Model operations: <https://ruby.sketchup.com/Sketchup/Model.html>
- Licensing: <https://ruby.sketchup.com/Sketchup/Licensing.html>
- Release notes: <https://ruby.sketchup.com/file.ReleaseNotes.html>
- Ruby 3.4 staff post (dated 2026-08-03 by the post): <https://forums.sketchup.com/t/important-upcoming-ruby-update/348793>
- Extension encryption and signing (last updated 2026-09-22): <https://help.sketchup.com/en/extension-warehouse/extension-encryption-and-signing>
- System requirements (last updated 2026-10-21): <https://help.sketchup.com/en/sketchup/system-requirements>
- C API documentation (footer generated 2026-10-09): <https://extensions.sketchup.com/developers/sketchup_c_api/sketchup/index.html>
- Official C extension examples (MIT): <https://github.com/SketchUp/ruby-c-extension-examples>
- SketchUp Pro licence (redirects to the offering terms): <https://sketchup.trimble.com/license/b/sketchup-pro>
- Trimble offering terms v4.1 (last updated 2026-10-13): <https://www.trimble.com/en/legal/offering-terms/terms/english>
- Trimble trademark guidelines V2.0 (last updated 2023-07-05): <https://www.trimble.com/legal/trademark-guidelines>
- Trimble developer pages (no APIs agreement found): <https://developer.sketchup.com/>, <https://developer.trimble.com/docs/sketchup>
- *Google LLC v. Oracle America, Inc.*, No. 18-956, syllabus: <https://www.supremecourt.gov/opinions/20pdf/18-956_d18f.pdf>
- CRuby COPYING: <https://github.com/ruby/ruby/blob/master/COPYING>
- CRuby C API hosting functions (Ruby 4.1.0dev page): <https://docs.ruby-lang.org/capi/en/master/d3/d97/interpreter_8h_source.html>
- mruby README and LICENSE: <https://github.com/mruby/mruby>, <https://github.com/mruby/mruby/blob/master/LICENSE>
- TruffleRuby README: <https://github.com/truffleruby/truffleruby>
- TT_Lib2 documentation (generated 2017): <https://www.thomthom.net/software/sketchup/tt_lib2/doc/index.html>
- GitHub topics for SketchUp (star counts): <https://github.com/topics/sketchup>
- Repositories read by shallow clone (read and searched only): <https://github.com/lairdubois/lairdubois-opencutlist-sketchup-extension> (commit `1ef3e14c`, 2026-10-19); <https://github.com/SketchUp/sketchup-stl> (commit `0b83322e`, 2021-01-04); <https://github.com/Eneroth3/open-newer-version> (commit `46510175`, 2022-08-04)
- Local kernel LSM list (`/sys/kernel/security/lsm`, kernel 7.2.8) and the local Ruby 3.4.10 measurements, 2026-10-26.

**Not verified:** Trimble's APIs agreement and Developer Agreement; the offering terms beyond §2.4 and §2.5; the Pro licence's own clause (only the redirect was read); the German and Italian "no Linux" statements (search snippets only); Wine reports (search snippets only); the *Google v. Oracle* holding outside the US; CRuby embedding (not built); SketchUp's embedded Ruby build; the CleanUp³-type paid extensions (not inspected); TT_Lib2's source (the Bitbucket clone failed); the transaction reply path for created IDs; Landlock on target distributions; and SketchUp's runtime size and start-up.
