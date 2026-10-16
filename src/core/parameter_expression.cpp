// R093.a: the parameter expression engine (ADR 0165 §2, §3, §7).
// Compiled with -ffp-contract=off and without fast-math (CMakeLists.txt).
// Only the C++ standard library and libm are used; parameter_expression_symbols
// checks the archive's undefined symbols for process, file, network and loader
// calls.
#include "core/parameter_expression.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <optional>
#include <system_error>

namespace sketchy {
namespace {

using Limits = ParameterExpressionLimits;
constexpr double piValue = 3.141592653589793; // the double nearest to pi

std::string excerptAt(std::string_view text, std::size_t index) {
    std::string excerpt;
    for (std::size_t i = index; i < text.size() && excerpt.size() < Limits::excerptBytes; ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        excerpt.push_back(byte >= 0x20 && byte < 0x7f ? char(byte) : '?');
    }
    return excerpt;
}

// `index` is zero-based and clamped so that the reported offset lies inside the
// input (an error at the end of the input names its last byte).
[[noreturn]] void fail(ParameterErrorCode code, std::string_view text, std::size_t index,
                       std::string_view reason) {
    const auto last = text.empty() ? 0 : text.size() - 1;
    index = std::min(index, last);
    const auto offset = index + 1;
    std::string message(parameterErrorCodeName(code));
    message += " at byte ";
    message += std::to_string(offset);
    message += ": ";
    message += reason;
    if (message.size() > Limits::messageBytes)
        message.resize(Limits::messageBytes);
    throw ParameterExpressionError(code, offset, excerptAt(text, index), message);
}

std::string quoted(std::string_view name) {
    // Names reaching a message are at most 32 bytes of [a-z0-9_]; bound anyway.
    std::string out = "'";
    out += excerptAt(name, 0);
    out += "'";
    return out;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isWordChar(char c) { return isLower(c) || isDigit(c) || c == '_'; }
bool isChoiceChar(char c) { return isLower(c) || isDigit(c) || c == '_' || c == '-'; }

struct FunctionSpec {
    std::string_view name;
    ParameterFunction function;
};
constexpr std::array<FunctionSpec, 17> functionTable{{
    {"if", ParameterFunction::If},
    {"min", ParameterFunction::Min},
    {"max", ParameterFunction::Max},
    {"clamp", ParameterFunction::Clamp},
    {"abs", ParameterFunction::Abs},
    {"floor", ParameterFunction::Floor},
    {"ceil", ParameterFunction::Ceil},
    {"round", ParameterFunction::Round},
    {"mod", ParameterFunction::Mod},
    {"sqrt", ParameterFunction::Sqrt},
    {"sin", ParameterFunction::Sin},
    {"cos", ParameterFunction::Cos},
    {"tan", ParameterFunction::Tan},
    {"asin", ParameterFunction::Asin},
    {"acos", ParameterFunction::Acos},
    {"atan", ParameterFunction::Atan},
    {"atan2", ParameterFunction::Atan2},
}};
struct UnitSpec {
    std::string_view name;
    ParameterUnit unit;
};
constexpr std::array<UnitSpec, 7> unitTable{{{"mm", ParameterUnit::Mm},
                                             {"cm", ParameterUnit::Cm},
                                             {"m", ParameterUnit::M},
                                             {"in", ParameterUnit::In},
                                             {"ft", ParameterUnit::Ft},
                                             {"deg", ParameterUnit::Deg},
                                             {"rad", ParameterUnit::Rad}}};
constexpr std::array<std::string_view, 6> keywordTable{"or", "and", "not", "true", "false", "pi"};

std::optional<ParameterFunction> functionNamed(std::string_view word) {
    for (const auto &entry : functionTable)
        if (entry.name == word)
            return entry.function;
    return std::nullopt;
}
std::string_view functionName(ParameterFunction function) {
    for (const auto &entry : functionTable)
        if (entry.function == function)
            return entry.name;
    return "?";
}
std::optional<ParameterUnit> unitNamed(std::string_view word) {
    for (const auto &entry : unitTable)
        if (entry.name == word)
            return entry.unit;
    return std::nullopt;
}
std::string_view unitName(ParameterUnit unit) {
    for (const auto &entry : unitTable)
        if (entry.unit == unit)
            return entry.name;
    return "";
}
ParameterDimension unitDimension(ParameterUnit unit) {
    switch (unit) {
    case ParameterUnit::Mm:
    case ParameterUnit::Cm:
    case ParameterUnit::M:
    case ParameterUnit::In:
    case ParameterUnit::Ft:
        return {1, 0};
    case ParameterUnit::Deg:
    case ParameterUnit::Rad:
        return {0, 1};
    case ParameterUnit::None:
        break;
    }
    return {0, 0};
}
// Fixed SI conversions (ADR 0165 §2), evaluated left to right.
double toSi(double value, ParameterUnit unit) {
    switch (unit) {
    case ParameterUnit::Mm:
        return value / 1000;
    case ParameterUnit::Cm:
        return value / 100;
    case ParameterUnit::In:
        return value * 254 / 10000;
    case ParameterUnit::Ft:
        return value * 3048 / 10000;
    case ParameterUnit::Deg:
        return value * piValue / 180;
    case ParameterUnit::M:
    case ParameterUnit::Rad:
    case ParameterUnit::None:
        break;
    }
    return value;
}

// ---------------------------------------------------------------- Lexer
enum class Tok : std::uint8_t {
    End,
    Number,
    Word,
    Choice,
    LParen,
    RParen,
    Comma,
    Plus,
    Minus,
    Star,
    Slash,
    Caret,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};
struct Token {
    Tok kind{Tok::End};
    std::size_t begin{}; // zero-based
    std::size_t end{};
};

class Lexer {
  public:
    explicit Lexer(std::string_view text) : text_(text) {}
    std::size_t count() const { return count_; }

    Token next() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t'))
            ++pos_;
        if (pos_ == text_.size())
            return {Tok::End, pos_, pos_};
        const auto begin = pos_;
        // The token limit is checked before the token is scanned.
        if (count_ == Limits::tokens)
            fail(ParameterErrorCode::LimitExceeded, text_, begin,
                 "expression has more than 512 tokens");
        ++count_;
        const char c = text_[pos_];
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x80)
            fail(ParameterErrorCode::Syntax, text_, begin, "expression text must be ASCII");
        if (isDigit(c) || c == '.')
            return number(begin);
        if (isLower(c)) {
            while (pos_ < text_.size() && isWordChar(text_[pos_]))
                ++pos_;
            return {Tok::Word, begin, pos_};
        }
        if (c == '"')
            return choice(begin);
        const auto single = [&](Tok kind) {
            ++pos_;
            return Token{kind, begin, pos_};
        };
        const auto pair = [&](char second, Tok both, std::optional<Tok> alone) {
            if (pos_ + 1 < text_.size() && text_[pos_ + 1] == second) {
                pos_ += 2;
                return Token{both, begin, pos_};
            }
            if (!alone)
                fail(ParameterErrorCode::Syntax, text_, begin, "unexpected character");
            ++pos_;
            return Token{*alone, begin, pos_};
        };
        switch (c) {
        case '(':
            return single(Tok::LParen);
        case ')':
            return single(Tok::RParen);
        case ',':
            return single(Tok::Comma);
        case '+':
            return single(Tok::Plus);
        case '-':
            return single(Tok::Minus);
        case '*':
            return single(Tok::Star);
        case '/':
            return single(Tok::Slash);
        case '^':
            return single(Tok::Caret);
        case '=':
            return pair('=', Tok::Equal, std::nullopt);
        case '!':
            return pair('=', Tok::NotEqual, std::nullopt);
        case '<':
            return pair('=', Tok::LessEqual, Tok::Less);
        case '>':
            return pair('=', Tok::GreaterEqual, Tok::Greater);
        default:
            break;
        }
        fail(ParameterErrorCode::Syntax, text_, begin, "unexpected character");
    }

