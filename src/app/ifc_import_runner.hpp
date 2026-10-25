#pragma once
// R097.e1: the IFC import's runner (ADR 0171 section 16.4) — a thin wrapper around the shared
// ExchangeRunner: the whole import (the helper, then the application's own conversion) runs on
// one worker thread, the helper's phases and counts come back as queued signals, Cancel is
// checked between tasks, and the result is read back after the thread has finished.
#include "app/exchange_runner.hpp"
#include "io/ifc_worker.hpp"
namespace sketchy {
class IfcImportRunner : public ExchangeRunner {
  public:
    void begin(const QString &path, IfcImportOptions import, IfcWorkerOptions worker);
};
} // namespace sketchy
