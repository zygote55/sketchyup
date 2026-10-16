// R093.a (ADR 0165 §2, §3, §7, §8): the parameter expression engine.
// Every production, precedence, unit algebra and type rule, every domain error
// with its byte offset, the canonical round trip, each limit at n and n + 1,
// bit-exact golden values and grammar closure.
//   parameter_expression_tests [--print-golden]
#include "core/parameter_expression.hpp"
#include <bit>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace sketchy;
namespace {

int failures = 0;
int checks = 0;
std::string context;
void check(bool ok, const std::string &message) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << message << (context.empty() ? "" : " [" + context + "]") << '\n';
    }
}

class Symbols final : public ParameterSymbols {
  public:
    Symbols() {
        table_["width"] = {ParameterType::Length, {}};
        table_["height"] = {ParameterType::Length, {}};
        table_["floor_area"] = {ParameterType::Area, {}};
        table_["tilt"] = {ParameterType::Angle, {}};
        table_["ratio"] = {ParameterType::Number, {}};
        table_["zero"] = {ParameterType::Number, {}};
        table_["shelves"] = {ParameterType::Count, {}};
        table_["open"] = {ParameterType::Boolean, {}};
        table_["style"] = {ParameterType::Choice, {"oak", "pine", "birch-2"}};
        table_["other_style"] = {ParameterType::Choice, {"oak", "pine", "birch-2"}};
        table_["finish"] = {ParameterType::Choice, {"matte", "gloss"}};
        table_[std::string(32, 'n')] = {ParameterType::Number, {}};
    }
    const ParameterSignature *find(std::string_view name) const override {
        const auto found = table_.find(name);
        return found == table_.end() ? nullptr : &found->second;
    }

  private:
    std::map<std::string, ParameterSignature, std::less<>> table_;
};
const Symbols symbols;

ParameterValue input(std::string_view name) {
    if (name == "width")
        return ParameterQuantity{0.9, {1, 0}};
    if (name == "height")
        return ParameterQuantity{0.75, {1, 0}};
    if (name == "floor_area")
        return ParameterQuantity{2.25, {2, 0}};
    if (name == "tilt")
        return ParameterQuantity{0.5, {0, 1}};
    if (name == "ratio")
        return ParameterQuantity{1.5, {0, 0}};
    if (name == "zero")
        return ParameterQuantity{0, {0, 0}};
    if (name == "shelves")
        return ParameterQuantity{3, {0, 0}};
    if (name == "open")
        return true;
    if (name == "style" || name == "other_style")
        return ParameterChoice{"pine"};
    if (name == "finish")
        return ParameterChoice{"matte"};
    return ParameterQuantity{2, {0, 0}};
}

ParameterSignature sig(ParameterType type) {
    if (type == ParameterType::Choice)
        return {type, {"oak", "pine", "birch-2"}};
    return {type, {}};
}
constexpr auto Length = ParameterType::Length;
constexpr auto Area = ParameterType::Area;
constexpr auto Angle = ParameterType::Angle;
constexpr auto Number = ParameterType::Number;
constexpr auto Count = ParameterType::Count;
constexpr auto Boolean = ParameterType::Boolean;
constexpr auto Choice = ParameterType::Choice;

constexpr std::size_t budgetDefault = 100000;

ParameterValue evaluate(std::string_view text, ParameterType type, std::size_t *visits = nullptr) {
    context = text;
    const auto tree = parseParameterExpression(text);
    const auto checked = checkParameterExpression(tree, symbols, sig(type));
    std::vector<ParameterValue> values;
    for (const auto &name : checked.references)
        values.push_back(input(name));
    ParameterEvaluationBudget budget{budgetDefault};
    auto value = evaluateParameterExpression(checked, values, budget);
    if (visits)
        *visits = budgetDefault - budget.remaining;
    return value;
}
double number(std::string_view text, ParameterType type = Number) {
    return std::get<ParameterQuantity>(evaluate(text, type)).value;
}
bool truth(std::string_view text) { return std::get<bool>(evaluate(text, Boolean)); }
std::string hex(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%016" PRIx64, std::bit_cast<std::uint64_t>(value));
    return buffer;
}
bool same(double a, double b) { return hex(a) == hex(b); }

// Returns the error thrown by any stage, or nullopt when the expression evaluates.
std::optional<ParameterExpressionError> failure(std::string_view text, ParameterType type = Number,
                                                std::size_t budget = budgetDefault) {
    try {
        const auto tree = parseParameterExpression(text);
        const auto checked = checkParameterExpression(tree, symbols, sig(type));
        std::vector<ParameterValue> values;
        for (const auto &name : checked.references)
            values.push_back(input(name));
        ParameterEvaluationBudget remaining{budget};
        evaluateParameterExpression(checked, values, remaining);
    } catch (const ParameterExpressionError &error) {
        return error;
    }
    return std::nullopt;
}
void expectError(std::string_view text, ParameterErrorCode code, std::size_t offset,
                 ParameterType type = Number) {
    const auto error = failure(text, type);
    const std::string label = "'" + std::string(text) + "' -> " +
                              std::string(parameterErrorCodeName(code)) + " at " +
                              std::to_string(offset);
    if (!error) {
        check(false, label + " but it was accepted");
        return;
    }
    check(error->code() == code && error->offset() == offset, label + " but got " + error->what());
    check(error->excerpt().size() <= 64, label + ": excerpt is at most 64 bytes");
    check(std::strlen(error->what()) <= 256, label + ": message is at most 256 bytes");
    check(std::string(error->what()).starts_with(parameterErrorCodeName(code)),
          label + ": message starts with the code");
}
void expectOk(std::string_view text, ParameterType type = Number) {
    const auto error = failure(text, type);
    check(!error, "'" + std::string(text) + "' is accepted" +
                      (error ? std::string(": ") + error->what() : std::string()));
}
std::string canonical(std::string_view text) {
    return canonicalParameterExpression(parseParameterExpression(text));
}

