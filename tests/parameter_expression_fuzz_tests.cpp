// R093.a (ADR 0165 §7): in-ctest seeded fuzzing of the parameter expression
// engine, in the dev and sanitize presets as geometry_fuzz is. A typed generator
// produces 20,000 expressions (mostly well-typed, with random spelling), and
// every seed is mutated 400 times. Each input goes through the shared harness:
// closed error codes with offsets inside the input, limits, canonical fixed
// point, same tree and same value after re-parsing the canonical text.
//   parameter_expression_fuzz_tests [--seed N] [--count N]
#include "fuzz/parameter_expression_harness.hpp"
#include <charconv>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace sketchy;
namespace {

class Random {
  public:
    explicit Random(std::uint32_t seed) : state_(seed ? seed : 0x9e3779b9u) {}
    std::uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    int pick(int n) { return int(next() % std::uint32_t(n)); }
    bool chance(int percent) { return pick(100) < percent; }

  private:
    std::uint32_t state_;
};

struct Dim {
    int l{}, a{};
};
bool inRange(Dim d) { return d.l >= -3 && d.l <= 3 && d.a >= -3 && d.a <= 3; }

class Generator {
  public:
    explicit Generator(Random &random) : r_(random) {}

    std::string expression() {
        depth_ = 1 + r_.pick(5);
        switch (r_.pick(7)) {
        case 0:
            return quantity({1, 0}, depth_);
        case 1:
            return quantity({2, 0}, depth_);
        case 2:
            return quantity({0, 1}, depth_);
        case 3:
        case 4:
            return quantity({0, 0}, depth_);
        case 5:
            return boolean(depth_);
        default:
            return choice(depth_);
        }
    }

  private:
    std::string gap() {
        switch (r_.pick(5)) {
        case 0:
            return "";
        case 1:
            return "\t";
        case 2:
            return "  ";
        default:
            return " ";
        }
    }
    std::string space() { return r_.chance(80) ? " " : (r_.chance(50) ? "\t" : "   "); }
    std::string binary(const std::string &a, const char *op, const std::string &b) {
        return a + gap() + op + gap() + b;
    }
    std::string word(const std::string &a, const char *op, const std::string &b) {
        return a + space() + op + space() + b;
    }
    std::string paren(const std::string &a) { return "(" + gap() + a + gap() + ")"; }
    std::string call(const char *name, const std::vector<std::string> &args) {
        std::string out = std::string(name) + "(";
        for (std::size_t i = 0; i < args.size(); ++i)
            out += (i ? "," + gap() : gap()) + args[i];
        return out + gap() + ")";
    }
    std::string number() {
        switch (r_.pick(9)) {
        case 0:
            return std::to_string(r_.pick(2000));
        case 1:
            return std::to_string(r_.pick(100)) + "." + std::to_string(r_.pick(1000));
        case 2:
            return "." + std::to_string(1 + r_.pick(99));
        case 3:
            return std::to_string(1 + r_.pick(9)) + ".";
        case 4:
            return std::to_string(1 + r_.pick(9)) + (r_.chance(50) ? "e" : "E") +
                   (r_.chance(50) ? "-" : (r_.chance(50) ? "+" : "")) + std::to_string(r_.pick(4));
        case 5:
            return "00" + std::to_string(r_.pick(10));
        case 6:
            return "0";
        default:
            return std::to_string(1 + r_.pick(20));
        }
    }
    std::string withUnit(const char *const *units, int count) {
        return number() + (r_.chance(50) ? " " : "") + units[r_.pick(count)];
    }

