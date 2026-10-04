#include "automation/commands.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected history API rejection");
}
QJsonObject batch(const Document &doc, QJsonObject history = {}) {
    QJsonObject request{{"apiVersion", 1},
                        {"documentId", QString::fromStdString(doc.identity())},
                        {"expectedRevision", QString::number(doc.revision())},
                        {"commands", QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                            {"body", "1"},
                                                            {"delta", QJsonArray{1, 0, 0}}}}}};
    if (!history.isEmpty())
        request["history"] = history;
    return request;
}
QJsonObject navigation(const Document &doc, size_t position) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"position", QString::number(position)}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        doc.markSaved();
        const auto initial = doc.bodies();
        executeBatch(doc, batch(doc));
        check(doc.history().entries.back().label ==
                  commandDescription("geometry.translate")["label"].toString().toStdString(),
              "Default batch label is human-facing command label");
        const QJsonObject metadata{{"label", "Place window assemblies"},
                                   {"taskId", "request-1"},
                                   {"request", "Move both windows one meter"},
                                   {"assistant", true}};
        auto request = batch(doc, metadata);
        const auto before = encodeContainer(doc);
        auto preview = previewBatch(doc, request);
        check(preview["status"] == "preview" && encodeContainer(doc) == before &&
                  doc.history().total == 2,
              "Preview adds no real history or task metadata");
        executeBatch(doc, request);
        const auto final = doc.bodies();
        auto query =
            executeQuery(doc, {{"query", "history.describe"}, {"offset", "2"}, {"limit", "1"}});
        const auto entry = query["entries"].toArray()[0].toObject();
        check(entry["label"] == metadata["label"] && entry["request"] == metadata["request"] &&
                  entry["taskId"] == "request-1" && entry["assistant"] == true,
              "History query preserves task metadata and request text");
        auto nav = navigation(doc, 1);
        auto result = executeHistory(doc, nav);
        check(result["status"] == "committed" && result["steps"] == "2" &&
                  doc.bodies() == initial && !doc.dirty() && doc.history().total == 3,
              "Public navigation shares existing history and saved-state semantics");
        rejects([&] { executeHistory(doc, nav); });
        executeHistory(doc, navigation(doc, 3));
        check(doc.bodies() == final, "Forward navigation restores task result");
        auto wrong = navigation(doc, 0);
        wrong["documentId"] = "other";
        rejects([&] { executeHistory(doc, wrong); });
        wrong = navigation(doc, 0);
        wrong["position"] = 0;
        rejects([&] { executeHistory(doc, wrong); });
        wrong = navigation(doc, 0);
        wrong["position"] = "00";
        rejects([&] { executeHistory(doc, wrong); });
        wrong = navigation(doc, 0);
        wrong["extra"] = true;
        rejects([&] { executeHistory(doc, wrong); });
        rejects([&] { executeQuery(doc, {{"query", "history.describe"}, {"limit", "1001"}}); });
        for (const auto &bad :
             {QJsonObject{{"label", ""}}, QJsonObject{{"assistant", true}},
              QJsonObject{{"assistant", "true"}}, QJsonObject{{"request", QString(4097, 'x')}},
              QJsonObject{{"taskId", QString(129, 'x')}}, QJsonObject{{"other", "ignored"}}}) {
            const auto state = encodeContainer(doc);
            const auto count = doc.history().total;
            rejects([&] { executeBatch(doc, batch(doc, bad)); });
            check(encodeContainer(doc) == state && doc.history().total == count,
                  "Invalid metadata publishes no geometry or history");
        }
        auto copy = decodeContainer(encodeContainer(doc));
        check(copy.history().total == 0 && !copy.dirty() && doc.history().total == 3,
              "Native save/reopen retains model but does not serialize task prompts or undo stack");
#ifdef CLI_PATH
        QTemporaryDir files;
        saveDocument(copy, files.filePath("source.sketchyup"));
        QFile recipe(files.filePath("move.json"));
        check(recipe.open(QIODevice::WriteOnly), "Write history CLI recipe");
        recipe.write(QJsonDocument(batch(copy)["commands"].toArray()).toJson());
        recipe.close();
        QProcess cli;
        cli.start(CLI_PATH,
                  {"--input", files.filePath("source.sketchyup"), "--script", recipe.fileName(),
                   "--history-position", "0", "--output", files.filePath("undone.sketchyup")});
        check(cli.waitForFinished(10000) && cli.exitCode() == 0,
              "CLI navigation runs after recipe");
        const auto response = QJsonDocument::fromJson(cli.readAllStandardOutput()).object();
        const auto reopened = loadDocument(files.filePath("undone.sketchyup"));
        check(response["historyNavigation"].toObject()["steps"] == "1" &&
                  *reopened.bodies().at(1) == *copy.bodies().at(1),
              "CLI publishes the state after history navigation, not stale recipe output");
        cli.start(CLI_PATH,
                  {"--input", files.filePath("source.sketchyup"), "--query", "history.describe"});
        check(cli.waitForFinished(10000) && cli.exitCode() == 0 &&
                  QJsonDocument::fromJson(cli.readAllStandardOutput()).object()["total"] == "0",
              "Headless history query reports reopened checkpoint without invented history");
#endif
        std::cout << "History API paging, labels, task requests, preview isolation, atomic "
                     "navigation, stale guards and transient metadata passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
