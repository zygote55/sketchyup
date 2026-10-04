#include "app/tool_session.hpp"
#include "automation/component_scope.hpp"
namespace sketchy {
void ToolSession::begin() {
    identity_ = document_.identity();
    revision_ = document_.revision();
    phase_ = Phase::Anchored;
    amendment_ = {};
    componentScope_ = scopeProvider_ ? scopeProvider_() : 0;
}
void ToolSession::cancel() { phase_ = Phase::Ready; }
QJsonObject ToolSession::request(const QJsonObject &command) const {
    if (!canRevise()) {
        if (!active())
            throw std::runtime_error("Choose a new point or face; the last operation is no longer "
                                     "eligible for revision");
        if (!current())
            throw std::runtime_error(
                "Document changed during this operation; choose a new starting point");
    }
    const auto scoped =
        componentScope_ ? componentScopeCommand(document_, componentScope_, {command}) : command;
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(document_.identity())},
            {"expectedRevision", QString::number(document_.revision())},
            {"commands", QJsonArray{scoped}}};
}
QJsonObject ToolSession::preview(const QJsonObject &command) {
    if (canRevise())
        return componentScopeResult(previewAmend(document_, amendment_, request(command)),
                                    componentScope_);
    if (active())
        phase_ = Phase::Anchored;
    auto result = previewBatch(document_, request(command));
    phase_ = Phase::Preview;
    return componentScopeResult(result, componentScope_);
}
QJsonObject ToolSession::commit(const QJsonObject &command) {
    auto result = canRevise() ? executeAmend(document_, amendment_, request(command))
                              : executeBatch(document_, request(command));
    phase_ = Phase::Committed;
    amendment_ = document_.amendmentStamp();
    return componentScopeResult(result, componentScope_);
}
} // namespace sketchy