    std::string leaf(Dim d) {
        static const char *const lengths[] = {"mm", "cm", "m", "in", "ft"};
        static const char *const angles[] = {"deg", "rad"};
        if (d.l == 0 && d.a == 0) {
            switch (r_.pick(5)) {
            case 0:
                return "pi";
            case 1:
                return "ratio";
            case 2:
                return "shelves";
            default:
                return number();
            }
        }
        if (d.l == 1 && d.a == 0)
            return r_.chance(40) ? "width" : withUnit(lengths, 5);
        if (d.l == 2 && d.a == 0 && r_.chance(60))
            return "floor_area";
        if (d.l == 0 && d.a == 1)
            return r_.chance(40) ? "tilt" : withUnit(angles, 2);
        // Compose from unit steps.
        if (d.l != 0) {
            const Dim rest{d.l > 0 ? d.l - 1 : d.l + 1, d.a};
            return paren(binary(leaf(rest), d.l > 0 ? "*" : "/", leaf({1, 0})));
        }
        const Dim rest{d.l, d.a > 0 ? d.a - 1 : d.a + 1};
        return paren(binary(leaf(rest), d.a > 0 ? "*" : "/", leaf({0, 1})));
    }

    std::string quantity(Dim d, int depth) {
        if (depth <= 0 || r_.chance(25))
            return leaf(d);
        const auto q = [&](Dim e) { return quantity(e, depth - 1); };
        const bool plain = d.l == 0 && d.a == 0;
        switch (r_.pick(plain ? 18 : (d.l == 0 && d.a == 1) ? 16 : 14)) {
        case 0:
            return binary(q(d), "+", q(d));
        case 1:
            return binary(q(d), "-", paren(q(d)));
        case 2: {
            const Dim left{r_.pick(3) - 1, r_.pick(3) - 1};
            const Dim right{d.l - left.l, d.a - left.a};
            if (!inRange(right))
                return leaf(d);
            return binary(paren(q(left)), "*", paren(q(right)));
        }
        case 3: {
            const Dim divisor{r_.pick(3) - 1, r_.pick(2)};
            const Dim dividend{d.l + divisor.l, d.a + divisor.a};
            if (!inRange(dividend))
                return leaf(d);
            return binary(paren(q(dividend)), "/", paren(q(divisor)));
        }
        case 4:
            return "-" + gap() + paren(q(d));
        case 5:
            return r_.chance(50) ? "+" + leaf(d) : "-" + leaf(d);
        case 6:
            return paren(q(d));
        case 7: {
            std::vector<std::string> args;
            for (int i = 0, n = 2 + r_.pick(3); i < n; ++i)
                args.push_back(q(d));
            return call(r_.chance(50) ? "min" : "max", args);
        }
        case 8:
            return call("clamp", {q(d), q(d), q(d)});
        case 9:
            return call("abs", {q(d)});
        case 10:
            return call("mod", {q(d), q(d)});
        case 11:
            return call("if", {boolean(depth - 1), q(d), q(d)});
        case 12: {
            for (const int n : {2, 3, -1, -2})
                if (d.l % n == 0 && d.a % n == 0 && r_.chance(50))
                    return paren(q({d.l / n, d.a / n})) + gap() + "^" + gap() + std::to_string(n);
            return paren(q(d)) + gap() + "^" + gap() + "1";
        }
        case 13: {
            const Dim square{d.l * 2, d.a * 2};
            if (!inRange(square))
                return leaf(d);
            return call("sqrt", {q(square)});
        }
        case 14:
        case 15:
            if (plain) {
                static const char *const rounding[] = {"floor", "ceil", "round"};
                return call(rounding[r_.pick(3)], {q({0, 0})});
            }
            if (r_.chance(50)) {
                static const char *const inverse[] = {"asin", "acos", "atan"};
                return call(inverse[r_.pick(3)], {q({0, 0})});
            }
            return call("atan2", {q({1, 0}), q({1, 0})});
        default: {
            static const char *const trig[] = {"sin", "cos", "tan"};
            return call(trig[r_.pick(3)], {q({0, 1})});
        }
        }
    }

