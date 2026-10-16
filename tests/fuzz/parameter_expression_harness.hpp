#pragma once
// R093.a (ADR 0165 §7): the `parameter-expression` fuzz input is one expression,
// checked and evaluated against a fixed table with one parameter of every type.
// Shared by the libFuzzer target (through parser_harness.hpp), the seed
// generator, the parser fault corpus and the in-ctest parameter_expression_fuzz.
// Standard library only.
#include "core/parameter_expression.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sketchy::fuzz {

// An invariant failure, never an ordinary rejection.
class ParameterHarnessFailure : public std::logic_error {
    using std::logic_error::logic_error;
};
inline void expressionRequire(bool value, const char *message) {
    if (!value)
        throw ParameterHarnessFailure(message);
}

class HarnessParameterSymbols final : public ParameterSymbols {
  public:
    HarnessParameterSymbols() {
        table_["width"] = {ParameterType::Length, {}};
        table_["floor_area"] = {ParameterType::Area, {}};
        table_["tilt"] = {ParameterType::Angle, {}};
        table_["ratio"] = {ParameterType::Number, {}};
        table_["shelves"] = {ParameterType::Count, {}};
        table_["open"] = {ParameterType::Boolean, {}};
        table_["style"] = {ParameterType::Choice, {"oak", "pine", "birch-2"}};
    }
    const ParameterSignature *find(std::string_view name) const override {
        const auto found = table_.find(name);
        return found == table_.end() ? nullptr : &found->second;
    }

  private:
    std::map<std::string, ParameterSignature, std::less<>> table_;
};

inline ParameterValue harnessParameterValue(std::string_view name) {
    if (name == "width")
        return ParameterQuantity{0.9, {1, 0}};
    if (name == "floor_area")
        return ParameterQuantity{2.5, {2, 0}};
    if (name == "tilt")
        return ParameterQuantity{0.3, {0, 1}};
    if (name == "ratio")
        return ParameterQuantity{1.5, {0, 0}};
    if (name == "shelves")
        return ParameterQuantity{3, {0, 0}};
    if (name == "open")
        return true;
    return ParameterChoice{"pine"};
}

// The seven expected types, the choice one with the table's `style` options.
inline std::array<ParameterSignature, 7> harnessExpectedTypes() {
    return {ParameterSignature{ParameterType::Length, {}},
            ParameterSignature{ParameterType::Area, {}},
            ParameterSignature{ParameterType::Angle, {}},
            ParameterSignature{ParameterType::Number, {}},
            ParameterSignature{ParameterType::Count, {}},
            ParameterSignature{ParameterType::Boolean, {}},
            ParameterSignature{ParameterType::Choice, {"oak", "pine", "birch-2"}}};
}

inline void harnessRejection(const ParameterExpressionError &error, std::string_view text) {
    const auto code = error.code();
    expressionRequire(code == ParameterErrorCode::Syntax || code == ParameterErrorCode::Name ||
                          code == ParameterErrorCode::Type || code == ParameterErrorCode::Domain ||
                          code == ParameterErrorCode::LimitExceeded,
                      "rejection code is from the closed set");
    const auto last = text.empty() ? std::size_t(1) : text.size();
    expressionRequire(error.offset() >= 1 && error.offset() <= last,
                      "rejection offset lies inside the input");
    expressionRequire(error.excerpt().size() <= ParameterExpressionLimits::excerptBytes,
                      "rejection excerpt is at most 64 bytes");
    expressionRequire(std::string_view(error.what()).size() <=
                          ParameterExpressionLimits::messageBytes,
                      "rejection message is at most 256 bytes");
}

