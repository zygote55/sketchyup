// D-039 performance check: the per-edit cost of Document::apply with 10,000
// placements that show a component parameter variant, against the same edit with
// 10,000 default placements. Before the fix, checkUniqueGeometry walked every
// variant placement's member records on each apply (O(placements x members)), so
// the variant document cost 2-3x the default one per move; with the placementUses
// counts in ComponentVariantIndex it costs the same (O(distinct variants)).
//
// The assertion is a ratio of two runs in one process, not an absolute time, so it
// holds across machines and build types. Each side is the best (lowest) median of
// several interleaved rounds, which discards load spikes on a shared host.
// Pass --report to print every round.
#include "core/component_parameters.hpp"
#include "core/component_regeneration.hpp"
#include "core/components.hpp"
#include "core/copy_array.hpp"
#include "core/transform_selection.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>
using namespace sketchy;
namespace {
constexpr int placements = 10000;
constexpr int rounds = 9;
constexpr int editsPerRound = 120;
// Measured on the fixed code: variant/default 1.0-1.3 (best-of-rounds, Debug and
// Release). The old code measured 2-3. 1.8 sits between them with margin on both
// sides: well above the fixed code's noise, below the regression's lower end.
constexpr double budgetRatio = 1.8;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Fixture {
    Document doc;
    Id edited{};
};

// The R094.g `variant` family: a 26-sided prism component, placed by copy array
// and row copies. With `variant`, every placement overrides "width" so each shows
// the one non-default variant (2.001 m); otherwise they are default placements.
Fixture build(bool variant) {
    Fixture f;
    Document &doc = f.doc;
    std::vector<Vec3> loop;
    for (int i = 0; i < 26; ++i) {
        const auto angle = 2 * std::numbers::pi * i / 26;
        loop.push_back({std::cos(angle), std::sin(angle), 0});
    }
    const auto body = doc.addFace({loop});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
    const auto component = createComponent(doc, body, "26-sided benchmark prism");
    const int columns = int(std::ceil(std::sqrt(double(placements))));
    if (variant) {
        ComponentParameterSpec width;
        width.name = "width";
        width.label = "Width";
        width.expression = "2 m";
        defineComponentParameter(doc, component.definition, width);
        ComponentDriverSpec stretch;
        stretch.kind = ComponentDriverKind::Stretch;
        stretch.member = component.movedGeometry.at(body);
        stretch.expression = "width";
        stretch.axis = ComponentAxis::X;
        stretch.at = 0;
        driveComponent(doc, component.definition, 0, stretch);
        overrideComponentParameters(
            doc, component.instance,
            {{"width", ParameterQuantity{quantizeParameterLength(2.001), {1, 0}}}});
    }
    std::vector<Id> row{component.instance};
    CopyArray array;
    array.delta = {4, 0, 0};
    array.copies = unsigned(columns - 1);
    for (const auto &copy : copyArraySelected(doc, {{component.instance}}, array).instances)
        row.push_back(copy.copies.at(component.instance));
    size_t count = row.size();
    for (int r = 1; count < size_t(placements); ++r) {
        const int n = int(std::min<size_t>(columns, placements - count));
        TransformTargets targets;
        for (int j = 0; j < n; ++j)
            targets.insert({row[size_t(j)]});
        transformSelected(doc, targets, Transform::translation({0, 4.0 * r, 0}), {},
                          TransformSpace::World, true);
        count += size_t(n);
    }
    check(doc.instances().size() == size_t(placements), "Fixture has 10,000 placements");
    check(variant ? doc.componentVariants().variants.size() == 1
                  : doc.componentVariants().variants.empty(),
          "Fixture shows one variant, or none for the default document");
    f.edited = component.instance;
    return f;
}

// Median milliseconds of single-placement move edits through Document::transform
// (one Document::apply each), the path the real_model_benchmark edit loop uses.
double round(Fixture &f, int &step) {
    std::vector<double> times;
    for (int i = 0; i < editsPerRound; ++i) {
        const auto start = std::chrono::steady_clock::now();
        f.doc.transform(f.edited, Transform::translation({0, 0, .01 * ++step}));
        times.push_back(std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start)
                            .count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}
} // namespace

int main(int argc, char **argv) {
    try {
        const bool report = argc > 1 && std::strcmp(argv[1], "--report") == 0;
        auto plain = build(false);
        auto varied = build(true);
        int plainStep = 0, variedStep = 0;
        round(plain, plainStep); // warm-up
        round(varied, variedStep);
        double bestPlain = 1e30, bestVaried = 1e30;
        for (int i = 0; i < rounds; ++i) {
            const double p = round(plain, plainStep), v = round(varied, variedStep);
            bestPlain = std::min(bestPlain, p);
            bestVaried = std::min(bestVaried, v);
            if (report)
                std::cout << "round " << i << ": default " << p << " ms, variant " << v
                          << " ms\n";
        }
        const double ratio = bestVaried / bestPlain;
        std::cout << "10,000 placements, per move edit (best median of " << rounds
                  << "): default " << bestPlain << " ms, variant " << bestVaried
                  << " ms, ratio " << ratio << " (budget " << budgetRatio << ")\n";
        if (ratio > budgetRatio) {
            std::cerr << "FAIL: variant edits cost " << ratio << "x default edits (D-039)\n";
            return 1;
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
