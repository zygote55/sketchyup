#pragma once
#include "automation/model_recipes.hpp"
namespace sketchy {
ModelRecipeResult executeSiteRecipe(Document &draft, const QJsonObject &command);
} // namespace sketchy
