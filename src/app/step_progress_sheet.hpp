#pragma once
// R098.a3: the STEP import's progress sheet (ADR 0172 section 8) — the shared exchange
// progress sheet with the STEP import's own face: the import's seven steps, the STEP
// helper's phases and the tessellation's bar.
#include "app/exchange_progress_sheet.hpp"
namespace sketchy {
class StepProgressSheet : public ExchangeProgressSheet {
    Q_OBJECT
  public:
    StepProgressSheet(ExchangeRunner *runner, const QString &fileName, QWidget *parent = nullptr)
        : ExchangeProgressSheet(runner, stepImportProgressConfig(), fileName, parent) {}
};
} // namespace sketchy
