#pragma once
#include "core/body.hpp"
#include <algorithm>
#include <map>
#include <vector>

namespace sketchy {
// For each record of `bodies`, in ID order: whether the record or any of its
// ancestors satisfies `changed(id, record)`. A record whose world transform was
// derived from the same records is then known to keep it without walking its
// chain. A missing parent, a cycle or a chain longer than 128 records also reports
// true, so callers recompute that chain and see its usual error. Linear in the
// number of records, with one binary search per record. `changed` is called once
// per record, in ascending ID order.
template <class Changed>
std::vector<char> changedChains(const std::map<Id, BodyPtr> &bodies, Changed &&changed) {
    const auto count = bodies.size();
    std::vector<Id> ids;
    std::vector<const Body *> records;
    ids.reserve(count);
    records.reserve(count);
    // 0 unknown, 1 unchanged chain, 2 changed chain.
    std::vector<char> state(count, 0);
    for (const auto &[id, body] : bodies) {
        if (changed(id, body))
            state[ids.size()] = 2;
        ids.push_back(id);
        records.push_back(body.get());
    }
    std::vector<size_t> path;
    for (size_t i = 0; i < count; ++i) {
        if (state[i])
            continue;
        path.clear();
        char result = 2;
        for (size_t current = i;;) {
            path.push_back(current);
            const auto parent = records[current]->parent;
            if (!parent) {
                result = 1;
                break;
            }
            if (path.size() >= 128)
                break; // Too deep or cyclic: recompute and report as before.
            const auto found = std::lower_bound(ids.begin(), ids.end(), parent);
            if (found == ids.end() || *found != parent)
                break; // Missing parent.
            current = size_t(found - ids.begin());
            if (state[current]) {
                result = state[current];
                break;
            }
        }
        for (auto index : path)
            state[index] = result;
    }
    for (auto &value : state)
        value = value == 2;
    return state;
}
} // namespace sketchy
