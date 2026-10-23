#include "ifc/access.hpp"

#include <algorithm>
#include <mutex>

namespace sketchyup::ifc {

int attributeIndex(const ifcopenshell::declaration& declaration, const std::string& name) {
  // The import is single-threaded where it reads attributes; the cache is still guarded so a later
  // caller on another thread cannot corrupt it.
  static std::mutex guard;
  static std::map<std::pair<const ifcopenshell::declaration*, std::string>, int> cache;
  std::lock_guard<std::mutex> lock(guard);
  const auto key = std::make_pair(&declaration, name);
  if (const auto found = cache.find(key); found != cache.end()) return found->second;
  int index = -1;
  if (const auto* entity = declaration.as_entity()) {
    const auto& attributes = entity->all_attributes();
    for (std::size_t i = 0; i < attributes.size(); ++i)
      if (attributes[i]->name() == name) {
        index = static_cast<int>(i);
        break;
      }
  }
  cache.emplace(key, index);
  return index;
}

std::optional<ifcopenshell::attribute_value> attr(const Inst& instance, const char* name) {
  if (!isEntity(instance)) return std::nullopt;
  const int index = attributeIndex(instance.declaration(), name);
  if (index < 0) return std::nullopt;
  try {
    auto value = instance.get_attribute_value(static_cast<std::size_t>(index));
    if (value.isNull()) return std::nullopt;
    const auto type = value.type();
    if (type == ifcopenshell::Argument_NULL || type == ifcopenshell::Argument_DERIVED ||
        type == ifcopenshell::Argument_UNKNOWN)
      return std::nullopt;
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

std::string text(const Inst& instance, const char* name) {
  const auto value = attr(instance, name);
  if (!value || value->type() != ifcopenshell::Argument_STRING) return {};
  try {
    return static_cast<std::string>(*value);
  } catch (...) {
    return {};
  }
}

std::optional<double> number(const Inst& instance, const char* name) {
  const auto value = attr(instance, name);
  if (!value) return std::nullopt;
  try {
    if (value->type() == ifcopenshell::Argument_DOUBLE) return static_cast<double>(*value);
    if (value->type() == ifcopenshell::Argument_INT) return static_cast<double>(static_cast<std::int64_t>(*value));
  } catch (...) {
  }
  return std::nullopt;
}

std::optional<std::int64_t> integer(const Inst& instance, const char* name) {
  const auto value = attr(instance, name);
  if (!value) return std::nullopt;
  try {
    if (value->type() == ifcopenshell::Argument_INT) return static_cast<std::int64_t>(*value);
  } catch (...) {
  }
  return std::nullopt;
}

std::optional<bool> flag(const Inst& instance, const char* name) {
  const auto value = attr(instance, name);
  if (!value) return std::nullopt;
  try {
    if (value->type() == ifcopenshell::Argument_BOOL) return static_cast<bool>(*value);
    if (value->type() == ifcopenshell::Argument_LOGICAL) {
      const boost::logic::tribool logical = *value;
      if (boost::logic::indeterminate(logical)) return std::nullopt;
      return static_cast<bool>(logical);
    }
  } catch (...) {
  }
  return std::nullopt;
}

std::string keyword(const Inst& instance, const char* name) {
  const auto value = attr(instance, name);
  if (!value || value->type() != ifcopenshell::Argument_ENUMERATION) return {};
  try {
    const ifcopenshell::enumeration_reference reference = *value;
    return reference.value();
  } catch (...) {
    return {};
  }
}

Inst ref(const Inst& instance, const char* name) {
  const auto value = attr(instance, name);
  if (!value || value->type() != ifcopenshell::Argument_ENTITY_INSTANCE) return {};
  try {
    return static_cast<Inst>(*value);
  } catch (...) {
    return {};
  }
}

std::vector<Inst> refs(const Inst& instance, const char* name) {
  std::vector<Inst> result;
  const auto value = attr(instance, name);
  if (!value) return result;
  try {
    if (value->type() == ifcopenshell::Argument_AGGREGATE_OF_ENTITY_INSTANCE) {
      for (const Inst& target : static_cast<std::vector<Inst>>(*value))
        if (target) result.push_back(target);
    } else if (value->type() == ifcopenshell::Argument_ENTITY_INSTANCE) {
      if (const Inst target = static_cast<Inst>(*value)) result.push_back(target);
    }
  } catch (...) {
  }
  return result;
}

std::vector<double> numbers(const Inst& instance, const char* name) {
  const auto value = attr(instance, name);
  if (!value) return {};
  try {
    if (value->type() == ifcopenshell::Argument_AGGREGATE_OF_DOUBLE) return static_cast<std::vector<double>>(*value);
    if (value->type() == ifcopenshell::Argument_AGGREGATE_OF_INT) {
      std::vector<double> result;
      for (const auto v : static_cast<std::vector<std::int64_t>>(*value)) result.push_back(static_cast<double>(v));
      return result;
    }
  } catch (...) {
  }
  return {};
}

Wrapped unwrap(const Inst& value) {
  Wrapped result;
  if (!value) return result;
  result.typeName = value.declaration().name();
  // A defined type instance wraps one value (attribute 0); an entity has attributes of its own.
  if (isEntity(value)) return result;
  try {
    const auto inner = value.get_attribute_value(0);
    if (inner.isNull()) return result;
    switch (inner.type()) {
      case ifcopenshell::Argument_BOOL:
        result.kind = Wrapped::Kind::Boolean;
        result.boolean = static_cast<bool>(inner);
        break;
      case ifcopenshell::Argument_LOGICAL: {
        const boost::logic::tribool logical = inner;
        if (boost::logic::indeterminate(logical)) {
          result.kind = Wrapped::Kind::Logical;
        } else {
          result.kind = Wrapped::Kind::Boolean;
          result.boolean = static_cast<bool>(logical);
        }
        break;
      }
      case ifcopenshell::Argument_INT:
        result.kind = Wrapped::Kind::Integer;
        result.integer = static_cast<std::int64_t>(inner);
        break;
      case ifcopenshell::Argument_DOUBLE:
        result.kind = Wrapped::Kind::Real;
        result.real = static_cast<double>(inner);
        break;
      case ifcopenshell::Argument_STRING:
        result.kind = Wrapped::Kind::Text;
        result.text = static_cast<std::string>(inner);
        break;
      default:
        break;
    }
  } catch (...) {
  }
  return result;
}

}  // namespace sketchyup::ifc
