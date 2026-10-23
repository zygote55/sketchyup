#pragma once
// R097.b: the application's side of the IFC import (ADR 0171 sections 3 and 4): a decoded conversion
// package becomes one new unsaved document in one edit, and the worker's findings and the
// conversion's own become the import report (apiVersion 1, format IFC).
#include "io/document_io.hpp"
#include "io/ifc_package.hpp"
#include <QJsonObject>
#include <QString>
#include <functional>
#include <stdexcept>
namespace sketchy {
enum class IfcSpaces { Hidden, Shown, Skip };
// ADR 0171 decision 6: keep the project axes (default), or rotate the model to the map grid.
enum class IfcGeoreference { KeepAxes, Grid };
struct IfcImportOptions {
    // Spaces import on the tag "IFC Space": hidden (the tag starts hidden), shown, or not at all.
    IfcSpaces spaces{IfcSpaces::Hidden};
    // Grid: a map conversion with a rotation turns the model by it (so +X is east) and sets the site origin; the
    // default keeps the project axes and reports the conversion. Without a rotation both do the same.
    IfcGeoreference georeference{IfcGeoreference::KeepAxes};
};
// What the application knows about the source and the run, which the worker does not.
struct IfcRunFacts {
    QString sha256;
    qint64 bytes{};
    QJsonObject run; // {restarts, threads, deadlineSeconds, excluded: [...], ...}: merged into the engine block
};
struct IfcImport {
    Document document;
    QJsonObject report;
};
// Refusals and failures that carry the report so far (limits, units, an unusable package).
class IfcImportError : public std::runtime_error {
  public:
    IfcImportError(std::string code, const std::string &message, QJsonObject report = {})
        : std::runtime_error(message), code_(std::move(code)), report_(std::move(report)) {}
    const std::string &code() const { return code_; }
    const QJsonObject &report() const { return report_; }

  private:
    std::string code_;
    QJsonObject report_;
};
struct IfcConversionHooks {
    // Returns true to cancel; checked between nodes. May be empty.
    std::function<bool()> cancelled;
};
// One document, one edit, validated as a whole serialization before it is returned.
IfcImport importIfcPackage(const IfcPackage &package, const IfcImportOptions &options,
                           const IfcRunFacts &facts, const IfcConversionHooks &hooks = {});
// Bounds a report to 64 KiB, 64 kinds, 32 locations per kind and 256-byte messages; counts stay complete.
void boundIfcReport(QJsonObject &report);
// The tag an element class gets: "IFC Wall", "IFC Building Element Proxy", ...
std::string ifcTagName(const std::string &className);
} // namespace sketchy
