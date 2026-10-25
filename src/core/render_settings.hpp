#pragma once
namespace sketchy {
// The still-render setup (ADR 0173 §3). It lives in core so that a render preset record
// can carry it; the render snapshot and the render panel use the same values.
enum class RenderEngine { Cycles, Eevee };
struct RenderSettings {
    int width{1024}, height{768}, samples{32}, seed{};
    RenderEngine engine{RenderEngine::Cycles};
    bool operator==(const RenderSettings &) const = default;
};
// Width and height 64 to 4096, samples 1 to 1024, seed 0 to 1,000,000, Cycles or Eevee.
void validateRenderSettings(const RenderSettings &settings);
} // namespace sketchy