    std::string boolean(int depth) {
        if (depth <= 0 || r_.chance(20)) {
            switch (r_.pick(3)) {
            case 0:
                return "true";
            case 1:
                return "false";
            default:
                return "open";
            }
        }
        const auto b = [&] { return boolean(depth - 1); };
        switch (r_.pick(8)) {
        case 0:
            return word(b(), "and", b());
        case 1:
            return word(b(), "or", paren(b()));
        case 2:
            return "not" + space() + b();
        case 3: {
            static const char *const ops[] = {"<", "<=", ">", ">="};
            const Dim d{r_.pick(3), r_.pick(2)};
            return binary(quantity(d, depth - 1), ops[r_.pick(4)], quantity(d, depth - 1));
        }
        case 4:
            return binary(quantity({0, 0}, depth - 1),
                          r_.chance(50) ? "==" : "!=", quantity({0, 0}, depth - 1));
        case 5:
            return binary(paren(b()), r_.chance(50) ? "==" : "!=", paren(b()));
        case 6:
            return binary(choice(depth - 1), "==", "style");
        default:
            return call("if", {b(), b(), b()});
        }
    }

    std::string choice(int depth) {
        static const char *const options[] = {"\"oak\"", "\"pine\"", "\"birch-2\""};
        if (depth <= 0 || r_.chance(40))
            return r_.chance(40) ? "style" : options[r_.pick(3)];
        return call("if", {boolean(depth - 1), choice(depth - 1), choice(depth - 1)});
    }

    Random &r_;
    int depth_{};
};

std::string mutate(std::string text, Random &r) {
    static const std::string alphabet = "0123456789.eE+-*/^()<>=!, \t\"abcdefghijklmnopqrstuvwxyz_"
                                        "$`;#&|{}[]'\\@~%?:\x7f\x80\xff\n";
    const auto at = text.empty() ? 0 : std::size_t(r.pick(int(text.size())));
    switch (r.pick(6)) {
    case 0:
        text.resize(at);
        break;
    case 1:
        if (!text.empty())
            text[at] = char(static_cast<unsigned char>(text[at]) ^ (1u << r.pick(8)));
        break;
    case 2:
        for (int i = 0, n = 1 + r.pick(4); i < n; ++i)
            text.insert(text.begin() + std::ptrdiff_t(std::min(at, text.size())),
                        alphabet[std::size_t(r.pick(int(alphabet.size())))]);
        break;
    case 3:
        text.erase(at, std::size_t(1 + r.pick(6)));
        break;
    case 4:
        text.insert(at, text.substr(std::size_t(r.pick(int(text.size()) + 1))));
        break;
    default:
        text = "(" + text + ")";
        break;
    }
    return text;
}

// FNV-1a over every outcome: lets dev and sanitize runs be compared bit for bit.
struct Digest {
    std::uint64_t value{0xcbf29ce484222325ull};
    void add(std::string_view bytes) {
        for (const unsigned char c : bytes) {
            value ^= c;
            value *= 0x100000001b3ull;
        }
        add(std::uint64_t(bytes.size()));
    }
    void add(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            value ^= (v >> (8 * i)) & 0xff;
            value *= 0x100000001b3ull;
        }
    }
};
void digestOutcome(std::string_view text, Digest &digest) {
    digest.add(text);
    try {
        const auto tree = parseParameterExpression(text);
        digest.add(canonicalParameterExpression(tree));
        const fuzz::HarnessParameterSymbols symbols;
        for (const auto &expected : fuzz::harnessExpectedTypes()) {
            try {
                const auto checked = checkParameterExpression(tree, symbols, expected);
                const auto out = fuzz::harnessEvaluate(checked);
                digest.add(std::uint64_t(out.ok ? 100 : int(out.code)));
                if (const auto *q = std::get_if<ParameterQuantity>(&out.value))
                    digest.add(std::bit_cast<std::uint64_t>(q->value));
                else if (const auto *b = std::get_if<bool>(&out.value))
                    digest.add(std::uint64_t(*b));
                else
                    digest.add(std::get<ParameterChoice>(out.value).value);
            } catch (const ParameterExpressionError &error) {
                digest.add(std::uint64_t(int(error.code()) + 10));
                digest.add(std::uint64_t(error.offset()));
            }
        }
    } catch (const ParameterExpressionError &error) {
        digest.add(std::uint64_t(int(error.code()) + 20));
        digest.add(std::uint64_t(error.offset()));
    }
}

