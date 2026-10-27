#pragma once
#include "core/model.hpp"
#include "io/catalog_io.hpp"
#include <QString>
#include <QStringList>
#include <vector>

namespace sketchy {

// Catalog import and export (ADR 0174 section 5 and section 7, the R100.d slice).
//
// Import copies the named entries into the destination with fresh identities, through the ADR 0131
// insertion path: a component goes through insertLibraryComponent, and a style, a material and an
// asset go through the same resource rules (an asset is reused only for an equal media type and
// equal bytes; a material only for the same name, colour, opacity and asset; a clashing name is
// renamed). Every entry the request needs is checked before the first change, so a refusal leaves
// the destination untouched. A template entry is refused: it opens as a new model, not as content.
//
// Export captures the chosen components, materials and the document style into a catalog through
// CatalogBuilder. A component is captured with the ADR 0130 closure, and an asset shared by two
// records is stored once. A missing asset refuses the export rather than being left out.

struct CatalogImportEntry {
    QString name;
    CatalogEntryKind kind{};
    bool requested{}; // false for an entry imported only because a requested entry depends on it
    Id definition{}, instance{}, material{};
    bool styleChanged{};
};

struct CatalogImportResult {
    std::vector<CatalogImportEntry> entries;
    size_t reusedAssets{}, reusedMaterials{}, renamedResources{};
};

// Refuses (throws, changing nothing) when a named entry is missing, ambiguous, a template, or not
// importable. The reason names the entry and the missing dependency id. `parent` is the folder the
// components are placed in, 0 for the root.
CatalogImportResult importCatalogEntries(Document &doc, const CatalogPackage &package,
                                         const QStringList &names, Id parent = 0);

struct CatalogExportRequest {
    QString name, description, version, licenseIdentifier;
    QStringList labels;
    std::vector<Id> components, materials;
    bool style{};
};

// Throws, with the record named, when a chosen record is missing, a name repeats, an asset is
// missing, or nothing is chosen. The catalog id is derived from the name.
QByteArray exportCatalog(const Document &doc, const CatalogExportRequest &request);

} // namespace sketchy
