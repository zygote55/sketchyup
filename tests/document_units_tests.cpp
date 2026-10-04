#include "core/components.hpp"
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected unit operation rejection");
}
int main() {
    try {
        Document doc(DisplayUnit::Millimeters);
        check(!doc.dirty() && !doc.canUndo() && doc.revision() == 0 &&
                  doc.displayUnits() == DisplayUnit::Millimeters,
              "New document units are a clean initial preference");
        rejects([] { Document invalid(static_cast<DisplayUnit>(99)); });
        rejects([] { parseDisplayUnit("cm"); });
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        doc.markSaved();
        const auto model = doc.bodies();
        const auto saved = doc.saveStamp();
        const auto revision = doc.revision();
        doc.setDisplayUnits(DisplayUnit::FeetInches);
        check(doc.bodies() == model && doc.revision() == revision + 1 && doc.dirty() &&
                  doc.history().entries.back().label == "Change document units",
              "Changing units is one labeled edit without rescaling geometry");
        const auto stamp = doc.amendmentStamp();
        doc.amendLast(stamp,
                      [](Document &candidate) { candidate.setDisplayUnits(DisplayUnit::Meters); });
        check(doc.displayUnits() == DisplayUnit::Meters && doc.history().total == 2,
              "Unit amendment remains one history item");
        doc.undo();
        check(doc.displayUnits() == DisplayUnit::Millimeters && !doc.dirty() &&
                  doc.isCurrentSnapshot(saved),
              "Undo restores original units and saved state");
        doc.redo();
        check(doc.displayUnits() == DisplayUnit::Meters && doc.bodies() == model,
              "Redo restores preferred units without changing model records");
        const auto unchanged = doc.revision();
        doc.setDisplayUnits(DisplayUnit::Meters);
        check(doc.revision() == unchanged, "Unchanged units are a no-op");
        Edit invalid{"Stale units", {}};
        invalid.displayUnits = std::pair{DisplayUnit::Millimeters, DisplayUnit::FeetInches};
        rejects([&] { doc.apply(invalid, doc.revision()); });
        invalid.displayUnits = std::pair{DisplayUnit::Meters, static_cast<DisplayUnit>(-1)};
        rejects([&] { doc.apply(invalid, doc.revision()); });
        invalid.displayUnits = std::pair{DisplayUnit::Meters, DisplayUnit::FeetInches};
        rejects([&] { doc.apply(invalid, doc.revision() - 1); });
        check(doc.revision() == unchanged && doc.bodies() == model &&
                  doc.displayUnits() == DisplayUnit::Meters,
              "Invalid/stale units are atomic");
        doc.move(1, {1, 0, 0});
        const auto move = doc.amendmentStamp();
        rejects([&] {
            doc.amendLast(move, [](Document &candidate) {
                candidate.setDisplayUnits(DisplayUnit::Millimeters);
            });
        });
        check(doc.canAmend(move), "Geometry amendment cannot replace its scope with units");
        doc.setDisplayUnits(DisplayUnit::Millimeters);
        auto component = createComponent(doc, 1, "Window");
        editComponentDefinition(doc, component.definition, [](Document &draft) {
            check(draft.displayUnits() == DisplayUnit::Millimeters,
                  "Shared edit inherits document units");
            for (const auto &[id, body] : draft.bodies())
                if (!body->surface.faces.empty()) {
                    draft.paint(id, {.1f, .2f, .3f});
                    break;
                }
            return ChangeReport{};
        });
        const auto beforeRejected = doc.saveStamp();
        rejects([&] {
            editComponentDefinition(doc, component.definition, [](Document &draft) {
                draft.setDisplayUnits(DisplayUnit::FeetInches);
                return ChangeReport{};
            });
        });
        check(doc.isCurrentSnapshot(beforeRejected), "Shared geometry scope cannot change units");
        std::cout
            << "Document units, atomic edits, undo/redo, amendment and shared scopes passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
