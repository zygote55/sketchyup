#pragma once
// R101.b (ADR 0175 §6.1): the CSV file of record for a report. It is a rendering of the report.run
// data object (automation/report_commands.hpp), so the CSV and the JSON are the same numbers.
#include <QByteArray>
#include <QJsonObject>

namespace sketchy {

// The exact v1 dialect: UTF-8 without a BOM, LF line endings, `#key=value` metadata lines in the
// fixed order, the header `section,group,tag,material,definition,metric,value`, one row per
// (section, group, metric[, material]), and minimal RFC 4180 quoting. Measures are the decimal
// strings of the data object and counts are integers, so no number is quoted.
QByteArray reportCsv(const QJsonObject &data);

} // namespace sketchy