  private:
    bool digitAt(std::size_t at) const { return at < text_.size() && isDigit(text_[at]); }

    // number = ( digits [ "." [ digits ] ] | "." digits ) [ ( "e" | "E" ) [ "+" | "-" ] digits ]
    Token number(std::size_t begin) {
        if (text_[pos_] == '.') {
            if (!digitAt(pos_ + 1))
                fail(ParameterErrorCode::Syntax, text_, begin, "unexpected character");
            ++pos_;
            while (digitAt(pos_))
                ++pos_;
        } else {
            while (digitAt(pos_))
                ++pos_;
            if (pos_ < text_.size() && text_[pos_] == '.') {
                ++pos_;
                while (digitAt(pos_))
                    ++pos_;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            auto at = pos_ + 1;
            if (at < text_.size() && (text_[at] == '+' || text_[at] == '-'))
                ++at;
            if (digitAt(at)) {
                pos_ = at;
                while (digitAt(pos_))
                    ++pos_;
            }
        }
        // Checked before the literal is converted.
        if (pos_ - begin > Limits::numberLiteral)
            fail(ParameterErrorCode::Syntax, text_, begin,
                 "number literal is longer than 32 characters");
        return {Tok::Number, begin, pos_};
    }

    Token choice(std::size_t begin) {
        ++pos_;
        // Scans at most one character past the limit.
        while (pos_ < text_.size() && isChoiceChar(text_[pos_]) &&
               pos_ - begin - 1 <= Limits::choiceBytes)
            ++pos_;
        const auto length = pos_ - begin - 1;
        if (length > Limits::choiceBytes)
            fail(ParameterErrorCode::Syntax, text_, begin,
                 "choice value is longer than 32 characters");
        if (pos_ == text_.size())
            fail(ParameterErrorCode::Syntax, text_, begin, "unterminated choice value");
        if (text_[pos_] != '"')
            fail(ParameterErrorCode::Syntax, text_, pos_,
                 "choice values contain only a-z, 0-9, '_' and '-'");
        if (length == 0)
            fail(ParameterErrorCode::Syntax, text_, begin, "empty choice value");
        ++pos_;
        return {Tok::Choice, begin, pos_};
    }

    std::string_view text_;
    std::size_t pos_{};
    std::size_t count_{};
};

// ---------------------------------------------------------------- Parser
class Parser {
  public:
    explicit Parser(std::string_view text) : text_(text), lexer_(text) {}

    ParameterExpression run() {
        advance();
        expression();
        if (current_.kind != Tok::End)
            unexpected();
        ParameterExpression result;
        result.source = std::string(text_);
        result.nodes = std::move(nodes_);
        result.tokenCount = lexer_.count();
        result.maxDepth = maxDepth_;
        return result;
    }

  private:
    void advance() { current_ = lexer_.next(); }
    std::string_view lexeme(const Token &token) const {
        return text_.substr(token.begin, token.end - token.begin);
    }
    bool word(std::string_view expected) const {
        return current_.kind == Tok::Word && lexeme(current_) == expected;
    }
    [[noreturn]] void unexpected() const {
        if (current_.kind == Tok::End)
            fail(ParameterErrorCode::Syntax, text_, current_.begin, "unexpected end of expression");
        if (current_.kind == Tok::Word && unitNamed(lexeme(current_)))
            fail(ParameterErrorCode::Syntax, text_, current_.begin, "a unit must follow a number");
        fail(ParameterErrorCode::Syntax, text_, current_.begin, "unexpected token");
    }
    // Nesting (parentheses, calls, unary operators) is checked before descending.
    void enter(const Token &token) {
        if (depth_ == Limits::depth)
            fail(ParameterErrorCode::LimitExceeded, text_, token.begin,
                 "expression nesting is deeper than 32");
        ++depth_;
        maxDepth_ = std::max(maxDepth_, depth_);
    }
    void leave() { --depth_; }

    // Appends a node whose children are the nodes from `mark` on.
    void push(ParameterNode node, std::size_t mark, std::uint16_t children) {
        if (nodes_.size() == Limits::nodes)
            fail(ParameterErrorCode::LimitExceeded, text_, node.offset - 1,
                 "expression has more than 256 syntax-tree nodes");
        node.argumentCount = children;
        node.subtreeSize = std::uint16_t(nodes_.size() - mark + 1);
        // Offsets grow left to right, so a subexpression starts at the smallest
        // offset among its nodes.
        node.start = node.offset;
        for (auto k = mark; k < nodes_.size(); ++k)
            node.start = std::min(node.start, nodes_[k].start);
        nodes_.push_back(std::move(node));
    }
    static ParameterNode make(ParameterOp op, const Token &token) {
        ParameterNode node;
        node.op = op;
        node.offset = std::uint32_t(token.begin + 1);
        return node;
    }

