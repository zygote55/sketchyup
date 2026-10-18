#include "core/component_regeneration.hpp"
#include "core/component_glue.hpp"
#include <algorithm>
#include <cmath>
namespace sketchy {
namespace {
using Context = ComponentParameterErrorContext;
[[noreturn]] void fail(const char *code, const std::string &message, Context context = {}) {
    throw ComponentParameterError(code, message, std::move(context));
}
std::string inputsKey(const ComponentOverrides &inputs) {
    std::string key;
    appendCanonicalOverrides(key, inputs);
    return key;
}
double &coordinate(Vec3 &point, std::size_t axis) {
    return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
}
double coordinate(const Vec3 &point, std::size_t axis) {
    return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
}
double quantity(const ParameterValue &value) { return std::get<ParameterQuantity>(value).value; }
// Every effect the drivers have on one member, relative to the template.
struct MemberEffects {
    std::array<std::vector<std::pair<double, double>>, 3> stretch; // (at, delta) per axis
    std::vector<std::pair<ComponentAxis, double>> rotations;        // driver-ID order
    Vec3 position{};
    bool positioned{};
    std::optional<bool> visible, visibleByDefault; // AND of the values and of the defaults
    Id driver{};                                    // the first driver, for diagnostics
    bool geometry() const {
        return !stretch[0].empty() || !stretch[1].empty() || !stretch[2].empty();
    }
};
// Visibility (ADR 0165 §1; R093.b reading): false hides the member; true keeps the
// template's flag while the default is true, and shows the member when the default
// is false (the template then has it hidden). At the defaults this is the identity.
bool drivenHidden(bool templateHidden, bool visible, bool visibleByDefault) {
    if (!visible)
        return true;
    return visibleByDefault ? templateHidden : false;
}
// The template coordinates decide which side of each stretch plane a point is on.
double stretchShift(const std::vector<std::pair<double, double>> &planes, double templateValue) {
    double shift = 0;
    for (const auto &[at, delta] : planes)
        if (templateValue > at)
            shift += delta;
    return shift;
}
Body driveMember(const Body &source, const MemberEffects &effects) {
    Body body = source;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto &planes = effects.stretch[axis];
        if (planes.empty())
            continue;
        for (auto &[id, point] : body.surface.vertices)
            coordinate(point, axis) +=
                stretchShift(planes, coordinate(source.surface.vertices.at(id), axis));
        for (auto &[id, guide] : body.guides)
            coordinate(guide.origin, axis) +=
                stretchShift(planes, coordinate(source.guides.at(id).origin, axis));
        // A curve lies wholly on one side of every plane (checked when the driver is
        // created and on every template edit), so it moves rigidly with its edges.
        for (auto &[id, curve] : body.curves) {
            const auto &original = source.curves.at(id);
            double side = coordinate(original.center, axis);
            for (const auto &edge : original.edges)
                if (const auto found = source.topology.edges.find(edge.edge);
                    found != source.topology.edges.end() &&
                    source.surface.vertices.contains(found->second.a)) {
                    side = coordinate(source.surface.vertices.at(found->second.a), axis);
                    break;
                }
            coordinate(curve.center, axis) += stretchShift(planes, side);
        }
    }
    for (const auto &[axis, delta] : effects.rotations)
        body.transform = body.transform * Transform::rotation(componentAxisVector(axis), delta);
    if (effects.positioned)
        body.transform = Transform::translation(effects.position) * body.transform;
    if (effects.visible)
        body.hidden = drivenHidden(source.hidden, *effects.visible, *effects.visibleByDefault);
    return body;
}
// A regenerated member must be valid geometry in the template's orientation
// (ADR 0165 §3, PARAMETER_REGENERATION).
void checkMember(const Body &source, const Body &body, Id definition, Id member, Id driver) {
    Context context;
    context.definition = definition;
    context.driver = driver;
    auto failure = [&](const std::string &what) {
        fail("PARAMETER_REGENERATION",
             "Driver #" + std::to_string(driver) + " regenerates member #" +
                 std::to_string(member) + " of definition #" + std::to_string(definition) +
                 " with " + what,
             context);
    };
    try {
        body.transform.validate();
    } catch (const std::exception &) {
        failure("an invalid transform");
    }
    if (body.surface.vertices == source.surface.vertices && body.guides == source.guides &&
        body.curves == source.curves)
        return;
    for (const auto &[id, point] : body.surface.vertices)
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
            std::abs(point.x) > coordinateLimit || std::abs(point.y) > coordinateLimit ||
            std::abs(point.z) > coordinateLimit)
            failure("a coordinate outside +-1e6 m");
    for (const auto &[id, edge] : body.topology.edges)
        if (length(body.surface.vertices.at(edge.a) - body.surface.vertices.at(edge.b)) <
            tolerance)
            failure("edge #" + std::to_string(id) + " shorter than the tolerance");
    for (const auto &[id, face] : body.surface.faces) {
        try {
            if (dot(source.surface.normal(id), body.surface.normal(id)) <= 0)
                failure("face #" + std::to_string(id) + " flipped relative to the template");
            (void)body.surface.triangulate(id);
        } catch (const ComponentParameterError &) {
            throw;
        } catch (const std::exception &error) {
            failure("invalid face #" + std::to_string(id) + " (" + error.what() + ")");
        }
    }
    try {
        body.surface.validate();
        body.topology.validate(body.surface);
        validateCurves(body.curves, body.surface, body.topology);
        validateGuides(body.guides, body.surface);
    } catch (const std::exception &error) {
        failure(std::string("invalid geometry (") + error.what() + ")");
    }
}
// Drivers keep the glue face in its plane (ADR 0165 §1).
void checkGlue(const ComponentDefinition &definition, const std::map<Id, BodyPtr> &members,
               Id driver) {
    if (!definition.glue)
        return;
    const auto member = definition.glue->member;
    const auto &before = *definition.members.at(member);
    const auto &after = *members.at(member);
    if (before.surface.vertices == after.surface.vertices &&
        before.transform == after.transform)
        return;
    Context context;
    context.definition = definition.id;
    context.driver = driver;
    try {
        const auto face = definition.glue->face;
        const auto &loop = before.surface.faces.at(face).loops.front();
        const auto n0 = before.surface.normal(face), n1 = after.surface.normal(face);
        const auto d0 = dot(n0, before.surface.vertices.at(loop.front()));
        const auto d1 = dot(n1, after.surface.vertices.at(loop.front()));
        if (std::abs(dot(n0, n1) - 1) > 1e-9 || std::abs(d1 - d0) > tolerance)
            fail("PARAMETER_REGENERATION",
                 "Driver #" + std::to_string(driver) + " moves the glue face of definition #" +
                     std::to_string(definition.id) + " off its plane",
                 context);
        auto view = definition;
        view.members = members;
        (void)resolveComponentGlue(view);
    } catch (const ComponentParameterError &) {
        throw;
    } catch (const std::exception &error) {
        fail("PARAMETER_REGENERATION",
             "Driver #" + std::to_string(driver) + " breaks the glue face of definition #" +
                 std::to_string(definition.id) + " (" + error.what() + ")",
             context);
    }
}
std::vector<Id> subtreeOf(const ComponentDefinition &definition, Id member) {
    std::map<Id, std::vector<Id>> children;
    for (const auto &[id, body] : definition.members)
        if (id != definition.root)
            children[body->parent].push_back(id);
    std::vector<Id> result, stack{member};
    while (!stack.empty()) {
        const auto id = stack.back();
        stack.pop_back();
        result.push_back(id);
        if (const auto found = children.find(id); found != children.end())
            stack.insert(stack.end(), found->second.begin(), found->second.end());
    }
    std::sort(result.begin(), result.end());
    return result;
}
ComponentSize bodySize(const Body &body) {
    return {1,
            body.surface.vertices.size(),
            body.surface.faces.size(),
            body.surface.wires.size(),
            body.topology.edges.size(),
            body.curves.size(),
            body.guides.size(),
            0,
            0};
}
void addSize(ComponentSize &total, const ComponentSize &size, std::size_t times = 1) {
    total.records += size.records * times;
    total.vertices += size.vertices * times;
    total.faces += size.faces * times;
    total.wires += size.wires * times;
    total.edges += size.edges * times;
    total.curves += size.curves * times;
    total.guides += size.guides * times;
    total.placements += size.placements * times;
}
std::size_t estimateBytes(const Body &body) {
    std::size_t bytes = sizeof(Body) + body.name.size() +
                        body.surface.vertices.size() * (sizeof(Vec3) + 64) +
                        body.surface.wires.size() * sizeof(std::array<Id, 2>) +
                        body.topology.edges.size() * (sizeof(EdgeRecord) + 64) +
                        body.guides.size() * (sizeof(Guide) + 64) +
                        body.curves.size() * (sizeof(Curve) + 64);
    for (const auto &[id, face] : body.surface.faces) {
        bytes += sizeof(Face) + 64;
        for (const auto &loop : face.loops)
            bytes += sizeof(loop) + loop.size() * sizeof(Id);
    }
    return bytes;
}
} // namespace

