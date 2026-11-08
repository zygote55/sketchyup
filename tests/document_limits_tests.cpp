// R082.gg document limits: unique geometry, placement records and materialized
// totals, with D06 diagnostics that name the limit, both values and the entity.
// R110 (1.7.0) raised the unique limits to 300,000 vertices, faces and wires and 900,000 edges.
// R115 (2026-11-07) raised them to 2,400,000 vertices, faces and wires and 7,200,000 edges.
// The boundaries are checked by counts (checkLimits with injected LimitCounts), so CI builds no
// geometry at a limit. The at-limit builds of R115 run on the GPU PC (docs/verification/R115-raised-limits.md).
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "core/model.hpp"
#include "geometry/topology.hpp"
#include "sanitizer_mode.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

template <class F> std::string rejection(F operation) {
    try {
        operation();
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}

Id prism(Document &doc, int sides) {
    std::vector<Vec3> loop;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        loop.push_back({std::cos(angle), std::sin(angle), 0});
    }
    const auto body = doc.addFace({loop});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
    return body;
}

// A flat 1,000-vertex face: unique-vertex ballast without extrusion cost.
Id ring(Document &doc, int level) {
    std::vector<Vec3> loop;
    for (int i = 0; i < 1000; ++i) {
        const auto angle = 2 * std::numbers::pi * i / 1000;
        loop.push_back({std::cos(angle), std::sin(angle), 2.0 * level});
    }
    return doc.addFace({loop});
}

void diagnostics() {
    // Locale-independent grouping and the fixed quantity order, at the R115 values.
    const LimitCounts counts{DocumentLimits::materialized.placements + 1,
                             DocumentLimits::unique.bodies + 1};
    check(exceededLimit(counts, LimitClass::Placement) == "placements",
          "Placements are reported before bodies");
    check(exceededLimit(counts, LimitClass::UniqueGeometry) == "bodies",
          "Unique geometry ignores placement records");
    check(rejection([&] { checkLimits(counts, LimitClass::Placement, "Test", "body #7"); }) ==
              "Test exceeds document placement limits: placements would be 80,001 of 80,000 "
              "(body #7)",
          "Placement diagnostic format");
    check(rejection([] { checkLimits({0, 0, 2500000}, LimitClass::UniqueGeometry, "Test", {}); }) ==
              "Test exceeds document unique geometry limits: vertices would be 2,500,000 of "
              "2,400,000",
          "Unique diagnostic format without context");
    check(rejection([] { checkLimits({80000, 320000}, LimitClass::Placement, "Test", {}); })
              .empty(),
          "Values at the limit are accepted");

    // Names: control characters replaced, UTF-8 sequences never split, 64 bytes.
    check(quotedLimitName("Chair\n\x01") == "'Chair?" "?'", "Control characters are replaced");
    std::string long_name;
    for (int i = 0; i < 40; ++i)
        long_name += "\xC3\xA9"; // é, two bytes each
    const auto quoted = quotedLimitName(long_name);
    check(quoted.size() <= limitNameBytes + 2, "Quoted names are bounded");
    check(quoted.substr(quoted.size() - 4) == "\xE2\x80\xA6'", "Truncated names end with an ellipsis");
    const auto body = quoted.substr(1, quoted.size() - 5);
    check(body.size() % 2 == 0 && body.size() == 60, "Truncation keeps whole UTF-8 sequences");

    // Whole messages stay bounded even with maximal values and a long name.
    const auto huge = std::string(500, 'x');
    const auto message = rejection([&] {
        checkLimits({SIZE_MAX, SIZE_MAX}, LimitClass::Placement, "Component placement",
                    "placing " + definitionLimitContext(huge, SIZE_MAX) + " into group #" +
                        std::to_string(SIZE_MAX));
    });
    check(!message.empty() && message.size() <= limitMessageBytes,
          "Diagnostics never exceed " + std::to_string(limitMessageBytes) + " bytes");
    check(message.find(std::string(65, 'x')) == std::string::npos,
          "Definition names are truncated in diagnostics");
}

