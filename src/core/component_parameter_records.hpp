#pragma once
// R093.b: component parameter records (ADR 0165 §1, §3, §4).
//
// A parametric definition carries one immutable ComponentParameters record: an
// ordered list of parameters and a list of drivers. A top-level placement may carry
// overrides for the definition's input parameters (ComponentInstance::overrides).
// Variants, values and driver states are derived, never stored. Schema 28 persists
// the records, the overrides and the driver allocator (R093.c, ADR 0165 §5).
#include "core/body.hpp"
#include "core/parameter_expression.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace sketchy {
// ADR 0165 §3. Each limit is checked before the work it bounds.
struct ComponentParameterLimits {
    static constexpr std::size_t parameters = 128;            // per definition
    static constexpr std::size_t drivers = 256;               // per definition
    static constexpr std::size_t parametricDefinitions = 1024; // per document
    static constexpr std::size_t options = 32;                // per choice parameter
    static constexpr std::size_t repeatMaxCount = 1000;       // per repeat driver
    static constexpr std::size_t reservedMembers = 4096;      // per definition
    static constexpr std::size_t nesting = 8;                 // parametric levels on one path
    static constexpr std::size_t definitionVisits = 200000;   // one definition evaluation
    static constexpr std::size_t documentVisits = 20000000;   // all distinct evaluations
    static constexpr std::size_t variants = 4096;             // distinct non-template variants
    static constexpr std::size_t recordBytes = 4 * 1024 * 1024; // parameter records, accounted
    static constexpr std::size_t labelBytes = 128;
};

enum class ComponentParameterMode : std::uint8_t { Input, Derived };
struct ComponentParameterOption {
    std::string value, label;
    bool operator==(const ComponentParameterOption &) const = default;
};
// Bounds are SI literals; they never add graph edges. `options` is for choices only.
struct ComponentParameter {
    std::string name, label;
    ParameterType type{ParameterType::Length};
    ComponentParameterMode mode{ComponentParameterMode::Input};
    std::string expression; // canonical text: the default (input) or the formula (derived)
    std::optional<double> min, max, step;
    std::vector<ComponentParameterOption> options;
    bool operator==(const ComponentParameter &) const = default;
};

enum class ComponentDriverKind : std::uint8_t {
    Stretch,
    Position,
    Rotation,
    Visibility,
    Repeat,
    ChildParameter
};
enum class ComponentAxis : std::uint8_t { X, Y, Z };
using ComponentDigest = std::array<std::uint8_t, 32>;
// One expression tied to one target member. Fields outside the driver's kind keep
// their defaults: `axis` (stretch, position, rotation, repeat), `at` (stretch, in
// member-local metres), `maxCount`, `spacing` and `firstMember` (repeat) and
// `parameter` (childParameter). `bound` is the SHA-256 of the canonical encoding
// of what the driver acts on when it was last stamped (ADR 0165 §4).
struct ComponentDriver {
    Id id{};
    ComponentDriverKind kind{ComponentDriverKind::Stretch};
    Id member{};
    std::string expression;
    ComponentAxis axis{ComponentAxis::X};
    double at{};
    std::uint32_t maxCount{};
    std::string spacing;
    Id firstMember{};
    std::string parameter;
    ComponentDigest bound{};
    bool operator==(const ComponentDriver &) const = default;
};
// The library assembly a definition was built from (ADR 0166 §6.2; R094.b): `id`
// matches [a-z0-9_-]{1,64}(/[a-z0-9_-]{1,64})? and `version` is 1-2,147,483,647.
// Bundle capture and insertion keep it; make-unique leaves it off the copy.
struct ComponentAssembly {
    std::string id;
    std::uint32_t version{};
    bool operator==(const ComponentAssembly &) const = default;
};
inline constexpr std::uint32_t componentAssemblyMaxVersion = 2147483647;
// True when `id` and `version` are a well-formed assembly identity.
bool validComponentAssembly(std::string_view id, double version);
struct ComponentParameters {
    std::vector<ComponentParameter> parameters; // display order
    std::vector<ComponentDriver> drivers;       // ascending ID
    std::optional<ComponentAssembly> assembly;  // optional library identity
    bool operator==(const ComponentParameters &) const = default;
};
using ComponentParametersPtr = std::shared_ptr<const ComponentParameters>;
// Override values keyed by parameter name: SI quantities (finalized and quantized
// like evaluated values), booleans or choice values.
using ComponentOverrides = std::map<std::string, ParameterValue>;