    void expression() { disjunction(); }
    void disjunction() {
        const auto mark = nodes_.size();
        conjunction();
        while (word("or")) {
            const auto op = current_;
            advance();
            conjunction();
            push(make(ParameterOp::Or, op), mark, 2);
        }
    }
    void conjunction() {
        const auto mark = nodes_.size();
        negation();
        while (word("and")) {
            const auto op = current_;
            advance();
            negation();
            push(make(ParameterOp::And, op), mark, 2);
        }
    }
    void negation() {
        if (!word("not")) {
            comparison();
            return;
        }
        const auto op = current_;
        enter(op);
        advance();
        const auto mark = nodes_.size();
        negation();
        leave();
        push(make(ParameterOp::Not, op), mark, 1);
    }
    static std::optional<ParameterOp> comparisonOp(Tok kind) {
        switch (kind) {
        case Tok::Equal:
            return ParameterOp::Equal;
        case Tok::NotEqual:
            return ParameterOp::NotEqual;
        case Tok::Less:
            return ParameterOp::Less;
        case Tok::LessEqual:
            return ParameterOp::LessEqual;
        case Tok::Greater:
            return ParameterOp::Greater;
        case Tok::GreaterEqual:
            return ParameterOp::GreaterEqual;
        default:
            return std::nullopt;
        }
    }
    void comparison() {
        const auto mark = nodes_.size();
        sum();
        if (const auto op = comparisonOp(current_.kind)) {
            const auto token = current_;
            advance();
            sum();
            push(make(*op, token), mark, 2);
            if (comparisonOp(current_.kind))
                fail(ParameterErrorCode::Syntax, text_, current_.begin,
                     "comparisons are not associative; use and/or");
        }
    }
    void sum() {
        const auto mark = nodes_.size();
        product();
        while (current_.kind == Tok::Plus || current_.kind == Tok::Minus) {
            const auto token = current_;
            advance();
            product();
            push(make(token.kind == Tok::Plus ? ParameterOp::Add : ParameterOp::Subtract, token),
                 mark, 2);
        }
    }
    void product() {
        const auto mark = nodes_.size();
        sign();
        while (current_.kind == Tok::Star || current_.kind == Tok::Slash) {
            const auto token = current_;
            advance();
            sign();
            push(make(token.kind == Tok::Star ? ParameterOp::Multiply : ParameterOp::Divide, token),
                 mark, 2);
        }
    }
    void sign() {
        if (current_.kind != Tok::Plus && current_.kind != Tok::Minus) {
            power();
            return;
        }
        const auto token = current_;
        enter(token);
        advance();
        const auto mark = nodes_.size();
        sign();
        leave();
        push(make(token.kind == Tok::Minus ? ParameterOp::Negate : ParameterOp::Plus, token), mark,
             1);
    }
    void power() {
        const auto mark = nodes_.size();
        primary();
        if (current_.kind != Tok::Caret)
            return;
        const auto token = current_;
        advance();
        bool negative = false;
        if (current_.kind == Tok::Minus) {
            negative = true;
            advance();
        }
        const auto digits = lexeme(current_);
        if (current_.kind != Tok::Number || digits.size() != 1 || digits[0] < '0' ||
            digits[0] > '3')
            fail(ParameterErrorCode::Syntax, text_, current_.begin,
                 "an exponent is one of 0, 1, 2 or 3, optionally negated");
        auto node = make(ParameterOp::Power, token);
        node.exponent = std::int8_t((negative ? -1 : 1) * (digits[0] - '0'));
        advance();
        push(std::move(node), mark, 1);
    }
    void primary() {
        const auto token = current_;
        const auto mark = nodes_.size();
        switch (token.kind) {
        case Tok::Number:
            return number();
        case Tok::Choice: {
            auto node = make(ParameterOp::Choice, token);
            node.text = std::string(lexeme(token).substr(1, token.end - token.begin - 2));
            advance();
            return push(std::move(node), mark, 0);
        }
        case Tok::LParen:
            enter(token);
            advance();
            expression();
            if (current_.kind != Tok::RParen)
                unexpected();
            leave();
            advance();
            return;
        case Tok::Word:
            return wordPrimary();
        default:
            unexpected();
        }
    }
    void number() {
        const auto token = current_;
        const auto text = lexeme(token);
        double value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error == std::errc::result_out_of_range)
            fail(ParameterErrorCode::Domain, text_, token.begin, "number literal is out of range");
        if (error != std::errc{} || end != text.data() + text.size())
            fail(ParameterErrorCode::Syntax, text_, token.begin, "malformed number literal");
        auto node = make(ParameterOp::Number, token);
        node.literal = value;
        advance();
        if (current_.kind == Tok::Word)
            if (const auto unit = unitNamed(lexeme(current_))) {
                node.unit = *unit;
                advance();
            }
        node.si = toSi(value, node.unit);
        if (!std::isfinite(node.si))
            fail(ParameterErrorCode::Domain, text_, token.begin,
                 "number literal is not finite in SI units");
        push(std::move(node), nodes_.size(), 0);
    }
    void wordPrimary() {
        const auto token = current_;
        const auto text = lexeme(token);
        const auto mark = nodes_.size();
        if (text == "true" || text == "false" || text == "pi") {
            advance();
            return push(make(text == "pi"     ? ParameterOp::Pi
                             : text == "true" ? ParameterOp::True
                                              : ParameterOp::False,
                             token),
                        mark, 0);
        }
        if (const auto function = functionNamed(text)) {
            advance();
            if (current_.kind != Tok::LParen)
                fail(ParameterErrorCode::Syntax, text_, current_.begin,
                     "a function name must be followed by '('");
            enter(current_);
            advance();
            std::size_t arguments = 1;
            expression();
            while (current_.kind == Tok::Comma) {
                advance();
                expression();
                ++arguments;
            }
            if (current_.kind != Tok::RParen)
                unexpected();
            leave();
            advance();
            auto node = make(ParameterOp::Call, token);
            node.function = *function;
            return push(std::move(node), mark, std::uint16_t(arguments));
        }
        if (unitNamed(text))
            fail(ParameterErrorCode::Syntax, text_, token.begin, "a unit must follow a number");
        if (std::find(keywordTable.begin(), keywordTable.end(), text) != keywordTable.end())
            unexpected();
        if (text.size() > Limits::nameBytes)
            fail(ParameterErrorCode::Name, text_, token.begin, "name is longer than 32 bytes");
        advance();
        if (current_.kind == Tok::LParen)
            fail(ParameterErrorCode::Name, text_, token.begin, "unknown function " + quoted(text));
        auto node = make(ParameterOp::Name, token);
        node.text = std::string(text);
        push(std::move(node), mark, 0);
    }

    std::string_view text_;
    Lexer lexer_;
    Token current_;
    std::vector<ParameterNode> nodes_;
    std::size_t depth_{};
    std::size_t maxDepth_{};
};

// Children of node `index`, in order.
std::vector<std::size_t> childrenOf(const std::vector<ParameterNode> &nodes, std::size_t index) {
    std::vector<std::size_t> children(nodes[index].argumentCount);
    auto at = index;
    for (auto k = children.size(); k-- > 0;) {
        --at;
        children[k] = at;
        at -= nodes[at].subtreeSize - 1;
    }
    return children;
}

// ---------------------------------------------------------------- Canonical text
int precedence(ParameterOp op) {
    switch (op) {
    case ParameterOp::Or:
        return 1;
    case ParameterOp::And:
        return 2;
    case ParameterOp::Not:
        return 3;
    case ParameterOp::Equal:
    case ParameterOp::NotEqual:
    case ParameterOp::Less:
    case ParameterOp::LessEqual:
    case ParameterOp::Greater:
    case ParameterOp::GreaterEqual:
        return 4;
    case ParameterOp::Add:
    case ParameterOp::Subtract:
        return 5;
    case ParameterOp::Multiply:
    case ParameterOp::Divide:
        return 6;
    case ParameterOp::Negate:
    case ParameterOp::Plus:
        return 7;
    case ParameterOp::Power:
        return 8;
    default:
        return 9;
    }
}
std::string_view operatorText(ParameterOp op) {
    switch (op) {
    case ParameterOp::Or:
        return "or";
    case ParameterOp::And:
        return "and";
    case ParameterOp::Equal:
        return "==";
    case ParameterOp::NotEqual:
        return "!=";
    case ParameterOp::Less:
        return "<";
    case ParameterOp::LessEqual:
        return "<=";
    case ParameterOp::Greater:
        return ">";
    case ParameterOp::GreaterEqual:
        return ">=";
    case ParameterOp::Add:
        return "+";
    case ParameterOp::Subtract:
        return "-";
    case ParameterOp::Multiply:
        return "*";
    case ParameterOp::Divide:
        return "/";
    default:
        return "?";
    }
}
std::string shortest(double value) {
    std::array<char, 64> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    (void)error; // 64 bytes always hold the shortest form of a finite double
    return std::string(buffer.data(), end);
}

// ---------------------------------------------------------------- Checker
enum class Kind : std::uint8_t { Boolean, Choice, Quantity };
constexpr std::uint32_t unbound = 0xffffffffu;
constexpr std::uint32_t expectedOptions = 0xfffffffeu;
struct TypeEntry {
    Kind kind{};
    ParameterDimension dimension;
    std::uint32_t options{unbound};      // Choice: reference index, expectedOptions or unbound
    std::vector<std::uint32_t> literals; // Choice: unchecked literal nodes (unbound only)
    std::size_t node{};
};

std::string_view kindName(const TypeEntry &entry) {
    if (entry.kind == Kind::Boolean)
        return "a boolean";
    if (entry.kind == Kind::Choice)
        return "a choice";
    const auto d = entry.dimension;
    if (d == ParameterDimension{1, 0})
        return "a length";
    if (d == ParameterDimension{2, 0})
        return "an area";
    if (d == ParameterDimension{0, 1})
        return "an angle";
    if (d == ParameterDimension{0, 0})
        return "a number";
    return "a quantity";
}

class Checker {
  public:
    Checker(const ParameterExpression &expression, const ParameterSymbols &symbols,
            const ParameterSignature &expected)
        : expression_(expression), nodes_(expression.nodes), symbols_(symbols),
          expected_(expected) {}