const ComponentVariantsPtr &emptyComponentVariants() {
    static const ComponentVariantsPtr empty = std::make_shared<const ComponentVariantIndex>();
    return empty;
}
namespace {
bool sameVariant(const VariantPtr &a, const VariantPtr &b) {
    if (a == b)
        return true;
    if (!a || !b || a->definition != b->definition || a->key != b->key ||
        a->references != b->references || a->generated != b->generated ||
        a->nestedValues != b->nestedValues || a->members.size() != b->members.size() ||
        a->nested.size() != b->nested.size())
        return false;
    for (const auto &[id, body] : a->members)
        if (!b->members.contains(id) || !(*body == *b->members.at(id)))
            return false;
    for (const auto &[id, nested] : a->nested)
        if (!b->nested.contains(id) || !sameVariant(nested, b->nested.at(id)))
            return false;
    return true;
}
} // namespace
bool sameComponentVariants(const ComponentVariantIndex &a, const ComponentVariantIndex &b) {
    if (a.visits != b.visits || a.placements.size() != b.placements.size() ||
        a.evaluations.size() != b.evaluations.size() || a.variants.size() != b.variants.size())
        return false;
    for (const auto &[root, variant] : a.placements)
        if (!b.placements.contains(root) || !sameVariant(variant, b.placements.at(root)))
            return false;
    for (const auto &[key, entry] : a.evaluations) {
        const auto found = b.evaluations.find(key);
        if (found == b.evaluations.end() ||
            entry->evaluation.parameters != found->second->evaluation.parameters ||
            entry->evaluation.drivers != found->second->evaluation.drivers ||
            entry->evaluation.spacing != found->second->evaluation.spacing ||
            entry->evaluation.visits != found->second->evaluation.visits)
            return false;
    }
    for (const auto &[key, variant] : a.variants)
        if (!b.variants.contains(key) || !sameVariant(variant, b.variants.at(key)))
            return false;
    return true;
}
std::size_t componentVariantBytes(const ComponentVariantIndex &index) {
    std::size_t bytes = sizeof(ComponentVariantIndex) + index.placements.size() * 64 +
                        index.placementUses.size() * 64;
    for (const auto &[key, entry] : index.evaluations)
        bytes += sizeof(ComponentEvaluationEntry) + 96 + key.second.size() +
                 (entry->evaluation.parameters.size() + entry->evaluation.drivers.size()) *
                     (sizeof(ParameterValue) + 32);
    for (const auto &[key, variant] : index.variants) {
        bytes += sizeof(ComponentVariant) + 128 +
                 (variant->members.size() + variant->references.size() +
                  variant->generated.size() + variant->dependencies.size()) *
                     64;
        // Members a driver does not change are the template's own records.
        for (const auto &[id, body] : variant->members) {
            const auto found = variant->source->members.find(id);
            if (found == variant->source->members.end() || found->second != body)
                bytes += estimateBytes(*body);
        }
    }
    return bytes;
}
ComponentDefinition componentVariantDefinition(const ComponentDefinition &definition,
                                               const VariantPtr &variant) {
    auto view = definition;
    if (variant) {
        view.members = variant->members;
        view.references = variant->references;
    }
    return view;
}