void uniqueBoundaries() {
    // R110, R115: every unique quantity is accepted exactly at its limit, and one more is refused
    // with the D06 message naming the new maximum. Placements are not part of the unique class.
    const LimitCounts atLimit{0, 80000, 2400000, 2400000, 2400000, 7200000, 80000, 80000};
    check(exceededLimit(atLimit, LimitClass::UniqueGeometry).empty(),
          "Unique values at the limits are accepted");
    check(rejection([&] { checkLimits(atLimit, LimitClass::UniqueGeometry, "Test", {}); }).empty(),
          "Unique values at the limits pass checkLimits");
    const std::vector<std::pair<LimitCounts, std::string>> over{
        {{0, 0, 2400001}, "vertices would be 2,400,001 of 2,400,000"},
        {{0, 0, 0, 2400001}, "faces would be 2,400,001 of 2,400,000"},
        {{0, 0, 0, 0, 2400001}, "wires would be 2,400,001 of 2,400,000"},
        {{0, 0, 0, 0, 0, 7200001}, "edges would be 7,200,001 of 7,200,000"},
        {{0, 80001}, "bodies would be 80,001 of 80,000"},
        {{0, 0, 0, 0, 0, 0, 80001}, "curves would be 80,001 of 80,000"},
        {{0, 0, 0, 0, 0, 0, 0, 80001}, "guides would be 80,001 of 80,000"},
    };
    for (const auto &[counts, text] : over)
        check(rejection([&] { checkLimits(counts, LimitClass::UniqueGeometry, "Test", {}); }) ==
                  "Test exceeds document unique geometry limits: " + text,
              "One more than the unique limit is refused: " + text);
    // Each unique value stays at or below its materialized value (checked at compile time too).
    check(DocumentLimits::unique.vertices == 2400000 && DocumentLimits::unique.edges == 7200000,
          "The unique values are the R115 values");
    check(DocumentLimits::materialized.vertices == 4800000 &&
              DocumentLimits::materialized.edges == 7200000 &&
              DocumentLimits::materialized.placements == 80000,
          "The materialized values and the placement limit are the R115 values");
}

void definitionBoundaries() {
    check(DocumentLimits::definition.vertices == 800000 && DocumentLimits::definition.faces == 800000 &&
              DocumentLimits::definition.wires == 800000 && DocumentLimits::definition.edges == 2400000 &&
              DocumentLimits::definition.bodies == DocumentLimits::unique.bodies &&
              DocumentLimits::definition.curves == DocumentLimits::unique.curves &&
              DocumentLimits::definition.guides == DocumentLimits::unique.guides,
          "The component definition values are the R115 values");
    const LimitCounts atLimit{0, 80000, 800000, 800000, 800000, 2400000, 80000, 80000};
    check(exceededLimit(atLimit, LimitClass::ComponentDefinition).empty() &&
              rejection([&] { checkLimits(atLimit, LimitClass::ComponentDefinition, "Test", {}); })
                  .empty(),
          "Definition values at the limits are accepted");
    const std::vector<std::pair<LimitCounts, std::string>> over{
        {{0, 0, 800001}, "vertices would be 800,001 of 800,000"},
        {{0, 0, 0, 800001}, "faces would be 800,001 of 800,000"},
        {{0, 0, 0, 0, 800001}, "wires would be 800,001 of 800,000"},
        {{0, 0, 0, 0, 0, 2400001}, "edges would be 2,400,001 of 2,400,000"},
        {{0, 80001}, "bodies would be 80,001 of 80,000"},
        {{0, 0, 0, 0, 0, 0, 80001}, "curves would be 80,001 of 80,000"},
        {{0, 0, 0, 0, 0, 0, 0, 80001}, "guides would be 80,001 of 80,000"},
    };
    for (const auto &[counts, text] : over)
        check(rejection([&] { checkLimits(counts, LimitClass::ComponentDefinition, "Test", {}); }) ==
                  "Test exceeds component definition limits: " + text,
              "One more than the definition limit is refused: " + text);
    const LimitCounts placed{80001};
    check(exceededLimit(placed, LimitClass::ComponentDefinition).empty(),
          "Placements are not counted by the component definition class");
    check(rejection([] {
              checkLimits({0, 0, 800001}, LimitClass::ComponentDefinition, "Component definition",
                          definitionLimitContext("Chair", 3));
          }) == "Component definition exceeds component definition limits: vertices would be "
                "800,001 of 800,000 (definition 'Chair' #3)",
          "The definition diagnostic names the definition");
}

