#include "core/component_parameters.hpp"
#include "core/component_publish.hpp"
#include "core/component_regeneration.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
namespace sketchy {
namespace {
using Context = ComponentParameterErrorContext;
[[noreturn]] void fail(const char *code, const std::string &message, Context context = {}) {
    throw ComponentParameterError(code, message, std::move(context));
}
std::string definitionText(Id id) { return "definition #" + std::to_string(id); }
std::string driverText(Id id) { return "driver #" + std::to_string(id); }
Context parameterContext(Id definition, const std::string &name) {
    Context context;
    context.definition = definition;
    context.parameter = name;
    return context;
}
Context driverContext(Id definition, Id driver) {
    Context context;
    context.definition = definition;
    context.driver = driver;
    return context;
}
struct Symbols final : ParameterSymbols {
    const std::map<std::string, std::size_t, std::less<>> &index;
    const std::vector<ParameterSignature> &signatures;
    Symbols(const std::map<std::string, std::size_t, std::less<>> &index,
            const std::vector<ParameterSignature> &signatures)
        : index(index), signatures(signatures) {}
    const ParameterSignature *find(std::string_view name) const override {
        const auto found = index.find(name);
        return found == index.end() ? nullptr : &signatures[found->second];
    }
};
// 1–128 bytes of valid UTF-8, no control characters, no leading or trailing space.
bool validLabel(std::string_view label) {
    if (label.empty() || label.size() > ComponentParameterLimits::labelBytes ||
        label.front() == ' ' || label.back() == ' ')
        return false;
    for (std::size_t i = 0; i < label.size();) {
        const auto c = static_cast<unsigned char>(label[i]);
        std::size_t length = 0;
        std::uint32_t code = 0;
        if (c < 0x80) {
            length = 1;
            code = c;
        } else if ((c & 0xE0) == 0xC0) {
            length = 2;
            code = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            length = 3;
            code = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            length = 4;
            code = c & 0x07;
        } else
            return false;
        if (i + length > label.size())
            return false;
        for (std::size_t j = 1; j < length; ++j) {
            const auto next = static_cast<unsigned char>(label[i + j]);
            if ((next & 0xC0) != 0x80)
                return false;
            code = code << 6 | (next & 0x3F);
        }
        const std::uint32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
        if (code < minimum[length] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF) ||
            code < 0x20 || code == 0x7F || (code >= 0x80 && code <= 0x9F))
            return false;
        i += length;
    }
    return true;
}
bool quantityType(ParameterType type) {
    return type == ParameterType::Length || type == ParameterType::Area ||
           type == ParameterType::Angle || type == ParameterType::Number ||
           type == ParameterType::Count;
}
double typeRange(ParameterType type) {
    switch (type) {
    case ParameterType::Length:
        return 1e6;
    case ParameterType::Area:
        return 1e12;
    case ParameterType::Angle:
        return 1e4;
    case ParameterType::Number:
        return 1e15;
    case ParameterType::Count:
        return 10000;
    default:
        return 0;
    }
}
double quantum(ParameterType type) {
    switch (type) {
    case ParameterType::Length:
        return parameterLengthQuantum;
    case ParameterType::Area:
        return parameterAreaQuantum;
    case ParameterType::Angle:
        return parameterAngleQuantum;
    case ParameterType::Number:
        return 1e-12;
    default:
        return 0;
    }
}
double quantizeBound(ParameterType type, double value) {
    switch (type) {
    case ParameterType::Length:
        return quantizeParameterLength(value);
    case ParameterType::Area:
        return quantizeParameterArea(value);
    case ParameterType::Angle:
        return quantizeParameterAngle(value);
    default:
        return value;
    }
}
ParameterSignature signatureOf(const ComponentParameter &parameter) {
    ParameterSignature signature{parameter.type, {}};
    for (const auto &option : parameter.options)
        signature.options.push_back(option.value);
    return signature;
}
CheckedParameterExpression checkText(const std::string &text, const ParameterSymbols &symbols,
                                     const ParameterSignature &expected, const Context &context) {
    try {
        const auto parsed = parseParameterExpression(text);
        if (canonicalParameterExpression(parsed) != text) {
            auto failure = context;
            failure.offset = 1;
            failure.excerpt = text.substr(0, 64);
            fail("PARAMETER_SYNTAX",
                 "Stored expression " + quotedParameterText(text) + " is not canonical text",
                 failure);
        }
        return checkParameterExpression(parsed, symbols, expected);
    } catch (const ParameterExpressionError &error) {
        throwParameterExpressionError(error, context);
    }
}
std::vector<Id> subtree(const ComponentDefinition &definition, Id member) {
    std::map<Id, std::vector<Id>> children;
    for (const auto &[id, body] : definition.members)
        if (id != definition.root)
            children[body->parent].push_back(id);
    std::vector<Id> result;
    std::vector<Id> stack{member};
    while (!stack.empty()) {
        const auto id = stack.back();
        stack.pop_back();
        result.push_back(id);
        if (result.size() > definition.members.size())
            throw std::runtime_error("Cyclic component member hierarchy");
        if (const auto found = children.find(id); found != children.end())
            stack.insert(stack.end(), found->second.begin(), found->second.end());
    }
    std::sort(result.begin(), result.end());
    return result;
}
Transform inverseTranspose(const Transform &transform) {
    const auto inverse = transform.inverse();
    Transform result;
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            result.m[column * 4 + row] = inverse.m[row * 4 + column];
    return result;
}
void checkBoundsField(ParameterType type, const std::optional<double> &value, const char *field,
                      const Context &context) {
    if (!value)
        return;
    if (!quantityType(type))
        fail("PARAMETER_TYPE",
             std::string("A ") + std::string(parameterTypeName(type)) + " parameter has no " +
                 field,
             context);
    if (!std::isfinite(*value) || std::abs(*value) > typeRange(type))
        fail("PARAMETER_DOMAIN",
             std::string("Parameter ") + field + " is outside the " +
                 std::string(parameterTypeName(type)) + " range",
             context);
    if (type == ParameterType::Count && (*value < 0 || *value != std::floor(*value)))
        fail("PARAMETER_DOMAIN", std::string("A count ") + field + " must be a whole number",
             context);
}
} // namespace

