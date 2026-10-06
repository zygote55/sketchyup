#pragma once
#include "automation/commands.hpp"
namespace sketchy {
struct ModelRecipeResult {
    QJsonObject report;
    QJsonArray steps; // Ordinary engine receipts, including topology lineage.
};
// Called only on the batch dispatcher's private document. Expands ordinary
// geometry/scene commands or adopts validated recipe relationships through a
// coherent core edit; the outer batch owns atomic publication and history.
ModelRecipeResult executeModelRecipe(Document &draft, const QJsonObject &command);
} // namespace sketchy