namespace {
template <class TemplateSize>
ComponentSize variantSize(const ComponentDefinition &definition, const VariantPtr &variant,
                          const TemplateSize &templateSize) {
    if (!variant)
        return templateSize(definition.id);
    ComponentSize total;
    for (const auto &[member, body] : variant->members) {
        const auto reference = variant->references.find(member);
        if (reference == variant->references.end()) {
            addSize(total, bodySize(*body));
            continue;
        }
        if (const auto nested = variant->nested.find(member); nested != variant->nested.end())
            addSize(total, variantSize(*nested->second->source, nested->second, templateSize));
        else
            addSize(total, templateSize(reference->second));
    }
    total.placements += 1;
    return total;
}
} // namespace

// ---- Engine ----
struct ComponentVariantEngine::State {
    State(const ComponentDefinitions &definitions, const ComponentVariantIndex *previous,
          bool extending)
        : definitions(definitions), previous(previous), extending(extending) {}
    const ComponentDefinitions &definitions;
    const ComponentVariantIndex *previous;
    // Extension mode keeps every entry of `previous`; new ones add to it.
    bool extending{};
    std::map<Id, CompiledParametersPtr> compiled;
    std::map<std::pair<Id, std::string>, EvaluationPtr> evaluations;
    std::map<std::pair<Id, ComponentDigest>, VariantPtr> variants;
    std::map<Id, bool> parametric;
    std::map<Id, std::map<Id, DefinitionPtr>> closures;
    std::map<Id, ComponentSize> templateSizes;
    std::size_t visits{};
    std::size_t variantCount() const {
        if (!extending)
            return variants.size();
        std::size_t count = previous->variants.size();
        for (const auto &[key, variant] : variants)
            count += previous->variants.contains(key) ? 0 : 1;
        return count;
    }
    const ComponentDefinition &definition(Id id) const {
        const auto found = definitions.find(id);
        if (found == definitions.end() || !found->second)
            fail("NOT_FOUND", "Component definition #" + std::to_string(id) + " does not exist",
                 {.definition = id});
        return *found->second;
    }
    bool isParametric(Id id, std::size_t depth = 0) {
        if (const auto found = parametric.find(id); found != parametric.end())
            return found->second;
        if (depth > 130)
            throw std::runtime_error("Cyclic component reference graph");
        const auto &record = definition(id);
        bool result = bool(record.parameters);
        for (const auto &[member, target] : record.references)
            result = isParametric(target, depth + 1) || result;
        return parametric[id] = result;
    }
    const std::map<Id, DefinitionPtr> &closure(Id id) {
        if (const auto found = closures.find(id); found != closures.end())
            return found->second;
        std::map<Id, DefinitionPtr> result{{id, definitions.at(id)}};
        for (const auto &[member, target] : definition(id).references)
            for (const auto &entry : closure(target))
                result.insert(entry);
        return closures[id] = std::move(result);
    }
    const ComponentSize &templateSize(Id id) {
        if (const auto found = templateSizes.find(id); found != templateSizes.end())
            return found->second;
        const auto &record = definition(id);
        ComponentSize total;
        for (const auto &[member, body] : record.members)
            if (const auto reference = record.references.find(member);
                reference != record.references.end())
                addSize(total, templateSize(reference->second));
            else
                addSize(total, bodySize(*body));
        total.placements += 1;
        return templateSizes[id] = total;
    }
    CompiledParametersPtr compile(Id id) {
        if (const auto found = compiled.find(id); found != compiled.end())
            return found->second;
        return compiled[id] = compileComponentParameters(definitions.at(id), definitions);
    }
    void addVisits(std::size_t added, Id definitionId) {
        if (added > ComponentParameterLimits::documentVisits - visits)
            fail("LIMIT_EXCEEDED",
                 "Component parameters exceed the document evaluation budget of 20,000,000 node "
                 "visits (definition #" +
                     std::to_string(definitionId) + ")",
                 {.definition = definitionId});
        visits += added;
    }
    const ComponentEvaluation &evaluate(Id id, const ComponentOverrides &inputs) {
        std::pair<Id, std::string> key{id, inputsKey(inputs)};
        if (const auto found = evaluations.find(key); found != evaluations.end())
            return found->second->evaluation;
        const auto &source = definitions.at(id);
        if (!source || !source->parameters)
            fail("PARAMETER_NAME",
                 "Definition #" + std::to_string(id) + " has no parameters to evaluate",
                 {.definition = id});
        if (previous)
            if (const auto found = previous->evaluations.find(key);
                found != previous->evaluations.end() && found->second->source == source) {
                if (!(extending && previous->evaluations.contains(key)))
                    addVisits(found->second->evaluation.visits, id);
                return evaluations.emplace(key, found->second).first->second->evaluation;
            }
        const auto compiledRecord = compile(id);
        auto entry = std::make_shared<ComponentEvaluationEntry>();
        entry->source = source;
        entry->evaluation = evaluateComponentParameters(
            *compiledRecord, inputs, ComponentParameterLimits::documentVisits - visits);
        addVisits(entry->evaluation.visits, id);
        return evaluations.emplace(key, std::move(entry)).first->second->evaluation;
    }
    // A variant's identity, computed without building geometry: the driver effects,
    // the repeats, the nested plans and the key. Identity plans show the template.
    struct Plan {
        Id definition{};
        bool identity{true};
        ComponentDigest key{};
        std::map<Id, MemberEffects> effects;
        std::map<Id, std::pair<std::size_t, std::size_t>> repeats; // member -> (driver, count)
        std::map<Id, ComponentOverrides> nestedValues;
        std::map<Id, std::shared_ptr<const Plan>> nested; // non-identity nested plans
        const ComponentEvaluation *evaluation{};
    };
    using PlanPtr = std::shared_ptr<const Plan>;
    std::map<std::pair<Id, std::string>, PlanPtr> plans;
    // Distinct non-identity keys planned so far (ADR 0165 §3: checked before building).
    std::set<std::pair<Id, ComponentDigest>> planned;
    PlanPtr plan(Id id, const ComponentOverrides &inputs);
    void checkPlanned() const {
        std::size_t count = planned.size();
        if (extending)
            for (const auto &key : previous->variants)
                count += planned.contains(key.first) ? 0 : 1;
        if (count > ComponentParameterLimits::variants)
            fail("LIMIT_EXCEEDED", "Component variants would exceed 4,096 per document");
    }
    VariantPtr build(const PlanPtr &plan);
    VariantPtr variant(Id id, const ComponentOverrides &inputs) { return build(plan(id, inputs)); }
    VariantPtr regenerate(const Plan &plan, std::map<Id, VariantPtr> nested);
};
ComponentVariantEngine::State::PlanPtr
ComponentVariantEngine::State::plan(Id id, const ComponentOverrides &inputs) {
    std::pair<Id, std::string> planKey{id, inputsKey(inputs)};
    if (const auto found = plans.find(planKey); found != plans.end())
        return found->second;
    const auto &definition = this->definition(id);
    auto result = std::make_shared<Plan>();
    result->definition = id;
    if (!isParametric(id)) {
        if (!inputs.empty())
            fail("PARAMETER_NAME",
                 "Definition #" + std::to_string(id) + " has no parameters to override",
                 {.definition = id});
        return plans[planKey] = result;
    }
    const ComponentEvaluation *defaults = nullptr;
    if (definition.parameters) {
        defaults = &evaluate(id, {});
        result->evaluation = inputs.empty() ? defaults : &evaluate(id, inputs);
    } else if (!inputs.empty())
        fail("PARAMETER_NAME",
             "Definition #" + std::to_string(id) + " has no parameters to override",
             {.definition = id});
    auto &effects = result->effects;
    auto &repeats = result->repeats;
    auto &nestedValues = result->nestedValues;
    if (definition.parameters) {
        const auto &drivers = definition.parameters->drivers;
        for (std::size_t i = 0; i < drivers.size(); ++i) {
            const auto &driver = drivers[i];
            const auto &value = result->evaluation->drivers[i];
            const auto &fallback = defaults->drivers[i];
            auto &effect = effects[driver.member];
            if (!effect.driver)
                effect.driver = driver.id;
            const auto axis = static_cast<std::size_t>(driver.axis);
            switch (driver.kind) {
            case ComponentDriverKind::Stretch:
                if (const auto delta = quantity(value) - quantity(fallback); delta != 0)
                    effect.stretch[axis].emplace_back(driver.at, delta);
                break;
            case ComponentDriverKind::Rotation:
                if (const auto delta = quantity(value) - quantity(fallback); delta != 0)
                    effect.rotations.emplace_back(driver.axis, delta);
                break;
            case ComponentDriverKind::Position:
                if (const auto delta = quantity(value) - quantity(fallback); delta != 0) {
                    coordinate(effect.position, axis) += delta;
                    effect.positioned = true;
                }
                break;
            case ComponentDriverKind::Visibility:
                effect.visible = effect.visible.value_or(true) && std::get<bool>(value);
                effect.visibleByDefault =
                    effect.visibleByDefault.value_or(true) && std::get<bool>(fallback);
                break;
            case ComponentDriverKind::Repeat:
                repeats[driver.member] = {i, static_cast<std::size_t>(quantity(value))};
                break;
            case ComponentDriverKind::ChildParameter:
                nestedValues[driver.member][driver.parameter] = value;
                break;
            }
        }
    }
    std::erase_if(effects, [&](const auto &entry) {
        const auto &[member, effect] = entry;
        const auto &body = *definition.members.at(member);
        return !effect.geometry() && effect.rotations.empty() && !effect.positioned &&
               (!effect.visible ||
                drivenHidden(body.hidden, *effect.visible, *effect.visibleByDefault) ==
                    body.hidden);
    });
    std::erase_if(repeats, [](const auto &entry) { return entry.second.second <= 1; });
    for (const auto &[member, target] : definition.references) {
        if (!isParametric(target))
            continue;
        const auto found = nestedValues.find(member);
        static const ComponentOverrides none;
        if (auto child = plan(target, found == nestedValues.end() ? none : found->second);
            !child->identity)
            result->nested[member] = std::move(child);
    }
    result->identity = effects.empty() && repeats.empty() && result->nested.empty();
    if (!result->identity) {
        // The variant key: the definition, every driver value and the nested keys.
        std::string bytes;
        for (int i = 0; i < 8; ++i)
            bytes.push_back(static_cast<char>((id >> (8 * i)) & 0xFF));
        if (const auto *evaluation = result->evaluation)
            for (std::size_t i = 0; i < evaluation->drivers.size(); ++i) {
                appendCanonicalValue(bytes, evaluation->drivers[i]);
                appendCanonicalValue(bytes, ParameterQuantity{evaluation->spacing[i], {1, 0}});
            }
        for (const auto &[member, target] : definition.references) {
            for (int i = 0; i < 8; ++i)
                bytes.push_back(static_cast<char>((member >> (8 * i)) & 0xFF));
            const auto found = result->nested.find(member);
            const auto key = found == result->nested.end() ? ComponentDigest{} : found->second->key;
            bytes.append(reinterpret_cast<const char *>(key.data()), key.size());
        }
        result->key = sha256Digest(bytes);
        planned.emplace(id, result->key);
    }
    return plans[planKey] = result;
}
VariantPtr ComponentVariantEngine::State::build(const PlanPtr &plan) {
    if (plan->identity)
        return nullptr;
    const std::pair<Id, ComponentDigest> variantKey{plan->definition, plan->key};
    if (const auto found = variants.find(variantKey); found != variants.end())
        return found->second;
    if (previous)
        if (const auto found = previous->variants.find(variantKey);
            found != previous->variants.end() &&
            found->second->dependencies == closure(plan->definition)) {
            // The reused variant's nested variants stay in use too: register them, so
            // the index lists every distinct variant as a fresh computation does.
            for (const auto &[member, child] : plan->nested)
                (void)build(child);
            return variants.emplace(variantKey, found->second).first->second;
        }
    std::map<Id, VariantPtr> nested;
    for (const auto &[member, child] : plan->nested)
        nested[member] = build(child);
    // Checked before the variant is built.
    if (variantCount() >= ComponentParameterLimits::variants)
        fail("LIMIT_EXCEEDED",
             "Component variants exceed 4,096 per document (definition #" +
                 std::to_string(plan->definition) + ")",
             {.definition = plan->definition});
    auto built = regenerate(*plan, std::move(nested));
    return variants.emplace(variantKey, std::move(built)).first->second;
}
VariantPtr ComponentVariantEngine::State::regenerate(const Plan &plan,
                                                     std::map<Id, VariantPtr> nested) {
    const auto id = plan.definition;
    const auto &definition = this->definition(id);
    const auto &effects = plan.effects;
    const auto &repeats = plan.repeats;
    const auto *evaluation = plan.evaluation;
    auto nestedValues = plan.nestedValues;
    const auto &key = plan.key;
    auto result = std::make_shared<ComponentVariant>();
    result->definition = id;
    result->source = definitions.at(id);
    result->members = definition.members;
    result->references = definition.references;
    result->dependencies = closure(id);
    result->key = key;
    for (const auto &[member, effect] : effects) {
        const auto &source = *definition.members.at(member);
        auto body = std::make_shared<Body>(driveMember(source, effect));
        if (effect.geometry() || effect.rotations.size() || effect.positioned)
            checkMember(source, *body, id, member, effect.driver);
        result->members[member] = std::move(body);
    }
    if (definition.glue && effects.contains(definition.glue->member))
        checkGlue(definition, result->members, effects.at(definition.glue->member).driver);
    // The materialized size, checked before any copy is built.
    auto memberSize = [&](Id member) -> ComponentSize {
        if (const auto reference = definition.references.find(member);
            reference != definition.references.end()) {
            const auto found = nested.find(member);
            return found != nested.end()
                       ? variantSize(this->definition(reference->second), found->second,
                                     [&](Id child) { return templateSize(child); })
                       : templateSize(reference->second);
        }
        return bodySize(*definition.members.at(member));
    };
    if (!repeats.empty()) {
        ComponentSize total;
        for (const auto &[member, body] : definition.members)
            addSize(total, memberSize(member));
        for (const auto &[member, repeat] : repeats)
            for (const auto copy : subtreeOf(definition, member))
                addSize(total, memberSize(copy), repeat.second - 1);
        total.placements += 1;
        checkLimits(limitCounts(total), LimitClass::Placement, "Component repeat",
                    definitionLimitContext(definition.name, id));
    }
    for (const auto &[member, repeat] : repeats) {
        const auto &driver = definition.parameters->drivers[repeat.first];
        const auto members = subtreeOf(definition, member);
        const auto spacing = evaluation->spacing[repeat.first];
        for (std::size_t k = 1; k < repeat.second; ++k) {
            const Id base = driver.firstMember + Id(k - 1) * members.size();
            std::map<Id, Id> ids;
            for (std::size_t i = 0; i < members.size(); ++i)
                ids[members[i]] = base + i;
            for (const auto original : members) {
                auto copy = std::make_shared<Body>(*result->members.at(original));
                copy->id = ids.at(original);
                if (original == member) {
                    copy->transform = Transform::translation(componentAxisVector(driver.axis) *
                                                             (double(k) * spacing)) *
                                      copy->transform;
                    try {
                        copy->transform.validate();
                    } catch (const std::exception &) {
                        fail("PARAMETER_REGENERATION",
                             "Driver #" + std::to_string(driver.id) +
                                 " places a repeat copy at an invalid transform",
                             {.definition = id, .driver = driver.id});
                    }
                } else
                    copy->parent = ids.at(copy->parent);
                result->generated[copy->id] = original;
                if (const auto reference = definition.references.find(original);
                    reference != definition.references.end()) {
                    result->references[copy->id] = reference->second;
                    if (const auto found = nested.find(original); found != nested.end())
                        nested[copy->id] = found->second;
                    if (const auto found = nestedValues.find(original); found != nestedValues.end())
                        nestedValues[copy->id] = found->second;
                }
                result->members[copy->id] = std::move(copy);
            }
        }
    }
    result->nested = std::move(nested);
    std::erase_if(nestedValues, [&](const auto &entry) {
        return !result->references.contains(entry.first);
    });
    result->nestedValues = std::move(nestedValues);
    return result;
}