    CheckedParameterExpression run() {
        if (nodes_.empty())
            fail(ParameterErrorCode::Syntax, expression_.source, 0, "empty expression");
        checked_.referenceSlots.assign(nodes_.size(), unbound);
        std::vector<TypeEntry> stack;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            const auto count = nodes_[i].argumentCount;
            std::vector<TypeEntry> operands(std::make_move_iterator(stack.end() - count),
                                            std::make_move_iterator(stack.end()));
            stack.resize(stack.size() - count);
            stack.push_back(node(i, operands));
        }
        result(stack.back());
        checked_.expression = expression_;
        checked_.result = expected_;
        emitProgram(nodes_.size() - 1);
        // Structural: a stack value is pushed only by a leaf, and leaves are
        // bounded by the node limit. Kept as a checked invariant.
        if (checked_.maxStack > Limits::valueStack)
            fail(ParameterErrorCode::LimitExceeded, expression_.source, 0,
                 "expression needs more than 256 value-stack entries");
        return std::move(checked_);
    }

  private:
    [[noreturn]] void error(ParameterErrorCode code, std::uint32_t offset,
                            std::string_view reason) const {
        fail(code, expression_.source, offset - 1, reason);
    }
    std::uint32_t at(std::size_t node) const { return nodes_[node].offset; }
    std::uint32_t startOf(std::size_t node) const { return nodes_[node].start; }

    const std::vector<std::string> &optionsOf(std::uint32_t options) const {
        return options == expectedOptions ? expected_.options
                                          : checked_.referenceTypes[options].options;
    }
    void verifyLiterals(const std::vector<std::uint32_t> &literals,
                        const std::vector<std::string> &options) const {
        for (const auto literal : literals)
            if (std::find(options.begin(), options.end(), nodes_[literal].text) == options.end())
                error(ParameterErrorCode::Type, at(literal),
                      "unknown choice value \"" + excerptAt(nodes_[literal].text, 0) + "\"");
    }
    // Two choices have the same type when they come from the same option list.
    TypeEntry unifyChoices(TypeEntry a, TypeEntry b, std::uint32_t offset) const {
        if (a.options != unbound && b.options != unbound) {
            if (optionsOf(a.options) != optionsOf(b.options))
                error(ParameterErrorCode::Type, offset, "choices have different option lists");
            return a;
        }
        if (a.options == unbound && b.options == unbound) {
            a.literals.insert(a.literals.end(), b.literals.begin(), b.literals.end());
            return a;
        }
        auto &bound = a.options != unbound ? a : b;
        auto &open = a.options != unbound ? b : a;
        verifyLiterals(open.literals, optionsOf(bound.options));
        bound.literals.clear();
        return bound;
    }
    static TypeEntry quantity(ParameterDimension dimension, std::size_t node) {
        TypeEntry entry;
        entry.kind = Kind::Quantity;
        entry.dimension = dimension;
        entry.node = node;
        return entry;
    }
    static TypeEntry boolean(std::size_t node) {
        TypeEntry entry;
        entry.kind = Kind::Boolean;
        entry.node = node;
        return entry;
    }
    void requireQuantity(const TypeEntry &entry, std::string_view what) const {
        if (entry.kind != Kind::Quantity)
            error(ParameterErrorCode::Type, startOf(entry.node),
                  std::string(what) + " needs a quantity, not " + std::string(kindName(entry)));
    }
    void requireBoolean(const TypeEntry &entry, std::string_view what) const {
        if (entry.kind != Kind::Boolean)
            error(ParameterErrorCode::Type, startOf(entry.node),
                  std::string(what) + " needs a boolean, not " + std::string(kindName(entry)));
    }
    void requireDimension(const TypeEntry &entry, ParameterDimension dimension,
                          std::string_view what) const {
        requireQuantity(entry, what);
        if (!(entry.dimension == dimension))
            error(ParameterErrorCode::Type, startOf(entry.node),
                  std::string(what) + " needs " +
                      std::string(kindName(quantity(dimension, entry.node))) + ", not " +
                      std::string(kindName(entry)));
    }
    ParameterDimension checkedDimension(int length, int angle, std::uint32_t offset) const {
        if (std::abs(length) > Limits::maxExponent || std::abs(angle) > Limits::maxExponent)
            error(ParameterErrorCode::Type, offset, "dimension exponent outside -3..3");
        return {length, angle};
    }
    void sameDimensions(const std::vector<TypeEntry> &operands, std::uint32_t offset,
                        std::string_view what) const {
        for (const auto &operand : operands)
            requireQuantity(operand, what);
        for (const auto &operand : operands)
            if (!(operand.dimension == operands.front().dimension))
                error(ParameterErrorCode::Type, offset,
                      std::string(what) + " needs operands of the same dimension");
    }

    TypeEntry node(std::size_t i, std::vector<TypeEntry> &operands) {
        const auto &n = nodes_[i];
        switch (n.op) {
        case ParameterOp::Number:
            return quantity(unitDimension(n.unit), i);
        case ParameterOp::Pi:
            return quantity({0, 0}, i);
        case ParameterOp::True:
        case ParameterOp::False:
            return boolean(i);
        case ParameterOp::Choice: {
            TypeEntry entry;
            entry.kind = Kind::Choice;
            entry.literals = {std::uint32_t(i)};
            entry.node = i;
            return entry;
        }
        case ParameterOp::Name:
            return name(i);
        case ParameterOp::Negate:
        case ParameterOp::Plus:
            requireQuantity(operands[0], n.op == ParameterOp::Negate ? "'-'" : "'+'");
            operands[0].node = i;
            return operands[0];
        case ParameterOp::Not:
            requireBoolean(operands[0], "'not'");
            return boolean(i);
        case ParameterOp::Or:
        case ParameterOp::And:
            requireBoolean(operands[0], n.op == ParameterOp::Or ? "'or'" : "'and'");
            requireBoolean(operands[1], n.op == ParameterOp::Or ? "'or'" : "'and'");
            return boolean(i);
        case ParameterOp::Equal:
        case ParameterOp::NotEqual:
            return equality(i, operands);
        case ParameterOp::Less:
        case ParameterOp::LessEqual:
        case ParameterOp::Greater:
        case ParameterOp::GreaterEqual:
            sameDimensions(operands, n.offset, "a comparison");
            return boolean(i);
        case ParameterOp::Add:
        case ParameterOp::Subtract:
            sameDimensions(operands, n.offset, n.op == ParameterOp::Add ? "'+'" : "'-'");
            return quantity(operands[0].dimension, i);
        case ParameterOp::Multiply:
        case ParameterOp::Divide: {
            const auto what = n.op == ParameterOp::Multiply ? "'*'" : "'/'";
            requireQuantity(operands[0], what);
            requireQuantity(operands[1], what);
            const auto s = n.op == ParameterOp::Multiply ? 1 : -1;
            return quantity(
                checkedDimension(operands[0].dimension.length + s * operands[1].dimension.length,
                                 operands[0].dimension.angle + s * operands[1].dimension.angle,
                                 n.offset),
                i);
        }
        case ParameterOp::Power:
            requireQuantity(operands[0], "'^'");
            return quantity(checkedDimension(operands[0].dimension.length * n.exponent,
                                             operands[0].dimension.angle * n.exponent, n.offset),
                            i);
        case ParameterOp::Call:
            return call(i, operands);
        }
        error(ParameterErrorCode::Syntax, n.offset, "unknown node");
    }

