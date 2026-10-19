#pragma once
// D-044 desktop tests: hold a terrain dry run on its worker at the start of triangulation, so a
// test sees the progress of a "slow" run and can cancel it. The hold ends when the run is
// cancelled, when the test releases it, or after 20 s (so a broken cancel fails, not hangs).
#include "app/terrain_run.hpp"
#include <atomic>
#include <chrono>
#include <thread>

namespace sketchy::test {
struct TerrainRunHold {
    std::atomic<bool> release{false};
    std::atomic<int> holding{0};   // runs held now
    std::atomic<int> held{0};      // runs held so far
    std::atomic<int> cancelled{0}; // held runs that ended by their cancel
    TerrainRunHold() {
        terrainRunTestHook() = [this](TerrainRunControl &control, TerrainStage stage,
                                      double fraction) {
            if (stage != TerrainStage::Triangulate || fraction != 0)
                return;
            ++held;
            ++holding;
            const auto start = std::chrono::steady_clock::now();
            while (!control.cancel.load() && !release.load() &&
                   std::chrono::steady_clock::now() - start < std::chrono::seconds(20))
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (control.cancel.load())
                ++cancelled;
            --holding;
        };
    }
    // Only once no run is held (the destructor of the dialog has waited for its worker).
    ~TerrainRunHold() { terrainRunTestHook() = {}; }
    TerrainRunHold(const TerrainRunHold &) = delete;
    TerrainRunHold &operator=(const TerrainRunHold &) = delete;
};
} // namespace sketchy::test