// ---------------------------------------------------------------- productions
void productions() {
    // disjunction, conjunction, negation
    check(truth("open or false"), "or");
    check(!truth("false or false or false"), "or chain");
    check(truth("false or false or open"), "or chain true");
    check(truth("open and true"), "and");
    check(!truth("open and true and false"), "and chain");
    check(!truth("not open"), "not");
    check(truth("not not open"), "not not");
    check(truth("not false"), "not literal");
    // comparison: every operator, on numbers and quantities
    check(truth("ratio == 1.5") && !truth("ratio != 1.5"), "== !=");
    check(truth("width < 1 m") && !truth("width < 900 mm"), "<");
    check(truth("width <= 900 mm") && !truth("width <= 899 mm"), "<=");
    check(truth("width > 899 mm") && !truth("width > 900 mm"), ">");
    check(truth("width >= 900 mm") && !truth("width >= 901 mm"), ">=");
    check(truth("tilt > 28 deg") && truth("floor_area < 3 m * 1 m"), "angle and area order");
    check(truth("open == true") && truth("open != false"), "boolean equality");
    check(truth("style == \"pine\"") && truth("style != \"oak\""), "choice equality");
    check(truth("\"pine\" == style"), "choice literal on the left");
    check(truth("style == other_style"), "choices with the same options");
    check(truth("(1 < 2) == (2 < 3)"), "boolean equality of comparisons");
    // sum, product, sign, power
    check(number("1 + 2") == 3 && number("5 - 7") == -2, "sum");
    check(number("6 * 7") == 42 && number("1 / 4") == 0.25, "product");
    check(number("-3") == -3 && number("+3") == 3 && number("--3") == 3, "sign");
    check(number("- + - 3") == 3, "sign chain");
    check(number("2 ^ 3") == 8 && number("2^0") == 1 && number("2 ^ 1") == 2, "power");
    check(number("2 ^ -1") == 0.5 && number("2 ^ -2") == 0.25 && number("2^-3") == 0.125,
          "negative power");
    check(number("0 ^ 0") == 1, "zero to the zero is 1 (empty product)");
    // primary: literals in every number spelling
    check(number("5") == 5 && number("5.") == 5 && number(".5") == 0.5 && number("5.25") == 5.25,
          "number spellings");
    check(number("1e3") == 1000 && number("1E3") == 1000 && number("1e+3") == 1000 &&
              number("2.5e-3") == 0.0025 && number("5.e1") == 50 && number(".5E1") == 5,
          "exponent spellings");
    check(number("007") == 7 && number("0") == 0, "leading zeros");
    // primary: units
    check(same(number("900 mm", Length), 0.9) && same(number("900mm", Length), 0.9), "mm");
    check(same(number("90 cm", Length), 0.9), "cm");
    check(same(number("0.9 m", Length), 0.9), "m");
    check(number("1 in", Length) == quantizeParameterLength(0.0254), "in");
    check(number("1 ft", Length) == quantizeParameterLength(0.3048), "ft");
    check(number("1 rad", Angle) == 1, "rad");
    check(number("180 deg", Angle) == quantizeParameterAngle(3.141592653589793), "deg");
    // primary: true, false, pi, choice, name, parentheses
    check(truth("true") && !truth("false"), "boolean literals");
    check(number("pi") == 3.141592653589793, "pi is the nearest double");
    check(std::get<ParameterChoice>(evaluate("\"birch-2\"", Choice)).value == "birch-2",
          "choice literal");
    check(std::get<ParameterChoice>(evaluate("style", Choice)).value == "pine", "choice name");
    check(number("width", Length) == 0.9 && number("((ratio))") == 1.5, "name and parentheses");
    check(number(std::string(32, 'n')) == 2, "a 32-byte name");
    // primary: every function
    check(std::get<ParameterQuantity>(evaluate("if(open, 1 m, 2 m)", Length)).value == 1, "if");
    check(number("if(not open, 1, 2)") == 2, "if else");
    check(number("min(3, 1, 2)") == 1 && number("max(3, 1, 2)") == 3, "min max");
    check(number("min(8, 7, 6, 5, 4, 3, 2, 1)") == 1, "min of 8");
    check(number("clamp(5, 0, 2)") == 2 && number("clamp(-5, 0, 2)") == 0 &&
              number("clamp(1, 0, 2)") == 1 && number("clamp(1, 1, 1)") == 1,
          "clamp");
    check(number("abs(-2.5)") == 2.5 && number("abs(-1 m)", Length) == 1, "abs");
    check(number("floor(2.7)") == 2 && number("floor(-2.5)") == -3, "floor");
    check(number("ceil(2.1)") == 3 && number("ceil(-2.5)") == -2, "ceil");
    check(number("round(2.5)") == 3 && number("round(-2.5)") == -3 && number("round(2.4)") == 2,
          "round half away from zero");
    check(number("mod(7, 3)") == 1 && number("mod(-7, 3)") == -1 && number("mod(7.5, 2)") == 1.5,
          "mod is the truncated remainder");
    check(number("sqrt(16)") == 4 && same(number("sqrt(floor_area)", Length), 1.5), "sqrt");
    check(number("sin(0 rad)") == 0 && number("cos(0 rad)") == 1 && number("tan(0 rad)") == 0,
          "sin cos tan");
    check(number("asin(0)", Angle) == 0 && number("acos(1)", Angle) == 0 &&
              number("atan(0)", Angle) == 0,
          "asin acos atan");
    check(number("atan2(0 m, 1 m)", Angle) == 0 &&
              number("atan2(1, 0)", Angle) == quantizeParameterAngle(1.5707963267948966),
          "atan2");
    check(number("floor(width / 300 mm)") == 3, "floor of a ratio of lengths (0.9 / 0.3 > 3)");
}

