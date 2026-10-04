#include "automation/commands.hpp"
namespace sketchy {
QJsonObject describeHistory(const Document &doc, size_t offset, size_t limit) {
    const auto page = doc.history(offset, limit);
    QJsonArray entries;
    for (const auto &entry : page.entries)
        entries.append(QJsonObject{{"position", QString::number(entry.position)},
                                   {"label", QString::fromStdString(entry.label)},
                                   {"applied", entry.applied},
                                   {"saved", entry.saved},
                                   {"assistant", entry.metadata.assistant},
                                   {"taskId", QString::fromStdString(entry.metadata.taskId)},
                                   {"request", QString::fromStdString(entry.metadata.request)}});
    return {{"documentId", QString::fromStdString(doc.identity())},
            {"revision", QString::number(doc.revision())},
            {"position", QString::number(page.position)},
            {"total", QString::number(page.total)},
            {"offset", QString::number(page.offset)},
            {"bytes", qint64(page.bytes)},
            {"pruned", page.pruned},
            {"baseSaved", page.baseSaved},
            {"truncated", offset + page.entries.size() < page.total},
            {"entries", entries}};
}
QJsonObject executeHistory(Document &doc, const QJsonObject &request) {
    const QStringList keys{"apiVersion", "documentId", "expectedRevision", "position"};
    if (request.size() != keys.size())
        throw std::runtime_error("Invalid history navigation fields");
    for (auto it = request.begin(); it != request.end(); ++it)
        if (!keys.contains(it.key()))
            throw std::runtime_error("Unknown history navigation field");
    if (request["apiVersion"] != 1 ||
        request["documentId"] != QString::fromStdString(doc.identity()))
        throw std::runtime_error("History navigation targets a different document or API version");
    if (request["expectedRevision"] != QString::number(doc.revision()))
        throw std::runtime_error("STALE_REVISION");
    bool ok = false;
    const auto position = request["position"].toString().toULongLong(&ok);
    if (!ok || QString::number(position) != request["position"].toString())
        throw std::runtime_error("Invalid history position");
    const auto before = doc.revision();
    doc.navigateHistory(position, before);
    auto result = describeHistory(doc, position > 100 ? position - 100 : 0);
    result["status"] = before == doc.revision() ? "unchanged" : "committed";
    result["steps"] = QString::number(doc.revision() - before);
    return result;
}
} // namespace sketchy