// ---- Bounds and literals ----
void checkComponentParameterBounds(const ComponentParameter &parameter, const ParameterValue &value,
                                   ComponentParameterErrorContext context) {
    const auto *quantity = std::get_if<ParameterQuantity>(&value);
    if (!quantity)
        return;
    const auto type = parameter.type;
    const auto v = quantity->value;
    auto outside = [&](const std::string &detail) {
        fail("PARAMETER_DOMAIN",
             "Value of parameter " + quotedParameterText(parameter.name) + " " + detail, context);
    };
    // Compare quantized values with quantized bounds: final values are quantized, and
    // a bound such as 2.4 m must accept the value 2.4 m (R093.a: quantization can
    // move a value by one ulp).
    // The error names the violated bound, its value and the refused value (R094.b).
    auto violated = [&](const char *bound, double limit, const std::string &detail) {
        context.bound = bound;
        context.limit = limit;
        context.value = v;
        outside(detail);
    };
    if (parameter.min && v < quantizeBound(type, *parameter.min))
        violated("min", *parameter.min, "is below its minimum");
    if (parameter.max && v > quantizeBound(type, *parameter.max))
        violated("max", *parameter.max, "is above its maximum");
    if (parameter.step) {
        const auto base = parameter.min.value_or(0);
        if (type == ParameterType::Count) {
            const auto k = (v - base) / *parameter.step;
            if (k != std::floor(k))
                violated("step", *parameter.step, "is not on its step");
        } else {
            const auto k = std::nearbyint((v - base) / *parameter.step);
            if (!(std::abs(v - (base + k * *parameter.step)) <= quantum(type)))
                violated("step", *parameter.step, "is not on its step");
        }
    }
}
namespace {
ParameterValue canonicalValue(const ComponentParameter &parameter, const ParameterValue &value,
                              const Context &context) {
    auto typeError = [&] {
        fail("PARAMETER_TYPE",
             "Value of parameter " + quotedParameterText(parameter.name) + " must be a " +
                 std::string(parameterTypeName(parameter.type)),
             context);
    };
    ParameterValue result = value;
    if (parameter.type == ParameterType::Boolean) {
        if (!std::holds_alternative<bool>(value))
            typeError();
    } else if (parameter.type == ParameterType::Choice) {
        const auto *choice = std::get_if<ParameterChoice>(&value);
        if (!choice || std::none_of(parameter.options.begin(), parameter.options.end(),
                                    [&](const auto &option) { return option.value == choice->value; }))
            fail("PARAMETER_TYPE",
                 "Value of parameter " + quotedParameterText(parameter.name) +
                     " is not one of its options",
                 context);
    } else {
        const auto *quantity = std::get_if<ParameterQuantity>(&value);
        if (!quantity || !(quantity->dimension == parameterDimension(parameter.type)))
            typeError();
        try {
            result = finalizeParameterValue(value, signatureOf(parameter), "", 1);
        } catch (const ParameterExpressionError &error) {
            fail(std::string(parameterErrorCodeName(error.code())).c_str(),
                 "Value of parameter " + quotedParameterText(parameter.name) + ": " + error.what(),
                 context);
        }
    }
    checkComponentParameterBounds(parameter, result, context);
    return result;
}
} // namespace
std::string componentParameterLiteral(const ParameterValue &value, ParameterType type) {
    if (const auto *flag = std::get_if<bool>(&value))
        return *flag ? "true" : "false";
    if (const auto *choice = std::get_if<ParameterChoice>(&value))
        return "\"" + choice->value + "\"";
    const auto number = std::get<ParameterQuantity>(value).value;
    char buffer[64];
    auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), number);
    if (error != std::errc())
        throw std::runtime_error("Unprintable parameter value");
    // A quantized value prints as the shortest decimal that quantizes back to it, so
    // the stored 1.2000000000000002 m reads "1.2 m".
    const bool quantized = type == ParameterType::Length || type == ParameterType::Area ||
                           type == ParameterType::Angle;
    for (int digits = 1; quantized && digits < 17; ++digits) {
        char shorter[64];
        const auto printed = std::to_chars(shorter, shorter + sizeof(shorter), number,
                                           std::chars_format::general, digits);
        double parsed{};
        if (printed.ec != std::errc() ||
            std::from_chars(shorter, printed.ptr, parsed).ec != std::errc() ||
            quantizeBound(type, parsed) != number)
            continue;
        std::copy(shorter, printed.ptr, buffer);
        end = buffer + (printed.ptr - shorter);
        break;
    }
    std::string text(buffer, end);
    if (text.starts_with("-")) // unary minus applies to the quantity literal
        text = "-" + text.substr(1);
    switch (type) {
    case ParameterType::Length:
        text += " m";
        break;
    case ParameterType::Angle:
        text += " rad";
        break;
    case ParameterType::Area:
        text += " * 1 m ^ 2";
        break;
    default:
        break;
    }
    return canonicalParameterExpression(parseParameterExpression(text));
}
ComponentOverrides canonicalComponentOverrides(const CompiledComponentParameters &compiled,
                                               const ComponentOverrides &values) {
    const auto &record = *compiled.definition->parameters;
    ComponentOverrides result;
    for (const auto &[name, value] : values) {
        const auto context = parameterContext(compiled.definition->id, name);
        const auto found = compiled.index.find(name);
        if (found == compiled.index.end())
            fail("PARAMETER_NAME",
                 "Definition #" + std::to_string(compiled.definition->id) +
                     " has no parameter " + quotedParameterText(name),
                 context);
        const auto &parameter = record.parameters[found->second];
        if (parameter.mode != ComponentParameterMode::Input)
            fail("PARAMETER_NAME",
                 "Derived parameter " + quotedParameterText(name) + " cannot be overridden",
                 context);
        result[name] = canonicalValue(parameter, value, context);
    }
    return result;
}

