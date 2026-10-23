#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <regex>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

std::optional<std::string> readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  std::string text = buffer.str();
  if (text.size() > 128ull * 1024 * 1024) return std::nullopt;
  return text;
}

int main(int argc, char** argv) {
  const std::optional<std::string> text = readFile(argv[1]);
  if (!text) {
    std::cout << "NO TEXT\n";
    return 1;
  }
  std::cout << "size: " << text->size() << "\n";
  static const std::regex schemaRe("FILE_SCHEMA\\(\\('\\s*'([^']*)'");
  std::smatch m;
  std::cout << "match: " << (std::regex_search(*text, m, schemaRe) ? m[1].str() : "NO") << "\n";
  static const std::regex unitRe(
      "LENGTH_UNIT\\(\\)\\s+NAMED_UNIT\\(\\*?\\)\\s+SI_UNIT\\(\\.(\\w+)\\.,\\.(METRE)\\.(\\w+)\\)");
  std::cout << "unit: " << (std::regex_search(*text, m, unitRe) ? m[1].str() : "NO") << "\n";
  return 0;
}
