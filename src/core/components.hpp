#pragma once
#include "core/model.hpp"
namespace sketchy {
struct ComponentResult {
    Id definition{}, instance{};
    ChangeReport changes;
    std::map<Id, Id> movedGeometry;
};
ComponentResult createComponent(Document &doc, Id root, std::string name = "Component");
ComponentResult placeComponent(Document &doc, Id definition, Transform local = {}, Id parent = 0,
                               std::string name = {});
ComponentResult replaceComponent(Document &doc, Id instance, Id definition);
ComponentResult makeComponentUnique(Document &doc, Id instance);
// Edit canonical member IDs privately, returning composed topology lineage.
ComponentResult editComponentDefinition(Document &doc, Id definition,
                                        const std::function<ChangeReport(Document &)> &edit,
                                        Transform editingFrame = {});
ComponentResult setComponentAxes(Document &doc, Id definition, Transform axes);
ComponentResult setComponentGlue(Document &doc, Id definition, std::optional<ComponentGlue> glue);
// Map a materialized definition-edit draft to one live placement, including references.
std::map<Id, Id> componentScopeMembers(const Document &doc, const Document &draft, Id instance);
} // namespace sketchy
