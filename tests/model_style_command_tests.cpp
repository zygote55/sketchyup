#include "automation/commands.hpp"
#include "automation/component_scope.hpp"
#include "automation/inspection.hpp"
#include "automation/staging.hpp"
#include "core/assets.hpp"
#include "core/components.hpp"
#include "io/document_io.hpp"
#include "io/model_style_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
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
    throw std::runtime_error("Expected style command rejection");
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject command(const ModelStyle &style) {
    return {{"command", "document.style"}, {"style", encodeModelStyle(style)}};
}
// The batch carries the label the history names the edit with (R100.f1, UX_DESIGN §18.3).
QJsonObject labelled(const Document &doc, QJsonArray commands, const std::string &label) {
    auto request = batch(doc, commands);
    request["history"] = QJsonObject{{"label", QString::fromStdString(label)}};
    return request;
}
// The label of the newest applied edit (history lists the oldest first).
std::string lastLabel(const Document &doc) {
    const auto position = doc.history().position;
    return doc.history(position - 1, 1).entries.front().label;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        doc.markSaved();
        auto style = doc.style();
        style.mode = ModelStyleMode::Monochrome;
        style.profiles = true;
        style.profileWidth = 3.5;
        const auto original = encodeContainer(doc);
        const auto saved = doc.saveStamp();
        const auto history = doc.history().total;
        const auto request = batch(doc, {command(style)});
        previewBatch(doc, request);
        check(encodeContainer(doc) == original, "Command preview leaves live bytes unchanged");
        StagingSession staging;
        const auto result = staging.prepare(doc, request);
        const auto token = result["stageId"].toString();
        const auto proposal = staging.proposal(doc, token);
        check(proposal->snapshot().style() == style && doc.style() == ModelStyle{},
              "Stage retains exact style without mutation");
        const auto changes = staging.changes(doc, token)["changes"].toArray();
        check(changes.size() == 1 && changes[0].toObject()["kind"] == "style",
              "Style-only stage has one explicit change row");
        doc.applyPrepared(*proposal);
        check(doc.style() == style && doc.history().total == history + 1 && doc.dirty(),
              "Staged publication is one style edit");
        check(describe(doc)["style"].toObject() == encodeModelStyle(style),
              "Legacy document query exposes full style");
        const auto inspected =
            inspectDocument(doc, {{"apiVersion", 1},
                                  {"documentId", QString::fromStdString(doc.identity())},
                                  {"expectedRevision", QString::number(doc.revision())},
                                  {"query", "document.describe"}});
        check(inspected["data"].toObject()["style"].toObject() == encodeModelStyle(style),
              "Bounded document query exposes full style");
        doc.undo();
        check(!doc.dirty() && doc.isCurrentSnapshot(saved) && doc.style() == ModelStyle{},
              "Style command Undo returns to saved state");
        doc.redo();
        check(doc.style() == style, "Style command Redo restores all fields");
        rejects([&] { executeBatch(doc, request); });
        rejects([&] { executeBatch(doc, batch(doc, {command(style)})); });
        const auto baseline = encodeContainer(doc);
        auto malformed = encodeModelStyle(style);
        for (auto it = malformed.begin(); it != malformed.end(); ++it) {
            auto bad = malformed;
            bad.remove(it.key());
            rejects([&] {
                executeBatch(
                    doc, batch(doc, {QJsonObject{{"command", "document.style"}, {"style", bad}}}));
            });
            bad = malformed;
            bad[it.key()] = QJsonValue::Null;
            rejects([&] {
                executeBatch(
                    doc, batch(doc, {QJsonObject{{"command", "document.style"}, {"style", bad}}}));
            });
        }
        for (int variant = 0; variant < 7; ++variant) {
            auto bad = malformed;
            if (variant == 0)
                bad["future"] = true;
            if (variant == 1)
                bad["mode"] = "future";
            if (variant == 2)
                bad["profileWidth"] = 9;
            if (variant == 3)
                bad["background"] = QJsonArray{1 + 1e-12, 0, 0};
            if (variant == 4)
                bad["groundHeight"] = 1000001;
            if (variant == 5)
                bad["profiles"] = 1;
            if (variant == 6)
                bad["xrayOpacity"] = 1;
            rejects([&] {
                executeBatch(
                    doc, batch(doc, {QJsonObject{{"command", "document.style"}, {"style", bad}}}));
            });
        }
        auto other = style;
        other.mode = ModelStyleMode::Wireframe;
        rejects([&] {
            executeBatch(doc, batch(doc, {command(other), QJsonObject{{"command", "unknown"}}}));
        });
        check(encodeContainer(doc) == baseline,
              "Malformed and partial-failure batches retain exact live bytes");
        executeBatch(doc,
                     batch(doc, {command(other), QJsonObject{{"command", "geometry.translate"},
                                                             {"body", QString::number(body)},
                                                             {"delta", QJsonArray{1, 0, 0}}}}));
        check(doc.style() == other && doc.history().total == history + 2,
              "Style and geometry compose into one atomic batch");
        doc.undo();
        check(doc.style() == style, "Compound Undo restores style with geometry");
        const auto component = createComponent(doc, body, "Style boundary");
        const auto scoped = encodeContainer(doc);
        rejects([&] {
            executeBatch(doc, batch(doc, {componentScopeCommand(doc, component.instance,
                                                                {command(other)})}));
        });
        check(encodeContainer(doc) == scoped,
              "Shared component scope cannot mutate document style");
        // R100.f1 (UX_DESIGN §18.3, owner choice 3): each edge stroke control is one labelled
        // document.style edit. Each is one history step with its label, and Undo restores the
        // style before it. The chain runs on the document, so each edit starts from the last.
        const auto texture =
            createAsset(doc, "Weave", "image/png",
                        std::make_shared<AssetPayload>(std::vector<std::uint8_t>{1, 2, 3}));
        const auto lost = createAsset(doc, "Lost weave", "image/png");
        auto step = [&](const StyleEdit &edit, const char *expected) {
            const auto before = doc.style();
            const auto total = doc.history().total;
            check(edit.label == expected, "Edit label names the control and its value");
            executeBatch(doc, labelled(doc, {command(edit.style)}, edit.label));
            check(doc.style() == edit.style && doc.history().total == total + 1 &&
                      lastLabel(doc) == expected,
                  "One labelled edit is one history step");
            doc.undo();
            check(doc.style() == before, "Undo restores the style before the edit");
            doc.redo();
            check(doc.style() == edit.style, "Redo restores the labelled edit");
        };
        rejects([&] { setEdgeStrokeWidth(doc.style(), 2.5); });
        rejects([&] { turnOffEdgeStroke(doc.style()); });
        step(turnOnEdgeStroke(doc.style()), "Turn on edge stroke");
        check(doc.style().edgeStroke && doc.style().edgeStroke->width == doc.style().profileWidth &&
                  doc.style().edgeStroke->opacity == 1 && doc.style().edgeStroke->texture == 0 &&
                  doc.style().edgeStroke->dash == EdgeDash::Solid,
              "Turning the stroke on copies the profile width and nothing else");
        rejects([&] { turnOnEdgeStroke(doc.style()); });
        step(setEdgeStrokeWidth(doc.style(), 2.5), "Set edge width 2.5 px");
        rejects([&] { setEdgeStrokeWidth(doc.style(), 9); });
        step(setEdgeStrokeOpacity(doc.style(), .6), "Set edge opacity 60 %");
        step(setEdgeStrokeDash(doc.style(), EdgeDash::Dashed), "Set edge dash Dashed");
        step(setEdgeStrokeDash(doc.style(), EdgeDash::DashDot), "Set edge dash Dash-dot");
        step(setEdgeStrokeTexture(doc.style(), texture, "Weave"), "Set edge texture “Weave”");
        step(setEdgeStrokeTexture(doc.style(), 0, ""), "Clear edge texture");
        step(setEdgeStrokeTexture(doc.style(), lost, "Lost weave"),
             "Set edge texture “Lost weave”");
        try {
            auto bad = doc.style();
            bad.edgeStroke->texture = 999;
            executeBatch(doc, batch(doc, {command(bad)}));
            check(false, "An unknown edge texture is refused");
        } catch (const std::runtime_error &error) {
            check(std::string(error.what()).find("unknown asset") != std::string::npos,
                  "The refusal names the unknown edge texture");
        }
        check(doc.style().edgeStroke->texture == lost, "The refused edit changes nothing");
        const auto onStyle = doc.style();
        const auto total = doc.history().total;
        const auto off = turnOffEdgeStroke(onStyle);
        executeBatch(doc, labelled(doc, {command(off.style)}, off.label));
        check(!doc.style().edgeStroke && doc.history().total == total + 1 &&
                  lastLabel(doc) == "Turn off edge stroke",
              "Turning the stroke off removes the sub-record in one labelled edit");
        check(!encodeModelStyle(doc.style()).contains("edgeStroke"),
              "A style without the stroke is the 14-field record");
        doc.undo();
        check(doc.style() == onStyle, "Undo restores the whole stroke");
        std::cout << "Style command schema, preview, staging, queries, rollback, history and scope "
                     "passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
