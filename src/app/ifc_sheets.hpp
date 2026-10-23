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
// The helper is missing (16.7): "IFC import is not installed". Nothing else is affected.
void showIfcUnavailable(QWidget *parent, const IfcProbe &probe, bool exportDirection);
} // namespace sketchy
