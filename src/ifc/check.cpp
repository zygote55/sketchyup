#include "ifc/check.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

#include "ifc/json_out.hpp"
#include "ifcparse/file.h"
#include "ifcparse/instance_data.h"
#include "ifcparse/schema.h"

namespace sketchyup::ifc {
namespace {

// An attribute value as a plain tree, so a schema type can be checked against it recursively.
struct V {
  enum class Kind { Null, Derived, Int, Bool, Logical, Real, Text, Binary, Enumeration, Instance, List } kind{Kind::Null};
  express::base instance;
  ifcopenshell::enumeration_reference enumeration;
  std::vector<V> list;
};

struct Reader {
  V operator()(ifcopenshell::blank) const { return {}; }
  V operator()(ifcopenshell::derived) const { return V{V::Kind::Derived, {}, {}, {}}; }
  V operator()(int64_t) const { return V{V::Kind::Int, {}, {}, {}}; }
  V operator()(bool) const { return V{V::Kind::Bool, {}, {}, {}}; }
  V operator()(boost::logic::tribool) const { return V{V::Kind::Logical, {}, {}, {}}; }
  V operator()(double) const { return V{V::Kind::Real, {}, {}, {}}; }
  V operator()(const std::string&) const { return V{V::Kind::Text, {}, {}, {}}; }
  V operator()(const boost::dynamic_bitset<>&) const { return V{V::Kind::Binary, {}, {}, {}}; }
  V operator()(ifcopenshell::enumeration_reference value) const {
    V v;
    v.kind = V::Kind::Enumeration;
    v.enumeration = value;
    return v;
  }
  V operator()(const express::base& value) const {
    V v;
    v.kind = V::Kind::Instance;
    v.instance = value;
    return v;
  }
  V operator()(ifcopenshell::empty_aggregate) const {
    V v;
    v.kind = V::Kind::List;
    return v;
  }
  V operator()(ifcopenshell::empty_aggregate_of_aggregate) const {
    V v;
    v.kind = V::Kind::List;
    return v;
  }
  template <class T> V operator()(const std::vector<T>& items) const {
    V v;
    v.kind = V::Kind::List;
    for (const auto& item : items) v.list.push_back((*this)(item));
    return v;
  }
};

bool conforms(const V& v, const ifcopenshell::parameter_type* type, std::string& why) {
  if (type == nullptr) return true;
  if (const auto* named = type->as_named_type()) {
    const auto* declared = named->declared_type();
    if (declared->as_entity() != nullptr || declared->as_select_type() != nullptr) {
      if (v.kind != V::Kind::Instance || !v.instance) {
        why = "expected an instance of " + declared->name();
        return false;
      }
      if (!v.instance.declaration().is(*declared)) {
        why = "an instance of " + v.instance.declaration().name() + " where " + declared->name() + " is declared";
        return false;
      }
      return true;
    }
    if (declared->as_enumeration_type() != nullptr) {
      if (v.kind != V::Kind::Enumeration || v.enumeration.enumeration() != declared->as_enumeration_type()) {
        why = "expected an enumeration item of " + declared->name();
        return false;
      }
      return true;
    }
    if (const auto* alias = declared->as_type_declaration()) return conforms(v, alias->declared_type(), why);
    return true;
  }
  if (const auto* simple = type->as_simple_type()) {
    using S = ifcopenshell::simple_type;
    bool ok = false;
    switch (simple->declared_type()) {
      case S::binary_type: ok = v.kind == V::Kind::Binary; break;
      case S::boolean_type: ok = v.kind == V::Kind::Bool; break;
      case S::integer_type: ok = v.kind == V::Kind::Int; break;
      case S::logical_type: ok = v.kind == V::Kind::Logical || v.kind == V::Kind::Bool; break;
      case S::number_type:
      case S::real_type: ok = v.kind == V::Kind::Real || v.kind == V::Kind::Int; break;
      case S::string_type: ok = v.kind == V::Kind::Text; break;
      default: ok = true;
    }
    if (!ok) why = "a value of the wrong simple type";
    return ok;
  }
  if (const auto* aggregate = type->as_aggregation_type()) {
    if (v.kind != V::Kind::List) {
      why = "expected an aggregate";
      return false;
    }
    const long long size = static_cast<long long>(v.list.size());
    if (aggregate->bound1() >= 0 && size < aggregate->bound1()) {
      why = "an aggregate with fewer than " + std::to_string(aggregate->bound1()) + " items";
      return false;
    }
    if (aggregate->bound2() >= 0 && size > aggregate->bound2()) {
      why = "an aggregate with more than " + std::to_string(aggregate->bound2()) + " items";
      return false;
    }
    for (const V& item : v.list)
      if (!conforms(item, aggregate->type_of_element(), why)) return false;
    return true;
  }
  return true;
}

bool globalIdShape(const std::string& guid) {
  static const std::string alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$";
  if (guid.size() != 22 || guid[0] < '0' || guid[0] > '3') return false;
  return std::all_of(guid.begin(), guid.end(), [&](char c) { return alphabet.find(c) != std::string::npos; });
}

}  // namespace

int runCheck(const std::string& path) {
  long long entities = 0, issues = 0;
  std::vector<std::string> samples;
  const auto report = [&](const express::base& instance, const std::string& what) {
    ++issues;
    if (samples.size() < 64) samples.push_back("#" + std::to_string(instance.id()) + " " + instance.declaration().name() + ": " + what);
  };
  try {
    ifcopenshell::file file(path, ifcopenshell::FT_AUTODETECT, true);
    if (!file.good()) {
      JsonOut json;
      json.beginObject().field("status", "invalid").field("entities", 0).field("issueCount", 1);
      json.key("issues").beginArray().value("the file could not be parsed").endArray().endObject();
      std::printf("R %s\n", json.str().c_str());
      return 3;
    }
    if (file.schema() == nullptr || file.schema()->name() != "IFC4") {
      JsonOut json;
      json.beginObject().field("status", "invalid").field("entities", 0).field("issueCount", 1);
      json.key("issues").beginArray().value("the file's schema is not IFC4").endArray().endObject();
      std::printf("R %s\n", json.str().c_str());
      return 3;
    }
    std::set<std::string> guids;
    std::vector<express::base> all;
    for (const auto& entry : file) all.push_back(entry.second);
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.id() < b.id(); });
    for (const auto& instance : all) {
      const auto* declared = instance.declaration().as_entity();
      if (declared == nullptr) continue;
      ++entities;
      if (declared->is_abstract()) report(instance, "an abstract entity was instantiated");
      const auto& attributes = declared->all_attributes();
      for (std::size_t i = 0; i < attributes.size(); ++i) {
        ifcopenshell::attribute_value stored;
        try {
          stored = instance.get_attribute_value(i);
        } catch (const std::exception& problem) {
          report(instance, attributes[i]->name() + " cannot be read: " + problem.what());
          continue;
        }
        const V v = stored.apply_visitor(Reader{});
        if (v.kind == V::Kind::Derived) continue;
        if (v.kind == V::Kind::Null) {
          if (!attributes[i]->optional()) report(instance, "mandatory attribute " + attributes[i]->name() + " is not set");
          continue;
        }
        std::string why;
        if (!conforms(v, attributes[i]->type_of_attribute(), why)) report(instance, attributes[i]->name() + ": " + why);
      }
      if (instance.declaration().is("IfcRoot")) {
        const std::string guid = static_cast<std::string>(instance.get_attribute_value(0));
        if (!globalIdShape(guid)) report(instance, "GlobalId '" + guid + "' is not a compressed GUID");
        else if (!guids.insert(guid).second) report(instance, "GlobalId '" + guid + "' is used twice");
      }
    }
  } catch (const std::exception& problem) {
    JsonOut json;
    json.beginObject().field("status", "invalid").field("entities", entities).field("issueCount", issues + 1);
    json.key("issues").beginArray().value(std::string("exception: ") + problem.what()).endArray().endObject();
    std::printf("R %s\n", json.str().c_str());
    return 3;
  }
  JsonOut json;
  json.beginObject().field("status", issues == 0 ? "ok" : "invalid").field("entities", entities).field("issueCount", issues);
  json.key("issues").beginArray();
  for (const auto& sample : samples) json.value(sample);
  json.endArray().endObject();
  std::printf("R %s\n", json.str().c_str());
  std::fflush(stdout);
  return issues == 0 ? 0 : 3;
}

}  // namespace sketchyup::ifc