    TypeEntry name(std::size_t i) {
        const auto &n = nodes_[i];
        std::uint32_t slot = unbound;
        for (std::size_t r = 0; r < checked_.references.size(); ++r)
            if (checked_.references[r] == n.text)
                slot = std::uint32_t(r);
        if (slot == unbound) {
            const auto *signature = symbols_.find(n.text);
            if (!signature)
                error(ParameterErrorCode::Name, n.offset, "unknown parameter " + quoted(n.text));
            slot = std::uint32_t(checked_.references.size());
            checked_.references.push_back(n.text);
            checked_.referenceTypes.push_back(*signature);
        }
        checked_.referenceSlots[i] = slot;
        const auto &signature = checked_.referenceTypes[slot];
        switch (signature.type) {
        case ParameterType::Boolean:
            return boolean(i);
        case ParameterType::Choice: {
            TypeEntry entry;
            entry.kind = Kind::Choice;
            entry.options = slot;
            entry.node = i;
            return entry;
        }
        default:
            return quantity(parameterDimension(signature.type), i);
        }
    }

    TypeEntry equality(std::size_t i, std::vector<TypeEntry> &operands) {
        const auto &n = nodes_[i];
        auto &a = operands[0];
        auto &b = operands[1];
        if (a.kind != b.kind)
            error(ParameterErrorCode::Type, n.offset,
                  "'" + std::string(operatorText(n.op)) + "' compares " + std::string(kindName(a)) +
                      " with " + std::string(kindName(b)));
        if (a.kind == Kind::Quantity) {
            if (!(a.dimension == ParameterDimension{}) || !(b.dimension == ParameterDimension{}))
                error(ParameterErrorCode::Type, n.offset,
                      "lengths, areas and angles are compared with '<' or '>', not '" +
                          std::string(operatorText(n.op)) + "'");
        } else if (a.kind == Kind::Choice) {
            const auto merged = unifyChoices(std::move(a), std::move(b), n.offset);
            if (merged.options == unbound)
                error(ParameterErrorCode::Type, at(merged.literals.front()),
                      "a choice value must be compared with a choice parameter");
        }
        return boolean(i);
    }

    TypeEntry call(std::size_t i, std::vector<TypeEntry> &operands) {
        const auto &n = nodes_[i];
        const auto name = functionName(n.function);
        const auto count = operands.size();
        const auto arity = [&](std::size_t low, std::size_t high) {
            if (count < low || count > high) {
                std::string reason(name);
                reason += low == high ? " takes " + std::to_string(low) + " argument" +
                                            (low == 1 ? "" : "s")
                                      : " takes " + std::to_string(low) + " to " +
                                            std::to_string(high) + " arguments";
                error(ParameterErrorCode::Type, n.offset, reason);
            }
        };
        const auto what = std::string(name) + "()";
        switch (n.function) {
        case ParameterFunction::If: {
            arity(3, 3);
            requireBoolean(operands[0], "the condition of if()");
            auto &a = operands[1];
            auto &b = operands[2];
            if (a.kind != b.kind)
                error(ParameterErrorCode::Type, startOf(b.node),
                      "if() branches have different types: " + std::string(kindName(a)) + " and " +
                          std::string(kindName(b)));
            if (a.kind == Kind::Quantity) {
                if (!(a.dimension == b.dimension))
                    error(ParameterErrorCode::Type, startOf(b.node),
                          "if() branches have different dimensions");
                return quantity(a.dimension, i);
            }
            if (a.kind == Kind::Boolean)
                return boolean(i);
            auto merged = unifyChoices(std::move(a), std::move(b), startOf(operands[2].node));
            merged.node = i;
            return merged;
        }
        case ParameterFunction::Min:
        case ParameterFunction::Max:
            arity(2, 8);
            sameDimensions(operands, n.offset, what);
            return quantity(operands[0].dimension, i);
        case ParameterFunction::Clamp:
            arity(3, 3);
            sameDimensions(operands, n.offset, what);
            return quantity(operands[0].dimension, i);
        case ParameterFunction::Mod:
            arity(2, 2);
            sameDimensions(operands, n.offset, what);
            return quantity(operands[0].dimension, i);
        case ParameterFunction::Abs:
            arity(1, 1);
            requireQuantity(operands[0], what);
            return quantity(operands[0].dimension, i);
        case ParameterFunction::Floor:
        case ParameterFunction::Ceil:
        case ParameterFunction::Round:
            arity(1, 1);
            requireDimension(operands[0], {0, 0}, what);
            return quantity({0, 0}, i);
        case ParameterFunction::Sqrt: {
            arity(1, 1);
            requireQuantity(operands[0], what);
            const auto d = operands[0].dimension;
            if (d.length % 2 != 0 || d.angle % 2 != 0)
                error(ParameterErrorCode::Type, startOf(operands[0].node),
                      "sqrt() needs even dimension exponents");
            return quantity({d.length / 2, d.angle / 2}, i);
        }
        case ParameterFunction::Sin:
        case ParameterFunction::Cos:
        case ParameterFunction::Tan:
            arity(1, 1);
            requireDimension(operands[0], {0, 1}, what);
            return quantity({0, 0}, i);
        case ParameterFunction::Asin:
        case ParameterFunction::Acos:
        case ParameterFunction::Atan:
            arity(1, 1);
            requireDimension(operands[0], {0, 0}, what);
            return quantity({0, 1}, i);
        case ParameterFunction::Atan2:
            arity(2, 2);
            sameDimensions(operands, n.offset, what);
            return quantity({0, 1}, i);
        case ParameterFunction::None:
            break;
        }
        error(ParameterErrorCode::Name, n.offset, "unknown function");
    }

    void result(TypeEntry &entry) const {
        const auto offset = nodes_.back().start;
        const auto type = expected_.type;
        const auto mismatch = [&] {
            error(ParameterErrorCode::Type, offset,
                  "expression is " + std::string(kindName(entry)) + "; the " +
                      std::string(parameterTypeName(type)) + " needs " +
                      (type == ParameterType::Boolean ? std::string("a boolean")
                       : type == ParameterType::Choice
                           ? std::string("a choice")
                           : std::string(kindName(quantity(parameterDimension(type), 0)))));
        };
        if (type == ParameterType::Boolean) {
            if (entry.kind != Kind::Boolean)
                mismatch();
        } else if (type == ParameterType::Choice) {
            if (entry.kind != Kind::Choice)
                mismatch();
            TypeEntry wanted;
            wanted.kind = Kind::Choice;
            wanted.options = expectedOptions;
            unifyChoices(std::move(entry), std::move(wanted), offset);
        } else if (entry.kind != Kind::Quantity || !(entry.dimension == parameterDimension(type))) {
            mismatch();
        }
    }

