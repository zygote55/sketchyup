#pragma once
#include "core/model.hpp"
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
namespace sketchy {
QJsonObject encodeCurve(Id id, const Curve &curve);
QJsonObject encodeGuide(Id id, const Guide &guide);
QJsonArray encodeBodies(const std::map<Id, BodyPtr> &records);
QByteArray encodeDocument(const Document &doc);
Document decodeDocument(const QByteArray &bytes);
QByteArray encodeContainer(const Document &doc);
Document decodeContainer(const QByteArray &bytes);
// Capture on the document thread. Bytes and stamp remain immutable while edits continue.
class SaveSnapshot {
  public:
    const QByteArray &bytes() const { return bytes_; }
    std::uint64_t revision() const { return revision_; }

  private:
    friend SaveSnapshot captureSave(const Document &doc);
    friend void saveSnapshot(Document &doc, const SaveSnapshot &snapshot, const QString &path);
    QByteArray bytes_;
    Document::SaveStamp stamp_;
    std::uint64_t revision_{};
};
SaveSnapshot captureSave(const Document &doc);
void saveSnapshot(Document &doc, const SaveSnapshot &snapshot, const QString &path);
void saveDocument(Document &doc, const QString &path);
Document loadDocument(const QString &path);
} // namespace sketchy