// ---------------------------------------------------------------- precedence
void precedence() {
    check(number("-2^2") == -4, "-x^2 is -(x^2)");
    check(number("-ratio^2") == -2.25, "-ratio^2");
    check(number("(-2)^2") == 4, "(-x)^2");
    check(canonical("-ratio^2") == "-ratio ^ 2", "canonical -x^2");
    check(canonical("(-ratio)^2") == "(-ratio) ^ 2", "canonical (-x)^2");
    check(number("2*3+4") == 10 && number("2+3*4") == 14, "* binds tighter than +");
    check(number("2-3-4") == -5 && number("8/4/2") == 1, "left associative");
    check(number("2*-3") == -6 && number("-2*3") == -6, "sign below product");
    check(number("2^2*3") == 12 && number("3*2^2") == 12, "^ binds tighter than *");
    check(!truth("not false and false"), "not binds tighter than and");
    check(truth("false and false or true"), "and binds tighter than or");
    check(truth("true or false and false"), "or is loosest");
    check(truth("1 + 2 < 4"), "comparison is looser than +");
    check(truth("not 1 > 2"), "not is looser than comparison");
    check(truth("not 1 > 2 and 2 > 1"), "not, comparison, and");
    // Comparisons are not associative; ^ does not chain.
    expectError("1 < 2 < 3", ParameterErrorCode::Syntax, 7, Boolean);
    expectError("ratio == 1 == true", ParameterErrorCode::Syntax, 12, Boolean);
    expectError("2 ^ 2 ^ 2", ParameterErrorCode::Syntax, 7);
    // Exponents: 0..3, optionally negated; nothing else.
    expectError("2 ^ 4", ParameterErrorCode::Syntax, 5);
    expectError("2 ^ 2.0", ParameterErrorCode::Syntax, 5);
    expectError("2 ^ 02", ParameterErrorCode::Syntax, 5);
    expectError("2 ^ +2", ParameterErrorCode::Syntax, 5);
    expectError("2 ^ ratio", ParameterErrorCode::Syntax, 5);
    expectError("2 ^ -4", ParameterErrorCode::Syntax, 6);
    expectError("2 ^ 2 m", ParameterErrorCode::Syntax, 7);
    // A unit binds only to the number before it.
    expectError("width m", ParameterErrorCode::Syntax, 7, Length);
    expectError("(1) m", ParameterErrorCode::Syntax, 5, Length);
    expectError("mm", ParameterErrorCode::Syntax, 1, Length);
    check(number("2 m ^ 2", Area) == 4, "a quantity literal as a power base");
}

// ---------------------------------------------------------------- units and types
void typeRules() {
    // Units and dimensions
    expectOk("width * height", Area);
    expectOk("floor_area / width", Length);
    expectOk("width / 1 m", Number);
    expectOk("width ^ 3 / width ^ 3", Number);
    expectOk("width ^ -3 * width ^ 3", Number);
    expectOk("sqrt(width * width)", Length);
    expectOk("tilt * tilt * tilt / (1 rad ^ 3)", Number);
    expectError("width + 1 rad", ParameterErrorCode::Type, 7, Length);
    expectError("width - 2", ParameterErrorCode::Type, 7, Length);
    expectError("width * width * width * width", ParameterErrorCode::Type, 23, Number);
    expectError("1 / width / width / width / width", ParameterErrorCode::Type, 27, Number);
    expectError("floor_area ^ 2", ParameterErrorCode::Type, 12, Number);
    expectError("width ^ -3 / width", ParameterErrorCode::Type, 12, Number);
    expectError("tilt ^ 2 * tilt ^ 2", ParameterErrorCode::Type, 10, Number);
    expectError("sqrt(width)", ParameterErrorCode::Type, 6, Number);
    expectError("sqrt(tilt)", ParameterErrorCode::Type, 6, Number);
    // Equality
    expectError("width == 1 m", ParameterErrorCode::Type, 7, Boolean);
    expectError("tilt != 0 rad", ParameterErrorCode::Type, 6, Boolean);
    expectError("floor_area == floor_area", ParameterErrorCode::Type, 12, Boolean);
    expectError("open == 1", ParameterErrorCode::Type, 6, Boolean);
    expectError("style == 1", ParameterErrorCode::Type, 7, Boolean);
    expectError("style == \"teak\"", ParameterErrorCode::Type, 10, Boolean);
    expectError("\"oak\" == \"oak\"", ParameterErrorCode::Type, 1, Boolean);
    expectError("style == finish", ParameterErrorCode::Type, 7, Boolean);
    expectOk("shelves == 3", Boolean);
    expectOk("ratio != pi", Boolean);
    // Order comparisons need equal dimensions; never booleans or choices
    expectError("width < 1", ParameterErrorCode::Type, 7, Boolean);
    expectError("open < true", ParameterErrorCode::Type, 1, Boolean);
    expectError("style < style", ParameterErrorCode::Type, 1, Boolean);
    // Boolean operators
    expectError("open and 1", ParameterErrorCode::Type, 10, Boolean);
    expectError("1 or open", ParameterErrorCode::Type, 1, Boolean);
    expectError("not ratio", ParameterErrorCode::Type, 5, Boolean);
    expectError("-open", ParameterErrorCode::Type, 2, Number);
    expectError("+style", ParameterErrorCode::Type, 2, Number);
    expectError("open * 2", ParameterErrorCode::Type, 1, Number);
    expectError("2 ^ 2 + open", ParameterErrorCode::Type, 9, Number);
    expectError("open ^ 2", ParameterErrorCode::Type, 1, Number);
    // if
    expectError("if(ratio, 1, 2)", ParameterErrorCode::Type, 4, Number);
    expectError("if(open, 1, open)", ParameterErrorCode::Type, 13, Number);
    expectError("if(open, 1 m, 1)", ParameterErrorCode::Type, 15, Length);
    expectError("if(open, 1, 2, 3)", ParameterErrorCode::Type, 1, Number);
    expectError("if(open, 1)", ParameterErrorCode::Type, 1, Number);
    expectOk("if(open, \"oak\", style)", Choice);
    expectOk("if(open, \"oak\", \"pine\")", Choice);
    expectOk("if(open, \"oak\", \"pine\") == style", Boolean);
    expectOk("if(open, true, false)", Boolean);
    expectError("if(open, \"teak\", \"oak\")", ParameterErrorCode::Type, 10, Choice);
    expectError("if(open, style, finish)", ParameterErrorCode::Type, 17, Choice);
    expectError("if(open, \"oak\", \"pine\") == \"oak\"", ParameterErrorCode::Type, 10, Boolean);
    // Functions: argument types and counts
    expectError("abs(open)", ParameterErrorCode::Type, 5, Number);
    expectError("floor(width)", ParameterErrorCode::Type, 7, Number);
    expectError("ceil(tilt)", ParameterErrorCode::Type, 6, Number);
    expectError("round(floor_area)", ParameterErrorCode::Type, 7, Number);
    expectOk("floor(width / 300 mm)", Count);
    expectError("sin(ratio)", ParameterErrorCode::Type, 5, Number);
    expectError("cos(width)", ParameterErrorCode::Type, 5, Number);
    expectError("tan(open)", ParameterErrorCode::Type, 5, Number);
    expectError("asin(tilt)", ParameterErrorCode::Type, 6, Angle);
    expectError("acos(width)", ParameterErrorCode::Type, 6, Angle);
    expectError("atan(1 m)", ParameterErrorCode::Type, 6, Angle);
    expectError("atan2(width, ratio)", ParameterErrorCode::Type, 1, Angle);
    expectError("atan2(1)", ParameterErrorCode::Type, 1, Angle);
    expectOk("atan2(width, width)", Angle);
    expectOk("atan2(tilt, tilt)", Angle);
    expectError("min(1)", ParameterErrorCode::Type, 1, Number);
    expectError("max(1, 2, 3, 4, 5, 6, 7, 8, 9)", ParameterErrorCode::Type, 1, Number);
    expectError("min(1 m, 1)", ParameterErrorCode::Type, 1, Length);
    expectError("max(open, open)", ParameterErrorCode::Type, 5, Number);
    expectError("clamp(1, 2)", ParameterErrorCode::Type, 1, Number);
    expectError("clamp(1 m, 0, 2 m)", ParameterErrorCode::Type, 1, Length);
    expectError("mod(width, 2)", ParameterErrorCode::Type, 1, Length);
    expectError("mod(1, 2, 3)", ParameterErrorCode::Type, 1, Number);
    expectError("abs(1, 2)", ParameterErrorCode::Type, 1, Number);
    expectError("sqrt(4, 9)", ParameterErrorCode::Type, 1, Number);
    // Result type
    expectError("width", ParameterErrorCode::Type, 1, Area);
    expectError("  width", ParameterErrorCode::Type, 3, Number);
    expectError("open", ParameterErrorCode::Type, 1, Length);
    expectError("1", ParameterErrorCode::Type, 1, Boolean);
    expectError("1 m", ParameterErrorCode::Type, 1, Count);
    expectError("style", ParameterErrorCode::Type, 1, Number);
    expectError("1 > 0", ParameterErrorCode::Type, 1, Choice);
    expectError("finish", ParameterErrorCode::Type, 1, Choice);
    expectError("\"teak\"", ParameterErrorCode::Type, 1, Choice);
    expectOk("tilt", Angle);
    expectOk("floor_area", Area);
    expectOk("shelves", Count);
    expectOk("shelves", Number);
    // Names
    expectError("depth", ParameterErrorCode::Name, 1, Length);
    expectError("width + depth", ParameterErrorCode::Name, 9, Length);
    expectError(std::string(33, 'n'), ParameterErrorCode::Name, 1);
    expectError("1 + " + std::string(33, 'n'), ParameterErrorCode::Name, 5);
    expectError("system(1)", ParameterErrorCode::Name, 1);
    // References are distinct names in first-use order with their declared types.
    const auto checked = checkParameterExpression(
        parseParameterExpression("ratio * width / width * 1 m + shelves * 1 m"), symbols,
        sig(Length));
    check(checked.references == std::vector<std::string>{"ratio", "width", "shelves"},
          "references in first-use order");
    check(checked.referenceTypes.size() == 3 &&
              checked.referenceTypes[1].type == ParameterType::Length,
          "reference types");
    // Evaluation refuses values that do not match the declared types.
    bool refused = false;
    try {
        std::vector<ParameterValue> values{ParameterQuantity{1, {}}, ParameterQuantity{1, {}},
                                           ParameterQuantity{1, {}}};
        ParameterEvaluationBudget budget{100};
        evaluateParameterExpression(checked, values, budget);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "a length reference refuses a dimensionless value");
}

