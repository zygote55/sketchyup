#pragma once
// R093.c: parametric documents shared by the schema-28 tests, the fuzz seeds and the
// parser fault corpus (ADR 0165 §1 table fixture, §7 seeds).
#include "core/component_parameters.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include <stdexcept>
namespace sketchy::parameter_fixture {
inline Id box(Document &doc, Vec3 low, Vec3 high, const std::string &name) {
    const auto id = doc.addFace({{{low.x, low.y, low.z},
                                  {high.x, low.y, low.z},
                                  {high.x, high.y, low.z},
                                  {low.x, high.y, low.z}}},
                                name);
    doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, high.z - low.z);
    return id;
}
inline ComponentParameterSpec parameter(std::string name, std::string label, ParameterType type,
                                        ComponentParameterMode mode, std::string expression,
                                        std::optional<double> min = {},
                                        std::optional<double> max = {},
                                        std::optional<double> step = {}) {
    ComponentParameterSpec result;
    result.name = std::move(name);
    result.label = std::move(label);
    result.type = type;
    result.mode = mode;
    result.expression = std::move(expression);
    result.min = min;
    result.max = max;
    result.step = step;
    return result;
}
inline ComponentDriverSpec driver(ComponentDriverKind kind, Id member, std::string expression,
                                  ComponentAxis axis = ComponentAxis::X) {
    ComponentDriverSpec result;
    result.kind = kind;
    result.member = member;
    result.expression = std::move(expression);
    result.axis = axis;
    return result;
}
inline ParameterValue metres(double value) {
    return ParameterQuantity{quantizeParameterLength(value), {1, 0}};
}
inline ParameterValue count(double value) { return ParameterQuantity{value, {0, 0}}; }

