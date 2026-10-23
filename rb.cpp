#include <fstream>
#include <sstream>
#include <iterator>
#include <iostream>
#include <string>
#include <regex>

int main() {
  std::string ib;
  {
    std::ifstream b("/src/step-corpus-tmp/sketchyup-step-box.step", std::ios::binary);
    std::ostringstream buffer;
    buffer << b.rdbuf();
    ib = buffer.str();
  }
  std::string ic;
  {
    std::ifstream c("/src/step-corpus-tmp/sketchyup-step-box.step", std::ios::binary);
    ic = std::string((std::istreambuf_iterator<char>(c)), std::istreambuf_iterator<char>());
  }
  static const std::regex re("FILE_SCHEMA\\(\\('\\s*'([^']*)'");
  std::smatch m1, m2;
  std::cout << "rdbuf-built: " << (std::regex_search(ib, m1, re) ? "YES" : "NO") << "\n";
  std::cout << "iterator-built:" << (std::regex_search(ic, m2, re) ? "YES" : "NO") << "\n";
  std::cout << "equal: " << (ib == ic ? "YES" : "NO") << " same addr: " << (ib.data() == ic.data() ? "YES" : "NO") << "\n";
  return 0;
}