std::uint32_t parse(const char *text) {
    std::uint32_t value{};
    const auto end = text + std::strlen(text);
    const auto result = std::from_chars(text, end, value);
    if (result.ec != std::errc{} || result.ptr != end)
        throw std::runtime_error("Expected uint32 argument");
    return value;
}

const char *const seeds[] = {
    "width - 60 mm",
    "max(shelves, 1)",
    "shelves > 0",
    "if(style == \"oak\", 900 mm, 2 ft)",
    "-ratio ^ 2",
    "clamp(width, 400 mm, 2.4 m)",
    "sqrt(floor_area) * 2",
    "atan2(width, 1 m) + 15 deg",
    "not open and (ratio <= 1.5 or shelves != 3)",
    "round(width / 300 mm)",
    "mod(width, 0.25 m) * ratio",
    "sin(tilt) ^ 2 + cos(tilt) ^ 2",
    "floor_area / width - 1e-3 m",
    "if(open, \"pine\", style)",
    "1 in + 1 ft + 1 cm + 1 mm + 1 m",
    "abs(-tilt) * 2 + pi * 1 rad",
    "width * width * width / (width ^ 2)",
    "min(1, 2, 3, 4, 5, 6, 7, 8)",
    "ceil(ratio) + floor(.5) - +2.",
    "asin(0.5) + acos(0.5) - atan(1)",
    "(width ^ -1) * 1 m",
    "true == (shelves >= 2)",
    "style != \"birch-2\" or false",
    "ratio * 1E+2 / 007",
};

} // namespace

int main(int argc, char **argv) {
    std::uint32_t seed = 0x093a0001, count = 20000;
    std::string current;
    try {
        for (int i = 1; i < argc; i += 2) {
            const std::string arg = argv[i];
            if (i + 1 >= argc)
                throw std::runtime_error("Expected --seed N or --count N");
            const auto value = parse(argv[i + 1]);
            if (arg == "--seed")
                seed = value;
            else if (arg == "--count")
                count = value;
            else
                throw std::runtime_error("Unknown argument " + arg);
        }
        Random random(seed);
        Generator generator(random);
        Digest digest;
        std::size_t accepted = 0, rejected = 0, longest = 0;
        const auto run = [&](const std::string &text) {
            current = text;
            longest = std::max(longest, text.size());
            if (fuzz::parameterExpressionAccepted(text))
                ++accepted;
            else
                ++rejected;
            digestOutcome(text, digest);
        };
        for (std::uint32_t i = 0; i < count; ++i)
            run(generator.expression());
        const auto generatedAccepted = accepted, generatedRejected = rejected;
        std::size_t seedCount = 0;
        for (const auto *text : seeds) {
            if (!fuzz::parameterExpressionAccepted(text))
                throw std::runtime_error(std::string("Seed is not accepted: ") + text);
            ++seedCount;
            for (int m = 0; m < 400; ++m) {
                auto mutated = std::string(text);
                for (int k = 0, n = 1 + random.pick(3); k < n; ++k)
                    mutated = mutate(mutated, random);
                run(mutated);
            }
        }
        const auto mutationAccepted = accepted - generatedAccepted;
        const auto mutationRejected = rejected - generatedRejected;
        if (generatedAccepted * 2 < count)
            throw std::runtime_error("Typed generator produced too few accepted expressions");
        if (mutationAccepted == 0 || mutationRejected == 0)
            throw std::runtime_error("Mutations must reach both acceptance and rejection");
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(digest.value));
        std::cout << "{\"seed\":" << seed << ",\"generated\":" << count
                  << ",\"generatedAccepted\":" << generatedAccepted << ",\"seeds\":" << seedCount
                  << ",\"mutations\":" << seedCount * 400
                  << ",\"mutationAccepted\":" << mutationAccepted
                  << ",\"mutationRejected\":" << mutationRejected << ",\"longestInput\":" << longest
                  << ",\"digest\":\"" << hex << "\"}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "parameter_expression_fuzz seed " << seed << ": " << error.what()
                  << "\ninput: " << current << '\n';
        return 1;
    }
}
