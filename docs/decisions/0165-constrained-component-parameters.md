# Constrained component parameters

Status: **Accepted (2026-10-16)** for the decision; the layers are implemented by
R093.a–e (see "Layer plan"). Roadmap [R093](../PR_ROADMAP.md#r093) (M10, scope
row [F02](../SCOPE.md)); shapes [R094](../PR_ROADMAP.md#r094). The choices that belong
to the owner are listed under "Open for the owner". Work proceeds on the stated
defaults; the owner has not yet decided them and may change any of them before the
F02 release.

## Context

F02 asks for configurable components: editable parameters, constrained formulas, an
attribute inspector and reusable architectural and furniture assemblies, with
compatibility defined separately from SketchUp Dynamic and Live Components. R093 is
the core layer. Its Build line: specify a bounded expression language, a dependency
graph, typed units and a parameter schema; keep formulas non-executable and detect
cycles and resource abuse. Its Verify line: changing width drives dependent geometry
deterministically; invalid or cyclic formulas cannot partially commit or execute host
code. R094 adds the inspector, authoring, previews and assemblies on top. Each Extended
row ships in its own gated minor release ([M10 acceptance](../M10_ACCEPTANCE.md),
[ADR 0162](0162-release-versioning.md)).

What the code and contracts give us:

- **Components** ([ADR 0007](0007-component-records.md), `src/core/component_records.hpp`).
  A `ComponentDefinition` owns a root, member bodies keyed by canonical member ID, a
  member allocator (`nextMemberId`), leaf `references` to other definitions and an
  optional glue face. A `ComponentInstance` (keyed by the placement's scene root) names
  its definition and maps every canonical member to a stable scene record. Resolved
  records are checked projections of the definition
  (`projectComponentPlacement`, `matchesComponentProjection`), never a second source
  of geometry. A definition edit updates every placement in one edit; a locked affected
  placement refuses the shared edit. Shared edits need an explicit definition scope
  (`component.edit`); `component.edit_instance` ([ADR 0042](0042-instance-only-component-edits.md))
  makes one placement unique and edits it.
- **Storage** ([ADR 0159](0159-instanced-component-storage.md)). Since schema 26 a file
  stores definitions, placement roots and explicit bindings, not member bodies. Loading
  rebuilds members through the same projection the editor uses, and checks the
  expanded size against the document limits first. In-memory placements share
  definition geometry (R082.dd–gg).
- **Hosted components** ([ADR 0054](0054-hosted-components.md)). A glued cutting
  component's opening is derived from the definition's glue frame and outline.
  Moving or resizing a cutter regenerates its cuts from the uncut host surface in the
  same edit (`regenerateHost`).
- **Limits.** `DocumentLimits` (`src/core/document_limits.hpp`) bounds unique and
  materialized geometry; limit messages are at most 256 bytes and quote at most 64
  bytes of user text. The geometry tolerance is 1e-7 m and coordinates are bounded by
  ±1e6 m (`src/geometry/surface.hpp`). The recipe runner declares
  `"expressions": false` and `"shell": false` (`recipeCapabilities()`); nothing in the
  product evaluates user formulas today.
- **Format.** The current schema is 27 (`nativeDocumentVersion = 27`). The writer emits
  the lowest schema the content needs: 26 without drawing sheets, 27 with them
  ([ADR 0163](0163-documentation-sheets.md) decision 2, `nativeDocumentVersionFor`).
  `decodeContainer` accepts only the exact historical `requiredFeatures` sets, so an
  older reader refuses a newer set without touching the file. 1.1.0 is released and
  writes schema 27, so schema 27 is frozen.
- **Assistant surface** ([ADR 0164](0164-sheet-layout-interface.md#assistant-tool-surface-110), D-035).
  `assistantVisible()` in `src/automation/assistant.cpp` is the only filter between the
  command catalogue and the in-app assistant's provider tool list. Tool schemas are
  re-sent on every provider turn, and `assistant_tool_surface_tests` pins the surface
  to `tests/assistant-tool-surface-baseline.json`.
- **Fuzzing.** Ten opt-in libFuzzer targets (`tests/fuzz/`, [R083.e](../verification/R083e-parser-fuzzing.md))
  and the in-ctest parser fault corpus (`tests/parser_fault_corpus_tests.cpp`) cover
  every parser.

## Options considered

### (a) Parameters on the definition; placements regenerate from a template (chosen)

The definition's member bodies are a **template**: the geometry at the definition's
default values. Parameters and drivers are a separate record on the definition.
A placement stores only its overrides. Its resolved records are the projection of a
**variant**: the template with the drivers applied for that placement's values.
Variants are derived in memory and shared between placements with equal values, so
instancing keeps working. Nothing derived is stored in the file.

### (b) Store each variant as a generated definition

Every distinct value set becomes an ordinary definition marked "generated from"
the source. Projection and persistence would not change. Rejected: the file stores
redundant geometry that must be checked against its source on every load, generated
definitions need their own identity rules and cleanup when the last placement leaves
them, and a manual edit to a generated definition has no meaning.

### (c) A general constraint solver

Geometric constraints (parallel, tangent, distance) solved iteratively. Rejected for
R093: unbounded or tolerance-dependent iteration, solutions that depend on the start
state, and failure modes (under- and over-constrained systems) that the Verify line's
"deterministically" rules out. One-way formulas over named values are enough for the
F02 assemblies.

### (d) A SketchUp Dynamic Components–compatible formula language

Rejected. Dynamic Components formulas include actions and host state (`onClick`,
`SET`, `ANIMATE`, `REDRAW`, `CURRENT`) and spreadsheet-style cross-entity references.
F02 asks for compatibility to be defined separately. SketchyUp neither reads nor
writes Dynamic Component attributes; the release notes will say so.

## Decision

Adopt (a). The sections below define the parameter model, the expression language,
the dependency graph, transactions, persistence, the APIs and the abuse budget.

### 1. Parameter model

**Where parameters live.** A definition has at most one parameter record:
an ordered list of parameters and a list of drivers. A top-level placement may hold
overrides for the definition's input parameters. Nested placements (members of
another definition) have no stored overrides; their values come from their own
defaults or from the parent's `childParameter` drivers. Nothing else in the document
can read a parameter.

**Parameters.**

| Field | Rule |
| --- | --- |
| `name` | `[a-z][a-z0-9_]{0,31}`, unique in the definition, not a reserved word (keywords, function names, unit names, `pi`) |
| `label` | 1–128 UTF-8 bytes, no control characters, no leading or trailing spaces. Display only |
| `type` | `length`, `area`, `angle`, `number`, `count`, `boolean` or `choice` |
| `mode` | `input` (has a default; placements may override it) or `derived` (computed; never overridden) |
| `expression` | The default for an input, the formula for a derived parameter. Canonical text (§2) |
| `min`, `max`, `step` | Optional literal bounds for `length`, `area`, `angle`, `number` and `count`, in SI units. `min ≤ max`, `step > 0`. A value must satisfy `min ≤ v ≤ max` and, with `step`, equal `min + k·step` (or `k·step` when `min` is absent) for a whole `k`, within the type's quantum (§2; 1e-12 for `number`, exact for `count`) |
| `options` | `choice` only: 1–32 entries `{value, label}`, `value` matching `[a-z0-9_-]{1,32}` and unique, `label` as above |

Types, their stored units and ranges:

| Type | Dimension | Stored as | Range |
| --- | --- | --- | --- |
| `length` | L | metres | ±1e6 |
| `area` | L² | square metres | ±1e12 |
| `angle` | A | radians | ±1e4 |
| `number` | 1 | — | ±1e15 |
| `count` | 1, whole | integer | 0–10,000 |
| `boolean` | — | `true`/`false` | — |
| `choice` | — | option `value` | one of `options` |

Bounds are literals, not expressions, so they never add graph edges. A default that
violates its own bounds is refused when the definition is changed; an override that
violates them is refused, never clamped. Display uses the document's display units
and precision ([ADR 0018](0018-document-units.md)); R093 stores no per-parameter
display precision.

**Drivers: what "dependent geometry" is.** A driver ties one expression to one
target member of the definition. Drivers are **relative**: a placement's geometry
differs from the template by the change between the placement's value and the
default value. At default values every driver is the identity, so the default
variant equals the template (except for `repeat` copies, which are never in the
template). Every driver has a document-global ID.

| Kind | Target | Expression type | Effect on the variant |
| --- | --- | --- | --- |
| `stretch` | a Geometry member; `axis` (x, y or z, member-local) and a plane offset `at` (metres, member-local) | length | Every vertex, guide point and whole curve whose template coordinate on `axis` is greater than `at` moves by Δ = value − default along `axis`. Vertices at or below `at` stay. Several stretches on one member add their Δ per axis |
| `position` | any non-root member; `axis` of the parent frame | length | The member's translation moves by Δ along `axis` |
| `rotation` | any non-root member; `axis` of the member's own frame, through its origin | angle | The member's transform is post-multiplied by a rotation of Δ |
| `visibility` | any non-root member | boolean | `false` sets the member's `hidden` flag in the variant |
| `repeat` | any non-root member and its subtree; `axis` of the parent frame, `maxCount` 1–1,000 | count (copies, 1–`maxCount`) plus a second `spacing` expression (length) | Copies 1…n−1 of the subtree, each offset by k·spacing along `axis` |
| `childParameter` | a reference member (a nested placement); `parameter`, an input of the referenced definition | that parameter's type | Supplies the nested placement's value for that parameter |

Application order per member is fixed: stretch, rotation, position, repeat,
visibility. Visibility therefore applies to a repeated member's copies too. Drivers
of one kind on one member apply in driver-ID order. The template
coordinates decide which side of a stretch plane a vertex is on, so the result does
not depend on driver order. An analytic curve (circle, arc, pie; R024) must lie wholly
on one side of every stretch plane of its member; a curve that crosses one refuses the
driver (and any later template edit that makes it cross). Text and reference-image
members accept every kind except `stretch`. Materials, texture mappings, tags, names
and edge appearance are never driven: copies inherit the original's, and a stretched
face keeps its authored mapping.

**Repeat identities.** A `repeat` driver reserves a contiguous block of canonical
member IDs, `firstMember … firstMember + (maxCount − 1)·s − 1`, where `s` is the
size of the target subtree. Copy k's members take the block in order: copy, then
template member ID ascending. The definition's `nextMemberId` stays above every
block. Placement bindings map a reserved ID to a scene record only while that copy
exists. When the count drops, the extra copies' scene records retire; when it rises,
new scene IDs are allocated above the current floor and are never reused. A
definition may reserve at most 4,096 IDs. Copies are generated, so they cannot be
edited, targeted by another driver or chosen as a glue face.

**Re-baselining.** Changing a default, a derived formula or a driver changes what
"default" means. The same edit therefore rewrites the template to the variant at the
new defaults (Δ = new default value − old default value; repeat copies are not added
to the template), moves each stretch plane `at` by the Δ of the other stretches on the
same member and axis whose planes lie below it, and regenerates every placement. The
invariant "variant at defaults equals template" holds after every committed edit.
It is what lets a shared geometry edit work in template coordinates (§4). Repeated
re-baselining can leave last-bit differences (900 → 1200 → 900 mm is not guaranteed
to restore the original bytes); Undo restores the exact records.

**Hosted components.** When a placement's variant changes a glued cutting
component's glue member or outline, the same edit regenerates its host opening
through the existing resize path ([ADR 0054](0054-hosted-components.md)). Drivers
must keep the glue face in its plane. `position` of the glue member along the glue
normal and `rotation` of the glue member are refused when the driver is created
(`PARAMETER_DRIVER_TARGET`); a stretch that moves the glue face off its plane is
refused when a variant is regenerated (`PARAMETER_REGENERATION`).

**Out of scope for R093.** Geometric or sketch constraints and solving; bidirectional
dimensions (editing geometry never changes a parameter); per-copy formulas (no copy
index); reading another component's parameters, a parent's parameters or a sibling's;
overrides on nested placements; formulas in overrides; material, text or tag drivers;
string operations; user-defined functions; Dynamic Component import or export. R094
may add a material driver and per-parameter display precision while schema 28 is
open (see "Open for the owner").

**Example** (the acceptance fixture). A table definition with input `width` (length,
default `900 mm`, `min` 0.4, `max` 2.4) and `shelves` (count, default `1`, 0–4), and
derived `leg_x` = `width - 60 mm`:

- `stretch` on the top, axis x, `at` 0.45, expression `width`;
- `position` on each right leg, axis x, expression `leg_x`;
- `repeat` on the shelf, axis z, count `max(shelves, 1)`, spacing
  `300 mm`, and `visibility` on the shelf, `shelves > 0`.

A placement that overrides `width` to `1200 mm` has a top 1.2 m long and right legs
0.3 m further out; the left legs and the template are unchanged.

### 2. Expression language

Formulas are data. The engine is a closed grammar, a fixed operator and function
table, a static type and unit checker, and an iterative evaluator. It has no host
calls, no I/O, no loops, no user functions, no recursion at evaluation time, and no
names except the definition's own parameters.

**Grammar** (ISO EBNF; whitespace is spaces and tabs between tokens; input is ASCII):

```ebnf
expression  = disjunction ;
disjunction = conjunction , { "or" , conjunction } ;
conjunction = negation , { "and" , negation } ;
negation    = "not" , negation | comparison ;
comparison  = sum , [ compare-op , sum ] ;            (* not associative *)
compare-op  = "==" | "!=" | "<" | "<=" | ">" | ">=" ;
sum         = product , { ( "+" | "-" ) , product } ;
product     = sign , { ( "*" | "/" ) , sign } ;
sign        = ( "-" | "+" ) , sign | power ;
power       = primary , [ "^" , [ "-" ] , ( "0" | "1" | "2" | "3" ) ] ;
primary     = quantity | choice | "true" | "false" | "pi"
            | call | name | "(" , expression , ")" ;
call        = function , "(" , expression , { "," , expression } , ")" ;
function    = "if" | "min" | "max" | "clamp" | "abs" | "floor" | "ceil" | "round"
            | "mod" | "sqrt" | "sin" | "cos" | "tan" | "asin" | "acos" | "atan"
            | "atan2" ;
quantity    = number , [ unit ] ;
number      = ( digits , [ "." , [ digits ] ] | "." , digits ) ,
              [ ( "e" | "E" ) , [ "+" | "-" ] , digits ] ;
digits      = digit , { digit } ;
unit        = "mm" | "cm" | "m" | "in" | "ft" | "deg" | "rad" ;
choice      = '"' , choice-char , { choice-char } , '"' ;  (* [a-z0-9_-], 1-32 *)
name        = lower , { lower | digit | "_" } ;            (* 1-32, not reserved *)
```

Precedence, lowest first: `or`, `and`, `not`, comparison, `+ -`, `* /`, unary sign,
`^`. So `-x^2` is `-(x^2)`. A unit binds only to the number before it (`900 mm`,
`900mm`); `width m` is a syntax error. There are no string operations, escapes,
comments, assignments or statement separators.

**Static types.** Every expression is checked before anything is evaluated, on every
change and on load. Types are `boolean`, `choice` and quantities with a dimension
`(L, A)`, each exponent an integer in −3…3; `number` and `count` are `(0, 0)`.

| Construct | Rule |
| --- | --- |
| `+`, `-`, `min`, `max`, `clamp`, `mod`, `<`, `<=`, `>`, `>=` | Operands have equal dimensions |
| `*`, `/` | Dimensions add or subtract |
| `x ^ n` | Dimension times `n`; computed by repeated multiplication |
| `==`, `!=` | Two booleans, two choices, or two dimensionless quantities (compared exactly). Lengths, areas and angles must use `<`/`>` (`PARAMETER_TYPE`) |
| `and`, `or`, `not` | Booleans; `and`/`or` short-circuit |
| `if(c, a, b)` | `c` boolean; `a`, `b` the same type; only the chosen branch is evaluated |
| `abs` | Any quantity |
| `floor`, `ceil`, `round` | Dimensionless (`floor(width / 300 mm)`); `round` is half away from zero |
| `sqrt` | Even exponents, which are halved |
| `sin`, `cos`, `tan` | Angle → number |
| `asin`, `acos`, `atan` | Number → angle; `atan2(y, x)` with equal dimensions → angle |
| choice literal | Must be one of the compared or assigned parameter's `options` |
| result | Matches the parameter's or driver's type (`PARAMETER_TYPE` otherwise) |

`min`/`max` take 2–8 arguments, `clamp(x, lo, hi)` needs `lo ≤ hi` at run time.

**Numbers.** IEEE 754 binary64 with round-to-nearest. Literals are parsed with
`std::from_chars` (correctly rounded, independent of locale). Units convert to SI by
fixed operations: `mm` ÷ 1000, `cm` ÷ 100, `m` and `rad` unchanged, `in` × 254 ÷ 10000,
`ft` × 3048 ÷ 10000, `deg` × π ÷ 180 with π the nearest double. Evaluation order is
left to right as parsed. The engine's translation unit is compiled with
`-ffp-contract=off` and never with fast-math options. Every intermediate result must be
finite. A `count` result must be a whole number (`PARAMETER_DOMAIN`, "use round()"),
and is range-checked before conversion to an integer. Final `length`, `area` and
`angle` values (parameters and driver inputs) are quantized to 1e-9 m, 1e-12 m² and
1e-12 rad (`std::nearbyint(v / q) * q`), so values that differ only in the last bits
share one variant. Results are deterministic for one build. `sin`, `cos`, `tan`,
`asin`, `acos` and `atan` come from the C library and may differ in the last bit
between C libraries; the file never stores evaluated geometry, so such a difference
cannot make a file invalid.

**Canonical text.** The engine prints an expression from its syntax tree: single
spaces around binary operators and after commas, `<number> <unit>` with the number
in shortest round-trip form and the author's unit kept, the minimum parentheses, and
lowercase keywords. Commands accept any valid spelling and return the canonical text;
files store only canonical text, and the reader refuses text that is not canonical.

**Override literals.** An override value is not an expression. Commands accept a JSON
number in SI units, a string holding one signed quantity literal (`"1200 mm"`), a JSON
boolean or a choice value. Files store SI numbers, booleans and choice values.

### 3. Dependency graph and limits

**Graph.** Nodes are the definition's parameters and its driver expressions; edges are
name references. On every definition change, and on load:

1. Every name must be a parameter of the definition (`PARAMETER_NAME`).
2. The graph must be acyclic (`PARAMETER_CYCLE`). A self-reference is a cycle. The
   error names up to 8 parameters of one cycle in order.
3. Evaluation order is Kahn's algorithm with the ready set ordered by name bytes, then
   drivers in ID order. The order never depends on insertion or map order.

The graph is static: an input parameter's default expression keeps its edges even
when a placement overrides it, so the same definition is acyclic for every placement.
Across definitions, `childParameter` drivers only push values down the component
reference graph, which is already acyclic ([ADR 0007](0007-component-records.md)).
A child never reads its parent.

**Evaluation.** For a placement: each input takes its override if present, otherwise
its default; derived parameters follow in order; then driver inputs. Results are
memoized by (definition record, canonical override tuple), and variants by
(definition record, quantized driver values, nested variant keys). Placements with
equal values share one variant's geometry.

**Limits.** Each limit is checked before the work it bounds. The numbers bound the
worst accepted document, not typical use.

| Quantity | Limit | Code on overrun |
| --- | --- | --- |
| Expression text | 1,024 bytes, ASCII | `LIMIT_EXCEEDED` |
| Tokens per expression | 512 | `LIMIT_EXCEEDED` |
| Syntax-tree nodes per expression | 256 | `LIMIT_EXCEEDED` |
| Nesting depth (parentheses, calls, unary operators) | 32, checked before the parser descends | `LIMIT_EXCEEDED` |
| Number literal | 32 characters | `PARAMETER_SYNTAX` |
| Parameters per definition | 128 | `LIMIT_EXCEEDED` |
| Drivers per definition | 256 | `LIMIT_EXCEEDED` |
| Parametric definitions per document | 1,024 | `LIMIT_EXCEEDED` |
| Choice options | 32 | `LIMIT_EXCEEDED` |
| Repeat `maxCount`; reserved member IDs per definition | 1,000; 4,096 | `LIMIT_EXCEEDED` |
| Parametric nesting along one reference path | 8 levels | `LIMIT_EXCEEDED` |
| Node visits for one definition evaluation | 200,000 (structural maximum 163,840) | `LIMIT_EXCEEDED` |
| Node visits for the whole document | 20,000,000 over all distinct evaluations, checked on every edit and on load | `LIMIT_EXCEEDED` |
| Distinct variants per document | 4,096 | `LIMIT_EXCEEDED` |
| Parameter records in history and snapshot accounting | 4 MiB | `LIMIT_EXCEEDED` |

Regenerated geometry uses the existing limits: each distinct variant other than the
template counts once against the unique-geometry limits, and every placement's
records count against the materialized limits, both checked before records are built
(`checkLimits`, `DocumentLimits`). Limit messages follow the D06 form and quote at
most 64 bytes of a name or expression.

**Error codes.** Each carries the definition, the parameter name or driver ID, and
for expression errors a 1-based byte offset and an excerpt of at most 64 bytes.

| Code | Meaning |
| --- | --- |
| `PARAMETER_SYNTAX` | The text does not match the grammar |
| `PARAMETER_NAME` | Unknown, reserved, duplicate or malformed name; removing or retyping a parameter that is still referenced (the referrers are listed) |
| `PARAMETER_TYPE` | Unit or type mismatch, bad argument count, unknown choice value |
| `PARAMETER_CYCLE` | The definition's graph has a cycle |
| `PARAMETER_DOMAIN` | Division or `mod` by zero, `sqrt` of a negative, `asin`/`acos` outside −1…1, a non-finite intermediate, a value outside its type range or bounds, a non-whole count, `clamp` with `lo > hi` |
| `PARAMETER_DRIVER_TARGET` | Missing, root, generated or wrong-kind target member; a curve crossing a stretch plane; a glue-moving driver; an unknown child parameter |
| `PARAMETER_REGENERATION` | A variant fails geometry validation: an edge shorter than the tolerance, a face whose orientation flips relative to the template, a coordinate outside ±1e6 m, an invalid transform. Names the driver and the placement |
| `PARAMETER_TEMPLATE_SCOPE` | A shared geometry edit was scoped through a placement that does not show the template (§4) |

Existing codes keep their meaning: `LIMIT_EXCEEDED`, `LOCKED_ENTITY`, `NOT_FOUND`,
`INVALID_TARGET`, `STALE_REVISION`.

### 4. Transactions

- **One change, one entry.** Every parameter, driver or override operation is an
  ordinary model edit. A batch may mix it with other model commands (for example
  `component.create`, `component_parameter.define` and `component_parameter.drive` in
  one batch) and publishes one history entry and one revision. Undo and redo restore
  exact records; allocator floors (`nextComponentDriverId`, definition `nextMemberId`,
  scene `nextId`) never rewind.
- **Fail closed.** All checks — syntax, names, types, cycles, bounds, limits, every
  placement's evaluation and regeneration, host re-cuts and document limits — run on
  the staged draft before publication. Any failure refuses the whole batch: the
  revision, history and every record are unchanged (pointer-identical), and the error
  names the first failure. There is no partial regeneration and no "regenerate later"
  state. A file whose parameters fail any check is refused on open and is not
  modified.
- **Shared and instance scope.** Definition-side operations are shared-definition
  edits: they name the definition, follow `component.edit`'s explicit-scope and
  permission rules, and are refused (`LOCKED_ENTITY`) if they would change a locked
  placement. Overrides are instance-scoped, like moving a placement, and refused on a
  locked placement. Component edit scopes reject parameter operations on other
  definitions, as they reject scene operations today.
- **Manual edits inside a parametric definition.**
  - `component.edit` with only a `definition` edits the template.
  - `component.edit` scoped through an `instance` is allowed only when that placement
    has no effective overrides (its variant is the template) and the edit does not
    name a repeat copy. Otherwise it is refused with `PARAMETER_TEMPLATE_SCOPE`.
  - `component.edit_instance` on a parametric placement makes it unique as today,
    then re-baselines the unique definition so that the placement's current values
    become its defaults, clears its overrides and applies the edits to the new
    template. The new definition keeps its parameters; the result reports
    `parametersPromoted: true`.
  - `component.make_unique` copies the parameter record (new driver IDs) and keeps the
    placement's overrides. `component.replace` drops overrides (reported in the
    result). Copying a placement copies its overrides. Exploding one gives the
    variant's records.
  - A template edit that deletes a driver's target, changes its kind, or makes a
    curve cross a stretch plane is refused (`PARAMETER_DRIVER_TARGET`) unless the same
    batch removes or retargets the driver. Any placement that the new template cannot
    regenerate refuses the edit (`PARAMETER_REGENERATION`).
- **Data for R094's "incompatible manual changes" warning.** Each driver stores
  `bound`: the SHA-256 of the canonical encoding of what it acts on (the target
  member's record, or for `repeat` its subtree, for `childParameter` the referenced
  definition's parameter record). The command layer stamps it when the driver is
  created or updated, after a re-baseline, and on `component_parameter.rebind`, as
  R090.d stamps accepted views. Other edits leave it. Driver state is derived, never
  stored: `bound` when the digest matches, `changedSinceBound` when it does not.
  A template edit's result lists the drivers it moved to `changedSinceBound`; the edit
  itself is not refused. R094 shows this state and offers Rebind.

### 5. Persistence (schema 28)

The public schema advances to **sketchyup-document-v28** under container version 2,
with document chunk encoding `json-v28` and required feature
**`component-parameters-v1`** appended after `drawing-sheets-v1`. A schema-28 root
always carries the schema-27 sheet tables (possibly empty), so the feature set is the
schema-27 set plus `component-parameters-v1`. ADR 0108 gets a "Schema 28" section.

Root additions (all required at schema 28; `allocatorFloors` adds
`nextComponentDriverId`, which must equal the root's):

- `componentParameters`: one record per parametric definition, ascending
  `definition`: `{definition, parameters, drivers}`.
  - `parameters`: array in display order of
    `{name, label, type, mode, expression, min, max, step, options}`; `min`, `max`,
    `step` and `options` are `null` where they do not apply.
  - `drivers`: ascending `id`, each `{id, kind, member, expression, bound, data}`;
    `data` holds exactly the kind's fields: `stretch {axis, at}`,
    `position {axis}`, `rotation {axis}`, `visibility {}`,
    `repeat {axis, maxCount, spacing, firstMember}`, `childParameter {parameter}`.
- `componentParameterOverrides`: ascending `instance` (a top-level placement root),
  `{instance, values}` with `values` an object of at least one entry, keys in byte
  order.
- `nextComponentDriverId`.

Canonical rules follow the house style: IDs and member references are canonical
nonzero decimal strings; numbers are SI in shortest round-trip form; expressions are
canonical text; `bound` is 64 lowercase hexadecimal digits; every field is required;
unknown fields are rejected. Instance bindings (schema 26 `members`) list exactly the
members the placement's variant materializes, including existing repeat copies. On
load, definitions and their parameters are validated and evaluated before placements
are projected, and the whole document then passes the ordinary restore validation.

**Writer policy** (extends ADR 0163 decision 2): no sheets and no parameter records →
schema 26; sheets but no parameter records → 27; any parameter record → 28. Deleting
every parameter record writes 27 or 26 again; the driver allocator is then not stored
and restarts at 1 on reopen, which is safe because overrides refer to parameters by
name and no record can name a retired driver. Schemas 1–27 load with empty tables; no
migration code is needed. The writer string changes only with the version.

**Older readers.** SketchyUp 1.1.x and 1.0.x refuse a schema-28 file at
`decodeContainer` with their error for a newer format, before reading the document,
and leave it unchanged. They keep opening parameter-free files written by 1.2.0.
R093.c records the exact message from the 1.1.0 package.

**Bundles and templates.** Component bundle capture ([ADR 0130](0130-contained-component-bundles.md))
includes the parameter records of every captured definition; the bundle's root
placement has no overrides. Insertion allocates new driver IDs and remaps `member`,
`firstMember` and `childParameter` targets with the definition. Template capture and
instantiation keep parameter records and overrides. A bundle or template with
parameters is schema 28.

### 6. APIs

All additions are additive (ADR 0162). Every name starts with `component_parameter`.

**Transaction operations:**

| Operation | Request (outline) | Scope |
| --- | --- | --- |
| `component_parameter.define` | `{definition, name, label, type, mode, expression, min?, max?, step?, options?, position?}` | shared |
| `component_parameter.update` | `{definition, name, label?, expression?, min?, max?, step?, options?, position?}`; type and mode are fixed | shared |
| `component_parameter.rename` | `{definition, name, newName}`; rewrites references in the definition, overrides on its placements and parent `childParameter` drivers | shared |
| `component_parameter.remove` | `{definition, name}`; refused while referenced | shared |
| `component_parameter.drive` | `{definition, driver?, kind, member, expression, data}`; creates, or updates `driver` | shared |
| `component_parameter.undrive` | `{definition, driver}` | shared |
| `component_parameter.rebind` | `{definition, drivers}` or `{definition, all: true}` | shared |
| `component_parameter.override` | `{body, values}`; `body` is a top-level placement | instance |
| `component_parameter.reset` | `{body, names}` or `{body, all: true}` | instance |

Batch results add `createdComponentDrivers` beside `createdScenes`. Every result adds
`regenerated {placements, variants}` and `parameterWarnings` (drivers that became
`changedSinceBound`).

**Inspection queries:**

- `component_parameters.describe {definition}`: parameters in evaluation order with
  canonical expressions, bounds, `dependsOn` and `usedBy`; drivers with kind, target,
  expression, `dependsOn` and state; placement and override counts.
- `component_parameters.values {body}`: the placement's values, each with its SI value,
  display string in document units and source (`override`, `default`, `derived` or
  `parent`), plus driver states.
- `component_parameters.evaluate {definition, overrides?, expression?}`: a read-only
  dry run. It checks an expression or a set of overrides against the definition and
  returns values or the first error (code, offset, message) and the regeneration size,
  within the same limits. R094's inspector validates as the user types through it.

`--inspect-native` adds `componentParameters`, `componentDrivers` and
`componentParameterOverrides` counts; `document.describe` does not change. The
catalogue regenerates `transactions-v1.json`, `inspection*-v1.json`,
`headless-session-v1.json`, the `mcp-*` files and `tool-reference-v1.json`
(ADR 0138). MCP, the headless session, `--inspect` and the CLI expose every operation
and query.

**Assistant surface.** The in-app assistant does not see these tools in the F02
release. `assistantVisible()` adds one rule beside the `drawing_` prefix: a name that
starts with `component_parameter` (which covers `component_parameters.*`) is not
assistant-visible. The assistant's top-level tools, `transaction.inspect` request
schemas and `transaction.apply` command schemas stay exactly the 1.0.x list, so
`tests/assistant-tool-surface-baseline.json` does not change and the guard test
passes unedited. A parameter tool call from the assistant is rejected as
`UNSUPPORTED_CAPABILITY`. Assistant-visible commands that touch a parametric
component follow §4 (for example `component.edit_instance` promotes values to the
unique copy's defaults). Exposing a subset later means updating the baseline on
purpose with a corpus re-run (see "Open for the owner").

### 7. Security and abuse

- **Non-executable by construction.** The evaluator is a `switch` over a closed opcode
  enum, run iteratively over a post-order node array with a value stack of at most 256
  entries. Names resolve only against the definition's parameter table; functions
  only against the fixed table above. There is no string type beyond choice values.
- **Isolated library.** The engine is a Qt-free static library,
  `sketchyup_parameter_expression`, depending only on the C++ standard library and
  libm. A ctest (`parameter_expression_symbols`) lists its undefined symbols with `nm`
  and fails if any process, file, network or loader symbol appears (`system`,
  `popen`, `exec*`, `fork`, `posix_spawn`, `dlopen`, `fopen`, `open`, `socket` and
  similar).
- **Fuzzing.** A new libFuzzer format `parameter-expression` joins `tests/fuzz/`: the
  input is one expression, checked and evaluated against a fixed table with one
  parameter of every type. Invariants: no crash or sanitizer report; an accepted
  expression's canonical text re-parses to the same tree and value; a rejected one
  gives a code from the closed set with an offset inside the input; node, depth and
  step counters stay within limits. `native-json`, `native-container`, `component`
  and `template` get seeds with parameter records (including a cycle, a repeat at
  `maxCount` and a nested `childParameter`). The parser fault corpus adds the new
  format and the schema-28 seeds, and its count is updated.
- **In-ctest fuzz.** `parameter_expression_fuzz` runs a seeded generator (20,000
  expressions plus mutations of the seeds) in the `dev` and `sanitize` presets, as
  `geometry_fuzz` does.
- **Budget.** Rejections are linear in input size: an over-long or over-deep input is
  refused before it is tokenized or descended. CI asserts generous ceilings (ten times
  the measured values) for parsing and checking a 1,024-byte expression, evaluating
  the worst accepted definition (128 parameters, 256 drivers, 256-node expressions),
  and loading the worst accepted document. R093.e records the measured values.

### 8. Verification plan

R093's Verify line maps to these checks:

- **Changing width drives dependent geometry deterministically.** The table fixture
  (§1): overriding `width` to 1.2 m gives a 1.2 m top and moves only the right legs by
  0.3 m; changing the default re-baselines the template and regenerates every
  placement; `shelves` 0–4 hides or repeats the shelf with stable copy IDs. The
  canonical encoding after the change is byte-identical across ten repetitions,
  across shuffled record insertion order, after undo and redo, after save and reopen,
  and between the `dev` and `sanitize` builds. A glued window with a driven width
  re-cuts its host opening in the same edit.
- **Invalid or cyclic formulas cannot partially commit.** For each error code: a batch
  whose last operation fails leaves revision, history and every record
  pointer-identical, and the dirty state unchanged. A regeneration failure in one
  placement of many refuses the edit. A file with a cycle, an unknown name or a
  non-canonical expression is refused on open and left byte-identical.
- **Formulas cannot execute host code.** Grammar-closure tests (shell, script and path
  text, `$()`, backticks, `;`, `__proto__`, `import`, unknown function names) give
  `PARAMETER_SYNTAX` or `PARAMETER_NAME`; the symbol test; the fuzz targets.
- **Compatibility.** The 1.1.0 package's CLI refuses the schema-28 fixture with
  `--validate-native` and leaves it unchanged, and still opens parameter-free saves
  from the new build. The provider corpus inputs have no parameters, so they stay
  schema 26.

## Consequences

- The public schema advances once, to `sketchyup-document-v28`. Files with parameters
  do not open in 1.1.x or 1.0.x; the open fails cleanly and the file is not modified.
  Parameter-free files keep the 26 and 27 shapes.
- Placements of a parametric definition can differ in geometry, so a definition no
  longer means "one geometry". Variants keep instancing for placements with equal
  values, and each distinct variant costs unique-geometry budget.
- Loading a document evaluates its parameters. The document evaluation budget bounds
  that cost.
- Shared geometry edits work in template coordinates. Editing through a placement that
  shows a different variant is refused until R094 designs that workflow.
- The in-app assistant cannot set parameters in the F02 release; MCP and the APIs can.

## Open for the owner

Each item states the default this ADR assumes.

1. **Release grouping.** Default: F02 (R093 and R094) ships alone as **1.2.0**, gated
   by its own record `docs/verification/F02-1.2.0.md`. Alternative: bundle it with
   another Extended row.
2. **Schema 28 stays open on `main` until the F02 release**, as decision 6 of ADR 0163
   did for 27. Default: yes, so R094 can add a material driver, per-parameter display
   precision or inspector metadata without a schema 29. No release writes 28 before
   then.
3. **Assistant visibility.** Default: hidden in 1.2.0 (§6). Alternative: expose only
   `component_parameter.override`, `component_parameter.reset` and
   `component_parameters.values`, after a corpus run measures the added per-turn
   tokens.
4. **`component.edit_instance` on a parametric placement.** Default: promote the
   placement's values to the unique copy's defaults and keep its parameters (§4).
   Alternatives: drop the parameters of the unique copy (bake), or refuse.
5. **Dynamic Components.** Default: no import, export or formula compatibility in
   F02; the release notes and support matrix say so. A conversion study would be a
   separate Investigate row.

## Layer plan

The roadmap entry [R093](../PR_ROADMAP.md#r093) lists the same slices with Build and
Verify lines. Each requires its predecessor.

**R093.a: expression engine.**
- Files: `src/core/parameter_expression.{hpp,cpp}` (lexer, parser, checker, iterative
  evaluator, canonical printer, limits, error codes) as the
  `sketchyup_parameter_expression` library; `CMakeLists.txt`.
- Tests: `tests/parameter_expression_tests.cpp` (every production, precedence, unit
  algebra, domain errors with offsets, canonical round trip, each limit at n and n+1,
  golden values bit-exact), `parameter_expression_fuzz`, `parameter_expression_symbols`;
  the `parameter-expression` libFuzzer format and seeds.

**R093.b: parameter records, graph and regeneration.**
- Files: `src/core/component_parameter_records.{hpp,cpp}`,
  `src/core/component_parameters.{hpp,cpp}` (graph, edit builders, re-baseline),
  `src/core/component_regeneration.{hpp,cpp}` (drivers, variants, cache),
  `src/core/components.cpp`, `src/core/model.{hpp,cpp}`, `src/core/history.cpp`,
  `src/core/prepared_edit.cpp`, host regeneration wiring.
- Tests: `tests/component_parameter_tests.cpp` (the table fixture, determinism,
  repeat identities, re-baseline, nested `childParameter`, hosted re-cut, locked
  placements, make-unique, replace, edit-instance promotion, template-scope refusal,
  fail-closed batches with pointer identity, limits). No persistence.

**R093.c: schema 28 persistence.**
- First commit: retain unmodified schema-27 writer output from the `v1.1.0` tag build
  as `tests/fixtures/release-1.1.0-v27.sketchyup` (README records the command and
  commit).
- Then `src/io/component_parameters_io.{hpp,cpp}`, root fields in `document_io.cpp`,
  the feature in `container.cpp`, `nativeDocumentVersion = 28` and the writer policy,
  `docs/api/native-format-v1.json`, the ADR 0108 section, bundle and template capture,
  and a `component-parameters-v28.sketchyup` writer fixture.
- Tests: round trip of every field; v1–27 fixtures load with empty tables and stay
  byte-identical; non-canonical, unknown or missing fields and allocator disagreement
  are refused; writer policy 26/27/28 and back; save, `.bak` and recovery; bundles and
  templates. Fuzz seeds and fault-corpus counts. Package acceptance against the 1.1.0-1
  package.

**R093.d: commands, inspection, MCP and assistant surface.**
- Files: `src/automation/component_parameter_commands.{hpp,cpp}`, `catalog.cpp`,
  `inspection.cpp`, `component_scope.cpp`, `assistant.cpp` (`assistantVisible`), the
  generated `docs/api` files.
- Tests: `tests/component_parameter_command_tests.cpp` (atomic batches, stale revision,
  undo, rebind stamps, every refusal code, dry-run evaluate leaves the document
  unchanged), the API schema tests, and `assistant_tool_surface_tests` passing against
  the unchanged baseline.

**R093.e: acceptance record.**
- `docs/verification/R093e-component-parameters.md` with a JSON summary: the Verify
  line, `dev` and `sanitize` suites, a 300-second campaign for `parameter-expression`
  and the native formats with the new seeds, the measured budget values, and the
  previous-release check. The ADR's status records R093.a–e as complete.

## Implementation notes (R093.b)

R093.b settled these points against the real component code. They refine the decision
above; where they differ, these notes govern.

- **Visibility is relative**, like every driver: `false` hides the member, `true` shows
  it when the default is `false`. A `false` default hides the template member, which
  re-baselining enforces. The whole-model reveal is refused while a visibility driver
  hides a member at the defaults. Several visibility drivers on one member combine
  with AND.
- **Stretch planes are not crossed by re-baselining.** A default change that would move
  template geometry across its own stretch plane is refused (`PARAMETER_REGENERATION`);
  otherwise later variants would silently stop stretching those vertices.
- **A driver whose kind, member, axis or plane changes** starts from the template
  instead of being re-baselined.
- **Repeated subtrees may not overlap** (no nested repeats). A template edit that
  changes a repeated subtree re-reserves the block, so those copies get new scene IDs.
- **One `childParameter` driver per (member, child parameter).**
- **`bound`** covers the member record without allocator floors; for `childParameter`
  it covers the child definition's parameter list, not its drivers.
- **Nesting and explode.** A placement with overrides cannot become a member of a new
  component (`INVALID_TARGET`) rather than silently losing its overrides. Explode turns
  the parent-supplied values of nested placements into overrides.
- **Promotion** (`component.edit_instance`, §4) requires a definition with exactly one
  placement, which `edit_instance` guarantees after making the placement unique.
  Promoted values are written as the shortest literal giving the same value.
- **Bounds** compare quantized values, so a 2.4 m maximum accepts 2.4 m.
- **Labels** that break the label rule are `PARAMETER_NAME`.
- **Document evaluation budget** counts each parametric definition's default
  evaluation plus each distinct placement evaluation.
- **Unsaved until R093.c.** Until schema 28 exists, the native writer refuses a
  document with parameter records (`UNSUPPORTED_FORMAT`). No command creates them
  before R093.d.
- **Rendering.** The viewport and inference still match scene records against
  definition members, so a variant placement renders with its own meshes rather than
  shared ones: correct, not instanced. Instanced variant rendering is R094 work.

## What R094 needs from R093

- `component_parameters.describe`, `.values` and `.evaluate` for the inspector and live
  validation, and the canonical text for display.
- Driver states (`changedSinceBound`) and `component_parameter.rebind` for the
  incompatible-manual-change warning.
- A decision on editing through a placement that shows a non-default variant
  (refused in R093 with `PARAMETER_TEMPLATE_SCOPE`).
- Regeneration that keeps scene IDs, materials and the open edit scope: the placement
  root and every template member keep their IDs, and copies keep theirs while they
  exist.
- Bundle capture of parametric definitions for reusable assemblies.
