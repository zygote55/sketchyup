// R097.b: the IFC conversion package decoder (ADR 0171 section 1.6): the canonical form, every
// bound, and each kind of corruption it must refuse. No IFC library or worker is involved.
#include "ifc_package_fixtures.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QtEndian>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(const char *label, F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(std::string("must be refused: ") + label);
}
// A package built around a manifest object and a mesh chunk, as the worker writes it.
QByteArray raw(const QJsonObject &manifest, const QByteArray &mesh, std::uint32_t reserved = 0) {
    const auto json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QByteArray out("SKIFCP01", 8);
    char word[8];
    qToLittleEndian<quint32>(quint32(json.size()), word);
    out.append(word, 4);
    qToLittleEndian<quint32>(reserved, word);
    out.append(word, 4);
    qToLittleEndian<quint64>(quint64(mesh.size()), word);
    out.append(word, 8);
    return out + json + mesh;
}
QJsonObject manifestOf(const QByteArray &package) {
    const auto size = qFromLittleEndian<quint32>(package.constData() + 8);
    return QJsonDocument::fromJson(package.mid(24, size)).object();
}
QByteArray meshOf(const QByteArray &package) {
    const auto size = qFromLittleEndian<quint32>(package.constData() + 8);
    return package.mid(24 + size);
}
QByteArray withManifest(const QByteArray &package, const std::function<void(QJsonObject &)> &edit) {
    auto manifest = manifestOf(package);
    edit(manifest);
    return raw(manifest, meshOf(package));
}
QByteArray withNode(const QByteArray &package, int index, const std::function<void(QJsonObject &)> &edit) {
    return withManifest(package, [&](QJsonObject &m) {
        auto nodes = m["nodes"].toArray();
        auto node = nodes[index].toObject();
        edit(node);
        nodes[index] = node;
        m["nodes"] = nodes;
    });
}
// Replace mesh chunk bytes and keep the digest honest, so the structural checks are what refuse.
QByteArray withMesh(const QByteArray &package, const std::function<void(QByteArray &)> &edit) {
    auto mesh = meshOf(package);
    edit(mesh);
    auto manifest = manifestOf(package);
    manifest["meshSha256"] = QString::fromLatin1(QCryptographicHash::hash(mesh, QCryptographicHash::Sha256).toHex());
    return raw(manifest, mesh);
}
void putF64(QByteArray &bytes, int offset, double value) {
    quint64 bits;
    std::memcpy(&bits, &value, 8);
    qToLittleEndian<quint64>(bits, bytes.data() + offset);
}
void putU32(QByteArray &bytes, int offset, quint32 value) { qToLittleEndian<quint32>(value, bytes.data() + offset); }
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto sample = ifc_fixture::samplePackage();
        const auto bytes = encodeIfcPackage(sample);
        const auto decoded = decodeIfcPackage(bytes);
        check(decoded == sample, "decode(encode(p)) == p");
        check(encodeIfcPackage(decoded) == bytes, "the encoding is canonical");
        check(decoded.nodes.size() == 6 && decoded.meshes.size() == 2 && decoded.materials.size() == 2,
              "counts survive");
        check(decoded.meshes[0].colors.size() == 12 && !decoded.meshes[0].colors[1] &&
                  decoded.meshes[0].colors[0] && (*decoded.meshes[0].colors[0])[2] == 0.5f,
              "colours survive, with a missing colour");
        check(decoded.meshes[0].featureEdges.size() == 2 && decoded.meshes[1].colors.empty(), "edges and colour flag");
        check(std::get<std::string>(decoded.nodes[3].properties[0].value) == "EI60" &&
                  std::get<bool>(decoded.nodes[3].properties[1].value) &&
                  std::get<double>(decoded.nodes[3].properties[2].value) == 4.5 && decoded.nodes[3].properties[2].origin == 1,
              "property values keep their types");
        check(decoded.nodes[5].hidden && decoded.nodes[5].role == IfcNodeRole::Space, "space is hidden");

        // The empty package is a valid package.
        {
            IfcPackage empty;
            check(decodeIfcPackage(encodeIfcPackage(empty)) == empty, "an empty package round-trips");
        }

        // Header and lengths.
        rejects("bad magic", [&] { auto b = bytes; b[0] = 'X'; decodeIfcPackage(b); });
        rejects("too short", [&] { decodeIfcPackage(bytes.left(23)); });
        rejects("truncated", [&] { decodeIfcPackage(bytes.left(bytes.size() - 1)); });
        rejects("trailing byte", [&] { decodeIfcPackage(bytes + QByteArray(1, 'x')); });
        rejects("reserved word", [&] { decodeIfcPackage(raw(manifestOf(bytes), meshOf(bytes), 1)); });
        rejects("manifest over 32 MiB", [&] {
            auto b = bytes;
            qToLittleEndian<quint32>(quint32(32 * 1024 * 1024 + 1), b.data() + 8);
            decodeIfcPackage(b);
        });
        rejects("mesh chunk over 128 MiB", [&] {
            auto b = bytes;
            qToLittleEndian<quint64>(quint64(128) * 1024 * 1024 + 1, b.data() + 16);
            decodeIfcPackage(b);
        });
        rejects("lengths overflow", [&] {
            auto b = bytes;
            qToLittleEndian<quint64>(0xFFFFFFFFFFFFFFFFull, b.data() + 16);
            decodeIfcPackage(b);
        });
        rejects("manifest is not JSON", [&] {
            QByteArray b("SKIFCP01", 8);
            b.append(QByteArray(16, '\0'));
            b[8] = 3;
            decodeIfcPackage(b + "not");
        });
        rejects("manifest is an array", [&] {
            const QByteArray json = "[]";
            QByteArray b("SKIFCP01", 8);
            char word[8];
            qToLittleEndian<quint32>(2, word);
            b.append(word, 4);
            b.append(QByteArray(12, '\0'));
            decodeIfcPackage(b + json);
        });

        // Manifest members.
        rejects("unknown format", [&] { decodeIfcPackage(withManifest(bytes, [](auto &m) { m["format"] = "other"; })); });
        rejects("unsupported version", [&] { decodeIfcPackage(withManifest(bytes, [](auto &m) { m["version"] = 2; })); });
        rejects("unknown member", [&] { decodeIfcPackage(withManifest(bytes, [](auto &m) { m["extra"] = 1; })); });
        rejects("missing member", [&] { decodeIfcPackage(withManifest(bytes, [](auto &m) { m.remove("units"); })); });
        rejects("section is not an object", [&] { decodeIfcPackage(withManifest(bytes, [](auto &m) { m["report"] = 3; })); });
        rejects("section nested too deep", [&] {
            decodeIfcPackage(withManifest(bytes, [](auto &m) {
                QJsonValue deep = 1;
                for (int i = 0; i < 12; ++i)
                    deep = QJsonObject{{"a", deep}};
                m["report"] = deep.toObject();
            }));
        });
        rejects("section string too long", [&] {
            decodeIfcPackage(withManifest(bytes, [](auto &m) {
                m["engine"] = QJsonObject{{"x", QString(70000, 'a')}};
            }));
        });
        rejects("section has too many values", [&] {
            decodeIfcPackage(withManifest(bytes, [](auto &m) {
                QJsonArray many;
                for (int i = 0; i < 200001; ++i)
                    many.append(1);
                m["source"] = QJsonObject{{"list", many}};
            }));
        });

        // Digest.
        rejects("digest mismatch", [&] {
            auto b = bytes;
            b[b.size() - 1] = char(b[b.size() - 1] ^ 1);
            decodeIfcPackage(b);
        });
        {
            // With the digest check off the same corruption is not caught by it (a vertex's lowest bit).
            auto b = bytes;
            const auto meshAt = bytes.size() - meshOf(bytes).size();
            b[meshAt] = char(b[meshAt] ^ 1);
            check(!(decodeIfcPackage(b, {false}) == sample), "the corrupted vertex is decoded without the digest");
            rejects("digest checked", [&] { decodeIfcPackage(b); });
        }

        // Materials.
        rejects("material colour range", [&] {
            decodeIfcPackage(withManifest(bytes, [](auto &m) {
                auto materials = m["materials"].toArray();
                auto first = materials[0].toObject();
                first["color"] = QJsonArray{2, 0, 0};
                materials[0] = first;
                m["materials"] = materials;
            }));
        });
        rejects("material opacity range", [&] {
            decodeIfcPackage(withManifest(bytes, [](auto &m) {
                auto materials = m["materials"].toArray();
                auto first = materials[0].toObject();
                first["opacity"] = -0.1;
                materials[0] = first;
                m["materials"] = materials;
            }));
        });

        // Mesh descriptors and data.
        rejects("a mesh without triangles", [&] {
            decodeIfcPackage(withManifest(bytes, [](auto &m) {
                auto meshes = m["meshes"].toArray();
                auto first = meshes[0].toObject();
                first["triangles"] = 0;
                meshes[0] = first;
                m["meshes"] = meshes;
            }));
        });
        rejects("descriptor disagrees with the chunk", [&] {
            decodeIfcPackage(withManifest(bytes, [](auto &m) {
                auto meshes = m["meshes"].toArray();
                auto first = meshes[0].toObject();
                first["edges"] = 3;
                meshes[0] = first;
                m["meshes"] = meshes;
            }));
        });
        rejects("a mesh over 100,000 triangles", [&] {
            decodeIfcPackage(withManifest(bytes, [](auto &m) {
                auto meshes = m["meshes"].toArray();
                auto first = meshes[0].toObject();
                first["triangles"] = 100001;
                meshes[0] = first;
                m["meshes"] = meshes;
            }));
        });
        rejects("meshes over 1,000,000 triangles", [&] {
            QJsonArray meshes;
            for (int i = 0; i < 11; ++i)
                meshes.append(QJsonObject{{"vertices", 300000}, {"triangles", 100000}, {"edges", 0}, {"colors", false}});
            auto manifest = manifestOf(bytes);
            manifest["meshes"] = meshes;
            manifest["nodes"] = QJsonArray{};
            decodeIfcPackage(raw(manifest, QByteArray()), {false});
        });
        rejects("a vertex beyond 1e6 m", [&] { decodeIfcPackage(withMesh(bytes, [](auto &m) { putF64(m, 0, 2e6); })); });
        rejects("a NaN vertex", [&] { decodeIfcPackage(withMesh(bytes, [](auto &m) { putF64(m, 8, std::nan("")); })); });
        rejects("an infinite vertex", [&] { decodeIfcPackage(withMesh(bytes, [](auto &m) { putF64(m, 16, INFINITY); })); });
        rejects("a triangle index past the vertices", [&] { decodeIfcPackage(withMesh(bytes, [](auto &m) { putU32(m, 8 * 24, 8); })); });
        rejects("a material index past the table", [&] { decodeIfcPackage(withMesh(bytes, [](auto &m) { putU32(m, 8 * 24 + 12 * 12, 2); })); });
        rejects("an edge index past the vertices", [&] { decodeIfcPackage(withMesh(bytes, [](auto &m) { putU32(m, 8 * 24 + 12 * 12 + 12 * 4, 9); })); });
        {
            const auto colourAt = 8 * 24 + 12 * 12 + 12 * 4 + 2 * 8;
            rejects("a colour of NaN and numbers", [&] {
                decodeIfcPackage(withMesh(bytes, [&](auto &m) { putU32(m, colourAt, 0x7FC00000u); }));
            });
            rejects("a colour above 1", [&] {
                decodeIfcPackage(withMesh(bytes, [&](auto &m) { putU32(m, colourAt, 0x40000000u); }));
            });
        }

        // Nodes.
        rejects("a parent that comes later", [&] { decodeIfcPackage(withNode(bytes, 1, [](auto &n) { n["parent"] = 2; })); });
        rejects("a node as its own parent", [&] { decodeIfcPackage(withNode(bytes, 0, [](auto &n) { n["parent"] = 0; })); });
        rejects("a mesh index past the meshes", [&] { decodeIfcPackage(withNode(bytes, 3, [](auto &n) { n["mesh"] = 2; })); });
        rejects("a class that is not IFC", [&] { decodeIfcPackage(withNode(bytes, 3, [](auto &n) { n["class"] = "Wall"; })); });
        rejects("a class with punctuation", [&] { decodeIfcPackage(withNode(bytes, 3, [](auto &n) { n["class"] = "IfcWall;DROP"; })); });
        rejects("an unknown role", [&] { decodeIfcPackage(withNode(bytes, 3, [](auto &n) { n["role"] = "roof"; })); });
        rejects("a non-affine transform", [&] {
            decodeIfcPackage(withNode(bytes, 3, [](auto &n) {
                auto t = n["transform"].toArray();
                t[15] = 2;
                n["transform"] = t;
            }));
        });
        rejects("a short transform", [&] {
            decodeIfcPackage(withNode(bytes, 3, [](auto &n) { n["transform"] = QJsonArray{1, 0}; }));
        });
        rejects("an unknown value type", [&] {
            decodeIfcPackage(withNode(bytes, 3, [](auto &n) {
                n["props"] = QJsonArray{QJsonArray{"S", "N", 0, "x", 1}};
            }));
        });
        rejects("a flag that is a number", [&] {
            decodeIfcPackage(withNode(bytes, 3, [](auto &n) {
                n["props"] = QJsonArray{QJsonArray{"S", "N", 0, "b", 1}};
            }));
        });
        rejects("an origin of 4", [&] {
            decodeIfcPackage(withNode(bytes, 3, [](auto &n) {
                n["props"] = QJsonArray{QJsonArray{"S", "N", 4, "d", 1}};
            }));
        });
        rejects("a hidden flag that is a string", [&] { decodeIfcPackage(withNode(bytes, 3, [](auto &n) { n["hidden"] = "yes"; })); });
        rejects("a name over 4096 bytes", [&] {
            decodeIfcPackage(withNode(bytes, 3, [](auto &n) { n["name"] = QString(4097, 'n'); }));
        });
        rejects("too many provenance entries", [&] {
            decodeIfcPackage(withNode(bytes, 3, [](auto &n) {
                QJsonArray list;
                for (int i = 0; i < 65; ++i)
                    list.append(QJsonArray{QString("ifc.K%1").arg(i), "d", 1});
                n["ifc"] = list;
            }));
        });
        rejects("a tree deeper than 128", [&] {
            IfcPackage deep;
            for (int i = 0; i <= 129; ++i)
                deep.nodes.push_back(ifc_fixture::node(unsigned(i + 1), "IfcBuildingElementProxy", "n", i - 1,
                                                       IfcNodeRole::Spatial));
            decodeIfcPackage(encodeIfcPackage(deep));
        });
        {
            IfcPackage deep;
            for (int i = 0; i <= 128; ++i)
                deep.nodes.push_back(ifc_fixture::node(unsigned(i + 1), "IfcBuildingElementProxy", "n", i - 1,
                                                       IfcNodeRole::Spatial));
            check(decodeIfcPackage(encodeIfcPackage(deep)).nodes.size() == 129, "a chain of 129 nodes is within the depth bound");
        }

        // Fuzz-style sweep: every single-byte change of the manifest or mesh header is either
        // refused or decodes to something that re-encodes to an equal package.
        for (qsizetype at = 0; at < bytes.size(); at += 7) {
            auto changed = bytes;
            changed[at] = char(changed[at] ^ 0x5A);
            try {
                const auto package = decodeIfcPackage(changed, {false});
                check(decodeIfcPackage(encodeIfcPackage(package)) == package, "accepted mutation re-encodes");
            } catch (const std::exception &) {
            }
        }
        // Real packages, written by the helper from the buildingSMART samples (regenerated by ifc_import_worker).
        {
            const QDir directory(QStringLiteral(SOURCE_DIR "/tests/fixtures/ifc/packages"));
            const auto names = directory.entryList({"*.skifc"}, QDir::Files, QDir::Name);
            check(names.size() == 3, "three checked-in packages");
            for (const auto &name : names) {
                QFile file(directory.filePath(name));
                check(file.open(QIODevice::ReadOnly), "read a package");
                const auto real = file.readAll();
                const auto package = decodeIfcPackage(real);
                check(!package.nodes.empty() && !package.meshes.empty(), "a real package has nodes and meshes");
                check(package.nodes.front().role == IfcNodeRole::Project && package.nodes.front().parent == -1, "and begins with the project");
                check(decodeIfcPackage(encodeIfcPackage(package)) == package, "and re-encodes to an equal package");
                check(package.source["schema"] == "IFC4" && package.engine["library"] == "IfcOpenShell", "and records its source and engine");
            }
        }
        std::cout << "ifc_package ok\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
