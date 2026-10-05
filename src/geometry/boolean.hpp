#pragma once
#include "geometry/solid.hpp"
#include <string>
namespace sketchy {
enum class BooleanOperation { Union, Subtract, Intersect };
class BooleanError : public std::runtime_error {
    std::string code_;
    int operand_;
    SolidReport report_;

  public:
    BooleanError(std::string code, std::string message, int operand = -1, SolidReport report = {})
        : std::runtime_error(std::move(message)), code_(std::move(code)), operand_(operand),
          report_(std::move(report)) {}
    const std::string &code() const { return code_; }
    int operand() const { return operand_; }
    const SolidReport &report() const { return report_; }
};
struct BooleanFaceSource {
    unsigned operand{};
    Id face{};
    bool reversed{};
    bool operator==(const BooleanFaceSource &) const = default;
};
struct BooleanPart {
    Surface surface;
    std::map<Id, BooleanFaceSource> sources;
    double volume{};
};
struct BooleanResult {
    std::vector<BooleanPart> parts;
    double volume{};
};
// Immutable bounded solid adapter. Inputs share one coordinate frame. Disconnected
// positive shells become separate parts; enclosed cavity shells reject explicitly.
BooleanResult booleanSolids(const Surface &a, const Surface &b, BooleanOperation operation);
} // namespace sketchy