ComponentVariantEngine::ComponentVariantEngine(const ComponentDefinitions &definitions,
                                               const ComponentVariantIndex *previous,
                                               bool retainPrevious)
    : state_(std::make_unique<State>(definitions, previous, retainPrevious && previous)) {}
ComponentVariantEngine::~ComponentVariantEngine() = default;
bool ComponentVariantEngine::parametric(Id definition) { return state_->isParametric(definition); }
VariantPtr ComponentVariantEngine::variant(Id definition, const ComponentOverrides &inputs) {
    return state_->variant(definition, inputs);
}
void ComponentVariantEngine::plan(Id definition, const ComponentOverrides &inputs) {
    (void)state_->plan(definition, inputs);
}
void ComponentVariantEngine::checkPlanned() const { state_->checkPlanned(); }
const ComponentEvaluation &ComponentVariantEngine::evaluate(Id definition,
                                                            const ComponentOverrides &inputs) {
    return state_->evaluate(definition, inputs);
}
CompiledParametersPtr ComponentVariantEngine::compiled(Id definition) {
    return state_->compile(definition);
}
void ComponentVariantEngine::collect(ComponentVariantIndex &index) {
    for (const auto &entry : state_->evaluations)
        index.evaluations.insert(entry);
    for (const auto &entry : state_->variants)
        index.variants.insert(entry);
    index.visits = 0;
    for (const auto &[key, entry] : index.evaluations)
        index.visits += entry->evaluation.visits;
}
ComponentSize componentVariantSize(const ComponentDefinition &definition, const VariantPtr &variant,
                                   const std::map<Id, ComponentSize> &templates) {
    return variantSize(definition, variant, [&](Id id) -> ComponentSize {
        const auto found = templates.find(id);
        if (found == templates.end())
            throw std::runtime_error("Missing component expansion size");
        return found->second;
    });
}

