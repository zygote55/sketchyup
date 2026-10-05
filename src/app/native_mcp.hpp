#pragma once
#include <QJsonObject>
#include <QObject>
#include <memory>
namespace sketchy {
class Viewport;
QJsonObject nativeMcpCapabilities();
class NativeMcpService : public QObject {
  public:
    NativeMcpService(Viewport &viewport, const QString &socketPath);
    ~NativeMcpService() override;
    bool listening() const;
    int clientCount() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
