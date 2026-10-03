#pragma once
#include "automation/commands.hpp"
namespace sketchy {
// View-only transaction state. Preview and cancellation never publish an edit.
class ToolSession {
  public:
    enum class Phase { Ready, Anchored, Preview, Committed };
    explicit ToolSession(Document &document) : document_(document) {}
    Phase phase() const { return phase_; }
    bool active() const { return phase_ == Phase::Anchored || phase_ == Phase::Preview; }
    bool canRevise() const { return phase_ == Phase::Committed && document_.canAmend(amendment_); }
    bool current() const {
        return identity_ == document_.identity() && revision_ == document_.revision();
    }
    void begin();
    void cancel();
    QJsonObject preview(const QJsonObject &command);
    QJsonObject commit(const QJsonObject &command);

  private:
    Document &document_;
    Phase phase_{Phase::Ready};
    std::string identity_;
    std::uint64_t revision_{};
    Document::AmendStamp amendment_;
    QJsonObject request(const QJsonObject &command) const;
};
} // namespace sketchy
