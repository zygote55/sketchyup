#pragma once
#include "integrations/render_snapshot.hpp"
#include <QByteArray>
#include <QString>
namespace sketchy {
struct GlbExport {
    QByteArray glb;
    QJsonObject manifest;
    QByteArray environment;
};
// The manifest.json bound (R099.f, ADR 0032): the largest manifest an export writes (indented,
// as published), that the Blender handoff reads back, and that the Blender worker loads. The
// manifest has one record per visible face, so a 249,218-triangle terrain writes 75.4 MB and a
// 20,000-body capture 29.1 MB. 128 MiB covers the largest capture the limits admit (100,000
// modelling faces and a 250,000-triangle terrain); the 10,000-placement capture writes 119 MB.
inline constexpr qsizetype glbManifestByteLimit = 128LL * 1024 * 1024;
GlbExport exportGlb(const RenderSnapshot &snapshot);
// Publishes only into a new explicit directory. The hash-bearing manifest is
// written last; consumers verify it before accepting the scene as complete.
void writeGlbExport(const GlbExport &scene, const QString &directory);
} // namespace sketchy
