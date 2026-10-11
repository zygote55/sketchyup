#pragma once
#include "core/body.hpp"
#include <map>
#include <stdexcept>
namespace sketchy {
// Read-through view of an authoritative record map plus a sparse overlay of
// changed records (nullptr = erased). Incremental validation uses it in place
// of copying the whole candidate map.
template <class Value> struct RecordOverlay {
    const std::map<Id, Value> &base, &changes;
    bool contains(Id id) const {
        const auto found = changes.find(id);
        return found != changes.end() ? bool(found->second) : base.contains(id);
    }
    const Value &at(Id id) const {
        const auto found = changes.find(id);
        if (found == changes.end())
            return base.at(id);
        if (!found->second)
            throw std::out_of_range("map::at");
        return found->second;
    }
};
} // namespace sketchy
