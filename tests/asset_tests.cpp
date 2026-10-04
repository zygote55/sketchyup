#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/materials.hpp"
#include <iostream>
#include <type_traits>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation, const char *message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, message);
}
int main() {
    static_assert(!std::is_assignable_v<AssetPayload &, AssetPayload>);
    try {
        std::vector<std::uint8_t> source{0, 1, 2, 3};
        const auto payload = std::make_shared<AssetPayload>(source);
        source[0] = 99;
        check(payload->bytes()[0] == 0, "Payload construction severs mutable input aliases");
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto asset = createAsset(doc, "Texture", "image/png", payload);
        const auto missing = createAsset(doc, "Missing image", "image/png");
        const auto material = createMaterial(doc, "Asset material", {1, 1, 1}, 1, asset);
        assignMaterial(doc, body, {}, material, true, false);
        const auto bodies = doc.bodies();
        const auto old = doc.assets().at(asset);
        const auto replacement = std::make_shared<AssetPayload>(std::vector<std::uint8_t>{4, 5, 6});
        replaceAsset(doc, asset, replacement);
        check(doc.bodies() == bodies && doc.assets().at(asset)->payload == replacement,
              "Asset replacement changes no geometry or material identity");
        doc.undo();
        check(doc.assets().at(asset) == old,
              "Asset replacement undo restores exact immutable record");
        doc.redo();
        replaceAsset(doc, missing, replacement);
        check(bool(doc.assets().at(missing)->payload),
              "Explicit missing record can be resolved in place");
        doc.undo();
        check(!doc.assets().at(missing)->payload, "Undo restores explicit missing status");
        const auto stable = doc.saveStamp();
        rejects([&] { eraseAsset(doc, asset); }, "Used asset cannot be deleted");
        rejects([&] { createMaterial(doc, "Dangling", {1, 1, 1}, 1, 999); },
                "Unknown asset references reject");
        rejects([&] { createAsset(doc, "Bad type", "image/png/extra", payload); },
                "Malformed media type rejects");
        rejects([&] { createAsset(doc, std::string("nul\0name", 8), "image/png", payload); },
                "Embedded NUL names reject");
        check(doc.isCurrentSnapshot(stable), "Rejected asset operations are atomic");
        const auto component = createComponent(doc, body, "Asset component");
        const auto member = component.movedGeometry.at(body);
        const auto peer = placeComponent(doc, component.definition).instance;
        editComponentDefinition(doc, component.definition, [&](Document &draft) {
            return assignMaterial(draft, member, {}, material, false, true);
        });
        check(doc.materials().at(material)->asset == asset &&
                  doc.bodies().at(doc.instances().at(peer)->members.at(member))->materials.back ==
                      material,
              "Component drafts preserve shared asset references");
        const auto scopeStamp = doc.saveStamp();
        rejects(
            [&] {
                editComponentDefinition(doc, component.definition, [&](Document &draft) {
                    replaceAsset(draft, asset, {});
                    return ChangeReport{};
                });
            },
            "Component geometry scope cannot mutate global assets");
        check(doc.isCurrentSnapshot(scopeStamp), "Rejected shared asset edit is atomic");
        auto mutableRecord = std::make_shared<AssetRecord>(
            AssetRecord{doc.nextAssetId(), "Frozen", "application/octet-stream", payload});
        Edit frozen{"Freeze asset", {}};
        frozen.assets.push_back({mutableRecord->id, nullptr, mutableRecord});
        doc.apply(frozen, doc.revision());
        mutableRecord->name = "Altered";
        check(doc.assets().at(mutableRecord->id)->name == "Frozen",
              "Published asset metadata has no mutable alias");
        Document history;
        const auto retired = createAsset(history, "First", "image/png");
        history.undo();
        const auto fresh = createAsset(history, "Second", "image/png");
        check(fresh > retired, "Undo never reuses asset IDs");
        replaceAsset(history, fresh, payload);
        const auto amend = history.amendmentStamp();
        history.amendLast(amend, [&](Document &draft) { replaceAsset(draft, fresh, replacement); });
        history.undo();
        check(!history.assets().at(fresh)->payload, "Asset amendment retains one undo boundary");
        rejects([&] { AssetPayload empty(std::vector<std::uint8_t>{}); },
                "Empty payload is not a missing record");
        rejects([&] { AssetPayload tooLarge(std::vector<std::uint8_t>(AssetPayload::limit + 1)); },
                "Per-asset byte budget enforced before copying");
        const auto large =
            std::make_shared<AssetPayload>(std::vector<std::uint8_t>(AssetPayload::limit));
        Document bounded;
        for (int i = 0; i < 4; ++i)
            createAsset(bounded, "Large", "application/octet-stream", large);
        const auto boundStamp = bounded.saveStamp();
        rejects([&] { createAsset(bounded, "Excess", "application/octet-stream", payload); },
                "Aggregate asset byte budget enforced");
        check(bounded.isCurrentSnapshot(boundStamp), "Aggregate budget rejection is atomic");
        std::cout
            << "Immutable assets, missing records, references, scope, history and limits passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
