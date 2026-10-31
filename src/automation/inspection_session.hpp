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
    // Charged now, against the live document: each snapshot holds only the records it does not
    // share with it, so a snapshot of a since-edited model stays charged until it is released.
    size_t retainedBytes(const Document &live) const;
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
        // Editor state captured with the snapshot, charged whatever the live document is.
        size_t editorBytes{};
    };
    Limits limits_;
    Now now_;
    std::map<QString, Snapshot> snapshots_;
    void prune(const Document &live);
    size_t charge(const Snapshot &snapshot, const Document &live) const;
};
} // namespace sketchy
