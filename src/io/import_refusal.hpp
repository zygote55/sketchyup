#pragma once
// R105.a (ADR 0179 section 7.3): the named refusals for the file types a user downloads and that
// SketchyUp cannot import: a ZIP archive, COLLADA (.dae), USDZ and FBX. The check runs on the file
// the user chose, before any importer does. It reads at most 64 KiB and changes nothing. A refusal
// names the format and the reason, and says what to do next; it is never a generic "unsupported
// file".
#include <QString>

#include <optional>
#include <string>

namespace sketchy {

struct ImportRefusal {
    std::string code; // "zipArchive", "collada", "usdz" or "fbx"
    QString message;  // the reason, which names the format
    QString whatToDo; // numbered next steps, or empty when the message already says them
};

// The refusal for a file of a refused type, or nothing: every other file goes to its importer,
// which reports its own faults. A file is judged by its extension first. A file whose extension no
// importer reads is judged by its first bytes, so a ZIP, an FBX or a COLLADA document under another
// name is still named.
std::optional<ImportRefusal> refusedImportFile(const QString &path);

} // namespace sketchy
