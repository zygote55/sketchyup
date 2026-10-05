#pragma once
#include "automation/inspection.hpp"
#include <QDateTime>
#include <chrono>
#include <functional>
#include <memory>
namespace sketchy {
QJsonObject inspectionSessionCapabilities();
// Caller serializes access with document/editor operations. No workers, files,
// sockets or model mutations are created by this session.
class InspectionSession {
  public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;
    struct Limits {
        size_t snapshots{4};
        size_t retainedBytes{32 * 1024 * 1024};
    };
    InspectionSession();
    explicit InspectionSession(Limits limits, Now now = Clock::now);
    QJsonObject execute(const Document &live, const QJsonObject &request,
                        const Selection *editor = nullptr);
    size_t retainedBytes() const { return bytes_; }
    size_t retainedCount() const { return snapshots_.size(); }
    void clear();

  private:
    struct Snapshot {
        std::unique_ptr<const Document> document;
        std::optional<Selection> editor;
        Document::SaveStamp source;
        uint64_t revision{};
        QDateTime capturedAt;
        Clock::time_point expires;
        size_t retainedBytes{};
    };
    Limits limits_;
    Now now_;
    size_t bytes_{};
    std::map<QString, Snapshot> snapshots_;
    void prune(const Document &live);
};
} // namespace sketchy
