#pragma once
// The `report.run` query's request and data (ADR 0175 §1, §3, §4, §6.2), and the CLI's report file
// (§8). The counting pass is core/report.hpp; this file validates the request and encodes the
// result.
#include "core/report.hpp"
#include <QJsonObject>
#include <QString>
#include <QStringList>
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

// `sketchyup-cli --report` (ADR 0175 §8, R101.b): the flags as they were typed. Metrics and
// precision are validated by reportRequestFrom, so the CLI and the session refuse the same things.
struct ReportFileOptions {
    QString path;                             // A new .csv or .json file; existing paths refused.
    QString format;                           // csv or json; empty means the path's extension.
    QString metrics{"all"};                   // all, or a comma list of counts, areas, volumes.
    QString groupBy{"none"};                  // none, kind, tag, material or definition.
    bool visible{};                           // --visible: the `visible` filter.
    QStringList tags, materials, definitions; // Repeated --tag, --material, --definition IDs.
    QString precision;                        // full or an integer; empty means the document's.
};

// Writes the report to a new file: CSV (the file of record) or the JSON `data` object, compact and
// without a final newline, so it is byte-identical to the session's report.run data. Returns the
// export report. The model is read, never changed.
QJsonObject writeReportFile(const Document &doc, const ReportFileOptions &options);

QJsonObject reportExportCapabilities();
QJsonObject reportExportDescription();

} // namespace sketchy