// ---- Index ----
namespace {
// Every placement root bound as a member of another placement.
std::set<Id> nestedRoots(const ComponentInstances &instances) {
    std::set<Id> result;
    for (const auto &[root, instance] : instances)
        for (const auto &[member, target] : instance->members)
            if (target != root && instances.contains(target))
                result.insert(target);
    return result;
}
template <class Lookup>
void walk(ComponentVariantIndex &index, const Lookup &instances, Id root, const VariantPtr &variant) {
    auto &shown = index.placements[root];
    if (shown != variant) {
        if (shown) {
            const auto use = index.placementUses.find(shown);
            if (--use->second == 0)
                index.placementUses.erase(use);
        }
        ++index.placementUses[variant];
        shown = variant;
    }
    const auto binding = instances(root);
    if (!binding)
        return;
    for (const auto &[member, nested] : variant->nested) {
        const auto target = binding->members.find(member);
        if (target != binding->members.end() && instances(target->second))
            walk(index, instances, target->second, nested);
    }
}
// Visibility at the defaults (ADR 0165 §1): a member whose visibility drivers
// evaluate false at the defaults must be hidden in the template, so the variant at
// the defaults equals the template.
void checkDefaults(const ComponentDefinition &definition, const ComponentEvaluation &defaults) {
    const auto &drivers = definition.parameters->drivers;
    std::map<Id, std::pair<bool, Id>> visibility;
    for (std::size_t i = 0; i < drivers.size(); ++i)
        if (drivers[i].kind == ComponentDriverKind::Visibility) {
            auto &[visible, driver] = visibility.try_emplace(drivers[i].member, true, drivers[i].id)
                                          .first->second;
            visible = visible && std::get<bool>(defaults.drivers[i]);
        }
    for (const auto &[member, entry] : visibility)
        if (!entry.first && !definition.members.at(member)->hidden)
            fail("PARAMETER_REGENERATION",
                 "Driver #" + std::to_string(entry.second) + " hides member #" +
                     std::to_string(member) + " at the defaults, but the template shows it "
                                              "(definition #" +
                     std::to_string(definition.id) + ")",
                 {.definition = definition.id, .driver = entry.second});
}
// Plan every top-level placement in root order (evaluation errors name the first
// failing placement), then check the distinct-variant limit before building any.
void planAll(ComponentVariantEngine &engine, const ComponentInstances &instances,
             const std::set<Id> &nested) {
    for (const auto &[root, instance] : instances) {
        if (nested.contains(root) ||
            (instance->overrides.empty() && !engine.parametric(instance->definition)))
            continue;
        try {
            engine.plan(instance->definition, instance->overrides);
        } catch (const ComponentParameterError &error) {
            throw error.withPlacement(root);
        }
    }
    engine.checkPlanned();
}
} // namespace
ComponentVariantsPtr computeComponentVariants(const ComponentDefinitions &definitions,
                                              const ComponentInstances &instances,
                                              const ComponentVariantIndex *previous) {
    const bool parametric = std::any_of(definitions.begin(), definitions.end(), [](const auto &entry) {
        return entry.second && entry.second->parameters;
    });
    if (!parametric) {
        // The common case stays a scan of the bindings: no parameter record, no override.
        for (const auto &[root, instance] : instances)
            if (instance && !instance->overrides.empty())
                fail("PARAMETER_NAME",
                     "Placement #" + std::to_string(root) + " overrides parameters of definition #" +
                         std::to_string(instance->definition) + ", which has none",
                     {.definition = instance->definition, .placement = root});
        return emptyComponentVariants();
    }
    const auto nested = nestedRoots(instances);
    for (const auto &[root, instance] : instances)
        if (!instance->overrides.empty() && nested.contains(root))
            fail("INVALID_TARGET",
                 "Nested placement #" + std::to_string(root) + " cannot hold parameter overrides",
                 {.definition = instance->definition, .placement = root});
    ComponentVariantEngine engine(definitions, previous);
    for (const auto &[id, definition] : definitions)
        if (definition->parameters)
            checkDefaults(*definition, engine.evaluate(id, {}));
    auto index = std::make_shared<ComponentVariantIndex>();
    auto lookup = [&](Id root) -> InstancePtr {
        const auto found = instances.find(root);
        return found == instances.end() ? nullptr : found->second;
    };
    planAll(engine, instances, nested);
    for (const auto &[root, instance] : instances) {
        if (nested.contains(root) ||
            (instance->overrides.empty() && !engine.parametric(instance->definition)))
            continue;
        if (const auto variant = engine.variant(instance->definition, instance->overrides))
            walk(*index, lookup, root, variant);
    }
    engine.collect(*index);
    return index;
}
ComponentVariantsPtr extendComponentVariants(const ComponentDefinitions &definitions,
                                             const ComponentInstances &existing,
                                             const ComponentInstances &added,
                                             const ComponentVariantIndex &current) {
    const auto nested = nestedRoots(added);
    const bool parametric = std::any_of(definitions.begin(), definitions.end(), [](const auto &entry) {
        return entry.second && entry.second->parameters;
    });
    for (const auto &[root, instance] : added)
        if (!instance->overrides.empty()) {
            if (nested.contains(root))
                fail("INVALID_TARGET",
                     "Nested placement #" + std::to_string(root) +
                         " cannot hold parameter overrides",
                     {.definition = instance->definition, .placement = root});
            if (!parametric)
                fail("PARAMETER_NAME",
                     "Placement #" + std::to_string(root) + " overrides parameters of definition #" +
                         std::to_string(instance->definition) + ", which has none",
                     {.definition = instance->definition, .placement = root});
        }
    if (!parametric || added.empty())
        return nullptr;
    ComponentVariantEngine engine(definitions, &current, true);
    auto index = std::make_shared<ComponentVariantIndex>(current);
    auto lookup = [&](Id root) -> InstancePtr {
        if (const auto found = added.find(root); found != added.end())
            return found->second;
        const auto found = existing.find(root);
        return found == existing.end() ? nullptr : found->second;
    };
    planAll(engine, added, nested);
    for (const auto &[root, instance] : added) {
        if (nested.contains(root) ||
            (instance->overrides.empty() && !engine.parametric(instance->definition)))
            continue;
        if (const auto variant = engine.variant(instance->definition, instance->overrides))
            walk(*index, lookup, root, variant);
    }
    engine.collect(*index);
    return index;
}

