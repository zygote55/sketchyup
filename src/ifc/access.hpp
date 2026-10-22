// Name-based access to IFC instances for the import (ADR 0171 section 3). The worker reads three
// schemas (IFC2X3, IFC4, IFC4X3_ADD2) through IfcParse's schema-independent entity interface, so
// every attribute is looked up by name and a missing or null attribute reads as "absent": a file
// that is malformed in one attribute never stops the import of the rest.
#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ifcparse/file.h"
#include "ifcparse/instance_data.h"

namespace sketchyup::ifc {

using Inst = express::base;

inline int stepOf(const Inst& instance) { return instance ? static_cast<int>(instance.id()) : 0; }
inline const std::string& className(const Inst& instance) {
  static const std::string none;
  return instance ? instance.declaration().name() : none;
}
inline bool isA(const Inst& instance, const char* name) { return instance && instance.declaration().is(name); }
inline bool isEntity(const Inst& instance) { return instance && instance.declaration().as_entity() != nullptr; }

// The index of a named attribute in an entity declaration, cached; -1 when the class has none.
int attributeIndex(const ifcopenshell::declaration& declaration, const std::string& name);

// The attribute as stored, or nullopt when the class has no such attribute, or it is null or derived.
std::optional<ifcopenshell::attribute_value> attr(const Inst& instance, const char* name);

std::string text(const Inst& instance, const char* name);
std::optional<double> number(const Inst& instance, const char* name);
std::optional<std::int64_t> integer(const Inst& instance, const char* name);
// A BOOLEAN or LOGICAL attribute; nullopt for null and for UNKNOWN.
std::optional<bool> flag(const Inst& instance, const char* name);
// An ENUMERATION attribute's keyword ("METRE", "MILLI", ...), upper-case as in the schema.
std::string keyword(const Inst& instance, const char* name);
Inst ref(const Inst& instance, const char* name);
std::vector<Inst> refs(const Inst& instance, const char* name);
std::vector<double> numbers(const Inst& instance, const char* name);

// What an IfcValue-style select instance (IfcLabel('x'), IfcLengthMeasure(2.5), ...) wraps.
struct Wrapped {
  enum class Kind { Null, Boolean, Logical, Integer, Real, Text } kind{Kind::Null};
  std::string typeName;  // IfcLabel, IfcLengthMeasure, ...
  bool boolean{};
  std::int64_t integer{};
  double real{};
  std::string text;
};
Wrapped unwrap(const Inst& value);
// Walks every attribute of the instance and calls `visit` for each referenced instance (single
// references and aggregates), in attribute order.
template <class F> void forEachReference(const Inst& instance, F&& visit) {
  const auto* entity = instance.declaration().as_entity();
  if (entity == nullptr) return;
  const std::size_t count = entity->all_attributes().size();
  for (std::size_t i = 0; i < count; ++i) {
    ifcopenshell::attribute_value value;
    try {
      value = instance.get_attribute_value(i);
    } catch (...) {
      continue;
    }
    switch (value.type()) {
      case ifcopenshell::Argument_ENTITY_INSTANCE: {
        const Inst target = static_cast<Inst>(value);
        if (target) visit(target);
        break;
      }
      case ifcopenshell::Argument_AGGREGATE_OF_ENTITY_INSTANCE: {
        for (const Inst& target : static_cast<std::vector<Inst>>(value))
          if (target) visit(target);
        break;
      }
      case ifcopenshell::Argument_AGGREGATE_OF_AGGREGATE_OF_ENTITY_INSTANCE: {
        for (const auto& row : static_cast<std::vector<std::vector<Inst>>>(value))
          for (const Inst& target : row)
            if (target) visit(target);
        break;
      }
      default:
        break;
    }
  }
}

}  // namespace sketchyup::ifc
