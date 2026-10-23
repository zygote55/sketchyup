#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <regex>

int main() {
  std::ifstream in("/src/step-corpus-tmp/sketchyup-step-box.step");
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const std::size_t i = text.find("FILE_SCHEMA");
  std::cout << "context: [" << text.substr(i, 60) << "]\n";
  const char* pats[] = {
      "FILE_SCHEMA\\(\\(\\s*'([^']*)'",
      "FILE_SCHEMA\\(\\s*\\(\\s*'([^']*)'",
      "FILE_SCHEMA\\(\\s*'([^']*)'",
      "FILE_SCHEMA\\(.*?\\)\\s*\\(\\s*'([^']*)'",
      "10303\\s+(\\d+)",
  };
  for (const char* pat : pats) {
    std::smatch m;
    std::cout << pat << " -> " << (std::regex_search(text, m, std::regex(pat)) ? m[1].str() : "NO") << "\n";
  }
  return 0;
}