    // Loop-free program; every jump is forward. Recursion here is bounded by the
    // tree's height (at most 256 nodes); evaluation itself is iterative.
    void emitProgram(std::size_t i) {
        const auto &n = nodes_[i];
        const auto children = childrenOf(nodes_, i);
        auto &program = checked_.program;
        const auto node = [&] {
            program.push_back({ParameterInstruction::Node, std::uint32_t(i), 0});
        };
        if (n.op == ParameterOp::Or || n.op == ParameterOp::And) {
            emitProgram(children[0]);
            const auto jump = program.size();
            program.push_back({n.op == ParameterOp::Or ? ParameterInstruction::OrShortCircuit
                                                       : ParameterInstruction::AndShortCircuit,
                               0, 0});
            --height_;
            emitProgram(children[1]);
            program[jump].target = std::uint32_t(program.size());
            node();
            return;
        }
        if (n.op == ParameterOp::Call && n.function == ParameterFunction::If) {
            emitProgram(children[0]);
            const auto test = program.size();
            program.push_back({ParameterInstruction::JumpIfFalse, 0, 0});
            --height_;
            const auto before = height_;
            emitProgram(children[1]);
            const auto skip = program.size();
            program.push_back({ParameterInstruction::Jump, 0, 0});
            program[test].target = std::uint32_t(program.size());
            height_ = before;
            emitProgram(children[2]);
            program[skip].target = std::uint32_t(program.size());
            node();
            return;
        }
        for (const auto child : children)
            emitProgram(child);
        node();
        height_ = height_ - children.size() + 1;
        checked_.maxStack = std::max(checked_.maxStack, height_);
    }

    const ParameterExpression &expression_;
    const std::vector<ParameterNode> &nodes_;
    const ParameterSymbols &symbols_;
    const ParameterSignature &expected_;
    CheckedParameterExpression checked_;
    std::size_t height_{};
};

// ---------------------------------------------------------------- Evaluator
struct Slot {
    Kind kind{};
    bool boolean{};
    double number{};
    ParameterDimension dimension;
    std::string_view choice;
};

class Evaluator {
  public:
    Evaluator(const CheckedParameterExpression &checked, std::span<const ParameterValue> values,
              ParameterEvaluationBudget &budget)
        : checked_(checked), nodes_(checked.expression.nodes), values_(values), budget_(budget) {}

    Slot run() {
        const auto &program = checked_.program;
        std::size_t pc = 0;
        while (pc < program.size()) {
            const auto &step = program[pc];
            switch (step.instruction) {
            case ParameterInstruction::Node:
                if (budget_.remaining == 0)
                    error(ParameterErrorCode::LimitExceeded, step.node,
                          "evaluation node-visit budget is exhausted");
                --budget_.remaining;
                evaluate(step.node);
                ++pc;
                break;
            case ParameterInstruction::JumpIfFalse:
                pc = pop().boolean ? pc + 1 : step.target;
                break;
            case ParameterInstruction::Jump:
                pc = step.target;
                break;
            case ParameterInstruction::AndShortCircuit:
                if (!top().boolean) {
                    pc = step.target;
                } else {
                    pop();
                    ++pc;
                }
                break;
            case ParameterInstruction::OrShortCircuit:
                if (top().boolean) {
                    pc = step.target;
                } else {
                    pop();
                    ++pc;
                }
                break;
            }
        }
        return pop();
    }

  private:
    [[noreturn]] void error(ParameterErrorCode code, std::size_t node,
                            std::string_view reason) const {
        fail(code, checked_.expression.source, nodes_[node].offset - 1, reason);
    }
    void push(const Slot &slot) {
        if (size_ == stack_.size())
            fail(ParameterErrorCode::LimitExceeded, checked_.expression.source, 0,
                 "evaluation needs more than 256 value-stack entries");
        stack_[size_++] = slot;
    }
    Slot pop() { return stack_[--size_]; }
    const Slot &top() const { return stack_[size_ - 1]; }

    void number(std::size_t node, double value, ParameterDimension dimension) {
        if (!std::isfinite(value))
            error(ParameterErrorCode::Domain, node, "intermediate result is not finite");
        Slot slot;
        slot.kind = Kind::Quantity;
        slot.number = value;
        slot.dimension = dimension;
        push(slot);
    }
    void boolean(bool value) {
        Slot slot;
        slot.kind = Kind::Boolean;
        slot.boolean = value;
        push(slot);
    }

    void evaluate(std::size_t i) {
        const auto &n = nodes_[i];
        switch (n.op) {
        case ParameterOp::Number:
            return number(i, n.si, unitDimension(n.unit));
        case ParameterOp::Pi:
            return number(i, piValue, {});
        case ParameterOp::True:
            return boolean(true);
        case ParameterOp::False:
            return boolean(false);
        case ParameterOp::Choice: {
            Slot slot;
            slot.kind = Kind::Choice;
            slot.choice = n.text;
            return push(slot);
        }
        case ParameterOp::Name:
            return name(i);
        case ParameterOp::Negate: {
            const auto a = pop();
            return number(i, -a.number, a.dimension);
        }
        case ParameterOp::Plus:
        case ParameterOp::Or:  // the short-circuit left the result on the stack
        case ParameterOp::And: // ditto
            return;
        case ParameterOp::Not:
            return boolean(!pop().boolean);
        case ParameterOp::Equal:
        case ParameterOp::NotEqual: {
            const auto b = pop();
            const auto a = pop();
            const bool same = a.kind == Kind::Boolean  ? a.boolean == b.boolean
                              : a.kind == Kind::Choice ? a.choice == b.choice
                                                       : a.number == b.number;
            return boolean(n.op == ParameterOp::Equal ? same : !same);
        }
        case ParameterOp::Less:
        case ParameterOp::LessEqual:
        case ParameterOp::Greater:
        case ParameterOp::GreaterEqual: {
            const auto b = pop().number;
            const auto a = pop().number;
            return boolean(n.op == ParameterOp::Less        ? a < b
                           : n.op == ParameterOp::LessEqual ? a <= b
                           : n.op == ParameterOp::Greater   ? a > b
                                                            : a >= b);
        }
        case ParameterOp::Add:
        case ParameterOp::Subtract: {
            const auto b = pop();
            const auto a = pop();
            return number(i, n.op == ParameterOp::Add ? a.number + b.number : a.number - b.number,
                          a.dimension);
        }
        case ParameterOp::Multiply: {
            const auto b = pop();
            const auto a = pop();
            return number(
                i, a.number * b.number,
                {a.dimension.length + b.dimension.length, a.dimension.angle + b.dimension.angle});
        }
        case ParameterOp::Divide: {
            const auto b = pop();
            const auto a = pop();
            if (b.number == 0)
                error(ParameterErrorCode::Domain, i, "division by zero");
            return number(
                i, a.number / b.number,
                {a.dimension.length - b.dimension.length, a.dimension.angle - b.dimension.angle});
        }
        case ParameterOp::Power:
            return power(i);
        case ParameterOp::Call:
            return call(i);
        }
    }

    void name(std::size_t i) {
        const auto &value = values_[checked_.referenceSlots[i]];
        Slot slot;
        if (const auto *b = std::get_if<bool>(&value)) {
            slot.kind = Kind::Boolean;
            slot.boolean = *b;
        } else if (const auto *c = std::get_if<ParameterChoice>(&value)) {
            slot.kind = Kind::Choice;
            slot.choice = c->value;
        } else {
            const auto &q = std::get<ParameterQuantity>(value);
            slot.kind = Kind::Quantity;
            slot.number = q.value;
            slot.dimension = q.dimension;
        }
        push(slot);
    }

