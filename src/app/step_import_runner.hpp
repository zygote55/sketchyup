#pragma once
// R098.a3: the STEP import's runner (ADR 0172 section 8) — a thin wrapper around the shared
// ExchangeRunner: the whole import (the helper's --import-step, then the application's own
// conversion) runs on one worker thread, the helper's phases and counts come back as queued
// signals, Cancel is checked between tasks, and the result is read back after the thread has
// finished.
#include "app/exchange_runner.hpp"
#include "io/step_worker.hpp"
namespace sketchy {
class StepImportRunner : public ExchangeRunner {
  public:
    void begin(const QString &path, StepWorkerOptions worker);
};
} // namespace sketchy
