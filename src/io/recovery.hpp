#pragma once
#include "io/document_io.hpp"
#include <QDateTime>
#include <memory>
namespace sketchy {
struct RecoveryContext {
    QString sourcePath;
    std::optional<std::uint64_t> savedRevision;
    QDateTime savedAt;
};
struct RecoveryInfo {
    QString key, documentId, sourcePath;
    std::uint64_t revision{};
    std::optional<std::uint64_t> savedRevision;
    QDateTime capturedAt, savedAt;
};
// Capture only on the document thread. The writer uses immutable bytes, never the live document.
class RecoverySnapshot {
  public:
    const RecoveryInfo &info() const { return info_; }

  private:
    friend RecoverySnapshot captureRecovery(const Document &, const RecoveryContext &);
    friend class RecoveryWriter;
    QByteArray bytes_;
    RecoveryInfo info_;
};
RecoverySnapshot captureRecovery(const Document &, const RecoveryContext & = {});
struct RecoveryRead {
    RecoveryInfo info;
    std::optional<Document> document;
    QString issue;
    bool incompleteTail{}, busy{}, verified{};
};
// Each writer owns a separate session beneath the document/epoch key. Destroying it releases
// the process lock but preserves recovery data. A failed write requires a fresh checkpoint.
class RecoveryWriter {
  public:
    RecoveryWriter(const QString &root, const QString &documentId);
    ~RecoveryWriter();
    RecoveryWriter(const RecoveryWriter &) = delete;
    RecoveryWriter &operator=(const RecoveryWriter &) = delete;
    RecoveryInfo write(const RecoverySnapshot &);
    const QString &key() const;
    void discard();

  private:
    struct State;
    std::unique_ptr<State> state_;
};
RecoveryRead readRecovery(const QString &root, const QString &key);
std::vector<RecoveryRead> listRecoveries(const QString &root);
void discardRecovery(const QString &root, const QString &key);
} // namespace sketchy
