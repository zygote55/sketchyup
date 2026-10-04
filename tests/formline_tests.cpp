#include "core/entity_measure.hpp"
#include "io/formline.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double value, double expected, const char *message) {
    check(std::abs(value - expected) < 1e-5, message);
}
template <class F> void rejects(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Malformed Formline source must reject");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QFile fixture(QString(SOURCE_DIR) + "/tests/fixtures/formline-v1.formline");
        check(fixture.open(QIODevice::ReadOnly), "Open synthetic Formline fixture");
        const auto bytes = fixture.readAll();
        auto imported = importFormline(bytes);
        auto &doc = imported.document;
        check(doc.dirty() && doc.revision() == 1 && doc.bodies().size() == 4 &&
                  doc.materials().size() == 2,
              "Import is one unsaved edit with a named group, geometry and reused swatches");
        check(doc.bodies().at(1)->name == "Migration study" &&
                  doc.bodies().at(1)->kind == BodyKind::Group &&
                  doc.bodies().at(2)->name == "Rotated box" && doc.bodies().at(3)->hidden &&
                  !doc.bodies().at(4)->hidden,
              "Model/object names and hidden/default-visible states preserved");
        check(std::get<std::string>(doc.bodies().at(2)->properties.at("formline.sourceId")) ==
                      "rotated-box" &&
                  imported.report["mapping"].toArray()[1].toObject()["body"] == "3",
              "Source identity maps to persistent native bodies");
        const auto box = measureEntity(doc, {2, SelectionKind::Body, 0});
        const auto bounds = *box.world.bounds;
        near(bounds.low.x, -1, "Rotated box minimum X");
        near(bounds.high.x, 5, "Rotated box maximum X");
        near(bounds.low.y, -7, "Y-up conversion minimum Y");
        near(bounds.high.y, -3, "Y-up conversion maximum Y");
        near(bounds.low.z, 3, "Original y is base height");
        near(bounds.high.z, 5, "Original h is vertical height");
        check(bool(box.world.volume), "Imported box has closed outward topology");
        near(*box.world.volume, 48, "Imported box volume in cubic meters");
        near(box.world.area, 88, "Imported box area in square meters");
        near(doc.worldTransform(2).vector({1, 0, 0}).y, 1, "Positive Y yaw becomes positive Z yaw");
        near(doc.worldTransform(4).vector({1, 0, 0}).y, -.5, "Negative rotation preserved");
        const auto cylinder = measureEntity(doc, {3, SelectionKind::Body, 0});
        if (!cylinder.world.volume) {
            std::cerr << "Cylinder solid status: " << cylinder.solid.status << " faces:";
            for (auto id : cylinder.solid.faces)
                std::cerr << ' ' << id;
            std::cerr << '\n';
        }
        check(doc.bodies().at(3)->surface.faces.size() == 50 &&
                  doc.bodies().at(3)->surface.vertices.size() == 96 && cylinder.world.volume,
              "Cylinder becomes an editable 48-sided closed prism");
        near(*cylinder.world.volume, 24 * std::sin(std::numbers::pi / 24) * 4,
             "Faceted cylinder volume matches prototype tessellation");
        near(cylinder.world.bounds->low.x, -4, "Cylinder width supplies diameter");
        near(cylinder.world.bounds->high.y, 8, "Source negative z becomes positive y");
        near(cylinder.world.bounds->high.z, 4.5, "Cylinder base and height preserved");
        for (const auto id : {2, 3, 4}) {
            const auto center = doc.worldTransform(id).point({0, 0,
                                                              id == 2   ? 1.
                                                              : id == 3 ? 2.
                                                                        : .01});
            for (const auto &triangle : doc.worldTriangles(id))
                check(dot(cross(triangle.b - triangle.a, triangle.c - triangle.a),
                          (triangle.a + triangle.b + triangle.c) * (1. / 3) - center) > 0,
                      "Every imported triangle has outward winding");
        }
        near(doc.materials().at(1)->color[0], 196. / 255, "Hex color preserved");
        check(imported.report["warnings"].toArray().size() == 1 &&
                  imported.report["warnings"].toArray()[0].toObject()["code"] ==
                      "cylinder_depth_ignored",
              "Cylinder depth mismatch is reported explicitly");
        const auto bodies = doc.bodies();
        const auto materials = doc.materials();
        doc.undo();
        check(doc.bodies().empty() && doc.materials().empty(), "One undo removes entire import");
        doc.redo();
        check(doc.bodies() == bodies && doc.materials() == materials,
              "Redo restores native identities");
        QTemporaryDir files;
        const auto source = files.filePath("source.formline");
        QFile original(source);
        check(original.open(QIODevice::WriteOnly) && original.write(bytes) == bytes.size(),
              "Write source copy");
        original.close();
        auto fromFile = loadFormline(source);
        saveDocument(fromFile.document, files.filePath("native.sketchyup"));
        check(
            QFile::copy(files.filePath("native.sketchyup"), files.filePath("relocated.sketchyup")),
            "Relocate native copy");
        auto reopened = loadDocument(files.filePath("relocated.sketchyup"));
        check(encodeDocument(reopened) == encodeDocument(fromFile.document),
              "Relocated import preserves all native records and identities");
        check(original.open(QIODevice::ReadOnly) && original.readAll() == bytes,
              "Import and native save leave source bytes intact");
        original.close();
        const auto face = reopened.bodies().at(2)->surface.faces.begin()->first;
        const auto before = encodeDocument(reopened);
        reopened.pushPull(2, face, .25);
        check(encodeDocument(reopened) != before, "Imported faces remain native editable topology");
        auto root = QJsonDocument::fromJson(bytes).object();
        auto malformed = [&](const std::function<void(QJsonObject &)> &edit) {
            auto copy = root;
            edit(copy);
            rejects([&] { importFormline(QJsonDocument(copy).toJson()); });
        };
        malformed([](auto &r) { r["version"] = 2; });
        malformed([](auto &r) { r["name"] = QString(201, 'x'); });
        malformed([](auto &r) {
            auto objects = r["objects"].toArray();
            objects.append(objects[0]);
            r["objects"] = objects;
        });
        for (const auto &key :
             {"id", "type", "name", "color", "x", "y", "z", "w", "h", "d", "rotation"})
            malformed([&](auto &r) {
                auto a = r["objects"].toArray();
                auto o = a[0].toObject();
                o.remove(key);
                a[0] = o;
                r["objects"] = a;
            });
        for (const auto &key : {"x", "y", "z", "rotation"})
            malformed([&](auto &r) {
                auto a = r["objects"].toArray();
                auto o = a[0].toObject();
                o[key] = 10001;
                a[0] = o;
                r["objects"] = a;
            });
        for (const auto &key : {"w", "h", "d"})
            malformed([&](auto &r) {
                auto a = r["objects"].toArray();
                auto o = a[0].toObject();
                o[key] = 0;
                a[0] = o;
                r["objects"] = a;
            });
        malformed([](auto &r) {
            auto a = r["objects"].toArray();
            auto o = a[0].toObject();
            o["color"] = "url(file)";
            a[0] = o;
            r["objects"] = a;
        });
        malformed([](auto &r) {
            QJsonArray a;
            for (int i = 0; i < 1001; ++i)
                a.append(QJsonObject{});
            r["objects"] = a;
        });
        rejects([&] { importFormline(bytes + QByteArray(32 * 1024 * 1024, ' ')); });
        rejects([&] { importFormline(bytes.first(bytes.size() - 3)); });
        auto invalidUnicode = bytes;
        invalidUnicode.replace("Migration study", "\\ud800");
        rejects([&] { importFormline(invalidUnicode); });
        auto unicode = root;
        unicode["name"] = QString::fromUtf8("模型 · \xf0\x9f\x8f\xa0");
        const auto unicodeModel = importFormline(QJsonDocument(unicode).toJson());
        check(QString::fromStdString(unicodeModel.document.bodies().at(1)->name) ==
                  unicode["name"].toString(),
              "Non-ASCII names preserve Unicode without lossy replacement");
        auto extra = root;
        auto a = extra["objects"].toArray();
        auto o = a[0].toObject();
        o["visible"] = "false";
        o["extra"] = "ignored";
        a[0] = o;
        extra["objects"] = a;
        const auto compatible = importFormline(QJsonDocument(extra).toJson());
        check(!compatible.document.bodies().at(2)->hidden &&
                  compatible.report["warnings"].toArray().size() == 3,
              "Legacy extra-field and false-only visibility semantics are reported");
        check(original.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                  original.write("bad") == 3,
              "Corrupt source fixture");
        original.close();
        rejects([&] { loadFormline(source); });
        check(original.open(QIODevice::ReadOnly) && original.readAll() == "bad",
              "Rejected source remains untouched");
        std::cout << "Formline axes, units, rotations, winding, faceted cylinders, colors, "
                     "identities, editability, source preservation and bounds passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
