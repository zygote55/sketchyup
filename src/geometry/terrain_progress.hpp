#pragma once
// D-044 (ADR 0168 section 6, "Parse and triangulate: <= 3 s, with progress and cancel"): stage
// progress and cancellation for a terrain run (parse, prepare, triangulate, tile). Standard
// library only, header-only: the Qt-free triangulation adapter and the Qt importers share it.
//
// One TerrainProgress belongs to one run on one thread. The run calls `at` at loop and phase
// boundaries; `at` reports forward movement through `report` and, once `canceled` has returned
// true, throws TerrainCancelled (and keeps throwing: cancellation is latched). The callbacks
// run on the run's thread, so a UI hands them atomics, not widgets. Without a TerrainProgress
// (nullptr everywhere it is accepted) nothing is reported or checked, and the results are the
// same bits as with one: checkpoints only read, they never change what is computed.
#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>

namespace sketchy {

// R096.a: Replay runs after Tile in Regenerate, Terrain settings and the edit-list commands
// (ADR 0169 §2); an import never replays.
enum class TerrainStage : std::uint8_t { Parse, Prepare, Triangulate, Tile, Replay };
inline constexpr int terrainStageCount = 5;
inline const char *terrainStageName(TerrainStage stage) {
    switch (stage) {
    case TerrainStage::Parse:
        return "parse";
    case TerrainStage::Prepare:
        return "prepare";
    case TerrainStage::Triangulate:
        return "triangulate";
    case TerrainStage::Tile:
        return "tile";
    case TerrainStage::Replay:
        return "replay";
    }
    return "tile";
}
// The whole run's fraction for a stage's fraction. The weights follow the measured 250,000-
// triangle runs (R095.f): parse 0-20 %, prepare 20-30 %, triangulate 30-95 %, tile 95-100 %.
inline double terrainOverallProgress(TerrainStage stage, double fraction) {
    constexpr double start[terrainStageCount + 1]{0, .20, .30, .95, 1, 1};
    const auto i = std::size_t(stage);
    return start[i] + (start[i + 1] - start[i]) * std::clamp(fraction, 0.0, 1.0);
}

// R096.a: the same for a replaying run (Regenerate, Terrain settings, Reset edits, Remove
// change): triangulate 0-40 %, tile 40-45 %, replay 45-100 %.
inline double terrainReplayProgress(TerrainStage stage, double fraction) {
    constexpr double start[terrainStageCount + 1]{0, 0, 0, .40, .45, 1};
    const auto i = std::size_t(stage);
    return start[i] + (start[i + 1] - start[i]) * std::clamp(fraction, 0.0, 1.0);
}

// Thrown out of a run when cancellation was requested. triangulateTerrain, runTerrainImport
// and runTerrainEdgeRequest return a cancelled result instead; the parsers and
// prepareTerrainSources let it propagate. A cancelled run leaves nothing behind.
class TerrainCancelled final : public std::exception {
  public:
    const char *what() const noexcept override { return "The terrain run was cancelled"; }
};

class TerrainProgress {
  public:
    using Canceled = std::function<bool()>;
    // The stage and the stage's fraction (0..1). Called only when either moved forward by at
    // least `reportStep`, when a stage starts and when a stage reaches 1.
    using Report = std::function<void(TerrainStage stage, double fraction)>;
    static constexpr double reportStep = 1.0 / 256;

    TerrainProgress() = default;
    explicit TerrainProgress(Canceled canceled, Report report = {})
        : canceled_(std::move(canceled)), report_(std::move(report)) {}

    // A checkpoint. Progress never goes back: an earlier stage or a smaller fraction is not
    // reported (a parser may run twice), but cancellation is still checked.
    void at(TerrainStage stage, double fraction) {
        fraction = std::clamp(fraction, 0.0, 1.0);
        const int s = int(stage);
        if (s > stage_ || (s == stage_ && fraction > fraction_)) {
            const bool due = s > stage_ || fraction - reported_ >= reportStep || fraction == 1.0;
            stage_ = s;
            fraction_ = fraction;
            if (due) {
                reported_ = fraction;
                if (report_)
                    report_(stage, fraction);
            }
        }
        check();
    }
    // Cancellation only.
    void check() {
        if (!cancelled_ && canceled_ && canceled_())
            cancelled_ = true;
        if (cancelled_)
            throw TerrainCancelled();
    }
    // Whether cancellation was requested (latched; polls `canceled` once more). Never throws.
    bool cancelled() {
        if (!cancelled_ && canceled_ && canceled_())
            cancelled_ = true;
        return cancelled_;
    }

  private:
    Canceled canceled_;
    Report report_;
    int stage_{-1};
    double fraction_{-1}, reported_{-1};
    bool cancelled_{};
};

// The nullable form the pipeline uses.
inline void terrainCheckpoint(TerrainProgress *progress, TerrainStage stage, double fraction) {
    if (progress)
        progress->at(stage, fraction);
}

} // namespace sketchy
