#include "io/obj_source.hpp"
#include <QCoreApplication>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Malformed OBJ must reject");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const QByteArray triangle = "v 0 0 0\nv 2000 0 0\nv 0 3000 0\nf -3 -2 -1\n";
        const auto parsed = parseObj(triangle, {.001, ObjUpAxis::Y});
        check(parsed.vertices[1] == Vec3{2, 0, 0} && parsed.vertices[2] == Vec3{0, 0, 3},
              "Explicit millimetres and Y up convert to native metres and Z up");
        check(parsed.faces.size() == 1 && parsed.faces[0].corners[0].vertex == 0 &&
                  parsed.faces[0].corners[2].vertex == 2,
              "Negative indices resolve at statement position");
        const auto later = parseObj(triangle + "v 9 9 9\n", {.001, ObjUpAxis::Z});
        check(later.faces[0].corners[2].vertex == 2,
              "Later vertices cannot change earlier negative references");
        const auto forward = parseObj("f 1 2 3\nv 0 0 0\nv 1 0 0\nv 0 1 0\n", {1, ObjUpAxis::Z});
        check(forward.faces[0].corners[2].vertex == 2,
              "Positive references validate against complete lists");
        const QByteArray attributed =
            "mtllib first.mtl second.mtl\no Building\ng Walls Exterior Walls\nusemtl Plaster\ns "
            "7\nv 0 0 0\nv 2 0 0\nv 0 3 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 2\nf 1/1/1 2/2/1 "
            "\\\n3/3/1 # continued face\nl 1/1 2/2\nvp 0 0 0\n";
        const auto rich = parseObj(attributed, {1, ObjUpAxis::Z});
        check(rich.materialLibraries == QStringList{"first.mtl", "second.mtl"} &&
                  rich.states[0].groups == QStringList{"Exterior", "Walls"} &&
                  rich.states[0].object == "Building" && rich.states[0].material == "Plaster" &&
                  rich.states[0].smoothing == 7,
              "Grouping and material references retained without executing them");
        check(rich.textures[0] == TextureCoordinate{0, 1} &&
                  rich.textures[2] == TextureCoordinate{0, 0} && rich.normals[0] == Vec3{0, 0, 1},
              "Texture V origin and explicit normals converted");
        check(rich.lines.size() == 1 && rich.report["omittedStatements"].toObject()["vp"] == 1,
              "Wires retained and unsupported records counted");
        const QByteArray concave =
            "v 0 0 0\nv 2 0 0\nv 2 1 0\nv 1 1 0\nv 1 2 0\nv 0 2 0\nf 1 2 3 4 5 6\n";
        const auto polygon = parseObj(concave, {1, ObjUpAxis::Z});
        Surface surface;
        std::vector<Vec3> loop;
        for (const auto &corner : polygon.faces[0].corners)
            loop.push_back(polygon.vertices[corner.vertex]);
        const auto face = surface.addFace({loop});
        check(std::abs(surface.area(face) - 3) < 1e-9 && surface.triangles().size() == 4,
              "Concave loop retains exact shape for native triangulation");
        check(parseObj(QByteArray("\xef\xbb\xbf") + triangle, {1, ObjUpAxis::Z}).faces.size() == 1,
              "UTF-8 BOM accepted");
        for (const QByteArray &bad :
             {QByteArray("f 0 2 3\n"), QByteArray("f -4 -2 -1\n"), QByteArray("f 1 2 4\n"),
              QByteArray("f 1/ 2/ 3/\n"), QByteArray("f 1//1 2 3\n"),
              QByteArray("csh touch sentinel\n"), QByteArray("call outside.obj\n"),
              QByteArray("v nan 0 0\n"), QByteArray("vn 0 0 0\n"), QByteArray("v 1 2 3 0\n"),
              QByteArray("vt 1 2 3\n"), QByteArray("s -1\n"), QByteArray("f 1 2 \\\n")})
            rejects([&] { parseObj(triangle + bad, {1, ObjUpAxis::Z}); });
        rejects([&] { parseObj(triangle + QByteArray(65537, 'x'), {1, ObjUpAxis::Z}); });
        rejects([&] { parseObj(triangle + QByteArray(1, '\0'), {1, ObjUpAxis::Z}); });
        rejects([&] { parseObj(triangle + QByteArray(1, char(0xff)), {1, ObjUpAxis::Z}); });
        rejects([&] { parseObj(QByteArray(64 * 1024 * 1024 + 1, ' '), {1, ObjUpAxis::Z}); });
        rejects([&] { parseObj(triangle, {0, ObjUpAxis::Z}); });
        rejects([&] { parseObj(triangle, {1, static_cast<ObjUpAxis>(999)}); });
        std::cout << "Bounded OBJ units, axes, concavity, references, groups and "
                     "executable-directive rejection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