    // Repeated multiplication (ADR 0165 §2): x^0 = 1, x^2 = x*x, x^3 = (x*x)*x,
    // x^-n = 1 / x^n.
    void power(std::size_t i) {
        const auto &n = nodes_[i];
        const auto a = pop();
        const int count = n.exponent < 0 ? -n.exponent : n.exponent;
        const ParameterDimension dimension{a.dimension.length * n.exponent,
                                           a.dimension.angle * n.exponent};
        double r = 1;
        if (count > 0) {
            r = a.number;
            for (int k = 1; k < count; ++k) {
                r = r * a.number;
                if (!std::isfinite(r))
                    error(ParameterErrorCode::Domain, i, "intermediate result is not finite");
            }
        }
        if (n.exponent < 0) {
            if (r == 0)
                error(ParameterErrorCode::Domain, i, "division by zero");
            r = 1 / r;
        }
        number(i, r, dimension);
    }

    void call(std::size_t i) {
        const auto &n = nodes_[i];
        const auto count = n.argumentCount;
        std::array<Slot, 8> args{};
        if (n.function != ParameterFunction::If) {
            for (std::size_t k = count; k-- > 0;)
                args[k] = pop();
        }
        const auto &a = args[0];
        const auto d = a.dimension;
        switch (n.function) {
        case ParameterFunction::If:
            return; // the chosen branch's value is already on the stack
        case ParameterFunction::Min:
        case ParameterFunction::Max: {
            double r = a.number;
            for (std::size_t k = 1; k < count; ++k)
                if (n.function == ParameterFunction::Min ? args[k].number < r : args[k].number > r)
                    r = args[k].number;
            return number(i, r, d);
        }
        case ParameterFunction::Clamp: {
            const auto lo = args[1].number;
            const auto hi = args[2].number;
            if (lo > hi)
                error(ParameterErrorCode::Domain, i, "clamp() needs lo <= hi");
            return number(i, a.number < lo ? lo : (a.number > hi ? hi : a.number), d);
        }
        case ParameterFunction::Abs:
            return number(i, std::fabs(a.number), d);
        case ParameterFunction::Floor:
            return number(i, std::floor(a.number), d);
        case ParameterFunction::Ceil:
            return number(i, std::ceil(a.number), d);
        case ParameterFunction::Round:
            return number(i, std::round(a.number), d); // half away from zero
        case ParameterFunction::Mod:
            if (args[1].number == 0)
                error(ParameterErrorCode::Domain, i, "mod() by zero");
            // Truncated remainder (sign of the dividend), exact in binary64.
            return number(i, std::fmod(a.number, args[1].number), d);
        case ParameterFunction::Sqrt:
            if (a.number < 0)
                error(ParameterErrorCode::Domain, i, "sqrt() of a negative value");
            return number(i, std::sqrt(a.number), {d.length / 2, d.angle / 2});
        case ParameterFunction::Sin:
            return number(i, std::sin(a.number), {});
        case ParameterFunction::Cos:
            return number(i, std::cos(a.number), {});
        case ParameterFunction::Tan:
            return number(i, std::tan(a.number), {});
        case ParameterFunction::Asin:
        case ParameterFunction::Acos:
            if (a.number < -1 || a.number > 1)
                error(ParameterErrorCode::Domain, i,
                      n.function == ParameterFunction::Asin ? "asin() outside -1..1"
                                                            : "acos() outside -1..1");
            return number(i,
                          n.function == ParameterFunction::Asin ? std::asin(a.number)
                                                                : std::acos(a.number),
                          {0, 1});
        case ParameterFunction::Atan:
            return number(i, std::atan(a.number), {0, 1});
        case ParameterFunction::Atan2:
            return number(i, std::atan2(a.number, args[1].number), {0, 1});
        case ParameterFunction::None:
            break;
        }
        error(ParameterErrorCode::Name, i, "unknown function");
    }

    const CheckedParameterExpression &checked_;
    const std::vector<ParameterNode> &nodes_;
    std::span<const ParameterValue> values_;
    ParameterEvaluationBudget &budget_;
    std::array<Slot, Limits::valueStack> stack_{};
    std::size_t size_{};
};

double quantize(double value, double quantum) {
    // nearbyint uses the current rounding mode, which is round-to-nearest-even:
    // the engine never changes it. Adding +0.0 turns -0 into +0.
    return std::nearbyint(value / quantum) * quantum + 0.0;
}

bool validValue(const ParameterValue &value, const ParameterSignature &signature) {
    switch (signature.type) {
    case ParameterType::Boolean:
        return std::holds_alternative<bool>(value);
    case ParameterType::Choice: {
        const auto *choice = std::get_if<ParameterChoice>(&value);
        return choice && std::find(signature.options.begin(), signature.options.end(),
                                   choice->value) != signature.options.end();
    }
    default: {
        const auto *q = std::get_if<ParameterQuantity>(&value);
        return q && std::isfinite(q->value) && q->dimension == parameterDimension(signature.type);
    }
    }
}

} // namespace

std::string_view parameterErrorCodeName(ParameterErrorCode code) {
    switch (code) {
    case ParameterErrorCode::Syntax:
        return "PARAMETER_SYNTAX";
    case ParameterErrorCode::Name:
        return "PARAMETER_NAME";
    case ParameterErrorCode::Type:
        return "PARAMETER_TYPE";
    case ParameterErrorCode::Domain:
        return "PARAMETER_DOMAIN";
    case ParameterErrorCode::LimitExceeded:
        return "LIMIT_EXCEEDED";
    }
    return "PARAMETER_SYNTAX";
}

ParameterExpressionError::ParameterExpressionError(ParameterErrorCode code, std::size_t offset,
                                                   std::string excerpt, const std::string &message)
    : std::runtime_error(message), code_(code), offset_(offset), excerpt_(std::move(excerpt)) {}

std::string_view parameterTypeName(ParameterType type) {
    switch (type) {
    case ParameterType::Length:
        return "length";
    case ParameterType::Area:
        return "area";
    case ParameterType::Angle:
        return "angle";
    case ParameterType::Number:
        return "number";
    case ParameterType::Count:
        return "count";
    case ParameterType::Boolean:
        return "boolean";
    case ParameterType::Choice:
        return "choice";
    }
    return "number";
}

ParameterDimension parameterDimension(ParameterType type) {
    switch (type) {
    case ParameterType::Length:
        return {1, 0};
    case ParameterType::Area:
        return {2, 0};
    case ParameterType::Angle:
        return {0, 1};
    default:
        return {0, 0};
    }
}

bool isReservedParameterWord(std::string_view word) {
    return functionNamed(word) || unitNamed(word) ||
           std::find(keywordTable.begin(), keywordTable.end(), word) != keywordTable.end();
}

bool isValidParameterName(std::string_view name) {
    if (name.empty() || name.size() > Limits::nameBytes || !isLower(name.front()))
        return false;
    for (const char c : name)
        if (!isWordChar(c))
            return false;
    return !isReservedParameterWord(name);
}

bool isValidParameterChoiceValue(std::string_view value) {
    if (value.empty() || value.size() > Limits::choiceBytes)
        return false;
    for (const char c : value)
        if (!isChoiceChar(c))
            return false;
    return true;
}

ParameterExpression parseParameterExpression(std::string_view text) {
    // Refused before it is tokenized.
    if (text.size() > Limits::textBytes)
        fail(ParameterErrorCode::LimitExceeded, text, Limits::textBytes,
             "expression is longer than 1,024 bytes");
    auto expression = Parser(text).run();
    // Stricter reading of ADR 0165 §2/§7: files store only canonical text and an
    // accepted expression's canonical text must re-parse, so the canonical text
    // must also fit the text limit (spacing and number spelling can lengthen it).
    // Tokens and depth never grow: canonical text has no more tokens or
    // parentheses than any spelling of the same tree.
    if (canonicalParameterExpression(expression).size() > Limits::textBytes)
        fail(ParameterErrorCode::LimitExceeded, text, 0,
             "canonical text is longer than 1,024 bytes");
    return expression;
}