struct Table {
    Document doc;
    Id definition{}, room{};
    // The table placed alone at its defaults, the overridden table, the room's
    // placement and the table nested in it (a member of the room's definition).
    Id defaults{}, wide{}, roomPlacement{}, nested{};
    Id top{}, legLeftFront{}, legRightFront{}, legRightBack{}, shelf{};
    Id stretch{}, repeat{}, child{};
};
// The ADR 0165 §1 table with every parameter type and every driver kind: width
// (length, 900 mm, 0.4-2.4 m), shelves (count, 1, 0-4, step 1), finish (choice),
// open (boolean), tilt (angle, bounded), derived leg_x, top_area and ratio; a stretch
// on the top, positions on the right legs, a rotation on a left leg, a repeat (at most
// `maxShelves`) and a visibility on the shelf. A room assembly nests one table and
// drives its width through childParameter. Another table overrides width, shelves and
// finish, so two shelf copies exist. One retired driver ID sits below the allocator.
// The room definition has the assembly identity sketchyup-tests/room, version 1.
inline Table makeTable(std::uint32_t maxShelves = 4) {
    using Kind = ComponentDriverKind;
    using Mode = ComponentParameterMode;
    Table t;
    auto &doc = t.doc;
    t.top = box(doc, {0, 0, .72}, {.9, .6, .75}, "Top");
    t.legLeftFront = box(doc, {0, 0, 0}, {.05, .05, .72}, "Leg left front");
    const auto legLeftBack = box(doc, {0, .55, 0}, {.05, .6, .72}, "Leg left back");
    t.legRightFront = box(doc, {.84, 0, 0}, {.89, .05, .72}, "Leg right front");
    t.legRightBack = box(doc, {.84, .55, 0}, {.89, .6, .72}, "Leg right back");
    t.shelf = box(doc, {.05, .05, .2}, {.84, .55, .22}, "Shelf");
    const auto group = createGroup(
        doc, {t.top, t.legLeftFront, legLeftBack, t.legRightFront, t.legRightBack, t.shelf},
        "Table");
    const auto created = createComponent(doc, group, "Table");
    t.definition = created.definition;
    const auto first = created.instance;
    defineComponentParameter(doc, t.definition,
                             parameter("width", "Width", ParameterType::Length, Mode::Input,
                                       "900mm", .4, 2.4));
    defineComponentParameter(doc, t.definition,
                             parameter("shelves", "Shelves", ParameterType::Count, Mode::Input, "1",
                                       0, maxShelves, 1));
    auto finish = parameter("finish", "Finish", ParameterType::Choice, Mode::Input, "\"oak\"");
    finish.options = {{"oak", "Oak"}, {"ash", "Ash \"natural\""}};
    defineComponentParameter(doc, t.definition, finish);
    defineComponentParameter(doc, t.definition, parameter("open", "Open shelf",
                                                          ParameterType::Boolean, Mode::Input,
                                                          "true"));
    defineComponentParameter(doc, t.definition,
                             parameter("tilt", "Leg tilt", ParameterType::Angle, Mode::Input,
                                       "0 deg", -.5, .5));
    defineComponentParameter(doc, t.definition,
                             parameter("leg_x", "Leg position", ParameterType::Length, Mode::Derived,
                                       "width-60 mm"));
    defineComponentParameter(doc, t.definition,
                             parameter("top_area", "Top area", ParameterType::Area, Mode::Derived,
                                       "width * 600 mm"));
    defineComponentParameter(doc, t.definition,
                             parameter("ratio", "Width ratio", ParameterType::Number, Mode::Derived,
                                       "width / 1 m"));
    auto stretch = driver(Kind::Stretch, t.top, "width");
    stretch.at = .45;
    t.stretch = driveComponent(doc, t.definition, 0, stretch).createdDrivers.at(0);
    driveComponent(doc, t.definition, 0, driver(Kind::Position, t.legRightFront, "leg_x"));
    driveComponent(doc, t.definition, 0, driver(Kind::Position, t.legRightBack, "leg_x"));
    driveComponent(doc, t.definition, 0,
                   driver(Kind::Rotation, t.legLeftFront, "tilt", ComponentAxis::Z));
    // A retired driver: its ID stays below the allocator and is never reused.
    const auto retired = driveComponent(doc, t.definition, 0,
                                        driver(Kind::Visibility, legLeftBack, "open"))
                             .createdDrivers.at(0);
    undriveComponent(doc, t.definition, retired);
    auto repeat = driver(Kind::Repeat, t.shelf, "max(shelves,1)", ComponentAxis::Z);
    repeat.maxCount = maxShelves;
    repeat.spacing = "300 mm";
    t.repeat = driveComponent(doc, t.definition, 0, repeat).createdDrivers.at(0);
    driveComponent(doc, t.definition, 0, driver(Kind::Visibility, t.shelf, "shelves > 0"));
    // The room nests the first table and drives its width.
    const auto floor = box(doc, {-1, -1, -.1}, {3, 3, 0}, "Floor");
    const auto roomGroup = createGroup(doc, {first, floor}, "Room");
    const auto room = createComponent(doc, roomGroup, "Room");
    t.room = room.definition;
    t.roomPlacement = room.instance;
    t.nested = first;
    defineComponentParameter(doc, t.room,
                             parameter("table_width", "Table width", ParameterType::Length,
                                       Mode::Input, "1500 mm"));
    auto child = driver(Kind::ChildParameter, first, "table_width");
    child.parameter = "width";
    t.child = driveComponent(doc, t.room, 0, child).createdDrivers.at(0);
    overrideComponentParameters(doc, t.roomPlacement, {{"table_width", metres(1.8)}});
    // The room carries a library assembly identity (ADR 0166 §6.2; R094.b).
    setComponentAssembly(doc, t.room, ComponentAssembly{"sketchyup-tests/room", 1});
    // Two top-level tables: one at its defaults, one overridden with shelf copies.
    t.defaults =
        placeComponent(doc, t.definition, Transform::translation({0, 4, 0})).instance;
    t.wide = placeComponent(doc, t.definition, Transform::translation({4, 0, 0})).instance;
    overrideComponentParameters(
        doc, t.wide,
        {{"width", metres(1.2)}, {"shelves", count(3)}, {"finish", ParameterChoice{"ash"}}});
    return t;
}
} // namespace sketchy::parameter_fixture
