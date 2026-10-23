#include <fstream>
#include <sstream>
#include <iterator>
#include <iostream>
#include <string>

int main() {
  std::string ia, ib;
  {
    std::ifstream a("/src/step-corpus-tmp/sketchyup-step-box.step", std::ios::binary);
    ia = std::string((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
  }
  {
    std::ifstream b("/src/step-corpus-tmp/sketchyup-step-box.step", std::ios::binary);
    std::ostringstream buffer;
    buffer << b.rdbuf();
    ib = buffer.str();
  }
  std::cout << "iterator: " << ia.size() << "  rdbuf: " << ib.size() << "\n";
  const std::size_t i = ia.find("FILE_SCHEMA");
  const std::size_t j = ib.find("FILE_SCHEMA");
  std::cout << "a ctx: [" << ia.substr(i, 45) << "]\n";
  std::cout << "b ctx: [" << ib.substr(j, 45) << "]\n";
  std::size_t k = 0;
  while (k < ia.size() && k < ib.size() && ia[k] == ib[k]) ++k;
  if (k == ia.size() && k == ib.size()) {
    std::cout << "identical\n";
  } else {
    std::cout << "first diff at " << k << ": "
              << (k < ia.size() ? static_cast<int>(ia[k]) : -1) << " vs "
              << (k < ib.size() ? static_cast<int>(ib[k]) : -1) << "\n";
  }
  return 0;
}
