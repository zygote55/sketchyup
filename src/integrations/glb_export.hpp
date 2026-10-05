#pragma once
#include "integrations/render_snapshot.hpp"
#include <QByteArray>
#include <QString>
namespace sketchy {
struct GlbExport {
    QByteArray glb;
    QJsonObject manifest;
};
GlbExport exportGlb(const RenderSnapshot &snapshot);
// Publishes only into a new explicit directory. The hash-bearing manifest is
// written last; consumers verify it before accepting the scene as complete.
void writeGlbExport(const GlbExport &scene, const QString &directory);
} // namespace sketchy
