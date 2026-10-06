#pragma once
#include "automation/inspection.hpp"
#include "automation/model_recipes.hpp"
#include <QJsonDocument>
namespace sketchy {
// Bounded ordinary-command expansion on the outer batch's private document.
struct RecipeBuilder {
    [[noreturn]] static void fail(const char *message) {
        throw InspectionError("INVALID_RECIPE", message);
    }
    Document &doc;
    ModelRecipeResult result;
    QJsonArray commands;
    explicit RecipeBuilder(Document &doc) : doc(doc) {}
    QJsonObject runBatch(const QJsonArray &batch) {
        for (const auto &value : batch) {
            if (value.toObject().value("command").toString().startsWith("assembly."))
                fail("Recipes cannot expand recursively");
            commands.append(value);
        }
        if (commands.size() > 100 ||
            QJsonDocument(commands).toJson(QJsonDocument::Compact).size() > 64 * 1024)
            fail("Recipe expansion exceeds bounded command budget");
        auto receipt = executeBatch(doc,
                                    {{"apiVersion", 1},
                                     {"documentId", QString::fromStdString(doc.identity())},
                                     {"expectedRevision", QString::number(doc.revision())},
                                     {"commands", batch}},
                                    BatchResponse::Changes);
        result.steps.append(receipt);
        doc = doc.readSnapshot(); // Do not retain scratch history between expansion steps.
        return receipt;
    }
    QJsonObject run(QJsonObject command) { return runBatch({command}); }
    Id created(QJsonObject command) {
        const auto reply = run(command);
        const auto ids = reply["created"].toArray();
        if (ids.size() != 1)
            fail("Expected one created recipe body");
        return ids[0].toString().toULongLong();
    }
    void properties(Id body, QJsonObject values) {
        run({{"command", "entity.properties"},
             {"body", QString::number(body)},
             {"values", values}});
    }
    Id material(QString name, QJsonArray color, double opacity = 1) {
        return run({{"command", "material.create"},
                    {"name", name},
                    {"color", color},
                    {"opacity", opacity}})["createdMaterials"]
            .toArray()[0]
            .toString()
            .toULongLong();
    }
    void paint(Id body, Id material) {
        run({{"command", "material.assign"},
             {"body", QString::number(body)},
             {"material", QString::number(material)},
             {"side", "both"}});
    }
    ModelRecipeResult finish(QJsonObject report) {
        report["expandedCommands"] = commands;
        result.report = report;
        if (QJsonDocument(result.report).toJson(QJsonDocument::Compact).size() > 128 * 1024)
            fail("Recipe report exceeds bound");
        return std::move(result);
    }
};
} // namespace sketchy
