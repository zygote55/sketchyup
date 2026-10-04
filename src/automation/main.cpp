#include "automation/commands.hpp"
#include "io/document_io.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"capabilities", "Print supported local commands"});
    parser.addOption({"describe-command", "Print a command parameter schema", "name"});
    parser.addOption({"query", "Run a read-only document or geometry query", "name"});
    parser.addOption({"query-file", "Run a read-only query object from a JSON file", "path"});
    parser.addOption({"context", "Body context for geometry.inspect", "id"});
    parser.addOption({"input", "Open a model", "path"});
    parser.addOption({"output", "Save the resulting model", "path"});
    parser.addOption(
        {"preview", "Validate a script and return prospective geometry without committing"});
    parser.addOption({"script", "Read a command array from a local JSON file", "path"});
    parser.process(app);
    try {
        if (parser.isSet("preview") && (!parser.isSet("script") || parser.isSet("output")))
            throw std::runtime_error(
                "Preview requires --script and cannot be combined with --output");
        if (parser.isSet("capabilities")) {
            std::cout << QJsonDocument(sketchy::capabilities()).toJson().toStdString();
            return 0;
        }
        if (parser.isSet("describe-command")) {
            std::cout << QJsonDocument(
                             sketchy::commandDescription(parser.value("describe-command")))
                             .toJson()
                             .toStdString();
            return 0;
        }
        auto doc = parser.isSet("input") ? sketchy::loadDocument(parser.value("input"))
                                         : sketchy::Document();
        QJsonObject result;
        if (int(parser.isSet("query")) + int(parser.isSet("query-file")) +
                int(parser.isSet("script")) >
            1)
            throw std::runtime_error("Choose either a query or an editing script");
        if (parser.isSet("query-file")) {
            QFile file(parser.value("query-file"));
            if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
                throw std::runtime_error("Cannot read query or query exceeds 1 MiB");
            QJsonParseError error;
            const auto json = QJsonDocument::fromJson(file.read(1024 * 1024 + 1), &error);
            if (error.error != QJsonParseError::NoError || !json.isObject())
                throw std::runtime_error("Query file must contain one JSON object");
            result = sketchy::executeQuery(doc, json.object());
        } else if (parser.isSet("query")) {
            QJsonObject query{{"query", parser.value("query")}};
            if (parser.isSet("context"))
                query["body"] = parser.value("context");
            result = sketchy::executeQuery(doc, query);
        } else if (parser.isSet("script")) {
            QFile file(parser.value("script"));
            if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
                throw std::runtime_error("Cannot read recipe or recipe exceeds 1 MiB");
            QJsonParseError error;
            auto json = QJsonDocument::fromJson(file.read(1024 * 1024 + 1), &error);
            if (error.error != QJsonParseError::NoError || !json.isArray())
                throw std::runtime_error("Recipe must be a JSON command array");
            const QJsonObject batch{{"apiVersion", 1},
                                    {"documentId", QString::fromStdString(doc.identity())},
                                    {"expectedRevision", QString::number(doc.revision())},
                                    {"commands", json.array()}};
            result = parser.isSet("preview") ? sketchy::previewBatch(doc, batch)
                                             : sketchy::executeBatch(doc, batch);
        } else
            result = sketchy::describe(doc);
        if (parser.isSet("output")) {
            sketchy::saveDocument(doc, parser.value("output"));
            result["saved"] = parser.value("output");
        }
        std::cout << QJsonDocument(result).toJson().toStdString();
        return 0;
    } catch (const sketchy::PlanarError &e) {
        std::cerr << QJsonDocument(QJsonObject{{"status", "failed"},
                                               {"code", QString::fromStdString(e.code())},
                                               {"error", e.what()}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return 1;
    } catch (const std::exception &e) {
        std::cerr << QJsonDocument(QJsonObject{{"status", "failed"}, {"error", e.what()}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return 1;
    }
}
