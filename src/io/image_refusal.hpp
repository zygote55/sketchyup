#pragma once
// R107.b2 (ADR 0181, R107.b2 design "Refusals"): a refused orthophoto, world file or GeoTIFF. Each
// refusal has a stable code (the CLI's refusalCode) and one plain sentence for the person (UX §21.3).

#include <stdexcept>
#include <string>
#include <vector>

namespace sketchy {

class ImageRefusal : public std::runtime_error {
  public:
    // The details fill the sentence's blanks in order (for example the zone and the site's zone).
    explicit ImageRefusal(std::string code, std::vector<std::string> details = {});
    const std::string &code() const { return code_; }
    // The plain sentence for the code, with its details filled in.
    std::string sentence() const;

  private:
    std::string code_;
    std::vector<std::string> details_;
};

} // namespace sketchy
