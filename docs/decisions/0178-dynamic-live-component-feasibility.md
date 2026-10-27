# 0178 — Dynamic and Live Component interoperability: feasibility

**Status: Accepted (2026-10-27).** The owner approved decisions 1 to 5, 7 and 9 as recommended on 2026-10-27. Formula mapping (R104.b) is **deferred indefinitely**, as R103 is, so decision 8 (counsel) is not exercised. On decision 6 the owner has no sample files and asked for them to be made: R104.a generates self-authored Dynamic Component fixtures with OpenSKP's writer (see the decision). Originally proposed 2026-10-26. This is the feasibility design for [R104](../PR_ROADMAP.md#r104) ("Investigate Dynamic and Live Component interoperability"), scope row C03, which [SCOPE](../SCOPE.md) keeps at "Investigate". It is research and design only: no product code and no dependency. **Verdict: import Dynamic Component attributes as read-only metadata (R104.a). Do not map Dynamic Component formulas onto native parameters for 1.x. Treat Live Components as service-dependent: only their geometry is known to work offline, and their behaviour is not available offline.** The owner decides on that verdict and on the open questions in [Owner decisions](#owner-decisions). ADR 0165's refusal of a Dynamic Components–compatible formula language (§(d)) stands. ADR 0176's rule that Dynamic Component behaviour is not imported stands. Section 4 names two ADR 0176 rules that this design changes, for the owner to accept or reject.

## Verdict in brief

- **Dynamic Components (DC) have a readable stored form, and a small part of it is already read.** Today's importer keeps instance-level String, Integer, Float and Boolean attributes as `skp.dynamic_attributes.<key>` properties. It drops Length, Vector, Array and Null values with a count-only notice, and it never reads definition-level dictionaries, because OpenSKP's `Definition` has no field for them. Those two gaps are the real work (R104.a).
- **DC formulas cannot be mapped onto native parameters for 1.x.** The documented language has 77 functions and 11 operators. 49 functions need host state, actions, randomness, text, lists, or functions the native table lacks. 10 more need rewrites. Even the 18 that map directly depend on unit intent that formula text does not carry: in DC, `LenX + 10` means 10 inches, and the native checker refuses to add a length to a bare number. No DC file has been read yet, so no formula result can be tested here.
- **Live Components (LC) are service-dependent.** The official FAQ says that authoring or reconfiguring needs an internet connection, and that copies of a placed Live Component work offline in SketchUp. Where the logic is stored and where it runs are not stated in any page I could read, and the owner's corpus has no Live Component. The evidence supports "geometry only" as the working assumption. It does not prove it.
- **R103's verdict also applies.** DC's evaluator is a Ruby extension bundled with SketchUp. A forum reply says it is closed source, which I could not verify. Running it is out, and reimplementing its documented language would need the clean-room and counsel steps of ADR 0177 §3.

## Context

