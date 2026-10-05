#pragma once
#include "automation/assistant.hpp"
namespace sketchy {
// Trusted in-process bridge to the existing native document. It never saves to
// the user's file. The host serializes GUI operations and honors uncertain().
class NativeAssistantSession {
  public:
    NativeAssistantSession(Document &document, const QString &outcomes,
                           const Selection *selection = nullptr, std::function<bool()> busy = {},
                           TransactionCoordinator::Options options = {});
    ~NativeAssistantSession();
    AssistantBackend backend();
    QJsonObject state() const;
    QJsonObject execute(const QJsonObject &request);
    std::shared_ptr<const Document::PreparedEdit> previewEdit(const QJsonObject &sealed);
    bool uncertain() const;
    void close();

  private:
    Document &document_;
    Document::SaveStamp scope_;
    const Selection *selection_;
    std::function<bool()> busy_;
    TransactionCoordinator actor_;
    TransactionDispatcher transactions_;
    InspectionSession inspection_;
    std::thread::id owner_{std::this_thread::get_id()};
    bool closed_{}, active_{};
    void checkScope() const;
    void validatePolicy(const Document::PreparedEdit &edit) const;
};
} // namespace sketchy
