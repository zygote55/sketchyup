#pragma once
#include "core/selection.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
class InspectionError : public std::runtime_error {
  public:
    InspectionError(std::string code, std::string message, QJsonObject details = {})
        : std::runtime_error(std::move(message)), code_(std::move(code)),
          details_(std::move(details)) {}
    const std::string &code() const { return code_; }
    // Optional structured fields of the refusal (component parameter errors carry
    // ADR 0165's context; R094.b). Reported as `details` when not empty.
    const QJsonObject &details() const { return details_; }

  private:
    std::string code_;
    QJsonObject details_;
};
inline constexpr int inspectionRequestBytes = 16 * 1024;
inline constexpr int inspectionResponseBytes = 256 * 1024;
inline constexpr int inspectionPageLimit = 100;
// This registry is also the authoritative parameter validator. The legacy local
// executeQuery interface is deliberately not the bounded transport contract.
QJsonArray inspectionCatalog();
QJsonObject inspectionCapabilities();
QJsonObject inspectionReference(const Document &doc, Id body, const QString &kind = "body",
                                Id entity = 0);
// Read-only, synchronous access. The caller owns document/editor serialization.
// A null selection means editor selection is unavailable, never fabricated empty.
QJsonObject inspectDocument(const Document &doc, const QJsonObject &request,
                            const Selection *selection = nullptr);
} // namespace sketchy