// ---- Compile ----
CompiledParametersPtr compileComponentParameters(const DefinitionPtr &definitionPtr,
                                                 const ComponentDefinitions &definitions) {
    if (!definitionPtr || !definitionPtr->parameters)
        throw std::runtime_error("Definition has no parameter record");
    const auto &definition = *definitionPtr;
    const auto &record = *definition.parameters;
    const auto id = definition.id;
    if (record.parameters.size() > ComponentParameterLimits::parameters)
        fail("LIMIT_EXCEEDED",
             "Component parameters exceed 128 per definition (" + definitionText(id) + ")",
             {.definition = id});
    if (record.drivers.size() > ComponentParameterLimits::drivers)
        fail("LIMIT_EXCEEDED",
             "Component drivers exceed 256 per definition (" + definitionText(id) + ")",
             {.definition = id});
    if (record.parameters.empty() && record.drivers.empty())
        fail("PARAMETER_NAME", "An empty parameter record is stored as no record",
             {.definition = id});
    auto compiled = std::make_shared<CompiledComponentParameters>();
    compiled->definition = definitionPtr;
    for (std::size_t i = 0; i < record.parameters.size(); ++i) {
        const auto &parameter = record.parameters[i];
        const auto context = parameterContext(id, parameter.name);
        if (!isValidParameterName(parameter.name))
            fail("PARAMETER_NAME",
                 "Parameter name " + quotedParameterText(parameter.name) +
                     " is malformed or reserved",
                 context);
        if (!compiled->index.emplace(parameter.name, i).second)
            fail("PARAMETER_NAME", "Duplicate parameter name " + quotedParameterText(parameter.name),
                 context);
        if (!validLabel(parameter.label))
            fail("PARAMETER_NAME",
                 "Parameter label must be 1-128 bytes of UTF-8 without control characters or "
                 "edge spaces",
                 context);
        if (parameter.type > ParameterType::Choice ||
            parameter.mode > ComponentParameterMode::Derived)
            fail("PARAMETER_TYPE", "Unknown parameter type or mode", context);
        checkBoundsField(parameter.type, parameter.min, "minimum", context);
        checkBoundsField(parameter.type, parameter.max, "maximum", context);
        checkBoundsField(parameter.type, parameter.step, "step", context);
        if (parameter.min && parameter.max && *parameter.min > *parameter.max)
            fail("PARAMETER_DOMAIN", "Parameter minimum is above its maximum", context);
        if (parameter.step && !(*parameter.step > 0))
            fail("PARAMETER_DOMAIN", "Parameter step must be positive", context);
        if (parameter.type == ParameterType::Choice) {
            if (parameter.options.size() > ComponentParameterLimits::options)
                fail("LIMIT_EXCEEDED",
                     "Choice parameter " + quotedParameterText(parameter.name) +
                         " exceeds 32 options",
                     context);
            if (parameter.options.empty())
                fail("PARAMETER_TYPE", "A choice parameter needs 1-32 options", context);
            std::set<std::string> values;
            for (const auto &option : parameter.options) {
                if (!isValidParameterChoiceValue(option.value) ||
                    !values.insert(option.value).second)
                    fail("PARAMETER_NAME",
                         "Choice option " + quotedParameterText(option.value) +
                             " is malformed or duplicated",
                         context);
                if (!validLabel(option.label))
                    fail("PARAMETER_NAME", "Choice option label is malformed", context);
            }
        } else if (!parameter.options.empty())
            fail("PARAMETER_TYPE", "Only choice parameters have options", context);
        compiled->signatures.push_back(signatureOf(parameter));
    }
    const Symbols symbols(compiled->index, compiled->signatures);
    std::vector<std::set<std::size_t>> dependencies(record.parameters.size());
    for (std::size_t i = 0; i < record.parameters.size(); ++i) {
        const auto &parameter = record.parameters[i];
        compiled->expressions.push_back(checkText(parameter.expression, symbols,
                                                  compiled->signatures[i],
                                                  parameterContext(id, parameter.name)));
        for (const auto &name : compiled->expressions.back().references)
            dependencies[i].insert(compiled->index.at(name));
    }
    // Kahn's algorithm with the ready set ordered by name bytes.
    std::vector<std::vector<std::size_t>> dependents(record.parameters.size());
    std::vector<std::size_t> pending(record.parameters.size());
    std::set<std::pair<std::string, std::size_t>> ready;
    for (std::size_t i = 0; i < record.parameters.size(); ++i) {
        pending[i] = dependencies[i].size();
        for (const auto dependency : dependencies[i])
            dependents[dependency].push_back(i);
        if (!pending[i])
            ready.emplace(record.parameters[i].name, i);
    }
    while (!ready.empty()) {
        const auto [name, next] = *ready.begin();
        ready.erase(ready.begin());
        compiled->order.push_back(next);
        for (const auto dependent : dependents[next])
            if (!--pending[dependent])
                ready.emplace(record.parameters[dependent].name, dependent);
    }
    if (compiled->order.size() != record.parameters.size()) {
        // Every remaining parameter waits on another remaining one, so following the
        // smallest-named remaining dependency always closes a cycle.
        std::set<std::size_t> remaining;
        for (std::size_t i = 0; i < pending.size(); ++i)
            if (pending[i])
                remaining.insert(i);
        auto smallest = [&](const auto &candidates) {
            std::size_t best = SIZE_MAX;
            for (const auto candidate : candidates)
                if (remaining.contains(candidate) &&
                    (best == SIZE_MAX ||
                     record.parameters[candidate].name < record.parameters[best].name))
                    best = candidate;
            return best;
        };
        std::vector<std::size_t> path;
        std::map<std::size_t, std::size_t> seen;
        for (auto node = smallest(remaining); !seen.contains(node);
             node = smallest(dependencies[node])) {
            seen[node] = path.size();
            path.push_back(node);
        }
        const auto revisit = smallest(dependencies[path.back()]);
        std::vector<std::size_t> cycle(path.begin() + std::ptrdiff_t(seen.at(revisit)), path.end());
        std::rotate(cycle.begin(),
                    std::min_element(cycle.begin(), cycle.end(),
                                     [&](auto a, auto b) {
                                         return record.parameters[a].name <
                                                record.parameters[b].name;
                                     }),
                    cycle.end());
        Context context{.definition = id};
        std::string chain;
        for (std::size_t i = 0; i < cycle.size() && i < 8; ++i) {
            context.cycle.push_back(record.parameters[cycle[i]].name);
            chain += (i ? " -> " : "") + record.parameters[cycle[i]].name;
        }
        chain += cycle.size() > 8 ? " -> ..." : " -> " + record.parameters[cycle.front()].name;
        context.parameter = context.cycle.front();
        fail("PARAMETER_CYCLE", "Parameter cycle in " + definitionText(id) + ": " + chain,
             context);
    }
    // Drivers.
    std::map<Id, std::vector<Id>> children;
    for (const auto &[member, body] : definition.members)
        if (member != definition.root)
            children[body->parent].push_back(member);
    struct Block {
        Id first{}, end{}, driver{};
    };
    std::vector<Block> blocks;
    std::set<Id> repeated;
    std::set<std::pair<Id, std::string>> childTargets;
    std::size_t reserved = 0;
    std::vector<Id> glueChain;
    Vec3 glueNormal{};
    if (definition.glue && definition.members.contains(definition.glue->member)) {
        const auto &glueBody = *definition.members.at(definition.glue->member);
        if (glueBody.surface.faces.contains(definition.glue->face))
            glueNormal = glueBody.surface.normal(definition.glue->face);
        for (auto member = definition.glue->member;
             member && member != definition.root && definition.members.contains(member) &&
             glueChain.size() <= 128;
             member = definition.members.at(member)->parent)
            glueChain.push_back(member);
    }
    Id previous = 0;
    for (const auto &driver : record.drivers) {
        const auto context = driverContext(id, driver.id);
        auto target = [&](const std::string &message) {
            fail("PARAMETER_DRIVER_TARGET", driverText(driver.id) + ": " + message, context);
        };
        if (!driver.id || driver.id == UINT64_MAX || driver.id <= previous)
            target("driver IDs must be nonzero and ascending");
        previous = driver.id;
        if (driver.kind > ComponentDriverKind::ChildParameter || driver.axis > ComponentAxis::Z)
            target("unknown driver kind or axis");
        if (!definition.members.contains(driver.member) &&
            std::any_of(record.drivers.begin(), record.drivers.end(), [&](const auto &repeat) {
                if (repeat.kind != ComponentDriverKind::Repeat || !repeat.maxCount ||
                    !definition.members.contains(repeat.member))
                    return false;
                const auto size = Id(repeat.maxCount - 1) * subtree(definition, repeat.member).size();
                return driver.member >= repeat.firstMember &&
                       driver.member - repeat.firstMember < size;
            }))
            target("generated repeat copies cannot be driven");
        if (!driver.member || !definition.members.contains(driver.member))
            target("its target member does not exist");
        if (driver.member == definition.root)
            target("the definition root cannot be driven");
        const auto &body = *definition.members.at(driver.member);
        const bool reference = definition.references.contains(driver.member);
        if (driver.kind != ComponentDriverKind::Stretch && driver.at != 0)
            target("only stretch drivers have a plane offset");
        if (driver.kind != ComponentDriverKind::Repeat &&
            (driver.maxCount || !driver.spacing.empty() || driver.firstMember))
            target("only repeat drivers have a count, spacing or reserved members");
        if (driver.kind != ComponentDriverKind::ChildParameter && !driver.parameter.empty())
            target("only childParameter drivers name a child parameter");
        if ((driver.kind == ComponentDriverKind::Visibility ||
             driver.kind == ComponentDriverKind::ChildParameter) &&
            driver.axis != ComponentAxis::X)
            target("this driver kind has no axis");
        ParameterSignature expected;
        std::optional<ComponentParameter> child;
        switch (driver.kind) {
        case ComponentDriverKind::Stretch: {
            if (body.kind != BodyKind::Geometry || reference || body.textSource ||
                body.referenceImage)
                target("stretch drives Geometry members only");
            if (!std::isfinite(driver.at) || std::abs(driver.at) > coordinateLimit)
                target("the stretch plane offset is outside the coordinate range");
            const auto axis = static_cast<std::size_t>(driver.axis);
            auto coordinate = [&](Vec3 point) {
                return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
            };
            for (const auto &[curveId, curve] : body.curves) {
                int above = 0, below = 0;
                for (const auto &edge : curve.edges) {
                    const auto found = body.topology.edges.find(edge.edge);
                    if (found == body.topology.edges.end())
                        continue;
                    for (const auto vertex : {found->second.a, found->second.b})
                        if (body.surface.vertices.contains(vertex))
                            (coordinate(body.surface.vertices.at(vertex)) > driver.at ? above
                                                                                     : below)++;
                }
                if (above && below)
                    target("curve #" + std::to_string(curveId) + " crosses the stretch plane");
            }
            expected = {ParameterType::Length, {}};
            break;
        }
        case ComponentDriverKind::Position:
        case ComponentDriverKind::Rotation: {
            const auto position = std::find(glueChain.begin(), glueChain.end(), driver.member);
            if (position != glueChain.end()) {
                if (driver.kind == ComponentDriverKind::Rotation)
                    target("a rotation would move the glue face off its plane");
                Transform chain;
                for (auto it = glueChain.begin(); it != std::next(position); ++it)
                    chain = definition.members.at(*it)->transform * chain;
                const auto normal = normalized(inverseTranspose(chain).vector(glueNormal));
                if (std::abs(dot(normal, componentAxisVector(driver.axis))) > 1e-9)
                    target("a position along the glue normal would move the glue face off its "
                           "plane");
            }
            expected = {driver.kind == ComponentDriverKind::Position ? ParameterType::Length
                                                                     : ParameterType::Angle,
                        {}};
            break;
        }
        case ComponentDriverKind::Visibility:
            expected = {ParameterType::Boolean, {}};
            break;
        case ComponentDriverKind::Repeat: {
            if (driver.maxCount > ComponentParameterLimits::repeatMaxCount)
                fail("LIMIT_EXCEEDED",
                     driverText(driver.id) + ": repeat maxCount exceeds 1,000 (" +
                         definitionText(id) + ")",
                     context);
            if (driver.maxCount < 1)
                fail("PARAMETER_DOMAIN", driverText(driver.id) + ": repeat maxCount must be 1-1,000",
                     context);
            const auto members = subtree(definition, driver.member);
            for (const auto member : members)
                if (!repeated.insert(member).second)
                    target("repeated subtrees cannot overlap");
            const auto size = Id(driver.maxCount - 1) * members.size();
            if (size > ComponentParameterLimits::reservedMembers - reserved)
                fail("LIMIT_EXCEEDED",
                     "Repeat copies would reserve more than 4,096 member IDs (" +
                         definitionText(id) + ")",
                     context);
            reserved += size;
            if (!driver.firstMember || driver.firstMember > definition.nextMemberId ||
                size > definition.nextMemberId - driver.firstMember)
                target("its reserved member block is outside the definition allocator");
            const Block block{driver.firstMember, driver.firstMember + size, driver.id};
            if (size) {
                if (const auto found = definition.members.lower_bound(block.first);
                    found != definition.members.end() && found->first < block.end)
                    target("its reserved member block overlaps member #" +
                           std::to_string(found->first));
                for (const auto &other : blocks)
                    if (other.first < block.end && block.first < other.end)
                        target("its reserved member block overlaps driver #" +
                               std::to_string(other.driver));
                blocks.push_back(block);
            }
            expected = {ParameterType::Count, {}};
            break;
        }
        case ComponentDriverKind::ChildParameter: {
            if (!reference)
                target("childParameter drives a nested component reference");
            const auto nested = definitions.find(definition.references.at(driver.member));
            const ComponentParameter *found = nullptr;
            if (nested != definitions.end() && nested->second && nested->second->parameters)
                for (const auto &candidate : nested->second->parameters->parameters)
                    if (candidate.name == driver.parameter)
                        found = &candidate;
            if (!found || found->mode != ComponentParameterMode::Input)
                target("the nested definition has no input parameter " +
                       quotedParameterText(driver.parameter));
            if (!childTargets.emplace(driver.member, driver.parameter).second)
                target("another driver already supplies this child parameter");
            child = *found;
            expected = signatureOf(*found);
            break;
        }
        }
        CompiledComponentParameters::Driver compiledDriver{
            expected, checkText(driver.expression, symbols, expected, context), {}, child};
        if (driver.kind == ComponentDriverKind::Repeat) {
            auto spacingContext = context;
            compiledDriver.spacing =
                checkText(driver.spacing, symbols, {ParameterType::Length, {}}, spacingContext);
        }
        compiled->drivers.push_back(std::move(compiledDriver));
    }
    return compiled;
}