- **What exists today (native).**
  - [ADR 0165](0165-constrained-component-parameters.md) defines parameters (types `length`, `area`, `angle`, `number`, `count`, `boolean`, `choice`), a closed expression language, and a dependency graph with transactions. The engine "has no host calls, no I/O, no loops, no user functions, no recursion at evaluation time, and no names except the definition's own parameters" (§2). Its §(d) rejects a Dynamic Components–compatible language and states that "SketchyUp neither reads nor writes Dynamic Component attributes".
  - [ADR 0166](0166-configurable-component-interface.md) and the R094 rows deliver the interface. The Components tab says Dynamic Components are not imported. ADR 0166 says "SketchUp Dynamic Components are not imported or converted."
  - R094 shipped in 1.2.0 ([release](https://github.com/zygote55/sketchyup/releases/tag/v1.2.0), per [PR_ROADMAP](../PR_ROADMAP.md)).
- **What exists today (import).** [ADR 0176](0176-skp-import.md) and the R102.a–d notes on `main`. Attribute handling is in `src/skp/import.cpp` (lines 537–575) and `addAttributeProperties` in `src/io/skp_import.cpp` (lines 192–212). See §4 for what the code does with DC data.
- **Contradiction to fix.** [RELEASE_NOTES.md](../RELEASE_NOTES.md) (line 328) says SketchyUp "neither reads nor writes Dynamic Component attributes". That was true for the 1.3.0 release, which has no `.skp` import ([SUPPORT_MATRIX](../SUPPORT_MATRIX.md) line 26). On `main`, R102.b already reads instance-level attributes. The wording must change when R102 ships (R104.d).
- **Scope row.** C03: "Determine format/runtime/service dependencies, update behavior and supported subsets; report differences from native parameters" ([SCOPE](../SCOPE.md) line 129).
- **Roadmap Verify line.** "Representative source assets retain claimed parameter/update behavior; offline and service-dependent cases are distinguished and no proprietary service is assumed available." Under the verdict above, this design claims no DC parameter or update behaviour. The Verify line is then met by the report saying so, and by the offline and service tests in §7.
- **Routes considered:**

  | Route | Verdict |
  |---|---|
  | Read DC attributes at instance and definition level as read-only metadata, and report every key not imported or not run | **Chosen for R104.a** (§5.2) |
  | Map a bounded formula subset onto native parameters | **Deferred to R104.b**, gated on owner samples, a parity reference and counsel (§5.4) |
  | Run DC's own evaluator (SketchUp's bundled Ruby) | **Rejected**: [ADR 0177](https://github.com/zygote55/sketchyup/pull/461) keeps Ruby out of 1.x; the closed-source claim is unverified |
  | Reimplement all of DC (formulas, REDRAW, copies, onclick) | **Rejected**: 49 functions and the redraw side effects have no native equivalent |
  | Call the Trimble Creator or Live Component service | **Rejected**: no proprietary service is assumed, and the offline rule requires none |
  | Keep current behaviour (instance-level String, number and Boolean only; count-only notice) | **Rejected**: definition-level data is invisible, and Length values are dropped without a name |
  | Import Live Component geometry, classify the service dependency, and report it | **Chosen for R104.c** (§2) |

## 1. Format inventory

### 1.1 Method

- **Sources.** Official SketchUp help pages (read 2026-10-26; the date each page shows is given below). Community forum threads, labelled as such. The repository's R102 code and fixtures. The owner's corpus, read only and reported in aggregate (§1.5). The vendored OpenSKP parser subset, compiled with g++ 16.2.1 into a throwaway driver under `.local/r104-research/`. That directory is git-ignored and is not committed.
- **Corpus probes.** A raw byte search finds no names, because SKP names sit inside compressed zip entries. The probe therefore decompresses each entry first. It finds the known names `GeoReference` and `GSU_ContributorsInfo` in the decompressed data, which calibrates it.
- **Page length.** The official pages are longer than one fetch. I read the first 100,000 characters of each. Every list quoted here is complete within that part, and the remainder holds examples I did not read.
- **Search summaries.** Some claims come only from search result summaries. They are marked "search summary" and treated as unverified.

### 1.2 Dynamic Components: the stored form

| Item | What the evidence says | Source (read 2026-10-26) |
|---|---|---|
| Dictionary | `dynamic_attributes` | Forum code, [2016 thread](https://forums.sketchup.com/t/setting-units-in-component-definition-dc/28353). The official pages I read do not name the dictionary. |
| Level | Both. The 2016 code sets attributes on the component definition (`dc.set_attribute`). The [2018 thread](https://forums.sketchup.com/t/manage-different-access-to-dynamic-attributes-in-ruby/62204) calls `set_attribute` on each `ComponentInstance`. The 2018 thread's "instance" reading is an inference, since the post does not say where the attributes are stored. | Forum code. A Sketchucation post (search summary, not opened, [post 1388224](https://community.sketchucation.com/post/1388224)) says a DC "is essentially a ComponentDefinition" with a `dynamic_attributes` dictionary. |
| Predefined names | `X`, `Y`, `Z`, `LenX`, `LenY`, `LenZ`, `RotX`, `RotY`, `RotZ`, `Hidden`, `Copies`, `Copy`, `Name`, `Summary`, `Description`, `ItemCode`, `ImageURL`, `DialogWidth`, `DialogHeight`, `Material`, `onClick` | [Predefined attributes](https://help.sketchup.com/en/sketchup/dynamic-component-predefined-attributes), last updated Sep 29, 2026 on the fetched page. A search snippet of the same page showed Apr 30, 2026. I used the fetched date. The page says "This is a Pro only feature." |
| Hidden metadata | Keys beginning with `_`. Examples: `_<name>_formula`, `_<name>_access` (values NONE, VIEW, TEXTBOX, LIST), `_<name>_units` (DEFAULT, INCHES, FEET, CENTIMETERS, MILLIMETERS, METERS), `_<name>_formulaunits`, `_<name>_formlabel`, `_<name>_options`, `_lengthunits`. | Forum code only, in the [2016](https://forums.sketchup.com/t/setting-units-in-component-definition-dc/28353) and [2018](https://forums.sketchup.com/t/manage-different-access-to-dynamic-attributes-in-ruby/62204) threads. The official predefined page lists no underscore keys. |
| Label keys | `_formlabel` in the 2018 thread. A search summary of another thread used `_<name>_label`. I did not open that thread, so `_label` is unverified. | Forum (2018); search summary. |
| Behaviour | `onClick`: a short script that runs when the component is clicked with the Interact tool. `Copies`: the number of duplicates SketchUp creates, each at the original's coordinates. `Copy`: the index of a duplicate, with 0 for the original, used inside formulas. | Predefined attributes page, as above. |
| Value types | Not documented. Formulas appear as strings in the forum code. Numeric values could be Integer, Float or Length, and the corpus has no DC sample to show which. | Not verified (§1.5). |
| Units | Lengths are in inches by default. The predefined page's formula example uses inch values. `_units` and `_lengthunits` change the units per attribute or per component. | [Predefined attributes](https://help.sketchup.com/en/sketchup/dynamic-component-predefined-attributes); forum code. |

### 1.3 Dynamic Components: the formula language

- **Operators.** Eleven: `+`, `-`, `*`, `/`, `<`, `>`, `<=`, `>=`, `=`, `()` and `<>`. The [operator page](https://help.sketchup.com/en/sketchup/dynamic-component-supported-operators), last updated Sep 29, 2026, gives no notes on precedence, types or text comparison.
- **Functions.** 77, in six groups: Math (19), SketchUp (14), Text (18), Trig (12), Logical (6), OnClick (8). The [functions page](https://help.sketchup.com/en/sketchup/dynamic-component-supported-functions), last updated Sep 29, 2026, says the list is 77 functions, and I counted 77 entries. §3.2 classifies them.
- **No parent or child references.** No function on the page refers to a parent, a child or another component. The only "contains" functions are `EDGES()`, `FACES()` and `FACEAREA()`, which count geometry in "the component or group that this function is called within", and `REDRAW()`, which "redraws the component that contains this function".
- **Trig.** Angles in degrees: `COS(number)` is "the cosine of an angle given in degrees", and `ACOS` returns degrees.
- **Redraw and actions.** The [onClick page](https://help.sketchup.com/en/sketchup/dynamic-components-onclick-function-examples), last updated Sep 29, 2026, documents ALERT, ANIMATE, ANIMATESLOW, ANIMATEFAST, ANIMATECUSTOM, GOTOSCENE, REDRAW and SET. These run on a click. When a formula is re-evaluated is not documented on the pages I read. A forum summary says attribute changes made from Ruby usually need a redraw (search summary, not opened).
- **Where the evaluator lives.** A forum reply in search results calls the Dynamic Components extension "closed source" and "a Ruby extension" bundled with SketchUp. The reply is not identified, and I did not open it. Treat this as unverified.
- **Cross-entity references.** [ADR 0165](0165-constrained-component-parameters.md) §(d) describes DC formulas as including "spreadsheet-style cross-entity references". The 77 functions and the operators include no such reference, and the pages I read show no reference syntax. This claim is unverified, and it is an open contradiction for the owner to resolve from a sample.

### 1.4 Live Components: the stored form and the service

- **What the official overview says.** The [overview](https://help.sketchup.com/en/sketchup-live-components), last updated Oct 21, 2026, describes Live Components as "a type of configurable component". They can be configured in 3D Warehouse before download or in SketchUp after the component is added, through "Configure Live Component" in the context menu. "Any configuration changes apply at the definition level and apply to all instances of that component." A downloaded configuration is "saved as a .skp file". Trimble Creator is "a web-based application to help you build the logic that powers a Live Component". The page calls the feature "a SketchUp Labs feature".
- **What the official FAQ says.** The [FAQ](https://help.sketchup.com/en/sketchup-live-components/frequently-asked-questions), last updated Sep 29, 2026 on the fetched page, says Live Components "can be configured in SketchUp for Desktop, SketchUp Web, and SketchUp for iPad". It also says: "Using Trimble Creator to create or change the configuration of Live Components requires an internet connection." And: "once you have a Live Component in your SketchUp model or on your computer, you can make copies and work with it offline". A search summary of the same FAQ says that Dynamic Components "aren't going anywhere and will continue to function in SketchUp for the foreseeable future". A search snippet of the FAQ showed a different update date (July 15, 2026). I used the fetched date.
- **Not stated in any page I could read.** Where the parameters and logic are stored in the `.skp`. Whether they are evaluated in the cloud or in SketchUp. Whether configuring a downloaded Live Component needs the service. What a file does when the service is unavailable. The [Trimble Creator](https://creator.trimble.com) page returned only its title when fetched on 2026-10-26.
- **Labs status.** "A SketchUp Labs feature" means the format and the service may change. This is an inference from the wording, not a stated fact.

### 1.5 What the owner's corpus and the repository fixtures show

**The owner's corpus, aggregate only.** The corpus holds one file, which the owner's README describes as SketchUp 2024 `{24.0.553}`. The file is local-only and is not quoted or copied here.

- It is a zip-based container of 42 entries, 145,795 bytes on disk and 323,130 bytes uncompressed.
- Geometry: the root plus one definition; 159 faces; 1,422 edges; 18 materials; one layer; units recorded; no scenes. These counts match the file that [ADR 0176](0176-skp-import.md) describes in R102.b.
- Instances: one. It carries one attribute dictionary with two String entries, and R102.b keeps both.
- Model-level dictionaries: three, with 2, 7 and 1 entries. R102.b does not map them, and it reports nothing about them (§4).
- **Dynamic Components: none.** No instance has a `dynamic_attributes` dictionary. No decompressed entry contains `dynamic_attributes`, a `_…_formula` or `_…_access` key, `LenX`, `Copies` or `onclick`, in UTF-8 or UTF-16LE. The file has no formulas.
- **Live Components: none found.** No decompressed entry contains `LiveComponent`, `Live Component`, `live_component`, `Creator` or `Trimble`. One occurrence each of the words "Dynamic" and "Live" appears in one entry. I did not classify them further, and neither matches a DC key or an LC marker.
- A first pass of this probe reported no instance, because the driver skipped the root definition. The driver was corrected and the counts above are from the corrected run.

**The repository fixture, for calibration.** `tests/fixtures/skp/named_attributes.skp` is written by OpenSKP's writer. It has two instances and one dictionary with five entries: String, Integer, Float, Boolean and Length. R102.b keeps four and drops the Length, as `tests/skp_mapping_tests.cpp` line 451 asserts. The probe gives the same result, so its counts can be trusted.

**Conclusion.** The corpus gives no evidence about DC or LC content. Every DC claim in this ADR rests on official pages, forum code, and the importer's behaviour. Any formula-subset decision would be untested against real files.

## 2. Service dependencies

The Verify line asks that offline and service-dependent cases be distinguished. Each capability is classified below, with its source.

| Capability | Where it runs | Needs a Trimble service? | Source |
|---|---|---|---|
| Read a DC `.skp` in our reader | Our reader | No. The reader needs no service and makes no network call. | Design. |
| Use a DC in SketchUp | SketchUp's bundled extension | Not stated. The FAQ says DCs keep working. | FAQ search summary; forum (closed-source claim, unverified). |
| Author an LC in Trimble Creator | Creator (web) | **Yes**, online. | [FAQ](https://help.sketchup.com/en/sketchup-live-components/frequently-asked-questions). |
| Change an LC's configuration | Creator (web), per the FAQ | **Yes**, online. | FAQ. |
| Configure an LC in 3D Warehouse | Web configurator | **Yes**, the details page needs the site. | [Overview](https://help.sketchup.com/en/sketchup-live-components). |
| Configure a placed LC in SketchUp | Not stated | Unknown. | Overview. |
| Copy and work with a placed LC | SketchUp | No, "offline". | FAQ. |
| Read a downloaded LC `.skp` in our reader | Our reader | No. The question is whether the file holds behaviour we can run, and that is unknown. | Design; §1.4. |

**Classification.**

- **Offline.** Reading the file, placing and copying it, and any DC behaviour SketchUp itself runs. Our reader offers only what is in the file.
- **Service-dependent.** Authoring, reconfiguring, and configuring in 3D Warehouse. Our reader cannot do any of these, and it does not try.
- **Unknown.** Storage of an LC's logic in the file, cloud or local evaluation, and in-SketchUp configuration of a placed LC.

**Rule.** The importer and the app make no call to any Trimble service. A feature that would need one is refused with a named reason (§6). This follows the offline rule of ADR 0177 and avoids assuming a proprietary service.

## 3. Formula semantics compared with native parameters (R094)

### 3.1 The comparison

| Aspect | Dynamic Components (documented) | Native parameters ([ADR 0165](0165-constrained-component-parameters.md) §§1–3) | Mapping |
|---|---|---|---|
| Formula storage | `_<name>_formula` string, under the `dynamic_attributes` key (forum). Level not settled (§1.2). | Canonical expression text on the parameter or driver record (§§1, 5). | Text can be carried over only if the grammar fits. |
| Operators | Eleven (§1.3). `=` and `<>` for equality. | `+ - * /`, comparisons `== != < <= > >=`, `and or not`, `^`, parentheses. | Direct, except `=` becomes `==` and `<>` becomes `!=`, with the type limits below. |
| Equality | `=` works on any values. | `==` and `!=` only for booleans, choices and dimensionless quantities. Lengths, areas and angles need `<` and `>` (§2, `PARAMETER_TYPE`). | Rewrite needed. A rewrite is accepted only when it is exact. |
| Units | Inches by default. Per-attribute units through `_units` (forum). Bare numbers are inches. | SI internally. Explicit suffixes (`mm`, `in`, `deg` …). Bare numbers are dimensionless. `+`, `-`, `min`, `max`, `clamp`, `mod` and the comparisons need equal dimensions (§2). | **Not translatable automatically.** `LenX + 10` means 10 inches in DC and is a type error natively. The translator would have to guess the author's intent. |
| Trig | Degrees in and out (§1.3). | Angle quantities. `sin`, `cos` and `tan` take an angle and return a number (§2). A bare number is not an angle. | Map by adding `deg`. |
| Rounding and steps | `ROUND(n, count)`, `FLOOR(n, sig)`, `CEILING(n, sig)`, `INT`, `ODD`, `EVEN`. | `round`, `floor` and `ceil` of one dimensionless argument. `round` is "half away from zero" (§2). | Map only the forms with count 0 and significance 1. The DC rounding mode is not verified. |
| Text | 18 text functions and string values. | No string operations. Choice literals only, matching `[a-z0-9_-]` (§2). | **Refuse.** |
| Lists and choices | `CHOOSE`, `NEAREST`, `OPTIONINDEX`, `OPTIONLABEL`, `LARGEST`, `SMALLEST`; list values through `_options`. | Choice parameters with `options` (§1). `min` and `max` take 2 to 8 arguments (§2). | Partial. `LARGEST` and `SMALLEST` map for up to 8 arguments. The rest is refused for 1.x. |
| Host queries | `FACES`, `EDGES`, `FACEAREA`, `CURRENT`, `LAT`, `LNG`, `SUNANGLE`, `SUNELEVATION`. | No host calls and no I/O (§2). | **Refuse.** |
| Randomness | `RAND`, `RANDBETWEEN`. | Deterministic results are required (ADR 0165 Verify line). | **Refuse.** |
| Actions | `onClick`: `ALERT`, `ANIMATE*`, `GOTOSCENE`, `REDRAW`, `SET`. | None. Native formulas are "data" with no actions (§2). | **Refuse.** Keep the text as metadata. |
| Copies | `Copies` makes N copies at the original's position, and `Copy` gives the index (predefined page). | Placements are explicit. Overrides are stored per placement, and no array count exists (§1 and ADR 0165 §(a)). | **Refuse** in 1.x. |
| Cross-entity references | Not in the function list. Syntax not seen. ADR 0165 §(d) claims they exist (unverified). | Names only from the definition's own parameters (§2). | **Refuse** until a sample shows the syntax. |
| Evaluation and redraw | Re-evaluated on change; `REDRAW` is a side effect of an action (§1.3). | Dependency graph, deterministic evaluation, one undo entry per edit, transactions (§§3–4). | Not a semantic match. Redraw side effects are refused. |
| Material | `Material` accepts a colour name, hex, RGB or a model material name (predefined page). `_material_formula` appears in a 2018 forum example. | No material parameter type (§1). Material drivers are excluded for schema 28 (ADR 0165, lines 169 and 204). | **Refuse** formulas. Keep the static `Material` text as metadata. |
| Visibility | `Hidden` is 1 or 0 (predefined page). | `visibility` driver on non-root members (ADR 0165, line 158). It does not hide a whole placement. | Candidate for a later slice. Refused in 1.x. |
| Error model | Not documented on the pages I read. | Named `PARAMETER_*` refusals, with the value left unchanged (§§2–4). | Not mapped. DC errors are not reproduced. |

### 3.2 The 77 functions

| Class | Count | Functions |
|---|---|---|
| **Direct** (for dimensionless or restricted arguments) | 18 | `ABS`, `SQRT`, `SIN`, `COS`, `TAN` (degrees converted to angles), `IF`, `AND`, `OR`, `NOT`, `TRUE`, `FALSE`, `PI`, `INT` (= `floor`), `FLOOR` and `CEILING` (significance 1 only), `ROUND` (0 decimal places only), `LARGEST` and `SMALLEST` (at most 8 arguments) |
| **Rewrite** (expressible with the native table and units) | 10 | `ASIN`, `ACOS`, `ATAN` (degrees to angle), `DEGREES`, `RADIANS`, `ODD`, `EVEN`, `ISODD` and `ISEVEN` (via `mod`), `SIGN` (via `if`) |
| **Refuse** | 49 | Host and state: `FACES`, `EDGES`, `FACEAREA`, `CURRENT`, `LAT`, `LNG`, `SUNANGLE`, `SUNELEVATION` (8). Randomness: `RAND`, `RANDBETWEEN` (2). Text: all 18 text functions. Actions: all 8 OnClick functions. Lists: `CHOOSE`, `NEAREST`, `OPTIONINDEX`, `OPTIONLABEL` (4). No native equivalent: `ACOSH`, `ASINH`, `ATANH`, `COSH`, `SINH`, `TANH`, `EXP`, `LN`, `LOG10` (9) |

Total: 18 + 10 + 49 = 77.

### 3.3 Why a translator cannot be exact from text alone

- **Units.** A number in a DC formula has no unit. In DC it is read as inches. Natively it must carry a unit or be dimensionless. Whether `10` in `LenX + 10` is inches, or a count, depends on the author. The formula text does not say.
- **Equality.** `IF(LenX = 600, …)` is legal in DC and refused natively. The rewrite to `<` and `>` changes behaviour at the boundary only if the values are not quantised the same way. Native lengths are quantised to 1e-9 m (ADR 0165 §2). DC's comparison tolerance is not documented.
- **Rounding.** The DC rounding mode is not documented. The native `round` is half away from zero. A translation of `ROUND` is exact only if DC matches that mode.

These three points are why R104.b is deferred, and why the subset in §5.2 excludes formulas altogether.

## 4. What the importer does with DC data today

- **Instance level only.** `src/skp/import.cpp` line 540 walks `instance.attribute_dictionaries`. No other entity is read.
- **Kinds.** String is kept, clipped to 2,048 bytes. Integer becomes a Number. Float becomes a Number if finite, and is kept unconverted. Boolean is kept. Length, Vec, Array and Null are dropped, with the count-only notice `attributeNotImported` (`src/skp/import.cpp` lines 561 and 572). The notice has no location, so the dropped key is not named.
- **Silent clipping.** A String longer than 2,048 bytes is clipped at `src/skp/import.cpp` line 553 and at `src/io/skp_import.cpp` line 206, and no notice is raised at either point. A long formula would be cut without a word.
- **Definition level: not read.** OpenSKP's `Definition` (`third_party/openskp/include/openskp/model.hpp` lines 469–500) has no attribute field. The parser extracts dictionaries only for instances (`third_party/openskp/src/geometry.cpp` line 332), for the model (line 413), and for legacy instances and layers (`third_party/openskp/src/legacy.cpp` lines 1590 and 1935). Definition-level DC data is therefore absent, and no notice says so.
- **Model level: not mapped.** The model-level dictionaries are parsed (`model.attribute_dictionaries`) but not used by the importer, and no notice reports them. This is not a DC issue, but it is the same kind of silent drop.
- **The `properties` view.** OpenSKP fills `Instance::properties` only for `dynamic_attributes`, as strings (`geometry.cpp` line 335). The importer does not use it.
- **Float values.** A Float is kept as stored. If a DC length is stored as a Float, it reaches the document as a bare number in inches, with no conversion and no unit. Whether DC stores lengths as Float is not verified.
- **Changes proposed.** ADR 0176 §4 says "only string, number and boolean values" are imported, and that a Length is "not imported". R104.a changes that for DC keys only, as owner decision 3 sets out. It also replaces the silent clipping and the count-only notice (§6).

## 5. Verdict and the supported subset

### 5.1 Verdict

1. **Dynamic Components: read-only metadata (R104.a).** Read the `dynamic_attributes` dictionary at instance and definition level. Keep it as inert properties, and report every key that is not imported or not run. Nothing is evaluated.
2. **Formula mapping: not viable for 1.x (R104.b deferred).** Reasons, each with its evidence:
   - 49 of 77 functions have no native equivalent (§3.2).
   - Even the 28 that can map depend on unit intent that the formula text does not carry (§3.3).
   - There is no DC file in the owner's corpus or the repository to test a result against (§1.5).
   - A faithful evaluator means reimplementing a documented, commercial language, which needs counsel (ADR 0177 §3.3).
3. **Live Components: service-dependent, geometry only as far as the evidence shows (R104.c).** The evidence supports this as the working assumption: authoring and reconfiguring need the service (FAQ), and copies work offline (FAQ). It does **not** prove that the file lacks the logic. A file could carry behaviour that the reader cannot run. We treat LC behaviour as not available offline, and we say so in the report.
4. **Behaviour is refused by name, never run.** Each refusal in §5.3 is reported with its key.

### 5.2 The subset D1: read-only metadata

A file is in D1 when all of the following hold. Each criterion is measurable.

- **D1.1 Keys read.** Every key in a `dynamic_attributes` dictionary, at instance or definition level, of kind String, Integer, Float, Boolean or Length, is imported as the property `skp.dynamic_attributes.<key>`. A Length is stored in metres: inches × 0.0254. A Float is stored as it is. Whether a DC Float is a length is for the owner's samples to settle (owner decision 6).
- **D1.2 Accounting.** For each level, the count of keys present equals imported plus reported. The same identity ADR 0176 §4 uses.
- **D1.3 Formulas are text.** A formula is imported as text and never evaluated. A fixture sets a formula on an input, changes the input, and asserts that geometry, parameters, dependencies and the revision are unchanged, and that no `component_parameter` record is created.
- **D1.4 Behaviour is text.** `onClick` is imported as text and never run. It is reported as `behaviourNotRun`. A fixture with `ALERT` and `REDRAW` asserts that nothing happens.
- **D1.5 Copies.** A placement with `Copies` greater than 0 imports as one placement. It is reported as `copiesNotReproduced`, with the count.
- **D1.6 Materials.** `Material` and `_material_formula` are kept as text and reported as `materialFormulaNotApplied` where a formula is present.
- **D1.7 Units.** A DC Length of one inch imports as 0.0254 m, to within 1e-12 m.
- **D1.8 Owner samples.** For each owner sample, the import counts equal OpenSKP's own reading, in the style of ADR 0176's S1. Only numbers are recorded.
- **D1.9 No service.** The import runs under `unshare -n` with no network, and it succeeds. A grep test finds no socket or HTTP code in `src/skp/` and `src/io/skp_*`.

The subset is **supported** when D1.1 to D1.9 pass and the report names every behaviour not run. It makes no claim that a DC parameter or update behaves as it does in SketchUp.

### 5.3 Refusals (each reported by name)

- Any formula, in all forms: evaluation, `_<name>_formula` and `_material_formula`.
- Actions: `ALERT`, `ANIMATE`, `ANIMATESLOW`, `ANIMATEFAST`, `ANIMATECUSTOM`, `GOTOSCENE`, `REDRAW`, `SET`.
- Host and state functions: `CURRENT`, `FACES`, `EDGES`, `FACEAREA`, `LAT`, `LNG`, `SUNANGLE`, `SUNELEVATION`.
- Randomness: `RAND`, `RANDBETWEEN`. Text functions. List functions: `CHOOSE`, `NEAREST`, `OPTIONINDEX`, `OPTIONLABEL`.
- `Copies` and `Copy`.
- Values of kind Vec, Array and Null.
- LC configuration and its logic (service-dependent, §2).

### 5.4 Conditions to revisit R104.b (formula mapping)

Reopen only when all of these hold:

- the owner supplies at least three DC files, and D1 has been checked on them, with the key and value facts confirmed;
- the owner runs a parity reference on SketchUp (Windows or macOS) for those files, and records only numbers;
- counsel answers the questions in owner decision 8;
- the candidate set is the 28 functions in §3.2, and the translator refuses any formula whose unit intent is ambiguous, meaning any bare number mixed with a length.

## 6. Reporting unsupported behaviour

Nothing is dropped silently. The report uses the convention of [ADR 0176](0176-skp-import.md) §5: `apiVersion` stays 1, the change is additive, the report stays at or below 64 KiB, messages are at most 256 bytes, and each kind keeps at most 32 sample locations.

- **A new section, `dynamicComponents`**, with a counter and samples for each kind below. A sample names the definition or instance, the dictionary, and the key. A sample never holds a value, so formula text and script text do not reach the report.
  - `metadataImported`: keys imported as properties.
  - `lengthConverted`: Length keys stored in metres.
  - `formulaNotEvaluated`: formula keys kept as text.
  - `behaviourNotRun`: `onClick` text kept and not run.
  - `copiesNotReproduced`: placements with `Copies` above 0.
  - `materialFormulaNotApplied`.
  - `attributeNotImported`: Vec, Array and Null values, with the key named. This replaces the count-only notice for DC keys.
  - `textTruncated`: strings clipped at 2,048 bytes. This replaces the silent clipping.
  - `definitionLevelRead`: definition dictionaries read, for the accounting identity.
  - `serviceDependent`: any Live Component detected, with the service rule stated.
- **The user line.** The import summary shows one line, in plain words: "Dynamic Components: N attributes kept as read-only properties; M formulas and behaviours were not run." The wording follows ADR 0176 §8, including the non-affiliation note.
- **The test.** The accounting identity in D1.2 is the test that nothing is silently dropped.

## 7. Verification and fixtures

- **CI fixtures, written by OpenSKP's writer.** `named_attributes.skp` already covers String, Integer, Float, Boolean and Length on instances. R104.a adds a fixture with the `dynamic_attributes` shape: formula, access, units, `onClick`, `Copies`, and a Length. The writer's support for definition-level attributes is **not verified**. `tests/fixtures/skp/generate.cpp` sets attributes only on instances. If the writer cannot write definition-level attributes, that path is tested by a hand-built decoder fixture and by the unit test for the vendored patch. The owner's sample is then the only end-to-end check.
- **Writer fixtures prove regressions, not fidelity.** ADR 0176 §7 makes the same point. Fidelity comes from real files.
- **Owner samples (local only).** At least three DC files and one LC file. The owner supplies them as self-authored, or as files whose licence is clear. They stay in the git-ignored `.local/skp-corpus/`. Only numbers are recorded, in the implementation notes of R104.a and R104.c.
- **Public samples: none vendored.** The 3D Warehouse holds DC and LC files, but each model's licence is separate and unverified. None is used or copied.
- **Reference runs.** D1 claims no DC behaviour, so D1 needs no SketchUp reference run. A reference run is needed only for the R104.b parity test, and it runs on the owner's machine, not in CI.
- **CI placement.** The `skp` job runs the new `skp_dc_tests` beside `skp_mapping`. The job tier follows [ADR 0170](0170-ci-tiers.md).

## 8. Budgets (proposed; R104.a measures)

| Item | Budget | Basis |
|---|---|---|
| Attributes per instance or definition | 256 | R102.b, `attributesPerInstance` (`src/io/skp_package.hpp`) |
| Property keys per body | 128 | ADR 0176 R102.b, `propertiesDropped` notice |
| Key length | 128 bytes | ADR 0176 §4 |
| Text value (formula, script, description) | 2,048 bytes, with `textTruncated` when cut | ADR 0176 §4 string cap |
| Samples per report kind | 32 | ADR 0176 §5 |
| Report size | 64 KiB | ADR 0176 §5 |
| Import time, owner file | No more than 10 % above the R102.b figure, 194–239 ms under load | ADR 0176 R102.b |
| Network traffic | 0 bytes | D1.9 |
| Parser address space | Unchanged from ADR 0176's budget | ADR 0176 §1 |

## 9. Risks

1. **Silent loss of definition-level data.** Verified in code (§4). Until R104.a lands, DC metadata may be missing without any notice.
2. **Units.** A Float is kept unconverted. A DC length stored as a Float would reach the document as bare inches. A DC length stored as a Length is converted. DC's inch default and native SI differ. Samples must settle which.
3. **Key names from forum posts.** The underscore keys come from 2016 and 2018 posts, not from an official page. The label key conflicts (`_formlabel` against `_label`). A wrong name would be read as metadata wrongly. Samples must confirm each key that D1 reads.
4. **Pro-only features.** The predefined page says "This is a Pro only feature." Reading a DC file is not a licence question for us, but the DC language is a Pro feature of SketchUp. That is one more reason not to reimplement it without counsel.
5. **Evaluator reimplementation.** R104.b would reproduce a documented but commercial language. Counsel first, as in ADR 0177 §3.3.
6. **Trimble names.** "Dynamic Components" and "Live Components" are Trimble's terms. Use the ADR 0176 §8 wording, and no product name may contain "SketchUp" (ADR 0177 §3.2, item 6).
7. **Labs format.** Live Components are a Labs feature, so the format or the service may change. The reader must treat an unknown LC as unknown, report it, and never crash on it. Any new parsing should run in the existing sandboxed helper (ADR 0176 §1).
8. **Coverage gap.** The paid and service-backed components users most want are outside D1 by design.
9. **Documentation contradiction.** [RELEASE_NOTES.md](../RELEASE_NOTES.md) line 328 and ADR 0165 §(d) say SketchyUp neither reads nor writes DC attributes. That is true for 1.3.0 but not for `main`. Fix it in R104.d.
10. **User expectation.** Showing DC metadata may read as DC support. The wording must say "read-only" and "not run" each time.
11. **No real DC evidence.** The owner's corpus has none, so the DC conclusions rest on documents, forum code, and the importer's behaviour. Owner samples are the way to close this.
12. **Unverified cross-references.** If DC has cross-entity references (ADR 0165 §(d)), the refusal list grows. The subset is safe, because it refuses them.

## 10. Implementation split

- **R104.a: read DC metadata (instance and definition level).** Patch the vendored OpenSKP to read definition-level dictionaries. The patch goes in `third_party/openskp/patches`, is recorded in `PATCHES.md`, and the pinned hash is regenerated through `scripts/vendor-openskp.py`. Map the keys in `src/skp/import.cpp` and `src/io/skp_import.cpp`. Add the `dynamicComponents` report section, the Length conversion for DC keys, and the notices in §6. Tests: `skp_dc_tests`, on synthetic fixtures, covering D1.1 to D1.7. Gate: owner decisions 1 to 4. Docs: the wording in `UX_DESIGN.md` and `USER_GUIDE.md`.
- **R104.b: formula subset (deferred; no slice starts).** A translator for the 28 functions in §3.2, with the refusals in §5.3. Gate: §5.4 in full. Not scheduled.
- **R104.c: Live Component classification.** The `serviceDependent` report, the `unshare -n` test (D1.9), and a test on the owner's LC sample. No network code. Gate: owner decisions 5 and 6.
- **R104.d: documentation and claims.** `SUPPORT_MATRIX.md`, `USER_GUIDE.md`, `RELEASE_NOTES.md` (the "neither reads" line), `CHANGELOG.md`, the Components tab note that says Dynamic Components are not imported, and a note on ADR 0165 §(d). Record the owner sample counts as implementation notes in this ADR.

## Owner decisions

1. **Verdict: read-only metadata for Dynamic Components; no formula mapping for 1.x.** *Recommended: yes.* No DC file has been read, and the formula layer needs both a parity reference and counsel.
2. **Read definition-level dictionaries by patching the vendored OpenSKP.** *Recommended: yes.* DC metadata sits at definition level in the forum evidence, and without this it is missing with no notice.
3. **Length values for DC keys: import as metres, reported as converted.** *Recommended: yes.* It keeps the value the author set and names the conversion. It changes ADR 0176 §4 for DC keys only.
4. **Formulas and scripts kept as inert text, capped at 2,048 bytes, never run.** *Recommended: yes.* The user can read what the author wrote, and nothing executes without an evaluator.
5. **Live Components: service-dependent, with geometry and metadata only; no call to any Trimble service.** *Recommended: yes.* The storage and evaluation are undocumented, and a network call would break the offline rule.
6. **Samples.** *Decided 2026-10-27: the owner has none; make them.* R104.a generates at least three self-authored Dynamic Component fixtures with OpenSKP's writer at the pinned commit (instance- and definition-level `dynamic_attributes`, String, Integer, Float, Boolean and Length values, formulas, `onClick`, `Copies` and `_material_formula`), with a committed generator script. Self-made fixtures prove the importer's handling, not SketchUp's own key spellings or whether SketchUp stores a DC length as a Float: those stay recorded as unconfirmed until a file authored in SketchUp is available. No Live Component fixture can be made (its storage is undocumented), so R104.c ships only the no-network test (D1.9) and the service-dependent label. The original proposal: The owner supplies at least three DC files and one LC file, self-authored or licence-clear, kept local. *Recommended: yes.* The key spellings come from 2016–2018 forum posts, and Float units and LC storage cannot be settled without real files.
7. **Wording.** "Dynamic Components" and "Live Components" are Trimble's terms. Use "not affiliated with or endorsed by Trimble" (ADR 0176 §8), and no product name may contain "SketchUp". *Recommended: yes.* Trademark guidelines bar confusingly similar names (ADR 0177 §3.2).
8. *Not exercised: R104.b is deferred indefinitely (owner, 2026-10-27).* **Counsel gate for R104.b.** Before any formula work, ask counsel the ADR 0177 §3.3 questions, plus: may a non-affiliated program implement the documented DC formula language, and what do the offering terms say about it? *Recommended: yes, when R104.b is proposed.* It is not needed for D1, which only reads files.
9. **Public wording.** "Dynamic Components: attributes read as read-only properties; formulas and behaviour are not run." *Recommended: yes,* in R104.d. It replaces the "neither reads nor writes" line in the release notes.

## Implementation notes (R104.a, R104.c, R104.d)

Branch `feat/r104a-dc-metadata`, based on `main` at `61adf107`. Owner decisions 1 to 5, 7 and 9 apply; decision 6 is met
by the self-made fixtures below. Formula mapping (R104.b) is not started, and no formula evaluator is written.

**What was built (D1.1 to D1.7).**

- A `dynamic_attributes` value at instance or definition level, of kind String, Integer, Float, Boolean or Length, becomes
  the property `skp.dynamic_attributes.<key>`. A Length is stored in metres (inches × 0.0254). Only the DC dictionary is
  converted; other dictionaries keep ADR 0176 section 4 (a Length there is still not imported).
- Formulas (`_<name>_formula`, `_material_formula`) and `onClick` are kept as text under the 2,048-byte cap, with
  `truncatedAttributes` for a cut. They are never evaluated or run. `Copies` is kept as a property and each placement with
  `Copies` above 0 is counted (`copiesNotReproduced`).
- The report has the `behaviourNotRun` and `materialFormulaNotApplied` notices and a `dynamicComponents` section.

**How a definition-level value attaches (the decision the brief left to this work).** The native model keeps properties on
bodies, and a definition has no body of its own: its placements are its only bodies. So each placement of a definition
carries the definition's `dynamic_attributes` values as its own properties. Rules:

- A value the placement's own attributes already name is not replaced; the instance's value wins, and the definition's
  value is dropped on that placement (`propertiesDropped`, with its sample).
- A definition's value is counted once however many placements hold it. A definition with no placement holds its values
  nowhere, so they are omitted and counted as such.
- A definition-level value that the placement cannot carry, and the root's dictionaries (the model, not a component), are
  reported as before (`definitionAttributesNotCarried`).
- Rejected: a field on `ComponentDefinition`. That changes the document's saved form for one metadata group, which R104.a
  does not need.

**The package.** A definition gains an optional `attributes` list, and attribute entries gain two kinds: `length` (the
value in metres) and `unsupported` (a vector, an array, a null, or a number that is not finite, with its kind named). The
decoder accepts both, and it counts the definition's kept values in the attribute identity. The helper keeps an
unsupported value so the app can count it per level. Outside the DC dictionary, the package is unchanged.

**Accounting (D1.2).** `accounting.attributes` (the whole file) now includes the definitions' kept values. The new
`dynamicComponents.accounting` gives `counted`, `imported` and `omitted` for each level, and `omitted` is `counted` minus
`imported` by construction. Helper-level losses (the 256-attribute cap, an empty key) stay in `accounting.attributes` and
are outside the DC levels.

**Where the report differs from §6 (recorded here, not silently).**

- `definitionLevelRead` is not a separate counter; `dynamicComponents.accounting.definition` covers the definition level.
- `textTruncated` is in the section; the notice stays `truncatedAttributes`, as the brief asks.
- `serviceDependent` is not emitted. No marker in a file identifies a Live Component, because its storage is undocumented
  (section 1.4) and the owner's file has none. A label keyed on a guessed marker would be wrong, so R104.c's label is not
  built. The user guide says so.
- The one-line user summary (§6) is on the import report sheet, in the "How" lines and the identity lines.
- The release note is in an Unreleased section. The 1.3.0 bullet stays as it was, because it was true for 1.3.0, which did
  not import `.skp` files.

**Fixtures (decision 6).** `tests/fixtures/skp/generate_dc.cpp` writes three self-authored fixtures with the pinned OpenSKP
writer: `dc_instance_kinds.skp` (every kind, two Lengths), `dc_definition_keys.skp` (a definition's Length, two formulas
and `onClick`, placed twice at the root and once nested) and `dc_copies_limits.skp` (`Copies` and a 200-byte formula).
The pinned writer reproduces `box.skp` and `face_with_hole.skp` byte-for-byte, so its build is the one the committed bytes
came from. The three DC fixtures are not byte-reproducible (random component GUIDs); the committed bytes are the reference.

The fixtures prove the importer's handling of the shape the writer can write. They do not prove SketchUp's key spellings,
and they do not prove that SketchUp stores a DC length as a Float; both stay open until a SketchUp-authored file exists.
The writer cannot write a vector, an array or a null, and it refuses a string over 255 bytes in a legacy file. So the
over-2,048-byte string and the unsupported values are tested with in-test packages (`tests/skp_dc_tests.cpp`), and the
helper's own code path for a vector, an array or a null is not run by any test.

**No network (D1.9, R104.c).** `skp_dc` scans `src/skp/` and `src/io/skp_*` for socket, network, HTTP and curl code, and it
checks that it scanned at least ten files. It also runs the helper under `unshare -rn` (a new user and network namespace
with no network), when the runner allows that. The runner used here refuses `unshare -n` without a user namespace, and
allows `unshare -rn`. The CI container refuses both (ADR 0183), so there the namespace run exits 77 (skipped) and the scan
stands alone. When the namespace run is skipped, the test prints that it was.

**The owner's file (decision 6: aggregate counts only).** BanyanHarbor.skp holds no Dynamic Component: the instance and
definition DC values are both zero, and nothing is kept or reported under `dynamicComponents`. The import's time is in the
run record below.

**Gaps and open items.**

- A DC length stored as a Float, and SketchUp's own key spellings, remain unconfirmed (decision 6).
- The sanitizer shard (`skp-sanitize`) runs `skp_dc` in CI's full tier; it was not run locally.
- The desktop files that changed (`ifc_sheets.cpp`, `components_tab.cpp` and the assemblies test) are compiled by the
  desktop build in CI; see the run record below for the local check.
- No Live Component fixture exists, so R104.c's label is not implemented (above).

**Run record (R104.a, 2026-10-27, this runner).**

- `skp` preset, all targets, built under the 10 GB cap with BFD. The CI skp set (`skp_package`, `skp_import`,
  `skp_import_real`, `skp_mapping`, `skp_mapping_real`, `skp_dc`, `skp_dc_real`, `skp_library_install`,
  `skp_library_source`) passed 9 of 9, with `SKETCHYUP_SKP_CORPUS` set to the owner's directory.
- `skp-sanitize`: `skp_package`, `skp_import`, `skp_mapping` and `skp_dc` passed under ASan and UBSan, with
  `ASAN_OPTIONS=detect_leaks=0`. ASan cannot run under an address-space `ulimit` of 8 GB, so the cap was a cgroup
  limit of 10 GB.
- The owner's file, aggregate only: 0 Dynamic Component values at instance level and 0 at definition level, so the
  `dynamicComponents` section is empty for it. The helper imports the file in 146 to 163 ms wall (three runs, load average
  near 9.5), into a package of 64,299 bytes.
- Desktop: `ifc_sheets.cpp`, `components_tab.cpp` and `assembly_library_input_tests.cpp` compile with the `dev` preset.
  One warning, in `ifc_sheets.cpp` line 209, is on code this change does not touch. The desktop tests were not run
  locally.

## Sources

Each page was read on 2026-10-26. "Last updated" dates are the page's own, as shown on the fetched page.

**Official SketchUp help (help.sketchup.com):**
- Dynamic Component predefined attributes, last updated Sep 29, 2026 on the fetched page (a search snippet showed Apr 30, 2026): <https://help.sketchup.com/en/sketchup/dynamic-component-predefined-attributes>
- Dynamic Component supported functions (77 functions), last updated Sep 29, 2026: <https://help.sketchup.com/en/sketchup/dynamic-component-supported-functions>
- Dynamic Component supported operators (11 operators), last updated Sep 29, 2026: <https://help.sketchup.com/en/sketchup/dynamic-component-supported-operators>
- Dynamic Component OnClick functions, last updated Sep 29, 2026: <https://help.sketchup.com/en/sketchup/dynamic-components-onclick-function-examples>
- SketchUp Live Components overview, last updated Oct 21, 2026: <https://help.sketchup.com/en/sketchup-live-components>
- SketchUp Live Components FAQ, last updated Sep 29, 2026 on the fetched page (a search snippet showed July 15, 2026): <https://help.sketchup.com/en/sketchup-live-components/frequently-asked-questions>. The sentence on Dynamic Components continuing to work comes from a search summary of this page, not the fetch.

**Trimble Creator:** <https://creator.trimble.com> returned only its title when fetched. Nothing on it is relied on.

**Community forums (not official; the replies are not Trimble statements):**
- "Setting units in Component Definition DC" (posts from July to November 2016; code on definitions): <https://forums.sketchup.com/t/setting-units-in-component-definition-dc/28353>
- "Manage different access to dynamic attributes in Ruby" (posts from February 16 to 22, 2018; code on instances, an inference): <https://forums.sketchup.com/t/manage-different-access-to-dynamic-attributes-in-ruby/62204>
- "Dynamic attribute documentation?" (reply of February 20, 2017, pointing to a help-site user guide; the thread does not show the guide's content): <https://forums.sketchup.com/t/dynamic-attribute-documentation/39571>
- Search summaries only, not opened: <https://forums.sketchup.com/t/how-to-use-ruby-code-to-set-the-default-unit-of-custom-attributes-in-a-dynamic-component-to-centimeters/294095>; <https://community.sketchucation.com/post/1388224> (a DC is a ComponentDefinition with a `dynamic_attributes` dictionary); and a reply in search results calling the DC extension closed source, whose URL I could not attribute.

**Repository (`origin/main` at `08c6446b`):**
- [ADR 0165](0165-constrained-component-parameters.md) (§§1–3, §(d), lines 158, 169 and 204); [ADR 0166](0166-configurable-component-interface.md); [ADR 0176](0176-skp-import.md) (§§1, 4, 5, 7, 8; R102.a–d notes); [ADR 0177](https://github.com/zygote55/sketchyup/pull/461) on branch `docs/r103-ruby-feasibility` (commit `f2fc9c6e`, not yet on `main`); [ADR 0170](0170-ci-tiers.md).
- Code: `src/skp/import.cpp` (lines 537–575); `src/io/skp_import.cpp` (lines 192–212); `src/io/skp_package.hpp`; `third_party/openskp/include/openskp/model.hpp` (lines 37–44, 316–355, 469–500); `third_party/openskp/src/geometry.cpp` (lines 332–335, 413); `third_party/openskp/src/legacy.cpp` (lines 1590, 1935); `tests/skp_mapping_tests.cpp` (line 451); `tests/fixtures/skp/generate.cpp` (lines 180–196); `src/core/parameter_expression.hpp` and `.cpp` (the function table and limits, which match ADR 0165 §2).
- [RELEASE_NOTES.md](../RELEASE_NOTES.md) (line 328), [SUPPORT_MATRIX.md](../SUPPORT_MATRIX.md) (line 26), [SCOPE.md](../SCOPE.md) (line 129), [PR_ROADMAP.md](../PR_ROADMAP.md) (R102 and R104 rows).

**Owner's corpus:** one file in `.local/skp-corpus/`, read only and local-only. Only the aggregate counts in §1.5 are recorded.

**Not verified**

- The storage level (definition or instance) of DC attributes, and whether a DC Float or Integer is ever a length. No DC file has been read.
- The DC key spellings. They come from 2016–2018 forum posts. `_formlabel` and `_label` conflict, and the official pages list no underscore keys.
- Whether the DC evaluator is closed source, and where it lives on disk. The claim is a forum reply, which I did not open.
- DC's rounding mode, its comparison tolerance, its cross-entity reference syntax (ADR 0165 §(d)), and when a formula is re-evaluated.
- The Live Component file format: where its logic is stored, whether it is cloud or local, whether configuring a downloaded file needs the service, and what happens when the service is down. The Trimble Creator page returned only its title.
- Whether the pinned OpenSKP writer can write definition-level attributes.
- The Pro-only status of the operator and function pages. The predefined page says it is Pro-only. The search summaries of the other pages say so too, but I did not confirm it on those pages.
- The user-facing display of R102.b's `attributeNotImported` notice. I did not check the UI.
- Counsel's view on any of the questions in §9 and owner decision 8.
