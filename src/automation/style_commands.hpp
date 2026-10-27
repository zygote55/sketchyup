#pragma once
#include "core/model.hpp"
#include <QJsonObject>
#include <QString>

namespace sketchy {

// The style commands (ADR 0174 §1, §5, §7). style.apply replaces the document style in one
// undoable edit, adding its edge texture as a managed asset (or reusing an identical one). A
// missing or mismatched texture refuses the whole style and names it: nothing falls back. The bytes
// travel as canonical base64 and no command reads or writes a path.
bool isStyleCommand(const QString &name);
// Returns the operation's report.
QJsonObject executeStyleCommand(Document &doc, const QJsonObject &command);
// style.export is a query, since a batch with no change is refused. It captures the document style
// and its texture as `.sketchystyle` bytes, returned as base64 `data`, and changes nothing.
QJsonObject styleExport(const Document &doc, const QJsonObject &request);

} // namespace sketchy