// ---------------------------------------------------------------- domain
void domain() {
    using C = ParameterErrorCode;
    expectError("ratio / 0", C::Domain, 7);
    expectError("1 / (ratio - 1.5)", C::Domain, 3);
    expectError("width / zero", C::Domain, 7, Length);
    expectError("mod(ratio, 0)", C::Domain, 1);
    expectError("1 + mod(1 m, 0 m) / 1 m", C::Domain, 5);
    expectError("sqrt(-ratio)", C::Domain, 1);
    expectError("2 * sqrt(0 - 1)", C::Domain, 5);
    expectOk("sqrt(0)");
    expectError("asin(2)", C::Domain, 1, Angle);
    expectError("acos(-1.5)", C::Domain, 1, Angle);
    expectOk("asin(-1) + acos(1)", Angle);
    expectError("1e308 * 10", C::Domain, 7);
    expectError("-1e308 - 1e308", C::Domain, 8);
    expectError("1e200 ^ 2", C::Domain, 7);
    expectError("1e-200 ^ -2", C::Domain, 8);
    expectError("0 ^ -1", C::Domain, 3);
    expectError("zero ^ -3", C::Domain, 6);
    expectError("1e400", C::Domain, 1);
    expectError("2 + 1e309 m", C::Domain, 5, Length);
    expectError("1e307 ft", C::Domain, 1, Length);
    expectError("1e307 in", C::Domain, 1, Length);
    expectError("clamp(1, 2, 1)", C::Domain, 1);
    expectError("3 * clamp(width, 2 m, 1 m) / 1 m", C::Domain, 5);
    expectOk("clamp(1, 1, 1)");
    // Final values: count whole and in range, type ranges
    expectError("ratio", C::Domain, 1, Count);
    expectError("  shelves / 2", C::Domain, 3, Count);
    expectOk("round(ratio)", Count);
    check(number("round(ratio)", Count) == 2, "round(1.5) is 2");
    expectError("-1", C::Domain, 1, Count);
    expectError("10001", C::Domain, 1, Count);
    expectOk("10000", Count);
    expectOk("0", Count);
    expectError("1000001 m", C::Domain, 1, Length);
    expectError("-1e6 m - 1 mm", C::Domain, 1, Length);
    expectOk("1e6 m", Length);
    expectOk("-1e6 m", Length);
    expectError("1e12 m * 2 m", C::Domain, 1, Area);
    expectOk("1e6 m * 1e6 m", Area);
    expectError("1e4 rad + 1 rad", C::Domain, 1, Angle);
    expectOk("1e4 rad", Angle);
    expectError("2e15", C::Domain, 1, Number);
    expectOk("1e15", Number);
    // Only the chosen branch and the needed operands are evaluated.
    expectOk("if(open, 1, 1 / 0)");
    expectOk("if(not open, sqrt(-1), 2)");
    expectError("if(open, 1 / 0, 1)", C::Domain, 12);
    expectOk("open or 1 / 0 > 1", Boolean);
    expectOk("not open and 1 / 0 > 1", Boolean);
    expectError("open and 1 / 0 > 1", C::Domain, 12, Boolean);
    expectError("not open or 1 / 0 > 1", C::Domain, 15, Boolean);
    // -0 never reaches a final value.
    check(hex(number("-0 * 1 m", Length)) == "0000000000000000", "-0 length becomes +0");
    check(hex(number("-0")) == "0000000000000000", "-0 number becomes +0");
    check(hex(number("-0", Count)) == "0000000000000000", "-0 count becomes +0");
}

