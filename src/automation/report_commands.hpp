#pragma once
// The `report.run` query's request and data (ADR 0175 §1, §3, §4, §6.2). The counting pass is
// core/report.hpp; this file validates the request and encodes the result.
#include "core/report.hpp"
#include <QJsonObject>
#include <set>

namespace sketchy {

// Parses the query's `request` object. Refuses unknown fields, unknown or repeated metrics, IDs
// that do not exist, precision outside the document's units and a selection without editor state,
// each with a named reason (InspectionError). Bounds on lengths and ID counts are enforced by the
// registry schema before this runs.
ReportRequest reportRequestFrom(const Document &doc, const QJsonObject &request,
                                bool editorAvailable);

// The `data` of report.run: the metadata block (the stamp, units, precision, applied filters) and
// the tables. Measures are decimal strings, counts are integers (ADR 0175 §6.2).
QJsonObject reportDescription(const Document &doc, const ReportRequest &request,
                              const std::set<Id> *selectedBodies);

} // namespace sketchy
