#pragma once
// D-044 (ADR 0168 section 6): progress and cancellation of a terrain dry run between a dialog
// on the UI thread and its worker. The dialog owns a shared TerrainRunControl per run and polls
// its atomics; the worker makes the run's TerrainProgress from it. Cancelling sets `cancel`,
// and the worker stops at its next checkpoint (well under 100 ms at 250,000 triangles), so a
// dialog's destructor waits only that long for its worker.
#include "geometry/terrain_progress.hpp"
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

namespace sketchy {

struct TerrainRunControl {
    std::atomic<bool> cancel{false};
    std::atomic<int> stage{-1};   // the TerrainStage last reported; -1 before the first report
    std::atomic<int> permille{0}; // the whole run (terrainOverallProgress), 0 to 1000
};

// Test hook, called on the worker after each reported checkpoint. Tests use it to hold a run
// (until `cancel` is set or they release it) or to slow it down. Set it only while no dry run
// is running; the application never sets it.
using TerrainRunHook = std::function<void(TerrainRunControl &, TerrainStage, double)>;
inline TerrainRunHook &terrainRunTestHook() {
    static TerrainRunHook hook;
    return hook;
}

// The worker's TerrainProgress for `control` (which it must keep alive for the run).
inline TerrainProgress terrainRunProgress(const std::shared_ptr<TerrainRunControl> &control) {
    auto *c = control.get();
    return TerrainProgress([c] { return c->cancel.load(std::memory_order_relaxed); },
                           [c](TerrainStage stage, double fraction) {
                               c->permille.store(
                                   int(1000 * terrainOverallProgress(stage, fraction)),
                                   std::memory_order_relaxed);
                               c->stage.store(int(stage), std::memory_order_relaxed);
                               if (const auto &hook = terrainRunTestHook())
                                   hook(*c, stage, fraction);
                           });
}

// What a run is doing, for a status line: "reading the source", "preparing the sources",
// "triangulating", "tiling", "replaying the edits" (empty before the first report).
inline QString terrainRunStageText(int stage) {
    switch (stage) {
    case int(TerrainStage::Parse):
        return QStringLiteral("reading the source");
    case int(TerrainStage::Prepare):
        return QStringLiteral("preparing the sources");
    case int(TerrainStage::Triangulate):
        return QStringLiteral("triangulating");
    case int(TerrainStage::Tile):
        return QStringLiteral("tiling");
    case int(TerrainStage::Replay):
        return QStringLiteral("replaying the edits");
    default:
        return {};
    }
}

} // namespace sketchy