// ---------------------------------------------------------------- canonical text
const std::vector<std::pair<std::string, std::string>> canonicalCases{
    {"900mm", "900 mm"},
    {"width-60mm", "width - 60 mm"},
    {"((width))", "width"},
    {"ratio-(ratio-ratio)", "ratio - (ratio - ratio)"},
    {"(ratio-ratio)-ratio", "ratio - ratio - ratio"},
    {"ratio/(ratio*ratio)", "ratio / (ratio * ratio)"},
    {"(ratio/ratio)*ratio", "ratio / ratio * ratio"},
    {"ratio*(ratio+1)", "ratio * (ratio + 1)"},
    {"max( shelves ,1 )", "max(shelves, 1)"},
    {"\tmin(1,\t2 , 3)", "min(1, 2, 3)"},
    {"-(1+2)", "-(1 + 2)"},
    {"- -1", "--1"},
    {"-(-1)", "--1"},
    {"+1", "+1"},
    {"-+1", "-+1"},
    {"2*-3", "2 * -3"},
    {"2*(-3)", "2 * -3"},
    {"(2*3)^2", "(2 * 3) ^ 2"},
    {"(2^2)", "2 ^ 2"},
    {"(-2)^-2", "(-2) ^ -2"},
    {"2m^2", "2 m ^ 2"},
    {"ratio^-0", "ratio ^ 0"},
    {".5", "0.5"},
    {"5.", "5"},
    {"1e3", "1000"},
    {"1E-7", "1e-07"},
    {"1e22", "1e+22"},
    {"007", "7"},
    {"0.10000000000000000000000000001", "0.1"},
    {"1.5E1deg", "15 deg"},
    {"not(open)", "not open"},
    {"not(not(open))", "not not open"},
    {"(not open) and open", "not open and open"},
    {"not (open and open)", "not (open and open)"},
    {"(open or open) and open", "(open or open) and open"},
    {"open or (open and open)", "open or open and open"},
    {"open and (open or open)", "open and (open or open)"},
    {"(open and open) and open", "open and open and open"},
    {"open and (open and open)", "open and (open and open)"},
    {"open or (open or open)", "open or (open or open)"},
    {"1+2<4", "1 + 2 < 4"},
    {"(1+2)<(4)", "1 + 2 < 4"},
    {"(1<2)==(2<3)", "(1 < 2) == (2 < 3)"},
    {"not(1<2)", "not 1 < 2"},
    {"(not open)==open", "(not open) == open"},
    {"style==\"oak\"", "style == \"oak\""},
    {"if(open,1mm,2in)", "if(open, 1 mm, 2 in)"},
    {"atan2((width),(width))", "atan2(width, width)"},
    {"abs((-1))", "abs(-1)"},
    {"(abs(1))^2", "abs(1) ^ 2"},
    {"(pi)^2", "pi ^ 2"},
    {"(true)", "true"},
    {"(\"oak\")==style", "\"oak\" == style"},
    {"ratio>=1 and ratio<=2 or not open", "ratio >= 1 and ratio <= 2 or not open"},
};

void canonicalText() {
    for (const auto &[input, expected] : canonicalCases) {
        context = input;
        std::string got;
        try {
            got = canonical(input);
        } catch (const std::exception &error) {
            got = std::string("error: ") + error.what();
        }
        check(got == expected,
              "canonical '" + input + "' is '" + expected + "', got '" + got + "'");
    }
    context.clear();
}

// canonical(parse(canonical(t))) == canonical(t), the same tree, the same value.
void roundTrip(const std::string &text) {
    context = text;
    ParameterExpression tree;
    try {
        tree = parseParameterExpression(text);
    } catch (const ParameterExpressionError &) {
        context.clear();
        return; // rejected inputs have no canonical text
    }
    const auto first = canonicalParameterExpression(tree);
    const auto again = parseParameterExpression(first);
    check(canonicalParameterExpression(again) == first, "canonical text is a fixed point");
    check(sameParameterTree(tree, again), "canonical text re-parses to the same tree");
    check(again.tokenCount <= tree.tokenCount && again.maxDepth <= tree.maxDepth,
          "canonical text adds no tokens or nesting");
    for (const auto type : {Length, Area, Angle, Number, Count, Boolean, Choice}) {
        const auto a = failure(text, type);
        const auto b = failure(first, type);
        check(a.has_value() == b.has_value(), "canonical text is accepted the same way");
        if (a && b)
            check(a->code() == b->code(), "canonical text fails the same way");
        if (!a && !b) {
            const auto x = evaluate(text, type);
            const auto y = evaluate(first, type);
            if (const auto *q = std::get_if<ParameterQuantity>(&x))
                check(same(q->value, std::get<ParameterQuantity>(y).value),
                      "canonical text evaluates bit-identically");
            else
                check(x == y, "canonical text evaluates identically");
        }
    }
    context.clear();
}

// ---------------------------------------------------------------- limits
std::string repeat(const std::string &unit, std::size_t count, const std::string &separator) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i)
        out += (i ? separator : "") + unit;
    return out;
}

