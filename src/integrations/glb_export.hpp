#pragma once
#include "core/document_limits.hpp"
#include "integrations/render_snapshot.hpp"
#include <QByteArray>
#include <QString>
#include <cstddef>
namespace sketchy {
struct GlbExport {
    QByteArray glb;
    QJsonObject manifest;
    QByteArray environment;
};
// The manifest.json bound (R099.f, ADR 0032, R112): the largest manifest an export writes (indented,
// as published), that the Blender handoff reads back, and that the Blender worker loads. The
// manifest has one record per visible face, about 306 bytes each, so a 249,218-triangle terrain
// writes 75.4 MB and a 20,000-body capture 29.1 MB. R112 raised the bound from 128 MiB to 192 MiB:
// an export of 600,000 visible modelling faces writes a 175.2 MiB manifest (measured, R112).
// src/integrations/blender_worker.py states the same number and must change with it.
inline constexpr qsizetype glbManifestByteLimit = 192LL * 1024 * 1024 * DocumentLimits::scale;
// R112 (1.8.0, owner approval 2026-11-06): the visible faces of modelling bodies one GLB export
// writes, counted across placements (ADR 0032, R112). A terrain tile's faces are counted against
// terrainTriangleLimit instead (ADR 0168 decision 3), so a terrain does not use this budget. The
// value equals the materialized face limit, so a valid document with everything visible is
// exported in full; the cap stays as the export's own bound.
inline constexpr std::size_t glbVisibleFaceLimit = 600000 * DocumentLimits::scale;
// `visibleFaceBudget` is the cap the export enforces; callers use the default. A test passes a
// smaller one to reach the refusal boundary without a 600,000-face document.
GlbExport exportGlb(const RenderSnapshot &snapshot,
                    std::size_t visibleFaceBudget = glbVisibleFaceLimit);
// Publishes only into a new explicit directory. The hash-bearing manifest is
// written last; consumers verify it before accepting the scene as complete.
void writeGlbExport(const GlbExport &scene, const QString &directory);
} // namespace sketchy