// ---- Evaluate ----
ComponentEvaluation evaluateComponentParameters(const CompiledComponentParameters &compiled,
                                                const ComponentOverrides &inputs,
                                                std::size_t documentRemaining,
                                                std::size_t definitionBudget) {
    const auto &definition = *compiled.definition;
    const auto &record = *definition.parameters;
    const auto id = definition.id;
    for (const auto &[name, value] : inputs) {
        const auto context = parameterContext(id, name);
        const auto found = compiled.index.find(name);
        if (found == compiled.index.end() ||
            record.parameters[found->second].mode != ComponentParameterMode::Input)
            fail("PARAMETER_NAME",
                 definitionText(id) + " has no input parameter " + quotedParameterText(name),
                 context);
        if (!(canonicalValue(record.parameters[found->second], value, context) == value))
            fail("PARAMETER_DOMAIN",
                 "Override of " + quotedParameterText(name) + " is not a canonical value", context);
    }
    const bool documentBound = documentRemaining < definitionBudget;
    ParameterEvaluationBudget budget{documentBound ? documentRemaining : definitionBudget};
    const auto initial = budget.remaining;
    auto run = [&](const CheckedParameterExpression &checked,
                   const std::vector<std::optional<ParameterValue>> &values,
                   const Context &context) {
        std::vector<ParameterValue> arguments;
        arguments.reserve(checked.references.size());
        for (const auto &name : checked.references)
            arguments.push_back(*values[compiled.index.at(name)]);
        try {
            return evaluateParameterExpression(checked, arguments, budget);
        } catch (const ParameterExpressionError &error) {
            if (error.code() == ParameterErrorCode::LimitExceeded) {
                auto limited = context;
                limited.offset = error.offset();
                limited.excerpt = error.excerpt();
                fail("LIMIT_EXCEEDED",
                     documentBound
                         ? "Component parameters exceed the document evaluation budget of "
                           "20,000,000 node visits (" +
                               definitionText(id) + ")"
                         : "Component parameters exceed the evaluation budget of 200,000 node "
                           "visits (" +
                               definitionText(id) + ")",
                     limited);
            }
            throwParameterExpressionError(error, context);
        }
    };
    ComponentEvaluation result;
    std::vector<std::optional<ParameterValue>> values(record.parameters.size());
    for (const auto index : compiled.order) {
        const auto &parameter = record.parameters[index];
        const auto context = parameterContext(id, parameter.name);
        if (parameter.mode == ComponentParameterMode::Input)
            if (const auto found = inputs.find(parameter.name); found != inputs.end()) {
                values[index] = found->second;
                continue;
            }
        values[index] = run(compiled.expressions[index], values, context);
        checkComponentParameterBounds(parameter, *values[index], context);
    }
    for (const auto &value : values)
        result.parameters.push_back(*value);
    for (std::size_t i = 0; i < record.drivers.size(); ++i) {
        const auto &driver = record.drivers[i];
        const auto &compiledDriver = compiled.drivers[i];
        const auto context = driverContext(id, driver.id);
        auto value = run(compiledDriver.value, values, context);
        double spacing = 0;
        if (driver.kind == ComponentDriverKind::Repeat) {
            const auto count = std::get<ParameterQuantity>(value).value;
            if (count < 1 || count > driver.maxCount)
                fail("PARAMETER_DOMAIN",
                     driverText(driver.id) + ": repeat count " +
                         std::to_string(static_cast<long long>(count)) + " is outside 1-" +
                         std::to_string(driver.maxCount),
                     context);
            spacing = std::get<ParameterQuantity>(run(*compiledDriver.spacing, values, context)).value;
        }
        if (compiledDriver.child)
            checkComponentParameterBounds(*compiledDriver.child, value, context);
        result.drivers.push_back(std::move(value));
        result.spacing.push_back(spacing);
    }
    result.visits = initial - budget.remaining;
    return result;
}