// The outcome of evaluating one checked expression: the value, or the error code.
struct HarnessEvaluation {
    bool ok{};
    ParameterErrorCode code{};
    ParameterValue value;
    std::size_t visits{};
};
inline bool sameHarnessValue(const ParameterValue &a, const ParameterValue &b) {
    if (a.index() != b.index())
        return false;
    if (const auto *q = std::get_if<ParameterQuantity>(&a)) {
        const auto &r = std::get<ParameterQuantity>(b);
        return std::bit_cast<std::uint64_t>(q->value) == std::bit_cast<std::uint64_t>(r.value) &&
               q->dimension == r.dimension;
    }
    return a == b;
}
inline HarnessEvaluation harnessEvaluate(const CheckedParameterExpression &checked) {
    std::vector<ParameterValue> values;
    for (const auto &name : checked.references)
        values.push_back(harnessParameterValue(name));
    constexpr std::size_t budget = 1000;
    ParameterEvaluationBudget remaining{budget};
    HarnessEvaluation out;
    try {
        out.value = evaluateParameterExpression(checked, values, remaining);
        out.ok = true;
    } catch (const ParameterExpressionError &error) {
        harnessRejection(error, checked.expression.source);
        expressionRequire(error.code() == ParameterErrorCode::Domain,
                          "evaluation rejects only with PARAMETER_DOMAIN within the budget");
        out.code = error.code();
    }
    out.visits = budget - remaining.remaining;
    expressionRequire(out.visits <= checked.expression.nodes.size(),
                      "evaluation visits each node at most once");
    return out;
}

// Returns true when the expression parses and checks against at least one
// expected type. Throws ParameterHarnessFailure when an invariant fails.
inline bool parameterExpressionAccepted(std::string_view text) {
    try {
        ParameterExpression tree;
        try {
            tree = parseParameterExpression(text);
        } catch (const ParameterExpressionError &error) {
            harnessRejection(error, text);
            return false;
        }
        using Limits = ParameterExpressionLimits;
        expressionRequire(text.size() <= Limits::textBytes, "text limit holds");
        expressionRequire(!tree.nodes.empty() && tree.nodes.size() <= Limits::nodes,
                          "node limit holds");
        expressionRequire(tree.tokenCount <= Limits::tokens, "token limit holds");
        expressionRequire(tree.maxDepth <= Limits::depth, "depth limit holds");
        expressionRequire(tree.nodes.back().subtreeSize == tree.nodes.size(),
                          "the root spans the node array");
        const auto canonical = canonicalParameterExpression(tree);
        ParameterExpression again;
        try {
            again = parseParameterExpression(canonical);
        } catch (const ParameterExpressionError &) {
            throw ParameterHarnessFailure("canonical text re-parses");
        }
        expressionRequire(sameParameterTree(tree, again),
                          "canonical text re-parses to the same tree");
        expressionRequire(canonicalParameterExpression(again) == canonical,
                          "canonical text is a fixed point");
        expressionRequire(again.tokenCount <= tree.tokenCount && again.maxDepth <= tree.maxDepth,
                          "canonical text adds no tokens or nesting");
        const HarnessParameterSymbols symbols;
        bool accepted = false;
        for (const auto &expected : harnessExpectedTypes()) {
            std::optional<CheckedParameterExpression> checked, recheck;
            std::optional<ParameterErrorCode> first, second;
            try {
                checked = checkParameterExpression(tree, symbols, expected);
            } catch (const ParameterExpressionError &error) {
                harnessRejection(error, text);
                first = error.code();
            }
            try {
                recheck = checkParameterExpression(again, symbols, expected);
            } catch (const ParameterExpressionError &error) {
                harnessRejection(error, canonical);
                second = error.code();
            }
            expressionRequire(first == second, "canonical text checks the same way");
            if (!checked)
                continue;
            accepted = true;
            expressionRequire(checked->maxStack <= Limits::valueStack &&
                                  checked->maxStack <= tree.nodes.size(),
                              "value stack bound holds");
            for (const auto &step : checked->program)
                if (step.instruction != ParameterInstruction::Node)
                    expressionRequire(step.target > 0 && step.target <= checked->program.size(),
                                      "jumps stay inside the program");
            const auto a = harnessEvaluate(*checked);
            const auto b = harnessEvaluate(*recheck);
            expressionRequire(a.ok == b.ok && a.visits == b.visits,
                              "canonical text evaluates the same way");
            if (a.ok)
                expressionRequire(sameHarnessValue(a.value, b.value),
                                  "canonical text evaluates to the same value");
            else
                expressionRequire(a.code == b.code, "canonical text fails the same way");
        }
        return accepted;
    } catch (const std::bad_alloc &) {
        throw ParameterHarnessFailure("resource exhaustion is not ordinary rejection");
    } catch (const ParameterHarnessFailure &) {
        throw;
    } catch (const ParameterExpressionError &) {
        throw ParameterHarnessFailure("an engine error escaped its stage");
    } catch (const std::exception &) {
        throw ParameterHarnessFailure("an unexpected exception escaped the engine");
    }
}

} // namespace sketchy::fuzz