void limits() {
    using C = ParameterErrorCode;
    // Text: 1,024 bytes. 1,025 is refused before tokenizing, even if it is garbage.
    const auto atText = "1" + std::string(1023, ' ');
    check(atText.size() == 1024, "1,024-byte input");
    expectOk(atText);
    expectError(atText + " ", C::LimitExceeded, 1025);
    expectError(std::string(1025, '$'), C::LimitExceeded, 1025);
    expectError(std::string(4096, '('), C::LimitExceeded, 1025);
    // Canonical text must fit as well (stricter reading; see parseParameterExpression).
    // "shelves+..." gains two spaces per operator.
    const auto tight = repeat("shelves", 128, "+");
    check(tight.size() == 1023, "a 1,023-byte spelling");
    expectError(tight, C::LimitExceeded, 1);
    expectOk(repeat("shelves", 102, "+")); // canonical 1,017 bytes
    check(canonical(repeat("shelves", 102, "+")).size() == 1017, "canonical just under");
    expectError(repeat("shelves", 103, "+"), C::LimitExceeded, 1); // canonical 1,027 bytes

    // Tokens: 512. Units add tokens without nodes.
    const auto angles = repeat("(1rad)", 102, "+");
    const auto at512 = "---" + angles;
    const auto at513 = "----" + angles;
    const auto tree512 = parseParameterExpression(at512);
    check(tree512.tokenCount == 512, "512 tokens, got " + std::to_string(tree512.tokenCount));
    check(tree512.nodes.size() <= 256, "within the node limit");
    expectOk(at512, Angle);
    expectError(at513, C::LimitExceeded, at513.size(), Angle); // the 513th token is the last
    // The token limit is checked before the token: garbage after token 512 is not seen.
    expectError(at512 + "+$", C::LimitExceeded, at512.size() + 1, Angle);
    // ...and a syntax error before token 513 wins.
    expectError("$" + at513, C::Syntax, 1, Angle);

    // Nodes: 256.
    const auto ones = repeat("1", 128, "+"); // 255 nodes
    const auto tree255 = parseParameterExpression(ones);
    check(tree255.nodes.size() == 255, "255 nodes");
    const auto at256 = "-" + ones;
    check(parseParameterExpression(at256).nodes.size() == 256, "256 nodes");
    expectOk(at256);
    check(number(at256) == 126, "256-node value");
    const auto at257 = "--" + ones;
    expectError(at257, C::LimitExceeded, at257.size() - 1); // the 257th node is the last '+'
    expectError(ones + "+1", C::LimitExceeded, ones.size() + 1);

    // Depth: 32, checked before descending (parentheses, calls, unary operators).
    const auto parens = [](std::size_t n, const std::string &inner) {
        return std::string(n, '(') + inner + std::string(n, ')');
    };
    expectOk(parens(32, "1"));
    check(parseParameterExpression(parens(32, "1")).maxDepth == 32, "depth 32");
    expectError(parens(33, "1"), C::LimitExceeded, 33);
    expectError(std::string(33, '(') + "$$$", C::LimitExceeded, 33);
    expectOk(std::string(32, '-') + "1");
    expectError(std::string(33, '-') + "1", C::LimitExceeded, 33);
    expectOk(repeat("not ", 32, "") + "open", Boolean);
    expectError(repeat("not ", 33, "") + "open", C::LimitExceeded, 129, Boolean);
    std::string calls = "1";
    for (int i = 0; i < 32; ++i)
        calls = "abs(" + calls + ")";
    expectOk(calls);
    expectError("abs(" + calls + ")", C::LimitExceeded, 4 * 32 + 4);
    expectOk(std::string(16, '-') + parens(16, "1"));
    expectError(std::string(16, '-') + parens(17, "1"), C::LimitExceeded, 33);
    // Binary chains do not nest: 128 operands at depth 0.
    check(tree255.maxDepth == 0, "binary chains do not add depth");

    // Number literal: 32 characters (PARAMETER_SYNTAX), before conversion.
    const auto literal32 = "1." + std::string(30, '0');
    check(literal32.size() == 32, "32-character literal");
    expectOk(literal32);
    expectError("2 + " + literal32 + "0", C::Syntax, 5);
    expectError("1e" + std::string(31, '9'), C::Syntax, 1);
    expectOk("1e" + std::string(29, '0') + "1");

    // Name: 32 bytes (PARAMETER_NAME); choice value: 32 characters (PARAMETER_SYNTAX).
    expectOk(std::string(32, 'n'));
    expectError(std::string(33, 'n'), C::Name, 1);
    expectError("style == \"" + std::string(32, 'a') + "\"", C::Type, 10, Boolean);
    expectError("style == \"" + std::string(33, 'a') + "\"", C::Syntax, 10, Boolean);

    // Evaluation node visits: the caller's budget, checked before each visit.
    std::size_t visits = 0;
    evaluate("ratio + 2 * 3", Number, &visits);
    check(visits == 5, "five node visits");
    {
        const auto tree = parseParameterExpression("ratio + 2 * 3");
        const auto checked = checkParameterExpression(tree, symbols, sig(Number));
        const std::vector<ParameterValue> values{input("ratio")};
        ParameterEvaluationBudget exact{5};
        check(std::get<ParameterQuantity>(evaluateParameterExpression(checked, values, exact))
                          .value == 7.5 &&
                  exact.remaining == 0,
              "a budget of exactly the visits is enough");
        ParameterEvaluationBudget shortBudget{4};
        try {
            evaluateParameterExpression(checked, values, shortBudget);
            check(false, "a budget one short is refused");
        } catch (const ParameterExpressionError &error) {
            check(error.code() == C::LimitExceeded && error.offset() == 7,
                  std::string("budget refusal names the unvisited node: ") + error.what());
        }
    }
    evaluate("if(open, 1, 2 + 3 + 4)", Number, &visits);
    check(visits == 3, "an untaken branch costs nothing, got " + std::to_string(visits));
    evaluate("open or 1 + 2 > 0", Boolean, &visits);
    check(visits == 2, "a short-circuited operand costs nothing, got " + std::to_string(visits));
    evaluate(at256, Number, &visits);
    check(visits == 256, "at most one visit per node");

    // Value stack: at most 256. Values are pushed only by leaves (at most 256),
    // so the bound is structural; the checker records the static maximum.
    const auto rightNested = [] {
        std::string text = "1";
        for (int i = 0; i < 31; ++i)
            text = "1 + (" + text + ")";
        return text;
    }();
    const auto deep =
        checkParameterExpression(parseParameterExpression(rightNested), symbols, sig(Number));
    check(deep.maxStack == 32,
          "right-nested sum needs 32 stack entries, got " + std::to_string(deep.maxStack));
    const auto wide = checkParameterExpression(
        parseParameterExpression("max(" + repeat("min(1, 2, 3, 4, 5, 6, 7, 8)", 8, ", ") + ")"),
        symbols, sig(Number));
    check(wide.maxStack == 15,
          "nested 8-argument calls need 15 entries, got " + std::to_string(wide.maxStack));
    check(checkParameterExpression(tree255, symbols, sig(Number)).maxStack == 2,
          "a left-associative chain needs 2 entries");
}