// R110, R115: a definition over the vertex, wire or edge limit is refused on create and on edit,
// and the refusal names the definition. The R115 limits are checked by counts here; a definition
// at 800,000 vertices is built only on the GPU PC (R115 acceptance).
void definitionGeometry() {
    check(rejection([] {
              checkLimits({0, 0, 800001}, LimitClass::ComponentDefinition,
                          "Component definition", definitionLimitContext("Edge", 1));
          }) == "Component definition exceeds component definition limits: vertices would be "
                "800,001 of 800,000 (definition 'Edge' #1)",
          "A definition of 800,001 vertices is refused on create");
    check(rejection([] {
              checkLimits({0, 0, 0, 0, 800001}, LimitClass::ComponentDefinition,
                          "Component definition", definitionLimitContext("Chord", 1));
          }) == "Component definition exceeds component definition limits: wires would be "
                "800,001 of 800,000 (definition 'Chord' #1)",
          "A definition of 800,001 wires is refused on edit");
    check(rejection([] {
              checkLimits({0, 0, 0, 0, 0, 2400001}, LimitClass::ComponentDefinition,
                          "Component definition", definitionLimitContext("Chord", 1));
          }) == "Component definition exceeds component definition limits: edges would be "
                "2,400,001 of 2,400,000 (definition 'Chord' #1)",
          "A definition of 2,400,001 edges is refused on edit");
}

void placementCount() {
    // A definition nesting 99 triangle placements expands to 100 placements and 199 records.
    // The placement limit (80,000 at R115) is checked by counts; the nested row is built.
    Document doc;
    const auto triangle =
        createComponent(doc, doc.addFace({{{0, 0, 0}, {.5, 0, 0}, {0, .5, 0}}}), "Triangle");
    std::set<Id> members{triangle.instance};
    for (int i = 1; i < 99; ++i)
        members.insert(placeComponent(doc, triangle.definition,
                                      Transform::translation({1.0 * i, 0, 0}))
                           .instance);
    const auto row = createComponent(doc, createGroup(doc, members, "Row"), "Row");
    check(doc.instances().size() == 100, "Nested row expands to 100 placements");
    check(rejection([] {
              checkLimits({DocumentLimits::materialized.placements}, LimitClass::Placement,
                          "Component placement", {});
          }).empty(),
          "Placements up to the placement limit pass checkLimits");
    check(rejection([&] {
              checkLimits({DocumentLimits::materialized.placements + 1}, LimitClass::Placement,
                          "Component placement",
                          "placing definition 'Triangle' #" + std::to_string(triangle.definition) +
                              " into the model");
          }) ==
              "Component placement exceeds document placement limits: placements would be "
              "80,001 of 80,000 (placing definition 'Triangle' #" +
                  std::to_string(triangle.definition) + " into the model)",
          "An over-limit placement count is rejected with the bounded diagnostic");
    check(rejection([&] {
              checkLimits({DocumentLimits::materialized.placements + 100}, LimitClass::Placement,
                          "Component placement",
                          "placing definition 'Row' #" + std::to_string(row.definition) +
                              " into group #" + std::to_string(row.instance));
          }) ==
              "Component placement exceeds document placement limits: placements would be "
              "80,100 of 80,000 (placing definition 'Row' #" +
                  std::to_string(row.definition) + " into group #" + std::to_string(row.instance) +
                  ")",
          "Nested placements count toward the placement limit");
}

void uniqueGeometry() {
    // Unique geometry counts loose records and each definition's members once. Three loose
    // 1,000-vertex faces and a 1,000-sided prism (2,000 vertices) placed ten times: 5,000 unique
    // vertices, 23,000 materialized. The R115 unique limit (2,400,000) is checked by counts.
    Document doc;
    for (int i = 0; i < 3; ++i)
        ring(doc, i);
    const auto component = createComponent(doc, prism(doc, 1000), "Large");
    for (int i = 1; i < 10; ++i)
        placeComponent(doc, component.definition, Transform::translation({3.0 * i, 0, 0}));
    check(doc.materializedTotals().vertices == 23000,
          "Materialized vertices count every placement of a definition");
    std::map<Id, size_t> uses;
    for (const auto &[root, instance] : doc.instances())
        ++uses[instance->definition];
    check(uniqueGeometryCounts(doc.materializedTotals(), uses, doc.definitions(),
                               &doc.componentVariants())
                  .vertices == 5000,
          "Unique vertices count a definition's members once");
}

