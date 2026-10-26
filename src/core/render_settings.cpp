#include "core/render_settings.hpp"
#include <stdexcept>
namespace sketchy {
void validateRenderSettings(const RenderSettings &s) {
    if (s.engine != RenderEngine::Cycles && s.engine != RenderEngine::Eevee)
        throw std::runtime_error("Unsupported render engine");
    if (!(s.width >= 64 && s.width <= 4096 && s.height >= 64 && s.height <= 4096 &&
          s.samples >= 1 && s.samples <= 1024 && s.seed >= 0 && s.seed <= 1000000))
        throw std::runtime_error("Render resolution, samples or seed exceed bounds");
}
} // namespace sketchy
