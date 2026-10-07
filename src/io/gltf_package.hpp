#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <memory>
struct cgltf_data;
namespace sketchy {
// Owns parsed glTF plus immutable buffer/image bytes. No network or arbitrary
// filesystem loader is installed. Relative sidecars must remain inside the folder.
class GltfPackage {
  public:
    static GltfPackage read(const QString &path);
    ~GltfPackage();
    GltfPackage(GltfPackage &&) noexcept;
    GltfPackage &operator=(GltfPackage &&) noexcept;
    const cgltf_data &data() const;
    const QByteArray &image(size_t index) const;
    QJsonObject report() const;

  private:
    struct Impl;
    explicit GltfPackage(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