// D-075: the draft of a component edit holds the edited definition's members once. The
// stored record that the edit replaces is not counted again, so a 160,000-vertex definition
// can be edited. The R115 limits are far above it, so the refusal side is checked by counts.
std::set<Id> ringParts(Document &doc, int rings, int firstLevel = 0) {
    std::set<Id> parts;
    for (int i = 0; i < rings; ++i)
        parts.insert(ring(doc, firstLevel + i));
    return parts;
}

ComponentResult componentOf(Document &doc, const std::set<Id> &parts, const std::string &name) {
    return createComponent(doc, createGroup(doc, parts, name), name);
}

Id firstMember(const Document &doc, Id definition) {
    const auto &record = *doc.definitions().at(definition);
    for (const auto &[member, body] : record.members)
        if (member != record.root)
            return member;
    throw std::runtime_error("Definition has no member");
}

void draftCountsEditedDefinitionOnce() {
    Document doc;
    const auto big = componentOf(doc, ringParts(doc, 160), "Big");
    check(doc.materializedTotals().vertices == 160000, "Single-instance definition is 160,000");
    const auto member = firstMember(doc, big.definition);
    editComponentDefinition(doc, big.definition, [&](Document &draft) {
        draft.move(member, {1, 0, 0});
        return ChangeReport{};
    });
    check(doc.materializedTotals().vertices == 160000,
          "Moving one member of a 160,000-vertex definition succeeds");
    doc.undo();
    check(doc.materializedTotals().vertices == 160000, "The move undoes to the same geometry");
    // Two copies of this definition (320,000 vertices) are within the R115 unique limit, so the
    // draft's counting is checked by the materialized total above, and the refusal by counts.
    check(rejection([] {
              checkLimits({0, 0, DocumentLimits::unique.vertices + 1}, LimitClass::UniqueGeometry,
                          "Document edit", "definition 'Big' #1");
          }) == "Document edit exceeds document unique geometry limits: vertices would be "
                "2,400,001 of 2,400,000 (definition 'Big' #1)",
          "A document edit one vertex over the unique limit is refused by name");
}

// A document of a 26-sided prism placed `count` times through the public path (the R082.a and
// ADR 0141 fixture shape). Its size is fixed, so CI builds the same geometry as before R115.
void publicPlacements(size_t count) {
    Document doc;
    const auto component = createComponent(doc, prism(doc, 26), "26-sided benchmark prism");
    const int columns = 100;
    for (size_t i = 1; i < count; ++i)
        placeComponent(doc, component.definition,
                       Transform::translation({4.0 * (i % columns), 4.0 * (i / columns), 0}));
    const auto &totals = doc.materializedTotals();
    check(doc.instances().size() == count && totals.records == 2 * count &&
              totals.vertices == 52 * count && totals.faces == 28 * count &&
              totals.edges == 78 * count,
          "The public path places " + std::to_string(count) + " instances (" +
              std::to_string(totals.vertices) + " vertices)");
    size_t triangles{};
    for (const auto &[id, body] : doc.bodies())
        triangles += body->surface.triangles().size();
    check(triangles == 100 * count, "The placed fixture has 100 triangles per instance");
}

} // namespace

int main() {
    try {
        diagnostics();
        uniqueBoundaries();
        definitionBoundaries();
        definitionGeometry();
        placementCount();
        uniqueGeometry();
        draftCountsEditedDefinitionOnce();
        // The full 10,000-placement public path runs in unsanitized builds only;
        // the serial sanitized CI job runs the same path at 1,000 placements.
        const size_t placements = test::sanitized ? 1000 : 10000;
        publicPlacements(placements);
        std::cout << "Document limits: unique geometry, placement records, materialized "
                     "totals and bounded diagnostics passed ("
                  << (test::sanitized ? "sanitized mode: " : "full mode: ") << placements
                  << " public placements)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
