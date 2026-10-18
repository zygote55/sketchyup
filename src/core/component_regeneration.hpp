#pragma once
// R093.b: drivers, variants and the variant cache (ADR 0165 §1, §3).
//
// A variant is the template with the drivers applied for one set of values: driven
// members, repeat copies under their reserved IDs, and the nested variants of the
// reference members. Variants are derived and immutable. Placements with equal
// values share one variant object, and a variant shares every member a driver does
// not change with the template. A variant that equals the template (no driver
// changes anything, no copies, every nested placement shows its template) is
// represented by a null pointer, so default placements project exactly as before.
#include "core/component_parameters.hpp"
namespace sketchy {
struct ComponentVariant {
    Id definition{};
    DefinitionPtr source; // the template record it was regenerated from
    // Template members (driven ones replaced) plus repeat copies.
    std::map<Id, BodyPtr> members;
    std::map<Id, Id> references; // reference members, copies included -> definition
    // Reference member -> nested variant; absent when the nested placement shows its template.
    std::map<Id, std::shared_ptr<const ComponentVariant>> nested;
    // Reference member -> the values its parent's childParameter drivers supply.
    std::map<Id, ComponentOverrides> nestedValues;
    std::map<Id, Id> generated; // repeat copy member -> the template member it copies
    // Every definition record this variant derives from (its reference closure).
    std::map<Id, DefinitionPtr> dependencies;
    ComponentDigest key{};
};
using VariantPtr = std::shared_ptr<const ComponentVariant>;
struct ComponentEvaluationEntry {
    DefinitionPtr source;
    ComponentEvaluation evaluation;
};
using EvaluationPtr = std::shared_ptr<const ComponentEvaluationEntry>;
// Derived state of a whole document: every placement (top-level and nested) whose
// variant differs from its template, the distinct evaluations in use (each
// parametric definition's defaults included) and the distinct variants.
struct ComponentVariantIndex {
    std::map<Id, VariantPtr> placements;
    // How many entries of `placements` show each variant object, kept with
    // `placements` as the index is built, so per-edit limit checks scale with the
    // distinct variants rather than the placements (D-039).
    std::map<VariantPtr, std::size_t> placementUses;
    std::map<std::pair<Id, std::string>, EvaluationPtr> evaluations;
    std::map<std::pair<Id, ComponentDigest>, VariantPtr> variants;
    std::size_t visits{};
};
using ComponentVariantsPtr = std::shared_ptr<const ComponentVariantIndex>;
const ComponentVariantsPtr &emptyComponentVariants();
// Equal placements, evaluations and variant contents (pointer identity not required).
bool sameComponentVariants(const ComponentVariantIndex &a, const ComponentVariantIndex &b);
std::size_t componentVariantBytes(const ComponentVariantIndex &index);

// One computation session. Results are memoized by (definition, canonical input
// values) and variants by (definition, driver values, nested variant keys). A
// previous index seeds both, so unchanged placements keep their variant objects.
class ComponentVariantEngine {
  public:
    // With `retainPrevious`, every entry of `previous` stays in use (incremental
    // insertion): limits count it and its evaluations are not counted twice.
    explicit ComponentVariantEngine(const ComponentDefinitions &definitions,
                                    const ComponentVariantIndex *previous = nullptr,
                                    bool retainPrevious = false);
    ~ComponentVariantEngine();
    ComponentVariantEngine(const ComponentVariantEngine &) = delete;
    ComponentVariantEngine &operator=(const ComponentVariantEngine &) = delete;
    // True when the definition or anything it references has a parameter record.
    bool parametric(Id definition);
    VariantPtr variant(Id definition, const ComponentOverrides &inputs);
    // Evaluate a placement and compute its variant key without building geometry;
    // checkPlanned() then refuses more than 4,096 distinct variants before any is built.
    void plan(Id definition, const ComponentOverrides &inputs);
    void checkPlanned() const;
    const ComponentEvaluation &evaluate(Id definition, const ComponentOverrides &inputs);
    CompiledParametersPtr compiled(Id definition);
    // Moves the used evaluations and variants into `index`.
    void collect(ComponentVariantIndex &index);

  private:
    struct State;
    std::unique_ptr<State> state_;
};
// Evaluates every parametric definition's defaults and every placement, top-down
// from the top-level placements in root order, and checks the document limits
// (distinct variants, evaluation visits). Nested placements must not hold overrides.
ComponentVariantsPtr computeComponentVariants(const ComponentDefinitions &definitions,
                                              const ComponentInstances &instances,
                                              const ComponentVariantIndex *previous = nullptr);
// Incremental form for edits that only insert placements (no definition changes):
// `current` plus the inserted top-level placements of `added`. Null when unchanged.
ComponentVariantsPtr extendComponentVariants(const ComponentDefinitions &definitions,
                                             const ComponentInstances &existing,
                                             const ComponentInstances &added,
                                             const ComponentVariantIndex &current);
// A definition-shaped view of a placement's variant (members and references), for
// glue resolution and projection. Returns the template itself for a null variant.
ComponentDefinition componentVariantDefinition(const ComponentDefinition &definition,
                                               const VariantPtr &variant);
// Re-baseline (ADR 0165 §1): the template rewritten to the variant at the new
// defaults, and the stretch planes moved by the stretches below them.
struct ComponentRebase {
    std::map<Id, BodyPtr> members;
    ComponentParameters record;
    std::set<Id> moved; // template members the re-baseline changed
};
// `staged` is `original` with the new record (and any newly reserved repeat block);
// its members are still the old template.
// `before` holds `original` as the document has it; `after` the definitions the new
// record is checked against (it may rename a child parameter in the same edit).
ComponentRebase rebaseComponentTemplate(const DefinitionPtr &original, const DefinitionPtr &staged,
                                        const ComponentDefinitions &before,
                                        const ComponentDefinitions &after);
// The materialized size of one placement of a variant.
ComponentSize componentVariantSize(const ComponentDefinition &definition, const VariantPtr &variant,
                                   const std::map<Id, ComponentSize> &templates);
} // namespace sketchy