// ---- Errors ----
// The codes are ADR 0165's: PARAMETER_SYNTAX, PARAMETER_NAME, PARAMETER_TYPE,
// PARAMETER_DOMAIN, PARAMETER_CYCLE, PARAMETER_DRIVER_TARGET, PARAMETER_REGENERATION,
// PARAMETER_TEMPLATE_SCOPE, and the reused LIMIT_EXCEEDED, LOCKED_ENTITY, NOT_FOUND
// and INVALID_TARGET. what() is at most 256 bytes and quotes at most 64 bytes of a
// name or expression.
struct ComponentParameterErrorContext {
    Id definition{}, driver{}, placement{};
    std::string parameter{};             // the parameter name, when one is involved
    std::size_t offset{};                // 1-based byte offset in the expression (0: none)
    std::string excerpt{};               // at most 64 bytes of the expression
    std::vector<std::string> cycle{};    // PARAMETER_CYCLE: up to 8 names of one cycle, in order
    std::vector<std::string> referrers{}; // PARAMETER_NAME on remove: what still refers to it
    // PARAMETER_DOMAIN from bounds (R094.b): which bound ("min", "max" or "step"), its
    // SI value and the refused SI value.
    std::string bound{};
    double limit{}, value{};
};
class ComponentParameterError : public std::runtime_error {
  public:
    ComponentParameterError(std::string code, const std::string &message,
                            ComponentParameterErrorContext context = {});
    const std::string &code() const noexcept { return code_; }
    const ComponentParameterErrorContext &context() const noexcept { return context_; }
    // The same error naming a placement (when it does not name one yet).
    ComponentParameterError withPlacement(Id placement) const;

  private:
    std::string code_;
    ComponentParameterErrorContext context_;
};
// A message of at most 256 bytes, cut at a UTF-8 boundary.
std::string boundedParameterMessage(std::string_view message);
// A single-quoted, control-free name or expression cut to 64 bytes.
std::string quotedParameterText(std::string_view text);
// Wrap an expression engine error with its definition and subject.
[[noreturn]] void throwParameterExpressionError(const ParameterExpressionError &error,
                                                ComponentParameterErrorContext context);

std::string_view componentDriverKindName(ComponentDriverKind kind);
std::string_view componentAxisName(ComponentAxis axis);
Vec3 componentAxisVector(ComponentAxis axis);

// ---- Accounting (ADR 0165 §3: parameter records in history and snapshots) ----
std::size_t componentParametersBytes(const ComponentParametersPtr &record);
std::size_t componentOverridesBytes(const ComponentOverrides &overrides);

// ---- Canonical encoding and digests ----
// A deterministic, platform-independent byte encoding: integers little-endian,
// doubles by their IEEE 754 bit pattern, strings length-prefixed, maps in key order.
// Body encodings omit the allocator floors (surface and topology nextId), which may
// rise on undo without changing the record. Used for `bound` digests and test dumps.
void appendCanonicalBody(std::string &out, const Body &body);
void appendCanonicalParameterList(std::string &out, const std::vector<ComponentParameter> &list);
void appendCanonicalParameters(std::string &out, const ComponentParameters &record);
void appendCanonicalValue(std::string &out, const ParameterValue &value);
void appendCanonicalOverrides(std::string &out, const ComponentOverrides &overrides);
ComponentDigest sha256Digest(std::string_view bytes);
std::string digestHex(const ComponentDigest &digest);
} // namespace sketchy
