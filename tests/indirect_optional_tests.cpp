// IndirectOptional (core/indirect_optional.hpp) behaves as std::optional: deep copies, value
// equality, reset, emplace and conversion both ways; and Body's terrain member with it.
#include "core/body.hpp"
#include "core/indirect_optional.hpp"
#include "core/terrain_records.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Counted {
    static inline int live = 0;
    std::vector<int> values;
    explicit Counted(std::vector<int> v = {}) : values(std::move(v)) { ++live; }
    Counted(const Counted &other) : values(other.values) { ++live; }
    Counted &operator=(const Counted &) = default;
    ~Counted() { --live; }
    bool operator==(const Counted &other) const { return values == other.values; }
};
void valueSemantics() {
    {
        IndirectOptional<Counted> empty;
        check(!empty && !empty.has_value() && empty == std::nullopt, "default is empty");
        IndirectOptional<Counted> a = Counted({1, 2});
        check(a && a->values.size() == 2 && Counted::live == 1, "holds a value");
        auto b = a; // deep copy
        check(Counted::live == 2 && a == b && &*a != &*b, "copies are deep and equal");
        b->values.push_back(3);
        check(!(a == b) && a->values.size() == 2, "copies are independent");
        IndirectOptional<Counted> c = std::move(b);
        check(!b && c->values.size() == 3 && Counted::live == 2, "move steals the value");
        c = a;
        check(c == a && Counted::live == 2, "copy assignment replaces the value");
        c = std::nullopt;
        check(!c && Counted::live == 1, "assigning nullopt destroys the value");
        c.emplace(std::vector<int>{7});
        check(c->values == std::vector<int>{7} && Counted::live == 2, "emplace");
        std::optional<Counted> standard = c;
        check(standard && standard->values == c->values, "converts to std::optional");
        IndirectOptional<Counted> back = standard;
        check(back == c, "converts from std::optional");
        c.reset();
        check(!c && !(c == a) && IndirectOptional<Counted>() == c, "reset and empty equality");
        bool threw = false;
        try {
            (void)c.value();
        } catch (const std::bad_optional_access &) {
            threw = true;
        }
        check(threw, "value() of an empty holder throws");
    }
    check(Counted::live == 0, "every value is destroyed");
}
void bodyTerrain() {
    Body plain;
    check(!plain.terrain, "a body has no terrain record by default");
    Body terrain;
    terrain.kind = BodyKind::Terrain;
    terrain.terrain = TerrainRecord{};
    terrain.terrain->nextEditId = 7;
    Body copy = terrain;
    check(copy == terrain && copy.terrain->nextEditId == 7, "a copied body copies its record");
    copy.terrain->nextEditId = 8;
    check(!(copy == terrain) && terrain.terrain->nextEditId == 7,
          "records of copied bodies are independent");
    copy.terrain.reset();
    check(!(copy == terrain), "a body without its record differs");
}
} // namespace
int main() {
    try {
        valueSemantics();
        bodyTerrain();
        std::cout << "indirect optional tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
