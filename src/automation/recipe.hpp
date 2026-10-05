#pragma once
#include "automation/session.hpp"
namespace sketchy {
inline constexpr int recipeInputBytes = 1024 * 1024;
inline constexpr int recipeBudgetBytes = 8 * 1024 * 1024;
QJsonObject recipeCapabilities();
class AutomationRecipe {
  public:
    struct Limits {
        size_t retainedBytes{recipeBudgetBytes};
        size_t outputBytes{recipeBudgetBytes};
    };
    static AutomationRecipe parse(const QByteArray &bytes);
    static AutomationRecipe load(const QString &path);
    int run(AutomationSession &session, QIODevice &output) const;
    int run(AutomationSession &session, QIODevice &output, Limits limits) const;

  private:
    explicit AutomationRecipe(QJsonArray steps) : steps_(std::move(steps)) {}
    QJsonArray steps_;
};
} // namespace sketchy