std::string canonicalParameterExpression(const ParameterExpression &expression) {
    const auto &nodes = expression.nodes;
    struct Printed {
        std::string text;
        int precedence{};
    };
    std::vector<Printed> stack;
    const auto wrap = [](const Printed &p, int minimum) {
        return p.precedence >= minimum ? p.text : "(" + p.text + ")";
    };
    for (const auto &n : nodes) {
        std::vector<Printed> args(std::make_move_iterator(stack.end() - n.argumentCount),
                                  std::make_move_iterator(stack.end()));
        stack.resize(stack.size() - n.argumentCount);
        Printed out{std::string(), precedence(n.op)};
        switch (n.op) {
        case ParameterOp::Number:
            out.text = shortest(n.literal);
            if (n.unit != ParameterUnit::None) {
                out.text += ' ';
                out.text += unitName(n.unit);
            }
            break;
        case ParameterOp::Choice:
            out.text = "\"" + n.text + "\"";
            break;
        case ParameterOp::True:
            out.text = "true";
            break;
        case ParameterOp::False:
            out.text = "false";
            break;
        case ParameterOp::Pi:
            out.text = "pi";
            break;
        case ParameterOp::Name:
            out.text = n.text;
            break;
        case ParameterOp::Negate:
            out.text = "-" + wrap(args[0], 7);
            break;
        case ParameterOp::Plus:
            out.text = "+" + wrap(args[0], 7);
            break;
        case ParameterOp::Not:
            out.text = "not " + wrap(args[0], 3);
            break;
        case ParameterOp::Or:
        case ParameterOp::And:
        case ParameterOp::Add:
        case ParameterOp::Subtract:
        case ParameterOp::Multiply:
        case ParameterOp::Divide: {
            // Left-associative: the right operand needs a strictly tighter level.
            const auto level = precedence(n.op);
            out.text = wrap(args[0], level) + " " + std::string(operatorText(n.op)) + " " +
                       wrap(args[1], level + 1);
            break;
        }
        case ParameterOp::Equal:
        case ParameterOp::NotEqual:
        case ParameterOp::Less:
        case ParameterOp::LessEqual:
        case ParameterOp::Greater:
        case ParameterOp::GreaterEqual:
            // Not associative: both operands are sums.
            out.text =
                wrap(args[0], 5) + " " + std::string(operatorText(n.op)) + " " + wrap(args[1], 5);
            break;
        case ParameterOp::Power:
            out.text = wrap(args[0], 9) + " ^ " + std::to_string(int(n.exponent));
            break;
        case ParameterOp::Call:
            out.text = std::string(functionName(n.function)) + "(";
            for (std::size_t k = 0; k < args.size(); ++k) {
                if (k)
                    out.text += ", ";
                out.text += args[k].text;
            }
            out.text += ")";
            break;
        }
        stack.push_back(std::move(out));
    }
    return stack.empty() ? std::string() : stack.back().text;
}

bool sameParameterTree(const ParameterExpression &a, const ParameterExpression &b) {
    if (a.nodes.size() != b.nodes.size())
        return false;
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        const auto &x = a.nodes[i];
        const auto &y = b.nodes[i];
        if (x.op != y.op || x.function != y.function || x.unit != y.unit ||
            x.exponent != y.exponent || x.argumentCount != y.argumentCount ||
            x.subtreeSize != y.subtreeSize ||
            std::bit_cast<std::uint64_t>(x.literal) != std::bit_cast<std::uint64_t>(y.literal) ||
            std::bit_cast<std::uint64_t>(x.si) != std::bit_cast<std::uint64_t>(y.si) ||
            x.text != y.text)
            return false;
    }
    return true;
}

CheckedParameterExpression checkParameterExpression(const ParameterExpression &expression,
                                                    const ParameterSymbols &symbols,
                                                    const ParameterSignature &expected) {
    return Checker(expression, symbols, expected).run();
}

ParameterValue evaluateParameterExpression(const CheckedParameterExpression &checked,
                                           std::span<const ParameterValue> values,
                                           ParameterEvaluationBudget &budget) {
    if (values.size() != checked.references.size())
        throw std::invalid_argument("Parameter values do not match the expression's references");
    for (std::size_t i = 0; i < values.size(); ++i)
        if (!validValue(values[i], checked.referenceTypes[i]))
            throw std::invalid_argument("Parameter value does not match its declared type");
    const auto slot = Evaluator(checked, values, budget).run();
    ParameterValue raw;
    if (slot.kind == Kind::Boolean)
        raw = slot.boolean;
    else if (slot.kind == Kind::Choice)
        raw = ParameterChoice{std::string(slot.choice)};
    else
        raw = ParameterQuantity{slot.number, slot.dimension};
    return finalizeParameterValue(raw, checked.result, checked.expression.source,
                                  checked.expression.nodes.back().start);
}

double quantizeParameterLength(double metres) { return quantize(metres, parameterLengthQuantum); }
double quantizeParameterArea(double squareMetres) {
    return quantize(squareMetres, parameterAreaQuantum);
}
double quantizeParameterAngle(double radians) { return quantize(radians, parameterAngleQuantum); }

ParameterValue finalizeParameterValue(const ParameterValue &value, const ParameterSignature &type,
                                      std::string_view source, std::size_t offset) {
    const auto index = offset == 0 ? 0 : offset - 1;
    const auto domain = [&](std::string_view reason) {
        fail(ParameterErrorCode::Domain, source, index, reason);
    };
    switch (type.type) {
    case ParameterType::Boolean:
        if (!std::holds_alternative<bool>(value))
            fail(ParameterErrorCode::Type, source, index, "value is not a boolean");
        return value;
    case ParameterType::Choice:
        if (!validValue(value, type))
            fail(ParameterErrorCode::Type, source, index, "value is not one of the options");
        return value;
    default:
        break;
    }
    const auto *q = std::get_if<ParameterQuantity>(&value);
    if (!q || !(q->dimension == parameterDimension(type.type)))
        fail(ParameterErrorCode::Type, source, index, "value has the wrong type");
    const auto v = q->value;
    if (!std::isfinite(v))
        domain("value is not finite");
    ParameterQuantity out = *q;
    switch (type.type) {
    case ParameterType::Length:
        if (v < -1e6 || v > 1e6)
            domain("length outside -1e6..1e6 m");
        out.value = quantizeParameterLength(v);
        break;
    case ParameterType::Area:
        if (v < -1e12 || v > 1e12)
            domain("area outside -1e12..1e12 m^2");
        out.value = quantizeParameterArea(v);
        break;
    case ParameterType::Angle:
        if (v < -1e4 || v > 1e4)
            domain("angle outside -1e4..1e4 rad");
        out.value = quantizeParameterAngle(v);
        break;
    case ParameterType::Number:
        if (v < -1e15 || v > 1e15)
            domain("number outside -1e15..1e15");
        out.value = v + 0.0;
        break;
    case ParameterType::Count:
        if (v != std::trunc(v))
            domain("count is not a whole number; use round()");
        if (v < 0 || v > 10000)
            domain("count outside 0..10,000");
        out.value = v + 0.0;
        break;
    default:
        break;
    }
    return out;
}

} // namespace sketchy
