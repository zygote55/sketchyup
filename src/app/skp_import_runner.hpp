#pragma once
// R102.c (ADR 0176 section 9): the SketchUp (.skp) import's runner — a thin wrapper around the shared
// ExchangeRunner, as the STEP import's is: the whole import (the helper's --import, then the
// application's decode and conversion) runs on one worker thread, the helper's phases come back as
// queued signals, Cancel is checked between tasks and by the helper's own run, and the result is read
// back after the thread has finished.
#include "app/exchange_runner.hpp"
#include "io/skp_worker.hpp"

namespace sketchy {

class SkpImportRunner : public ExchangeRunner {
  public:
    void begin(const QString &path, SkpWorkerOptions worker);
};

} // namespace sketchy
