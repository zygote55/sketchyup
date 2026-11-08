#pragma once
#include <QByteArray>
#include <cstddef>
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
// The parser's budgets (R115, review finding 5), for the tests: each one admits what the app's own GLB export
// writes for a model inside the unique limit (see gltf_package.cpp).
struct GltfParseBudgets {
    std::size_t file, json, values, accessorCount, accessorElements, primitiveElements, primitives, records;
};
GltfParseBudgets gltfParseBudgets();
} // namespace sketchy