// ---- Document-wide record checks ----
void validateComponentParameterRecords(const ComponentDefinitions &definitions,
                                       const ComponentDefinitions *baseline, Id nextDriverId) {
    std::size_t parametric = 0;
    for (const auto &[id, definition] : definitions)
        parametric += definition && definition->parameters ? 1 : 0;
    if (parametric > ComponentParameterLimits::parametricDefinitions)
        fail("LIMIT_EXCEEDED", "Parametric component definitions exceed 1,024 per document");
    if (!parametric)
        return;
    std::size_t bytes = 0;
    for (const auto &[id, definition] : definitions)
        if (definition->parameters) {
            bytes += componentParametersBytes(definition->parameters);
            if (bytes > ComponentParameterLimits::recordBytes)
                fail("LIMIT_EXCEEDED",
                     "Component parameter records exceed their 4 MiB budget (" +
                         definitionText(id) + ")",
                     {.definition = id});
        }
    for (const auto &[id, definition] : definitions)
        if (const auto &record = definition->parameters;
            record && record->assembly &&
            !validComponentAssembly(record->assembly->id, record->assembly->version))
            fail("PARAMETER_NAME",
                 definitionText(id) + " has a malformed assembly identity " +
                     quotedParameterText(record->assembly->id),
                 {.definition = id});
    std::set<Id> drivers;
    for (const auto &[id, definition] : definitions)
        if (definition->parameters)
            for (const auto &driver : definition->parameters->drivers)
                if (!driver.id || driver.id >= nextDriverId || !drivers.insert(driver.id).second)
                    fail("INVALID_TARGET",
                         driverText(driver.id) + " is reused or above the driver allocator",
                         driverContext(id, driver.id));
    // Parametric levels along one reference path (the graph is acyclic and at most
    // 128 deep, as validateComponentDefinitions checks first).
    std::map<Id, std::size_t> depth;
    auto levels = [&](auto &&self, Id id, std::size_t guard) -> std::size_t {
        if (const auto found = depth.find(id); found != depth.end())
            return found->second;
        if (guard > 130 || !definitions.contains(id))
            throw std::runtime_error("Cyclic or dangling component reference graph");
        const auto &definition = *definitions.at(id);
        std::size_t deepest = 0;
        for (const auto &[member, target] : definition.references)
            deepest = std::max(deepest, self(self, target, guard + 1));
        return depth[id] = deepest + (definition.parameters ? 1 : 0);
    };
    for (const auto &[id, definition] : definitions)
        if (levels(levels, id, 0) > ComponentParameterLimits::nesting)
            fail("LIMIT_EXCEEDED",
                 "Parametric components nest more than 8 levels (" + definitionText(id) + ")",
                 {.definition = id});
    auto changed = [&](Id id) {
        if (!baseline)
            return true;
        const auto found = baseline->find(id);
        if (found == baseline->end() || found->second != definitions.at(id))
            return true;
        for (const auto &[member, target] : definitions.at(id)->references) {
            const auto child = baseline->find(target);
            if (child == baseline->end() || child->second != definitions.at(target))
                return true;
        }
        return false;
    };
    for (const auto &[id, definition] : definitions)
        if (definition->parameters && changed(id))
            (void)compileComponentParameters(definition, definitions);
}

// ---- Driver state ----
ComponentDigest componentDriverDigest(const ComponentDefinition &definition,
                                      const ComponentDefinitions &definitions,
                                      const ComponentDriver &driver) {
    std::string bytes;
    if (!definition.members.contains(driver.member))
        bytes = "missing";
    else if (driver.kind == ComponentDriverKind::Repeat) {
        for (const auto member : subtree(definition, driver.member))
            appendCanonicalBody(bytes, *definition.members.at(member));
    } else if (driver.kind == ComponentDriverKind::ChildParameter) {
        const auto reference = definition.references.find(driver.member);
        const auto child = reference == definition.references.end()
                               ? definitions.end()
                               : definitions.find(reference->second);
        if (child == definitions.end() || !child->second || !child->second->parameters)
            bytes = "missing";
        else
            appendCanonicalParameterList(bytes, child->second->parameters->parameters);
    } else
        appendCanonicalBody(bytes, *definition.members.at(driver.member));
    return sha256Digest(bytes);
}
ComponentDriverState componentDriverState(const ComponentDefinition &definition,
                                          const ComponentDefinitions &definitions,
                                          const ComponentDriver &driver) {
    return componentDriverDigest(definition, definitions, driver) == driver.bound
               ? ComponentDriverState::Bound
               : ComponentDriverState::ChangedSinceBound;
}
} // namespace sketchy

