#pragma once
// R097.e1: the IFC import sheets (ADR 0171 sections 16.5, 16.6, 16.7). The import report in the
// shared report sheet (banner, what came in, the accounting identity with its bar, how it was
// read, the grouped list, the footer), the refusal sheet, and the "not installed" / "cannot
// start" information sheet. All reuse the shared ReportSheet.
#include "app/report_sheet.hpp"
#include "app/import_report.hpp"
#include "app/ifc_availability.hpp"
#include <QJsonObject>
#include <functional>
namespace sketchy {
// The IFC import's report face (ADR 0171 section 16.5) and the STEP import's (ADR 0172
// section 8) are the faces of the one shared report builder (showImportReport, in this
// file); a new format (R102.c, the .skp import) plugs in with its own face.
// The one shared refusal sheet (the screens refactor): the import's (what was checked and what
// failed, the words the helper used) and the export's (nothing was written, the code, what to
// do next) are the same shape — a red top line, the summary, the helper's own details (when it
// has any), the footer. The model is unchanged either way.
struct RefusalSheet {
    QString sheetName;
    QString title;
    int width = 640;
    int height = 420;
    QString prefix; // the objectName prefix
    QString leadingLine; // the red "✗ …" line, or ""
    QString refusedLine; // the "Refused: …" line, or ""
    QString summary;
    QJsonObject report;
    bool copyDetails = false;
    QString closeButton = "OK";
    QString closeSuffix = "OK"; // the objectName suffix of the footer button
    bool closeIsDefault = false;
};
void showRefusalSheet(QWidget *parent, const RefusalSheet &face);
// A refusal (16.6): the IFC and STEP import refusals are thin wrappers over showRefusalSheet;
// the model is unchanged.
void showIfcRefusal(QWidget *parent, const QString &fileName, const std::string &code,
                      const QString &message, const QJsonObject &report);
void showStepRefusal(QWidget *parent, const QString &fileName, const std::string &code,
                      const QString &message, const QJsonObject &report);
// The one shared availability sheet (the screens refactor): the helper's own states with the
// application's own words; the broken state shows the helper's own last log line at the bottom
// of the message, so a person can act on it (ADR 0171 §16.6).
struct AvailabilitySheet {
    QString sheetName; // the state's own sheet name (the IFC's Limited has its own)
    QString title; // "IFC import" / "IFC export" / "STEP import"
    int width = 600;
    int height = 380;
    QString prefix;
    QString text;
};
void showAvailabilitySheet(QWidget *parent, const AvailabilitySheet &face);
void showIfcUnavailable(QWidget *parent, const IfcProbe &probe, bool exportDirection);
// The STEP import's and the STEP export's (R098.c2) availability: the same helper, the export's own words.
void showStepUnavailable(QWidget *parent, const IfcProbe &probe, bool exportDirection = false);
// The IFC export refusal, the same shape with the export's own words (what to do next, the
// copy button): a thin wrapper over showRefusalSheet.
void showIfcExportRefusal(QWidget *parent, const QString &path, const std::string &code,
                          const QString &message, const QJsonObject &report);
// R097.e2: the IFC export screens (ADR 0171 section 16.8) — the result and the refusal, the
// import's own shapes with the export's words: nothing was written, the model is unchanged.
void showIfcExportReport(QWidget *parent, const QString &path, const QJsonObject &report,
                          qint64 elapsedMs, const std::function<void(qint64)> &selectInModel = nullptr);
// R098.c2 (ADR 0172 section 8): the STEP export's refusal, the IFC export refusal's shape with the
// STEP export's words: nothing was written, the model is unchanged; the code names the check that
// failed (AP242 is refused for the schema, the size limits, the helper stopping or a mismatch).
void showStepExportRefusal(QWidget *parent, const QString &path, const std::string &code,
                           const QString &message, const QJsonObject &report);
} // namespace sketchy
