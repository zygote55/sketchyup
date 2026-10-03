#include "app/tool_session.hpp"
namespace sketchy {
void ToolSession::begin() {
    identity_ = document_.identity();
    revision_ = document_.revision();
    phase_ = Phase::Anchored;
}
void ToolSession::cancel() { phase_ = Phase::Ready; }
QJsonObject ToolSession::request(const QJsonObject &command) const {
    if (!active())
        throw std::runtime_error("Choose the first point or face before committing");
    if (!current())
        throw std::runtime_error(
            "Document changed during this operation; choose a new starting point");
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(identity_)},
            {"expectedRevision", QString::number(revision_)},
            {"commands", QJsonArray{command}}};
}
QJsonObject ToolSession::preview(const QJsonObject &command) {
    if (active())
        phase_ = Phase::Anchored;
    auto result = previewBatch(document_, request(command));
    phase_ = Phase::Preview;
    return result;
}
QJsonObject ToolSession::commit(const QJsonObject &command) {
    auto result = executeBatch(document_, request(command));
    phase_ = Phase::Committed;
    return result;
}
} // namespace sketchy
