#pragma once
// R093.a: the parameter expression engine (ADR 0165 §2, §3 limits, §7).
//
// Formulas are data. This library is a closed grammar, a fixed operator and
// function table, a static type and unit checker, a canonical printer and an
// iterative evaluator over a loop-free instruction array. It depends only on the
// C++ standard library and libm: no Qt, no I/O, no host calls. It is compiled
// with -ffp-contract=off and never with fast-math options.
//
// Pipeline: parseParameterExpression(text) -> checkParameterExpression(tree,
// symbols, expected) -> evaluateParameterExpression(checked, values, budget).
// canonicalParameterExpression(tree) prints the canonical text.
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sketchy {

// ---- Limits (ADR 0165 §3). Each is checked before the work it bounds. ----
struct ParameterExpressionLimits {
    static constexpr std::size_t textBytes = 1024;   // LIMIT_EXCEEDED, before lexing
    static constexpr std::size_t tokens = 512;       // LIMIT_EXCEEDED, before the 513th token
    static constexpr std::size_t nodes = 256;        // LIMIT_EXCEEDED, before the 257th node
    static constexpr std::size_t depth = 32;         // LIMIT_EXCEEDED, before descending
    static constexpr std::size_t numberLiteral = 32; // PARAMETER_SYNTAX, before conversion
    static constexpr std::size_t valueStack = 256;   // evaluator value stack entries
    static constexpr std::size_t nameBytes = 32;     // [a-z][a-z0-9_]{0,31}
    static constexpr std::size_t choiceBytes = 32;   // [a-z0-9_-]{1,32}
    static constexpr std::size_t excerptBytes = 64;  // error excerpt
    static constexpr std::size_t messageBytes = 256; // error message (D06)
    static constexpr int maxExponent = 3;            // dimension exponents in -3..3
};

// ---- Errors ----
enum class ParameterErrorCode : std::uint8_t {
    Syntax,        // PARAMETER_SYNTAX
    Name,          // PARAMETER_NAME
    Type,          // PARAMETER_TYPE
    Domain,        // PARAMETER_DOMAIN
    LimitExceeded, // LIMIT_EXCEEDED
};
std::string_view parameterErrorCodeName(ParameterErrorCode code);

// Every rejection carries its code, a 1-based byte offset into the expression
// text and an excerpt of at most 64 bytes starting at that offset (bytes outside
// printable ASCII replaced by '?'). The offset always lies inside the input:
// 1 <= offset <= max(1, text size); an error at the end of the input names its
// last byte. what() is at most 256 bytes and quotes at most 64 bytes of input.
class ParameterExpressionError : public std::runtime_error {
  public:
    ParameterExpressionError(ParameterErrorCode code, std::size_t offset, std::string excerpt,
                             const std::string &message);
    ParameterErrorCode code() const noexcept { return code_; }
    std::size_t offset() const noexcept { return offset_; }
    const std::string &excerpt() const noexcept { return excerpt_; }

  private:
    ParameterErrorCode code_;
    std::size_t offset_;
    std::string excerpt_;
};

// ---- Types, dimensions and values ----
enum class ParameterType : std::uint8_t { Length, Area, Angle, Number, Count, Boolean, Choice };
std::string_view parameterTypeName(ParameterType type);

// A quantity's dimension: exponents of length (L) and angle (A), each in -3..3.
struct ParameterDimension {
    int length{};
    int angle{};
    friend bool operator==(const ParameterDimension &, const ParameterDimension &) = default;
};

// The declared type of a parameter (or the expected type of an expression).
// `options` holds the choice values of a Choice type and is empty otherwise.
struct ParameterSignature {
    ParameterType type{ParameterType::Number};
    std::vector<std::string> options;
};

struct ParameterQuantity {
    double value{};
    ParameterDimension dimension;
    friend bool operator==(const ParameterQuantity &, const ParameterQuantity &) = default;
};
struct ParameterChoice {
    std::string value;
    friend bool operator==(const ParameterChoice &, const ParameterChoice &) = default;
};
using ParameterValue = std::variant<bool, ParameterChoice, ParameterQuantity>;

// The symbol table the caller supplies: the definition's parameters by name.
class ParameterSymbols {
  public:
    virtual ~ParameterSymbols() = default;
    // nullptr when no parameter has this name.
    virtual const ParameterSignature *find(std::string_view name) const = 0;
};

// ---- Syntax tree ----
enum class ParameterOp : std::uint8_t {
    Number, // literal quantity (number and optional unit)
    Choice, // "value"
    True,
    False,
    Pi,
    Name,
    Negate, // unary -
    Plus,   // unary +
    Not,
    Or,
    And,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Add,
    Subtract,
    Multiply,
    Divide,
    Power, // base ^ exponent (exponent is an immediate in -3..3)
    Call,
};
enum class ParameterFunction : std::uint8_t {
    None,
    If,
    Min,
    Max,
    Clamp,
    Abs,
    Floor,
    Ceil,
    Round,
    Mod,
    Sqrt,
    Sin,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Atan2,
};
enum class ParameterUnit : std::uint8_t { None, Mm, Cm, M, In, Ft, Deg, Rad };

// One node of the post-order node array. A node's children are the
// `argumentCount` subtrees immediately before it; `subtreeSize` counts the node
// and all its descendants.
struct ParameterNode {
    ParameterOp op{};
    ParameterFunction function{ParameterFunction::None};
    ParameterUnit unit{ParameterUnit::None};
    std::int8_t exponent{};        // Power only
    std::uint16_t argumentCount{}; // number of children
    std::uint16_t subtreeSize{1};
    double literal{};       // Number: the value as written (before unit conversion)
    double si{};            // Number: the value in SI units
    std::string text;       // Name: the name; Choice: the value (no quotes)
    std::uint32_t offset{}; // 1-based offset of the node's own token
    std::uint32_t start{};  // 1-based offset of the subexpression's first token
};

// The parsed (not yet checked) expression.
struct ParameterExpression {
    std::string source;
    std::vector<ParameterNode> nodes; // post-order; the root is nodes.back()
    std::size_t tokenCount{};
    std::size_t maxDepth{};
};

// Same operators, functions, literals, names and shape; offsets are ignored.
bool sameParameterTree(const ParameterExpression &a, const ParameterExpression &b);

// ---- Parse ----
// Throws ParameterExpressionError (PARAMETER_SYNTAX, PARAMETER_NAME for a
// malformed name or an unknown function, PARAMETER_DOMAIN for a literal that is
// not finite, LIMIT_EXCEEDED for the text, token, node and depth limits).
ParameterExpression parseParameterExpression(std::string_view text);

// Canonical text (ADR 0165 §2): single spaces around binary operators and after
// commas, `<number> <unit>` with the shortest round-trip number and the
// author's unit, the minimum parentheses, lowercase keywords.
std::string canonicalParameterExpression(const ParameterExpression &expression);

// Reserved words: keywords, function names, unit names and `pi`.
bool isReservedParameterWord(std::string_view word);
// [a-z][a-z0-9_]{0,31} and not reserved.
bool isValidParameterName(std::string_view name);
// [a-z0-9_-]{1,32}
bool isValidParameterChoiceValue(std::string_view value);

// ---- Check ----
enum class ParameterInstruction : std::uint8_t {
    Node,            // evaluate tree node `node` (pops its operands, pushes one value)
    JumpIfFalse,     // pop a boolean; jump to `target` when false
    Jump,            // jump to `target`
    AndShortCircuit, // top false: keep it and jump to `target`; else pop
    OrShortCircuit,  // top true: keep it and jump to `target`; else pop
};
struct ParameterStep {
    ParameterInstruction instruction{};
    std::uint32_t node{};   // Node
    std::uint32_t target{}; // jumps (always forward)
};

struct CheckedParameterExpression {
    ParameterExpression expression;
    ParameterSignature result;           // the expected type it was checked against
    std::vector<std::string> references; // distinct names in first-use order
    std::vector<ParameterSignature> referenceTypes;
    std::vector<std::uint32_t> referenceSlots; // per node: reference index for Name nodes
    std::vector<ParameterStep> program;        // loop-free: every jump is forward
    std::size_t maxStack{};                    // static bound on the value stack
};

// Checks every name against `symbols` and every operator, function and the
// result against ADR 0165 §2. Throws PARAMETER_NAME (unknown name) or
// PARAMETER_TYPE.
CheckedParameterExpression checkParameterExpression(const ParameterExpression &expression,
                                                    const ParameterSymbols &symbols,
                                                    const ParameterSignature &expected);

// ---- Evaluate ----
// Node visits left for the caller's evaluation (ADR 0165 §3: 200,000 per
// definition, 20,000,000 per document). Every evaluated tree node costs one
// visit, checked before the node is evaluated; a skipped `if` branch or a
// short-circuited operand costs nothing. Running out throws LIMIT_EXCEEDED.
struct ParameterEvaluationBudget {
    std::size_t remaining{};
};

// `values` is aligned with `checked.references`. Each must match its declared
// type (std::invalid_argument otherwise: a caller error, not a formula error).
// The result is finalized for `checked.result.type`: range-checked, a count
// checked to be whole, a length, area or angle quantized. Throws
// PARAMETER_DOMAIN or LIMIT_EXCEEDED.
ParameterValue evaluateParameterExpression(const CheckedParameterExpression &checked,
                                           std::span<const ParameterValue> values,
                                           ParameterEvaluationBudget &budget);

// ---- Final values (ADR 0165 §1, §2) ----
inline constexpr double parameterLengthQuantum = 1e-9; // metres
inline constexpr double parameterAreaQuantum = 1e-12;  // square metres
inline constexpr double parameterAngleQuantum = 1e-12; // radians
// std::nearbyint(v / q) * q, with -0 normalized to +0 so that values differing
// only in sign of zero share one variant. `v` must be finite and within the
// type's range (checked by finalizeParameterValue first).
double quantizeParameterLength(double metres);
double quantizeParameterArea(double squareMetres);
double quantizeParameterAngle(double radians);
// The dimension of a quantity type (Length, Area, Angle, Number, Count).
ParameterDimension parameterDimension(ParameterType type);
// Range-check (length ±1e6, area ±1e12, angle ±1e4, number ±1e15, count
// 0-10,000 and whole), then quantize lengths, areas and angles. Throws
// PARAMETER_DOMAIN with the given offset into `source` for the excerpt.
ParameterValue finalizeParameterValue(const ParameterValue &value, const ParameterSignature &type,
                                      std::string_view source, std::size_t offset);

} // namespace sketchy