// ---------------------------------------------------------------- golden values
struct Golden {
    const char *text;
    ParameterType type;
    const char *hex;
};
// Bit patterns of the final values (quantized for length, area and angle).
// Identical in dev and sanitize; regenerate with --print-golden only on purpose.
const Golden golden[] = {
    {"900 mm", Length, "3feccccccccccccd"},
    {"1 in", Length, "3f9a027525460aa7"},
    {"1 ft", Length, "3fd381d7dbf487fd"},
    {"1 cm", Length, "3f847ae147ae147b"},
    {"36 in", Length, "3fed42c3c9eecbfc"},
    {"3 ft + 1 in", Length, "3fee12d77318fc51"},
    {"180 deg", Angle, "400921fb54442eea"},
    {"45 deg", Angle, "3fe921fb54441d52"},
    {"1e6 m", Length, "412e848000000001"},
    {"width - 60 mm", Length, "3feae147ae147ae2"},
    {"width * height", Area, "3fe5999999999999"},
    {"0.1 + 0.2", Number, "3fd3333333333334"},
    {"1 / 3", Number, "3fd5555555555555"},
    {"pi * 2", Number, "401921fb54442d18"},
    {"ratio ^ 3", Number, "400b000000000000"},
    {"ratio ^ -3", Number, "3fd2f684bda12f68"},
    {"sqrt(2)", Number, "3ff6a09e667f3bcd"},
    {"mod(1 m, 0.3 m) / 1 m", Number, "3fb999999999999c"},
    {"round(width / 300 mm)", Count, "4008000000000000"},
    {"floor(width / 300 mm)", Count, "4008000000000000"},
    {"clamp(width * 2, 400 mm, 2.4 m)", Length, "3ffccccccccccccd"},
    {"min(width, height) + max(width, height)", Length, "3ffa666666666667"},
    {"0.000123456789123 m", Length, "3f202e878e0f88f1"},
    {"0.0000000001 m", Length, "0000000000000000"},
    {"1e-13 m * 1 m", Area, "0000000000000000"},
    {"1 rad / 3", Angle, "3fd5555555553de0"},
};

void goldenValues(bool print) {
    for (const auto &g : golden) {
        context = g.text;
        double value{};
        try {
            value = number(g.text, g.type);
        } catch (const std::exception &error) {
            check(false, std::string("golden expression failed: ") + error.what());
            continue;
        }
        if (print)
            std::cout << "    {\"" << g.text << "\", " << parameterTypeName(g.type) << ", \""
                      << hex(value) << "\"},\n";
        check(hex(value) == g.hex,
              std::string("golden ") + g.text + " is " + g.hex + ", got " + hex(value));
    }
    context.clear();
}

// ---------------------------------------------------------------- grammar closure
void grammarClosure() {
    const char *const hostile[] = {
        "$(rm -rf /)",
        "`ls`",
        "width; rm -rf /",
        "width;",
        "__proto__",
        "constructor",
        "prototype",
        "import",
        "import os",
        "system(\"ls\")",
        "eval(width)",
        "exec(1)",
        "open(\"/etc/passwd\")",
        "require(\"fs\")",
        "/bin/sh",
        "../../etc/passwd",
        "C:\\Windows",
        "width && 1",
        "width || 1",
        "a = 1",
        "x.y",
        "Math.PI",
        "${width}",
        "#",
        "width // comment",
        "/* c */ 1",
        "'1'",
        "1\n",
        "1\r",
        "\x01",
        "1 # x",
        "true;false",
        "NOT open",
        "IF(open, 1, 2)",
        "Width",
        "width.length",
        "width[0]",
        "{}",
        "[]",
        "@width",
        "width ? 1 : 2",
        "1 % 2",
        "~1",
        "\"oak\\\"",
        "\"oak",
        "\"Oak\"",
        "\"\"",
        "f()",
        "abs()",
        "abs(1,)",
        "abs 1",
        "min(,1)",
        "1 2",
        "1..2",
        "1.2.3",
        "0x10",
        "1e",
        "1e+",
        "e1",
        "\xc3\xa9",
        "width\xe2\x80\x8b",
        "pi(1)",
        "true(1)",
        "sin",
        "if",
        "and",
        "or open",
        "not",
        "open and",
        "",
        " ",
        "\t",
        "()",
        "(1",
        "1)",
        "1 +",
        "1 ==",
        "== 1",
        "= 1",
        "!1",
        "1 !== 1",
        "1 <> 1",
        "1 => 1",
        "1 =< 1",
        "\x00"
        "1",
        "fopen(\"x\")",
        "popen(\"x\")",
        "dlopen(1)",
        "socket(1)",
        "__import__(\"os\")",
        "getattr(width, 1)",
        "width.__class__",
    };
    int count = 0;
    for (const auto *text : hostile) {
        std::string input = text;
        if (input.empty() && text[0] == 0 && std::strcmp(text, "") != 0)
            continue;
        ++count;
        const auto error = failure(input, Number);
        context = input;
        if (!error) {
            check(false, "hostile text is refused");
            continue;
        }
        check(error->code() == ParameterErrorCode::Syntax ||
                  error->code() == ParameterErrorCode::Name,
              std::string("hostile text gives PARAMETER_SYNTAX or PARAMETER_NAME, got ") +
                  error->what());
        check(error->offset() >= 1 && error->offset() <= std::max<std::size_t>(1, input.size()),
              "offset lies inside the input");
    }
    // An embedded NUL byte is refused at its offset.
    expectError(std::string("1 \0 1", 5), ParameterErrorCode::Syntax, 3);
    context.clear();
    check(count > 90, "every hostile text was tried");
    // Unknown functions and names are refused at the name.
    expectError("1 + exec(1)", ParameterErrorCode::Name, 5);
    expectError("import", ParameterErrorCode::Name, 1);
    expectError("__proto__", ParameterErrorCode::Syntax, 1);
    expectError("width; rm", ParameterErrorCode::Syntax, 6, Length);
    expectError("`ls`", ParameterErrorCode::Syntax, 1);
    expectError("$(x)", ParameterErrorCode::Syntax, 1);
    expectError("1 +\xc3\xa9", ParameterErrorCode::Syntax, 4);
    expectError("1 +", ParameterErrorCode::Syntax, 3);
    expectError("", ParameterErrorCode::Syntax, 1);
    // Errors quote at most 64 bytes, printable.
    const auto error = failure("1 + " + std::string(200, '$'));
    check(error && error->excerpt() == std::string(64, '$'), "64-byte excerpt");
    const auto binary = failure("1 + \x01\x02");
    check(binary && binary->excerpt() == "??", "unprintable bytes are replaced");
}

