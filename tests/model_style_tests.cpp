#include "core/components.hpp"
#include <iostream>
#include <limits>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected style rejection");
}
} // namespace
int main() {
    try {
        ModelStyle defaults;
        defaults.validate();
        for (auto mode :
             {ModelStyleMode::Textured, ModelStyleMode::Shaded, ModelStyleMode::Monochrome,
              ModelStyleMode::Wireframe, ModelStyleMode::XRay})
            check(parseStyleMode(styleModeCode(mode)) == mode, "Stable mode names roundtrip");
        rejects([] { parseStyleMode("XRay"); });
        rejects([] { styleModeCode(static_cast<ModelStyleMode>(99)); });
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        doc.markSaved();
        const auto bodies = doc.bodies();
        const auto saved = doc.saveStamp();
        const auto revision = doc.revision();
        auto style = defaults;
        style.mode = ModelStyleMode::Wireframe;
        style.background = {.1f, .2f, .3f};
        style.profiles = true;
        doc.setStyle(style);
        check(doc.style() == style && doc.bodies() == bodies && doc.dirty() &&
                  doc.revision() == revision + 1 &&
                  doc.history().entries.back().label == "Change model style",
              "Style is one labeled edit without geometry changes");
        const auto snapshot = doc.readSnapshot();
        check(snapshot.style() == style && !snapshot.canUndo(),
              "Read snapshot retains style without history");
        doc.undo();
        check(doc.style() == defaults && !doc.dirty() && doc.isCurrentSnapshot(saved),
              "Undo restores style and saved state");
        check(snapshot.style() == style, "Published snapshot remains immutable");
        doc.redo();
        const auto unchanged = doc.revision();
        doc.setStyle(style);
        check(doc.revision() == unchanged, "Identical style is a no-op");
        for (int variant = 0; variant < 13; ++variant) {
            auto bad = style;
            if (variant == 0)
                bad.mode = static_cast<ModelStyleMode>(-1);
            if (variant == 1)
                bad.background[0] = -.1f;
            if (variant == 2)
                bad.ground[1] = 1.1f;
            if (variant == 3)
                bad.front[2] = std::numeric_limits<float>::quiet_NaN();
            if (variant == 4)
                bad.back[0] = std::numeric_limits<float>::infinity();
            if (variant == 5)
                bad.edge[1] = -1;
            if (variant == 6)
                bad.groundHeight = 1000001;
            if (variant == 7)
                bad.groundHeight = std::numeric_limits<double>::quiet_NaN();
            if (variant == 8)
                bad.profileWidth = .999;
            if (variant == 9)
                bad.profileWidth = 8.001;
            if (variant == 10)
                bad.xrayOpacity = .009;
            if (variant == 11)
                bad.xrayOpacity = .951;
            if (variant == 12)
                bad.xrayOpacity = std::numeric_limits<double>::infinity();
            rejects([&] { doc.setStyle(bad); });
        }
        Edit invalid{"Stale style", {}};
        invalid.style = std::pair{defaults, style};
        rejects([&] { doc.apply(invalid, doc.revision()); });
        invalid.style = std::pair{style, style};
        rejects([&] { doc.apply(invalid, doc.revision()); });
        invalid.style = std::pair{style, defaults};
        rejects([&] { doc.apply(invalid, doc.revision() - 1); });
        check(doc.revision() == unchanged && doc.style() == style && doc.bodies() == bodies,
              "Malformed and stale styles leave authoritative state unchanged");
        auto replacement = style;
        replacement.mode = ModelStyleMode::XRay;
        const auto amendment = doc.amendmentStamp();
        doc.amendLast(amendment, [&](Document &draft) { draft.setStyle(replacement); });
        check(doc.style() == replacement && doc.history().total == 2,
              "Style amendment stays one edit");
        doc.undo();
        check(doc.style() == defaults && !doc.dirty(), "Amended Undo returns to original style");
        doc.redo();
        const auto proposal = doc.prepareEdit([&](Document &draft) { draft.setStyle(style); });
        check(proposal.snapshot().style() == style && doc.style() == replacement,
              "Prepared style is isolated from live document");
        doc.applyPrepared(proposal);
        check(doc.style() == style && doc.bodies() == bodies,
              "Prepared style publishes atomically");
        rejects([&] { doc.applyPrepared(proposal); });
        doc.move(body, {1, 0, 0});
        const auto geometry = doc.amendmentStamp();
        rejects(
            [&] { doc.amendLast(geometry, [&](Document &draft) { draft.setStyle(defaults); }); });
        check(doc.canAmend(geometry) && doc.style() == style,
              "Geometry amendment cannot change style scope");
        auto draft = doc.readSnapshot();
        draft.setStyle(replacement);
        Edit composed{"Composed scene metadata", {}};
        appendSceneMetadataChanges(composed, doc, draft);
        doc.apply(composed, doc.revision());
        check(doc.style() == replacement, "Compound scene publication includes style");
        const auto component = createComponent(doc, body, "Styled component");
        editComponentDefinition(doc, component.definition, [&](Document &candidate) {
            check(candidate.style() == replacement, "Shared edit inherits model style");
            for (const auto &[id, record] : candidate.bodies())
                if (!record->surface.faces.empty()) {
                    candidate.paint(id, {.1f, .2f, .3f});
                    break;
                }
            return ChangeReport{};
        });
        const auto beforeScope = doc.saveStamp();
        rejects([&] {
            editComponentDefinition(doc, component.definition, [&](Document &candidate) {
                candidate.setStyle(defaults);
                return ChangeReport{};
            });
        });
        check(doc.isCurrentSnapshot(beforeScope) && doc.style() == replacement,
              "Shared geometry cannot mutate document presentation");
        auto bad = defaults;
        bad.profileWidth = 0;
        rejects([&] {
            doc.restore(doc.identity(), 1, {}, 0, {}, {}, 1, {}, 1, {}, 1, {}, 1,
                        DisplayUnit::Meters, std::make_shared<const HostedComponents>(), bad);
        });
        check(doc.isCurrentSnapshot(beforeScope),
              "Invalid restored style is rejected before mutation");
        std::cout << "Model style validation, history, snapshots, proposals and scope passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
