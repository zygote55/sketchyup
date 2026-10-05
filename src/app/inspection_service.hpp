#pragma once
#include "automation/inspection_session.hpp"
namespace sketchy {
class Viewport;
QJsonObject desktopInspectionCapabilities();
QByteArray desktopInspectionStamp(const Viewport &viewport);
class DesktopInspection {
  public:
    explicit DesktopInspection(Viewport &viewport) : viewport_(viewport) {}
    QJsonObject execute(const QJsonObject &request);

  private:
    Viewport &viewport_;
    InspectionSession session_;
};
} // namespace sketchy