// ---------------------------------------------------------------- helpers
void helpers() {
    check(isValidParameterName("width") && isValidParameterName("a") &&
              isValidParameterName("leg_x2") && isValidParameterName(std::string(32, 'a')),
          "valid names");
    check(!isValidParameterName("") && !isValidParameterName(std::string(33, 'a')) &&
              !isValidParameterName("Width") && !isValidParameterName("_x") &&
              !isValidParameterName("2x") && !isValidParameterName("a-b"),
          "malformed names");
    for (const auto *word :
         {"or",   "and",   "not",   "true",  "false", "pi",   "if",  "min", "max", "clamp",
          "abs",  "floor", "ceil",  "round", "mod",   "sqrt", "sin", "cos", "tan", "asin",
          "acos", "atan",  "atan2", "mm",    "cm",    "m",    "in",  "ft",  "deg", "rad"}) {
        check(isReservedParameterWord(word) && !isValidParameterName(word),
              std::string("reserved: ") + word);
    }
    check(!isReservedParameterWord("e") && !isReservedParameterWord("width"), "not reserved");
    check(isValidParameterChoiceValue("birch-2") && isValidParameterChoiceValue("a") &&
              isValidParameterChoiceValue(std::string(32, 'z')),
          "valid choice values");
    check(!isValidParameterChoiceValue("") && !isValidParameterChoiceValue("Oak") &&
              !isValidParameterChoiceValue(std::string(33, 'z')) &&
              !isValidParameterChoiceValue("a b"),
          "invalid choice values");
    // Quantization: nearbyint(v / q) * q, ties to even, -0 becomes +0.
    check(same(quantizeParameterLength(0.9), 0.9), "0.9 m is stable");
    check(same(quantizeParameterLength(0.9 + 1e-12), quantizeParameterLength(0.9)),
          "sub-quantum differences share one value");
    check(same(quantizeParameterLength(-1e-12), 0.0), "-0 length is +0");
    check(same(quantizeParameterArea(-1e-14), 0.0), "-0 area is +0");
    check(same(quantizeParameterAngle(-1e-14), 0.0), "-0 angle is +0");
    check(same(quantizeParameterLength(0.5e-9), 0.0) &&
              same(quantizeParameterLength(1.5e-9), quantizeParameterLength(2e-9)),
          "ties to even");
    check(parameterErrorCodeName(ParameterErrorCode::Syntax) == "PARAMETER_SYNTAX" &&
              parameterErrorCodeName(ParameterErrorCode::Name) == "PARAMETER_NAME" &&
              parameterErrorCodeName(ParameterErrorCode::Type) == "PARAMETER_TYPE" &&
              parameterErrorCodeName(ParameterErrorCode::Domain) == "PARAMETER_DOMAIN" &&
              parameterErrorCodeName(ParameterErrorCode::LimitExceeded) == "LIMIT_EXCEEDED",
          "error code names");
    // Program shape: loop-free, every jump forward.
    const auto checked = checkParameterExpression(
        parseParameterExpression("if(open and not open or true, 1, 2)"), symbols, sig(Number));
    bool forward = true;
    for (std::size_t pc = 0; pc < checked.program.size(); ++pc)
        if (checked.program[pc].instruction != ParameterInstruction::Node)
            forward = forward && checked.program[pc].target > pc;
    check(forward, "every jump is forward");
}

} // namespace

int main(int argc, char **argv) {
    const bool print = argc > 1 && std::strcmp(argv[1], "--print-golden") == 0;
    try {
        productions();
        precedence();
        typeRules();
        domain();
        canonicalText();
        limits();
        goldenValues(print);
        grammarClosure();
        helpers();
        // Round trip over every expression used above.
        for (const auto &[input, expected] : canonicalCases)
            roundTrip(input);
        for (const auto &g : golden)
            roundTrip(g.text);
        for (const auto *text :
             {"-2^2", "-ratio^2", "(-2)^2", "2*-3", "not 1 > 2 and 2 > 1", "ratio ^ -3",
              "if(open, \"oak\", style) == other_style", "sqrt(floor_area) / (1 m)",
              "atan2(1 m, 2 m) + 15 deg", "mod(-7, 3)", "round(-2.5)", "ceil(.5E1)", "1e-7 m",
              "1e22", ".000001"})
            roundTrip(text);
    } catch (const std::exception &error) {
        std::cerr << "Unexpected exception: " << error.what()
                  << (context.empty() ? "" : " [" + context + "]") << '\n';
        return 1;
    }
    std::cout << "{\"checks\":" << checks << ",\"failures\":" << failures << "}\n";
    return failures == 0 ? 0 : 1;
}
