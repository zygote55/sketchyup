#include "io/import_refusal.hpp"

#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace sketchy {
namespace {

// The bytes read to judge a file by its content: enough for the COLLADA root element after an XML
// declaration and a comment, and for every magic number here. Bounded, so a large download costs no
// more than a small one.
constexpr qint64 headBytes = 64 * 1024;

// The extensions an importer reads (the File › Import pickers and the CLI's --import-* options). A
// file with one of them is left to its importer, whatever its first bytes are. The IFC and STEP
// importers name their own refusals (an .ifczip, an ifcXML or an ifcJSON file).
const QStringList importerSuffixes{"skp",      "ifc",  "ifczip", "ifcxml", "ifcjson", "step",
                                   "stp",      "stl",  "obj",    "glb",    "gltf",    "dxf",
                                   "formline", "json", "csv",    "txt",    "xyz",     "pts"};

ImportRefusal zipArchive() {
    return {
        "zipArchive",
        QStringLiteral("This is a ZIP archive. Extract it, then import the model file inside it."),
        {}};
}

ImportRefusal collada() {
    return {
        "collada",
        QStringLiteral("A COLLADA (.dae) file is not imported. SketchyUp has no COLLADA reader."),
        QStringLiteral("1. If the source offers a newer .skp version of this model, download that "
                       "file instead.\n"
                       "2. The .skp import is not released in this version, so the file cannot be "
                       "imported yet.")};
}

ImportRefusal usdz() {
    return {"usdz",
            QStringLiteral("A USDZ (.usdz) file is not imported. SketchyUp has no USD reader."),
            QStringLiteral(
                "1. If the source offers a glTF or GLB version of this model, download that file.\n"
                "2. Import it with File › Import GLB/glTF…")};
}

ImportRefusal fbx() {
    return {"fbx",
            QStringLiteral("An FBX (.fbx) file is not imported. SketchyUp has no FBX reader."),
            QStringLiteral(
                "1. If the source offers a glTF or GLB version of this model, download that file.\n"
                "2. Import it with File › Import GLB/glTF…")};
}

} // namespace

std::optional<ImportRefusal> refusedImportFile(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "usdz")
        return usdz();
    if (suffix == "dae")
        return collada();
    if (suffix == "fbx")
        return fbx();
    if (suffix == "zip")
        return zipArchive();
    if (importerSuffixes.contains(suffix))
        return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt; // a file that cannot be read is the importer's to report
    const QByteArray head = file.read(headBytes);
    // A ZIP local header (or an empty archive's end record); the "PK" pair is the ZIP magic.
    if (head.startsWith("PK\x03\x04") || head.startsWith("PK\x05\x06"))
        return zipArchive();
    // The binary FBX header, or an ASCII FBX file's comment line.
    if (head.startsWith("Kaydara FBX Binary") || head.startsWith("; FBX"))
        return fbx();
    if (head.contains("<COLLADA"))
        return collada();
    return std::nullopt;
}

} // namespace sketchy
