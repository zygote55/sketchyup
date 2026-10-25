// The worker's progress channel (ADR 0176 section 1.6, mirroring the IFC helper): short
// "P ..." lines on stdout that the application reads while the worker runs.
#pragma once

#include <cstdio>
#include <string>

namespace sketchyup::skp {

class Progress {
public:
  explicit Progress(bool enabled = true) : enabled_(enabled) {}
  void phase(const std::string& name) { line("P phase " + name); }
  void line(const std::string& text) {
    if (!enabled_) return;
    std::fputs(text.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
  }

private:
  bool enabled_;
};

}  // namespace sketchyup::skp
