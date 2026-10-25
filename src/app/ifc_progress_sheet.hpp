#pragma once
// R097.e1: the IFC import's progress sheet (ADR 0171 section 16.4) — the shared exchange
// progress sheet with the import's own face: the seven steps, the helper's phases, the
// restart line.
#include "app/exchange_progress_sheet.hpp"
namespace sketchy {
class IfcProgressSheet : public ExchangeProgressSheet {
    Q_OBJECT
  public:
    IfcProgressSheet(ExchangeRunner *runner, const QString &fileName, QWidget *parent = nullptr)
        : ExchangeProgressSheet(runner, ifcImportProgressConfig(), fileName, parent) {}
};
} // namespace sketchy
