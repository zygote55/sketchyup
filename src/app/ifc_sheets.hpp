#pragma once
// R097.e1: the IFC import sheets (ADR 0171 sections 16.5, 16.6, 16.7). The import report in the
// shared report sheet (banner, what came in, the accounting identity with its bar, how it was
// read, the grouped list, the footer), the refusal sheet, and the "not installed" / "cannot
// start" information sheet. All reuse the shared ReportSheet.
#include "app/report_sheet.hpp"
#include "app/ifc_availability.hpp"
#include <QJsonObject>
namespace sketchy {
// The import report (16.5). `elapsedMs` and `restarts` are the application's; the rest is data.
void showIfcImportReport(QWidget *parent, const QString &fileName, const QJsonObject &report,
                          qint64 elapsedMs, int restarts);
// A refusal (16.6): what was checked and what failed, the words the helper used, what is
// unaffected. The model is unchanged.
void showIfcRefusal(QWidget *parent, const QString &fileName, const std::string &code,
                      const QString &message, const QJsonObject &report);
// R097.e2: the IFC export screens (ADR 0171 section 16.8) — the result and the refusal, the
// import's own shapes with the export's words: nothing was written, the model is unchanged.
void showIfcExportReport(QWidget *parent, const QString &path, const QJsonObject &report,
                          qint64 elapsedMs);
void showIfcExportRefusal(QWidget *parent, const QString &path, const std::string &code,
                          const QString &message, const QJsonObject &report);
// The helper is missing (16.7): "IFC import is not installed". Nothing else is affected.
void showIfcUnavailable(QWidget *parent, const IfcProbe &probe, bool exportDirection);
// R098.a3: the STEP versions (ADR 0172 section 8): the same screens' own words. The report shows
// the helper's own counts (they ride the report as `step`) beside the application's conversion;
// the tessellation policy and the triangle caps come from the application's own choice (the
// report's `tessellation`).
void showStepImportReport(QWidget *parent, const QString &fileName, const QJsonObject &report,
                          qint64 elapsedMs, int restarts);
void showStepRefusal(QWidget *parent, const QString &fileName, const std::string &code,
                      const QString &message, const QJsonObject &report);
// The STEP import runs in the IFC helper; it needs none of the IFC kernel, so only "not
// installed" and "cannot start" reach this sheet (the helper's Limited state is a non-event
// for STEP, and the window does not show the sheet for it).
void showStepUnavailable(QWidget *parent, const IfcProbe &probe);
} // namespace sketchy