// ---- Re-baseline ----
ComponentRebase rebaseComponentTemplate(const DefinitionPtr &original, const DefinitionPtr &staged,
                                        const ComponentDefinitions &before,
                                        const ComponentDefinitions &after) {
    ComponentRebase result;
    result.members = original->members;
    const auto &next = staged->parameters;
    if (!next)
        return result;
    result.record = *next;
    auto candidates = after;
    candidates[original->id] = staged;
    // The new record must hold against the template before anything is evaluated.
    const auto compiledNext = compileComponentParameters(staged, candidates);
    const auto oldDefaults =
        original->parameters
            ? evaluateComponentParameters(*compileComponentParameters(original, before), {})
            : ComponentEvaluation{};
    const auto newDefaults = evaluateComponentParameters(*compiledNext, {});
    std::map<Id, std::size_t> previous;
    if (original->parameters)
        for (std::size_t i = 0; i < original->parameters->drivers.size(); ++i)
            previous[original->parameters->drivers[i].id] = i;
    std::map<Id, MemberEffects> effects;
    std::vector<double> deltas(next->drivers.size());
    for (std::size_t i = 0; i < next->drivers.size(); ++i) {
        const auto &driver = next->drivers[i];
        const auto found = previous.find(driver.id);
        const auto *old =
            found == previous.end() ? nullptr : &original->parameters->drivers[found->second];
        // A driver that is new or changes kind, target, axis or plane starts from the
        // template. A visibility driver then starts from "shown": one whose default is
        // false hides its member in the template, so the defaults show the template.
        if (!old || old->kind != driver.kind || old->member != driver.member ||
            old->axis != driver.axis || old->at != driver.at) {
            if (driver.kind == ComponentDriverKind::Visibility) {
                auto &effect = effects[driver.member];
                if (!effect.driver)
                    effect.driver = driver.id;
                effect.visible =
                    effect.visible.value_or(true) && std::get<bool>(newDefaults.drivers[i]);
                effect.visibleByDefault = effect.visibleByDefault.value_or(true);
            }
            continue;
        }
        const auto &value = newDefaults.drivers[i];
        const auto &fallback = oldDefaults.drivers[found->second];
        auto &effect = effects[driver.member];
        if (!effect.driver)
            effect.driver = driver.id;
        const auto axis = static_cast<std::size_t>(driver.axis);
        switch (driver.kind) {
        case ComponentDriverKind::Stretch:
            deltas[i] = quantity(value) - quantity(fallback);
            if (deltas[i] != 0)
                effect.stretch[axis].emplace_back(driver.at, deltas[i]);
            break;
        case ComponentDriverKind::Rotation:
            if (const auto delta = quantity(value) - quantity(fallback); delta != 0)
                effect.rotations.emplace_back(driver.axis, delta);
            break;
        case ComponentDriverKind::Position:
            if (const auto delta = quantity(value) - quantity(fallback); delta != 0) {
                coordinate(effect.position, axis) += delta;
                effect.positioned = true;
            }
            break;
        case ComponentDriverKind::Visibility:
            effect.visible = effect.visible.value_or(true) && std::get<bool>(value);
            effect.visibleByDefault =
                effect.visibleByDefault.value_or(true) && std::get<bool>(fallback);
            break;
        default:
            break; // repeat copies are never added to the template; nested values are not geometry
        }
    }
    for (const auto &[member, effect] : effects) {
        const auto &source = *original->members.at(member);
        auto body = std::make_shared<Body>(driveMember(source, effect));
        if (*body == source)
            continue;
        checkMember(source, *body, original->id, member, effect.driver);
        result.members[member] = std::move(body);
        result.moved.insert(member);
    }
    if (original->glue && result.moved.contains(original->glue->member))
        checkGlue(*original, result.members, effects.at(original->glue->member).driver);
    // Move each stretch plane by the stretches of the same member and axis below it,
    // and keep every point on the side of every plane it was on.
    for (std::size_t i = 0; i < next->drivers.size(); ++i) {
        auto &driver = result.record.drivers[i];
        if (driver.kind != ComponentDriverKind::Stretch)
            continue;
        double shift = 0;
        for (std::size_t j = 0; j < next->drivers.size(); ++j) {
            const auto &other = next->drivers[j];
            if (j != i && other.kind == ComponentDriverKind::Stretch &&
                other.member == driver.member && other.axis == driver.axis &&
                other.at < next->drivers[i].at)
                shift += deltas[j];
        }
        driver.at = next->drivers[i].at + shift;
    }
    for (const auto member : result.moved) {
        const auto &templateBefore = *original->members.at(member);
        const auto &templateAfter = *result.members.at(member);
        for (std::size_t i = 0; i < next->drivers.size(); ++i) {
            const auto &driver = next->drivers[i];
            if (driver.kind != ComponentDriverKind::Stretch || driver.member != member)
                continue;
            const auto axis = static_cast<std::size_t>(driver.axis);
            const auto oldAt = driver.at, newAt = result.record.drivers[i].at;
            auto keepSide = [&](Vec3 a, Vec3 b) {
                if ((coordinate(a, axis) > oldAt) != (coordinate(b, axis) > newAt))
                    fail("PARAMETER_REGENERATION",
                         "Changing the defaults would move geometry of member #" +
                             std::to_string(member) + " across the plane of driver #" +
                             std::to_string(driver.id),
                         {.definition = original->id, .driver = driver.id});
            };
            for (const auto &[id, point] : templateBefore.surface.vertices)
                keepSide(point, templateAfter.surface.vertices.at(id));
            for (const auto &[id, guide] : templateBefore.guides)
                keepSide(guide.origin, templateAfter.guides.at(id).origin);
        }
    }
    return result;
}
} // namespace sketchy
