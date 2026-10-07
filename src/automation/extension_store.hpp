#pragma once
#include "automation/extension.hpp"
#include <optional>
namespace sketchy {
struct InstalledExtension {
    QString id;
    QByteArray source;
    std::optional<ExtensionManifest> manifest;
    bool enabled{};
    QString error;
};
class ExtensionStore {
  public:
    explicit ExtensionStore(QString directory);
    const std::map<QString, InstalledExtension> &entries() const { return entries_; }
    void install(const QByteArray &manifest);
    void setEnabled(const QString &id, bool enabled);
    void recordFailure(const QString &id, const QString &error);
    void remove(const QString &id);
    const ExtensionManifest &enabledManifest(const QString &id) const;

  private:
    QString directory_;
    QByteArray registryDigest_;
    std::map<QString, InstalledExtension> entries_;
    void commit(std::map<QString, InstalledExtension> replacement);
};
} // namespace sketchy
