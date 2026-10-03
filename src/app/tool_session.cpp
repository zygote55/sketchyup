#include "app/tool_session.hpp"
namespace sketchy {
void ToolSession::begin() {
    identity_ = document_.identity();
    revision_ = document_.revision();
    phase_ = Phase::Anchored;
    amendment_ = {};
}
void ToolSession::cancel() { phase_ = Phase::Ready; }
QJsonObject ToolSession::request(const QJsonObject &command) const {
    if (canRevise())
        return {{"apiVersion", 1},
                {"documentId", QString::fromStdString(document_.identity())},
                {"expectedRevision", QString::number(document_.revision())},
                {"commands", QJsonArray{command}}};
    if (!active())
        throw std::runtime_error(
            "Choose a new point or face; the last operation is no longer eligible for revision");
    if (!current())
        throw std::runtime_error(
            "Document changed during this operation; choose a new starting point");
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(identity_)},
            {"expectedRevision", QString::number(revision_)},
            {"commands", QJsonArray{command}}};
}
QJsonObject ToolSession::preview(const QJsonObject &command) {
    if (canRevise())
        return previewAmend(document_, amendment_, request(command));
    if (active())
        phase_ = Phase::Anchored;
    auto result = previewBatch(document_, request(command));
    phase_ = Phase::Preview;
    return result;
}
QJsonObject ToolSession::commit(const QJsonObject &command) {
    auto result = canRevise() ? executeAmend(document_, amendment_, request(command))
                              : executeBatch(document_, request(command));
    phase_ = Phase::Committed;
    amendment_ = document_.amendmentStamp();
    return result;
}
} // namespace sketchy
