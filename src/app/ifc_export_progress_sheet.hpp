#pragma once
// R097.e2: the IFC export's progress sheet (ADR 0171 section 16.8) — the shared exchange
// progress sheet with the export's own face: the four steps, the busy bar and the focused
// Cancel; a cancelled export just closes (no finished state, no report).
#include "app/exchange_progress_sheet.hpp"
namespace sketchy {
class IfcExportProgressSheet : public ExchangeProgressSheet {
    Q_OBJECT
  public:
    IfcExportProgressSheet(ExchangeRunner *runner, const QString &fileName, QWidget *parent = nullptr)
        : ExchangeProgressSheet(runner, ifcExportProgressConfig(), fileName, parent) {}
};
} // namespace sketchy
