// The worker's progress channel (ADR 0171 section 1.6): short lines on stdout that the application
// reads while the worker runs. The line before a task says which products it is about to convert,
// so when the worker crashes or is killed the application knows which products to leave out of the
// next run.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace sketchyup::ifc {

class Progress {
public:
  explicit Progress(bool enabled = true) : enabled_(enabled) {}
  void phase(const std::string& name) { line("P phase " + name); }
  void tasks(std::size_t count) { line("P tasks " + std::to_string(count)); }
  void next(const std::vector<int>& products) {
    std::string text = "P next";
    for (const int step : products) text += " " + std::to_string(step);
    line(text);
  }
  // The index of the task the run is about to start (a restart can try the tasks from here one by one).
  void at(long long task) { line("P at " + std::to_string(task)); }
  // Products the run itself found to stop it and left out.
  void excluded(const std::vector<int>& products) {
    std::string text = "P excluded";
    for (const int step : products) text += " " + std::to_string(step);
    line(text);
  }
  void done(std::size_t count) { line("P done " + std::to_string(count)); }
  void threads(int count) { line("P threads " + std::to_string(count)); }
  void line(const std::string& text) {
    if (!enabled_) return;
    std::fputs(text.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
  }

private:
  bool enabled_;
};

}  // namespace sketchyup::ifc
