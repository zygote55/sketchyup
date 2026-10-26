#include "io/catalog_io.hpp"
namespace sketchy {
QString catalogKindName(CatalogEntryKind kind) {
    switch (kind) {
    case CatalogEntryKind::Component:
        return QStringLiteral("component");
    case CatalogEntryKind::Template:
        return QStringLiteral("template");
    case CatalogEntryKind::Style:
        return QStringLiteral("style");
    case CatalogEntryKind::Material:
        return QStringLiteral("material");
    }
    throw std::runtime_error("Unknown catalog entry kind");
}
} // namespace sketchy