// ---- Edit builders ----
namespace sketchy {
namespace {
const DefinitionPtr &requireDefinition(const Document &doc, Id id) {
    const auto found = doc.definitions().find(id);
    if (found == doc.definitions().end())
        fail("NOT_FOUND", "Component definition #" + std::to_string(id) + " does not exist",
             {.definition = id});
    return found->second;
}
std::string canonicalText(const std::string &text, const Context &context) {
    try {
        return canonicalParameterExpression(parseParameterExpression(text));
    } catch (const ParameterExpressionError &error) {
        throwParameterExpressionError(error, context);
    }
}
std::size_t parameterIndex(const ComponentParameters &record, const std::string &name, Id definition) {
    for (std::size_t i = 0; i < record.parameters.size(); ++i)
        if (record.parameters[i].name == name)
            return i;
    fail("PARAMETER_NAME",
         definitionText(definition) + " has no parameter " + quotedParameterText(name),
         parameterContext(definition, name));
}
// The top-level placements of `definition` (placement roots no binding owns).
std::set<Id> nestedPlacementRoots(const Document &doc) {
    std::set<Id> nested;
    for (const auto &[root, instance] : doc.instances())
        for (const auto &[member, target] : instance->members)
            if (target != root && doc.instances().contains(target))
                nested.insert(target);
    return nested;
}
Id lockedPlacement(const Document &doc, Id id) {
    for (std::size_t depth = 0; id && doc.bodies().contains(id) && depth <= 128; ++depth) {
        if (doc.bodies().at(id)->locked)
            return id;
        id = doc.bodies().at(id)->parent;
    }
    return 0;
}
InstancePtr requireTopLevel(const Document &doc, Id root) {
    const auto found = doc.instances().find(root);
    if (found == doc.instances().end())
        fail("NOT_FOUND", "Placement #" + std::to_string(root) + " is not a component placement",
             {.placement = root});
    if (nestedPlacementRoots(doc).contains(root))
        fail("INVALID_TARGET",
             "Placement #" + std::to_string(root) +
                 " is nested; its values come from its parent's drivers",
             {.definition = found->second->definition, .placement = root});
    return found->second;
}
struct Staged {
    std::map<Id, DefinitionPtr> definitions;      // changed definitions
    std::map<Id, ComponentOverrides> overrides;   // changed placement overrides
    Id nextDriver{};
    std::vector<Id> created;
};
ComponentResult commit(Document &doc, Staged staged, Id definition, Id instance, std::string label) {
    auto definitions = doc.definitions();
    for (const auto &[id, record] : staged.definitions)
        definitions[id] = record;
    auto bindings = doc.instances();
    for (const auto &[root, overrides] : staged.overrides) {
        auto binding = std::make_shared<ComponentInstance>(*bindings.at(root));
        binding->overrides = overrides;
        bindings[root] = binding;
    }
    const auto beforeDefinitions = doc.definitions();
    const auto beforeVariants = doc.componentVariantRecords();
    ComponentResult result{definition, instance, {}, {}};
    ComponentPublishOptions options;
    options.parameterLocks = true;
    result.changes = publishComponentRecords(doc, definitions, bindings, doc.bodies(), doc.nextId(),
                                             doc.nextDefinitionId(),
                                             std::max(staged.nextDriver, doc.nextComponentDriverId()),
                                             std::move(label), options);
    result.createdDrivers = std::move(staged.created);
    result.regenerated = componentRegeneration(doc, beforeDefinitions, *beforeVariants);
    return result;
}
// Stage one definition's new record: reserve repeat blocks, re-baseline the template
// (ADR 0165 §1) and stamp `bound` for the drivers in `stamp` and, after a
// re-baseline, for every driver that was bound before it.
DefinitionPtr stageRecord(const Document &doc, const DefinitionPtr &original,
                          ComponentParameters record, const std::set<Id> &stamp,
                          const ComponentDefinitions &context) {
    auto staged = std::make_shared<ComponentDefinition>(*original);
    if (record.parameters.empty() && record.drivers.empty()) {
        staged->parameters = nullptr;
        return staged;
    }
    reserveRepeatBlocks(*staged, record, original.get(), original->parameters.get());
    staged->parameters = std::make_shared<const ComponentParameters>(record);
    const auto rebase = rebaseComponentTemplate(original, staged, doc.definitions(), context);
    auto result = std::make_shared<ComponentDefinition>(*staged);
    result->members = rebase.members;
    auto stamped = rebase.record;
    auto candidates = context;
    candidates[original->id] = result;
    for (auto &driver : stamped.drivers) {
        const ComponentDriver *before = nullptr;
        if (original->parameters)
            for (const auto &candidate : original->parameters->drivers)
                if (candidate.id == driver.id)
                    before = &candidate;
        const bool wasBound = before && componentDriverState(*original, doc.definitions(), *before) ==
                                            ComponentDriverState::Bound;
        if (stamp.contains(driver.id) || (wasBound && !rebase.moved.empty()))
            driver.bound = componentDriverDigest(*result, candidates, driver);
        else if (before && before->bound != driver.bound)
            driver.bound = before->bound; // Other edits leave it.
    }
    result->parameters = std::make_shared<const ComponentParameters>(std::move(stamped));
    return result;
}
ComponentParameters recordOf(const DefinitionPtr &definition) {
    return definition->parameters ? *definition->parameters : ComponentParameters{};
}
// Rename every reference to `from` in one canonical expression.
std::string renameIn(const std::string &text, const std::string &from, const std::string &to) {
    if (text.empty())
        return text;
    auto expression = parseParameterExpression(text);
    bool changed = false;
    for (auto &node : expression.nodes)
        if (node.op == ParameterOp::Name && node.text == from) {
            node.text = to;
            changed = true;
        }
    return changed ? canonicalParameterExpression(expression) : text;
}
bool references(const std::string &text, const std::string &name) {
    if (text.empty())
        return false;
    const auto expression = parseParameterExpression(text);
    return std::any_of(expression.nodes.begin(), expression.nodes.end(), [&](const auto &node) {
        return node.op == ParameterOp::Name && node.text == name;
    });
}
ComponentParameter parameterFrom(const ComponentParameterSpec &spec, Id definition) {
    const auto context = parameterContext(definition, spec.name);
    ComponentParameter parameter;
    parameter.name = spec.name;
    parameter.label = spec.label;
    parameter.type = spec.type;
    parameter.mode = spec.mode;
    parameter.expression = canonicalText(spec.expression, context);
    parameter.min = spec.min;
    parameter.max = spec.max;
    parameter.step = spec.step;
    parameter.options = spec.options;
    return parameter;
}
} // namespace

ComponentResult defineComponentParameter(Document &doc, Id id, ComponentParameterSpec spec,
                                         std::optional<std::size_t> position) {
    const auto &original = requireDefinition(doc, id);
    auto record = recordOf(original);
    if (record.parameters.size() >= ComponentParameterLimits::parameters)
        fail("LIMIT_EXCEEDED",
             "Component parameters exceed 128 per definition (" + definitionText(id) + ")",
             {.definition = id});
    auto parameter = parameterFrom(spec, id);
    const auto at = position.value_or(record.parameters.size());
    if (at > record.parameters.size())
        fail("INVALID_TARGET", "Parameter position is outside the parameter list",
             parameterContext(id, spec.name));
    record.parameters.insert(record.parameters.begin() + std::ptrdiff_t(at), std::move(parameter));
    Staged staged;
    staged.definitions[id] = stageRecord(doc, original, std::move(record), {}, doc.definitions());
    return commit(doc, std::move(staged), id, 0, "Define component parameter");
}
ComponentResult updateComponentParameter(Document &doc, Id id, const std::string &name,
                                         const ComponentParameterUpdate &update) {
    const auto &original = requireDefinition(doc, id);
    auto record = recordOf(original);
    const auto index = parameterIndex(record, name, id);
    auto parameter = record.parameters[index];
    const auto context = parameterContext(id, name);
    if (update.label)
        parameter.label = *update.label;
    if (update.expression)
        parameter.expression = canonicalText(*update.expression, context);
    if (update.clearMin)
        parameter.min.reset();
    if (update.clearMax)
        parameter.max.reset();
    if (update.clearStep)
        parameter.step.reset();
    if (update.min)
        parameter.min = update.min;
    if (update.max)
        parameter.max = update.max;
    if (update.step)
        parameter.step = update.step;
    if (update.options)
        parameter.options = *update.options;
    record.parameters.erase(record.parameters.begin() + std::ptrdiff_t(index));
    const auto at = update.position.value_or(index);
    if (at > record.parameters.size())
        fail("INVALID_TARGET", "Parameter position is outside the parameter list", context);
    record.parameters.insert(record.parameters.begin() + std::ptrdiff_t(at), std::move(parameter));
    Staged staged;
    staged.definitions[id] = stageRecord(doc, original, std::move(record), {}, doc.definitions());
    return commit(doc, std::move(staged), id, 0, "Update component parameter");
}
ComponentResult renameComponentParameter(Document &doc, Id id, const std::string &name,
                                         const std::string &newName) {
    const auto &original = requireDefinition(doc, id);
    auto record = recordOf(original);
    const auto index = parameterIndex(record, name, id);
    const auto context = parameterContext(id, newName);
    if (!isValidParameterName(newName))
        fail("PARAMETER_NAME",
             "Parameter name " + quotedParameterText(newName) + " is malformed or reserved",
             context);
    if (newName == name)
        return {id, 0, {}, {}};
    for (const auto &parameter : record.parameters)
        if (parameter.name == newName)
            fail("PARAMETER_NAME", "Duplicate parameter name " + quotedParameterText(newName),
                 context);
    record.parameters[index].name = newName;
    for (auto &parameter : record.parameters)
        parameter.expression = renameIn(parameter.expression, name, newName);
    for (auto &driver : record.drivers) {
        driver.expression = renameIn(driver.expression, name, newName);
        driver.spacing = renameIn(driver.spacing, name, newName);
    }
    Staged staged;
    staged.definitions[id] = stageRecord(doc, original, std::move(record), {}, doc.definitions());
    // Overrides on its placements follow the name.
    for (const auto &[root, instance] : doc.instances())
        if (instance->definition == id && instance->overrides.contains(name)) {
            auto overrides = instance->overrides;
            auto value = overrides.at(name);
            overrides.erase(name);
            overrides[newName] = std::move(value);
            staged.overrides[root] = std::move(overrides);
        }
    // Parent childParameter drivers follow the name and are restamped.
    auto candidates = doc.definitions();
    candidates[id] = staged.definitions.at(id);
    for (const auto &[parentId, parent] : doc.definitions()) {
        if (parentId == id || !parent->parameters)
            continue;
        auto parentRecord = *parent->parameters;
        std::set<Id> rewritten;
        for (auto &driver : parentRecord.drivers)
            if (driver.kind == ComponentDriverKind::ChildParameter && driver.parameter == name &&
                parent->references.contains(driver.member) &&
                parent->references.at(driver.member) == id) {
                driver.parameter = newName;
                rewritten.insert(driver.id);
            }
        if (!rewritten.empty())
            staged.definitions[parentId] =
                stageRecord(doc, parent, std::move(parentRecord), rewritten, candidates);
    }
    return commit(doc, std::move(staged), id, 0, "Rename component parameter");
}
ComponentResult removeComponentParameter(Document &doc, Id id, const std::string &name) {
    const auto &original = requireDefinition(doc, id);
    auto record = recordOf(original);
    const auto index = parameterIndex(record, name, id);
    auto context = parameterContext(id, name);
    for (const auto &parameter : record.parameters)
        if (parameter.name != name && references(parameter.expression, name))
            context.referrers.push_back("parameter " + parameter.name);
    for (const auto &driver : record.drivers)
        if (references(driver.expression, name) || references(driver.spacing, name))
            context.referrers.push_back(driverText(driver.id));
    for (const auto &[parentId, parent] : doc.definitions())
        if (parent->parameters)
            for (const auto &driver : parent->parameters->drivers)
                if (driver.kind == ComponentDriverKind::ChildParameter && driver.parameter == name &&
                    parent->references.contains(driver.member) &&
                    parent->references.at(driver.member) == id)
                    context.referrers.push_back(driverText(driver.id) + " of " +
                                                definitionText(parentId));
    if (!context.referrers.empty()) {
        std::string list;
        for (std::size_t i = 0; i < context.referrers.size() && i < 4; ++i)
            list += (i ? ", " : "") + context.referrers[i];
        if (context.referrers.size() > 4)
            list += ", ...";
        fail("PARAMETER_NAME",
             "Parameter " + quotedParameterText(name) + " is still referenced by " + list, context);
    }
    record.parameters.erase(record.parameters.begin() + std::ptrdiff_t(index));
    Staged staged;
    staged.definitions[id] = stageRecord(doc, original, std::move(record), {}, doc.definitions());
    // An override of a parameter nothing reads has no effect; it goes with it.
    for (const auto &[root, instance] : doc.instances())
        if (instance->definition == id && instance->overrides.contains(name)) {
            auto overrides = instance->overrides;
            overrides.erase(name);
            staged.overrides[root] = std::move(overrides);
        }
    return commit(doc, std::move(staged), id, 0, "Remove component parameter");
}
ComponentResult driveComponent(Document &doc, Id id, Id driverId, const ComponentDriverSpec &spec) {
    const auto &original = requireDefinition(doc, id);
    auto record = recordOf(original);
    Staged staged;
    staged.nextDriver = doc.nextComponentDriverId();
    ComponentDriver driver;
    if (!driverId) {
        if (record.drivers.size() >= ComponentParameterLimits::drivers)
            fail("LIMIT_EXCEEDED",
                 "Component drivers exceed 256 per definition (" + definitionText(id) + ")",
                 {.definition = id});
        if (staged.nextDriver == UINT64_MAX)
            throw std::runtime_error("Component driver identity space exhausted");
        driverId = staged.nextDriver++;
        staged.created.push_back(driverId);
    } else {
        const auto found = std::find_if(record.drivers.begin(), record.drivers.end(),
                                        [&](const auto &candidate) { return candidate.id == driverId; });
        if (found == record.drivers.end())
            fail("NOT_FOUND", definitionText(id) + " has no " + driverText(driverId),
                 driverContext(id, driverId));
        driver = *found;
        record.drivers.erase(found);
    }
    const auto context = driverContext(id, driverId);
    const auto previousFirst = driver.firstMember;
    driver = {};
    driver.id = driverId;
    driver.kind = spec.kind;
    driver.member = spec.member;
    driver.expression = canonicalText(spec.expression, context);
    switch (spec.kind) {
    case ComponentDriverKind::Stretch:
        driver.axis = spec.axis;
        driver.at = spec.at;
        break;
    case ComponentDriverKind::Position:
    case ComponentDriverKind::Rotation:
        driver.axis = spec.axis;
        break;
    case ComponentDriverKind::Repeat:
        driver.axis = spec.axis;
        driver.maxCount = spec.maxCount;
        driver.spacing = canonicalText(spec.spacing, context);
        driver.firstMember = previousFirst;
        break;
    case ComponentDriverKind::ChildParameter:
        driver.parameter = spec.parameter;
        break;
    case ComponentDriverKind::Visibility:
        break;
    }
    const auto at = std::lower_bound(record.drivers.begin(), record.drivers.end(), driverId,
                                     [](const auto &candidate, Id value) { return candidate.id < value; });
    record.drivers.insert(at, std::move(driver));
    staged.definitions[id] = stageRecord(doc, original, std::move(record), {driverId}, doc.definitions());
    return commit(doc, std::move(staged), id, 0, "Drive component member");
}
ComponentResult undriveComponent(Document &doc, Id id, Id driverId) {
    const auto &original = requireDefinition(doc, id);
    auto record = recordOf(original);
    const auto found = std::find_if(record.drivers.begin(), record.drivers.end(),
                                    [&](const auto &candidate) { return candidate.id == driverId; });
    if (found == record.drivers.end())
        fail("NOT_FOUND", definitionText(id) + " has no " + driverText(driverId),
             driverContext(id, driverId));
    record.drivers.erase(found);
    Staged staged;
    staged.definitions[id] = stageRecord(doc, original, std::move(record), {}, doc.definitions());
    return commit(doc, std::move(staged), id, 0, "Remove component driver");
}
ComponentResult rebindComponentDrivers(Document &doc, Id id, const std::vector<Id> &drivers) {
    const auto &original = requireDefinition(doc, id);
    auto record = recordOf(original);
    std::set<Id> stamp;
    for (const auto driver : drivers) {
        if (std::none_of(record.drivers.begin(), record.drivers.end(),
                         [&](const auto &candidate) { return candidate.id == driver; }))
            fail("NOT_FOUND", definitionText(id) + " has no " + driverText(driver),
                 driverContext(id, driver));
        stamp.insert(driver);
    }
    if (drivers.empty())
        for (const auto &driver : record.drivers)
            stamp.insert(driver.id);
    Staged staged;
    staged.definitions[id] = stageRecord(doc, original, std::move(record), stamp, doc.definitions());
    return commit(doc, std::move(staged), id, 0, "Rebind component drivers");
}
ComponentResult setComponentAssembly(Document &doc, Id id,
                                     std::optional<ComponentAssembly> assembly) {
    const auto &original = requireDefinition(doc, id);
    if (!original->parameters)
        fail("PARAMETER_NAME", definitionText(id) + " has no parameters", {.definition = id});
    if (assembly && !validComponentAssembly(assembly->id, assembly->version))
        fail("PARAMETER_NAME",
             "Assembly identity " + quotedParameterText(assembly->id) +
                 " must match [a-z0-9_-]{1,64}(/[a-z0-9_-]{1,64})? with a version 1-2147483647",
             {.definition = id});
    if (original->parameters->assembly == assembly)
        return {id, 0, {}, {}};
    auto record = *original->parameters;
    record.assembly = std::move(assembly);
    auto staged = std::make_shared<ComponentDefinition>(*original);
    staged->parameters = std::make_shared<const ComponentParameters>(std::move(record));
    Staged edit;
    edit.definitions[id] = std::move(staged);
    return commit(doc, std::move(edit), id, 0, "Set component assembly identity");
}
ComponentResult overrideComponentParameters(Document &doc, Id root,
                                            const ComponentOverrides &values) {
    const auto binding = requireTopLevel(doc, root);
    const auto &definition = requireDefinition(doc, binding->definition);
    if (!definition->parameters)
        fail("PARAMETER_NAME", definitionText(definition->id) + " has no parameters",
             {.definition = definition->id, .placement = root});
    if (lockedPlacement(doc, root))
        fail("LOCKED_ENTITY",
             "Placement #" + std::to_string(root) + " is locked; unlock it before changing its "
                                                    "parameters",
             {.definition = definition->id, .placement = root});
    ComponentOverrides canonical;
    try {
        canonical = canonicalComponentOverrides(
            *compileComponentParameters(definition, doc.definitions()), values);
    } catch (const ComponentParameterError &error) {
        throw error.withPlacement(root);
    }
    auto overrides = binding->overrides;
    for (auto &[name, value] : canonical)
        overrides[name] = std::move(value);
    Staged staged;
    staged.overrides[root] = std::move(overrides);
    return commit(doc, std::move(staged), definition->id, root, "Set component parameters");
}
ComponentResult resetComponentParameters(Document &doc, Id root,
                                         const std::vector<std::string> &names) {
    const auto binding = requireTopLevel(doc, root);
    const auto &definition = requireDefinition(doc, binding->definition);
    if (lockedPlacement(doc, root))
        fail("LOCKED_ENTITY",
             "Placement #" + std::to_string(root) + " is locked; unlock it before changing its "
                                                    "parameters",
             {.definition = definition->id, .placement = root});
    auto overrides = binding->overrides;
    if (names.empty())
        overrides.clear();
    for (const auto &name : names) {
        if (!definition->parameters)
            fail("PARAMETER_NAME", definitionText(definition->id) + " has no parameters",
                 {.definition = definition->id, .placement = root});
        (void)parameterIndex(*definition->parameters, name, definition->id);
        overrides.erase(name);
    }
    if (overrides == binding->overrides)
        return {definition->id, root, {}, {}};
    Staged staged;
    staged.overrides[root] = std::move(overrides);
    return commit(doc, std::move(staged), definition->id, root, "Reset component parameters");
}
ComponentResult promoteComponentParameters(Document &doc, Id root) {
    const auto binding = requireTopLevel(doc, root);
    const auto &original = requireDefinition(doc, binding->definition);
    if (!original->parameters)
        return {original->id, root, {}, {}};
    // New defaults would move every other default placement: promotion follows
    // make-unique (component.edit_instance), so the placement must be the only one.
    for (const auto &[other, instance] : doc.instances())
        if (other != root && instance->definition == original->id)
            fail("INVALID_TARGET",
                 "Placement #" + std::to_string(root) +
                     " shares its definition; make it unique before promoting its values",
                 {.definition = original->id, .placement = root});
    auto record = *original->parameters;
    for (const auto &[name, value] : binding->overrides) {
        auto &parameter = record.parameters[parameterIndex(record, name, original->id)];
        parameter.expression = componentParameterLiteral(value, parameter.type);
    }
    Staged staged;
    staged.definitions[original->id] =
        stageRecord(doc, original, std::move(record), {}, doc.definitions());
    if (!binding->overrides.empty())
        staged.overrides[root] = {};
    auto result = commit(doc, std::move(staged), original->id, root,
                         "Promote component parameters");
    result.parametersPromoted = true;
    return result;
}
ComponentOverrides componentPlacementInputs(const Document &doc, Id root) {
    const auto found = doc.instances().find(root);
    if (found == doc.instances().end())
        fail("NOT_FOUND", "Placement #" + std::to_string(root) + " is not a component placement",
             {.placement = root});
    for (const auto &[parent, instance] : doc.instances())
        for (const auto &[member, target] : instance->members)
            if (target == root && parent != root) {
                const auto variant = doc.componentVariant(parent);
                if (!variant)
                    return {};
                const auto values = variant->nestedValues.find(member);
                return values == variant->nestedValues.end() ? ComponentOverrides{}
                                                             : values->second;
            }
    return found->second->overrides;
}
void requireComponentTemplateScope(const Document &doc, Id instance,
                                   const std::set<Id> &sceneRecords) {
    const auto found = doc.instances().find(instance);
    if (found == doc.instances().end())
        fail("NOT_FOUND", "Placement #" + std::to_string(instance) + " is not a component placement",
             {.placement = instance});
    const auto &binding = *found->second;
    const auto &definition = requireDefinition(doc, binding.definition);
    Context context{.definition = definition->id, .placement = instance};
    if (definition->parameters) {
        const auto inputs = componentPlacementInputs(doc, instance);
        if (!inputs.empty()) {
            ComponentVariantEngine engine(doc.definitions(), &doc.componentVariants());
            if (engine.evaluate(definition->id, inputs).parameters !=
                engine.evaluate(definition->id, {}).parameters)
                fail("PARAMETER_TEMPLATE_SCOPE",
                     "Placement #" + std::to_string(instance) +
                         " shows non-default parameter values; edit the definition directly or "
                         "reset its overrides",
                     context);
        }
    }
    const auto variant = doc.componentVariant(instance);
    if (!variant || variant->generated.empty() || sceneRecords.empty())
        return;
    std::map<Id, Id> owners; // scene record -> canonical member
    for (const auto &[member, target] : binding.members)
        owners[target] = member;
    for (auto record : sceneRecords)
        for (std::size_t depth = 0; record && record != instance && doc.bodies().contains(record) &&
                                    depth <= 128;
             ++depth, record = doc.bodies().at(record)->parent)
            if (const auto owner = owners.find(record);
                owner != owners.end() && variant->generated.contains(owner->second))
                fail("PARAMETER_TEMPLATE_SCOPE",
                     "Scene record #" + std::to_string(record) +
                         " is a generated repeat copy; edit the original member",
                     context);
}
} // namespace sketchy
