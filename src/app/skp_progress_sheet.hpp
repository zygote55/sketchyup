#pragma once
// R102.c (ADR 0176 section 9): the SketchUp import's progress sheet — the shared exchange progress
// sheet with the .skp import's own face: the five steps, the helper's phases and the application's
// own. The bar is busy, since the helper reports phases rather than a running count.
#include "app/exchange_progress_sheet.hpp"

namespace sketchy {

class SkpProgressSheet : public ExchangeProgressSheet {
    Q_OBJECT
  public:
    SkpProgressSheet(ExchangeRunner *runner, const QString &fileName, QWidget *parent = nullptr)
        : ExchangeProgressSheet(runner, skpImportProgressConfig(), fileName, parent) {}
};

} // namespace sketchy
