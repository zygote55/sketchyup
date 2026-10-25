#pragma once
// R098.c2 (ADR 0172 section 8): the STEP export's progress sheet — the shared exchange progress
// sheet with the STEP export's own face: the four steps (as the IFC export's), a busy bar and a
// focused Cancel; a cancelled export just closes (no finished state, no report).
#include "app/exchange_progress_sheet.hpp"
namespace sketchy {
class StepExportProgressSheet : public ExchangeProgressSheet {
    Q_OBJECT
  public:
    StepExportProgressSheet(ExchangeRunner *runner, const QString &fileName, QWidget *parent = nullptr)
        : ExchangeProgressSheet(runner, stepExportProgressConfig(), fileName, parent) {}
};
} // namespace sketchy
