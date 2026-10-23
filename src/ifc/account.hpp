// The accounting identity (ADR 0171 section 4): every entity instance of the file is converted,
// supporting or omitted, and the three add up to the number of instances the library read.
#pragma once

#include <map>
#include <set>
#include <string>

#include "ifc/access.hpp"

namespace sketchyup::ifc {

struct Accounting {
  long long entities{0}, converted{0}, supporting{0}, omitted{0};
  std::map<std::string, long long> omittedByClass;
};

class Accountant {
public:
  explicit Accountant(ifcopenshell::file& file) : file_(file) {}
  // An instance that produced native content (a group, a body, a recorded zone).
  void converted(int step) { converted_.insert(step); }
  // An instance consumed as data of a converted one: the instance alone.
  void supporting(int step) { supporting_.insert(step); }
  // The instance and everything it references that is placement, geometry, unit, owner or style
  // data (and not a product or a relationship), except through the named attributes of the root.
  void claimTree(const Inst& root, const std::set<std::string>& skipAttributes = {});
  // Finishes: also claims the styled items of claimed representation items, the definition
  // representations of used materials and the colour maps of used face sets.
  Accounting finish();

private:
  bool supportClass(const Inst& instance) const;
  ifcopenshell::file& file_;
  std::set<int> converted_, supporting_;
};

}  // namespace sketchyup::ifc
