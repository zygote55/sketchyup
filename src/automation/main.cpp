#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/session.hpp"
#include "io/document_io.hpp"
#include "io/formline.hpp"
#include "io/recovery.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"session", "Run a persistent bounded JSON-lines automation session"});
    parser.addOption(
        {"new", "Create a new model at the explicit output before starting a session"});
    parser.addOption({"outcomes", "Private durable transaction outcome directory", "directory"});
    parser.addOption(
        {"recover-latest", "Explicitly recover the latest durable transaction in this session"});
    parser.addOption({"session-capabilities", "Print the bounded session and transaction schemas"});
    parser.addOption({"capabilities", "Print supported local commands"});
    parser.addOption({"describe-command", "Print a command parameter schema", "name"});
    parser.addOption({"query", "Run a read-only document or geometry query", "name"});
    parser.addOption({"query-file", "Run a read-only query object from a JSON file", "path"});
    parser.addOption({"inspect", "Run a bounded query using the explicit input document", "name"});
    parser.addOption({"inspect-file", "Run a versioned bounded inspection request", "path"});
    parser.addOption({"context", "Body context for geometry.inspect", "id"});
    parser.addOption({"input", "Open a model", "path"});
    parser.addOption({"import-formline", "Import Formline v1 into a new native model", "path"});
    parser.addOption(
        {"recovery-list", "List verified inactive recovery sessions in a directory", "directory"});
    parser.addOption(
        {"recover", "Open a verified recovery session directory as an unsaved copy", "directory"});
    parser.addOption(
        {"history-position", "Move retained history cursor after input or script", "position"});
    parser.addOption({"output", "Save the resulting model", "path"});
    parser.addOption(
        {"preview", "Validate a script and return prospective geometry without committing"});
    parser.addOption({"script", "Read a command array from a local JSON file", "path"});
    try {
        // Parse without QCommandLineParser's unstructured error exit.
        if (!parser.parse(app.arguments()))
            throw sketchy::InspectionError("INVALID_REQUEST", parser.errorText().toStdString());
        if (parser.isSet("help"))
            parser.showHelp();
        if (parser.isSet("session") || parser.isSet("session-capabilities")) {
            for (const auto *option :
                 {"capabilities", "describe-command", "query", "query-file", "inspect",
                  "inspect-file", "context", "import-formline", "recovery-list", "recover",
                  "history-position", "preview", "script"})
                if (parser.isSet(option))
                    throw sketchy::InspectionError(
                        "INVALID_REQUEST",
                        "Session mode cannot be combined with legacy model operations");
            if (!parser.positionalArguments().isEmpty())
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Unexpected positional arguments");
            if (parser.isSet("session-capabilities")) {
                for (const auto *option :
                     {"session", "input", "output", "new", "outcomes", "recover-latest"})
                    if (parser.isSet(option))
                        throw sketchy::InspectionError(
                            "INVALID_REQUEST", "Session discovery is a standalone operation");
                std::cout << QJsonDocument(sketchy::sessionCapabilities()).toJson().toStdString();
                return 0;
            }
            sketchy::AutomationSession session({parser.value("input"), parser.value("output"),
                                                parser.value("outcomes"), parser.isSet("new"),
                                                parser.isSet("recover-latest")});
            QFile input, output;
            if (!input.open(stdin, QIODevice::ReadOnly) ||
                !output.open(stdout, QIODevice::WriteOnly))
                throw sketchy::InspectionError("INPUT_ERROR", "Cannot open automation streams");
            return sketchy::runAutomationStream(session, input, output);
        }
        for (const auto *option : {"new", "outcomes", "recover-latest"})
            if (parser.isSet(option))
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Session options require --session");
        if (parser.isSet("inspect") || parser.isSet("inspect-file")) {
            if (!parser.isSet("input") || (parser.isSet("inspect") && parser.isSet("inspect-file")))
                throw sketchy::InspectionError("INVALID_REQUEST",
                                               "Choose one inspection mode with --input");
            for (const auto *option : {"capabilities", "describe-command", "query", "query-file",
                                       "context", "import-formline", "recovery-list", "recover",
                                       "history-position", "output", "preview", "script"})
                if (parser.isSet(option))
                    throw sketchy::InspectionError(
                        "INVALID_REQUEST", "Inspection cannot be combined with other operations");
            const auto document = sketchy::loadDocument(parser.value("input"));
            QJsonObject request;
            if (parser.isSet("inspect-file")) {
                QFile file(parser.value("inspect-file"));
                if (!file.open(QIODevice::ReadOnly))
                    throw sketchy::InspectionError("INPUT_ERROR", "Cannot open inspection request");
                const auto bytes = file.read(sketchy::inspectionRequestBytes + 1);
                if (bytes.size() > sketchy::inspectionRequestBytes)
                    throw sketchy::InspectionError("LIMIT_EXCEEDED",
                                                   "Inspection request exceeds 16 KiB");
                QJsonParseError error;
                const auto json = QJsonDocument::fromJson(bytes, &error);
                if (error.error != QJsonParseError::NoError || !json.isObject())
                    throw sketchy::InspectionError("INVALID_REQUEST",
                                                   "Expected one JSON request object");
                request = json.object();
            } else {
                request = {{"apiVersion", 1},
                           {"documentId", QString::fromStdString(document.identity())},
                           {"expectedRevision", QString::number(document.revision())},
                           {"query", parser.value("inspect")}};
            }
            std::cout << QJsonDocument(sketchy::inspectDocument(document, request))
                             .toJson(QJsonDocument::Compact)
                             .toStdString()
                      << '\n';
            return 0;
        }
        if (parser.isSet("history-position") &&
            (parser.isSet("preview") || parser.isSet("query") || parser.isSet("query-file")))
            throw std::runtime_error(
                "History navigation cannot be combined with a query or preview");
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
        if (parser.isSet("recovery-list")) {
            for (const auto &option : {"input", "import-formline", "recover", "output", "script",
                                       "query", "query-file", "history-position"})
                if (parser.isSet(option))
                    throw std::runtime_error(
                        "--recovery-list cannot be combined with model operations");
            QJsonArray sessions;
            for (const auto &session : sketchy::listRecoveries(parser.value("recovery-list")))
                sessions.append(sketchy::describeRecovery(session));
            std::cout
                << QJsonDocument(QJsonObject{{"recoveries", sessions}}).toJson().toStdString();
            return 0;
        }
        if (int(parser.isSet("input")) + int(parser.isSet("import-formline")) +
                int(parser.isSet("recover")) >
            1)
            throw std::runtime_error("Choose --input, --import-formline or --recover");
        if (parser.isSet("import-formline") && parser.isSet("output") &&
            QFileInfo(parser.value("import-formline")).exists() &&
            QFileInfo(parser.value("import-formline")).canonicalFilePath() ==
                QFileInfo(parser.value("output")).canonicalFilePath())
            throw std::runtime_error("Import output must not replace the Formline source");
        QJsonObject importReport, recoveryReport;
        auto doc = parser.isSet("input") ? sketchy::loadDocument(parser.value("input"))
                                         : sketchy::Document();
        if (parser.isSet("import-formline")) {
            auto imported = sketchy::loadFormline(parser.value("import-formline"));
            doc = std::move(imported.document);
            importReport = std::move(imported.report);
        }
        if (parser.isSet("recover")) {
            const QFileInfo session(QDir(parser.value("recover")).absolutePath());
            auto recovered = sketchy::readRecovery(session.absolutePath(), session.fileName());
            if (!recovered.verified || !recovered.document)
                throw std::runtime_error(("Cannot recover: " + recovered.issue).toStdString());
            if (parser.isSet("output")) {
                const QFileInfo output(parser.value("output"));
                const auto recoveryPath = session.canonicalFilePath();
                const auto parent = output.absoluteDir().canonicalPath();
                const auto target = output.canonicalFilePath();
                if (parent == recoveryPath || parent.startsWith(recoveryPath + "/") ||
                    target.startsWith(recoveryPath + "/"))
                    throw std::runtime_error(
                        "Recovery output must be outside the recovery session");
            }
            if (parser.isSet("output") && !recovered.info.sourcePath.isEmpty()) {
                const QFileInfo source(recovered.info.sourcePath), output(parser.value("output"));
                if (source.absoluteFilePath() == output.absoluteFilePath() ||
                    (source.exists() && source.canonicalFilePath() == output.canonicalFilePath()))
                    throw std::runtime_error(
                        "Recovery output must not replace the original saved file");
            }
            recoveryReport = sketchy::describeRecovery(recovered);
            doc = std::move(*recovered.document);
        }
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
        if (parser.isSet("history-position")) {
            const auto navigation = sketchy::executeHistory(
                doc, {{"apiVersion", 1},
                      {"documentId", QString::fromStdString(doc.identity())},
                      {"expectedRevision", QString::number(doc.revision())},
                      {"position", parser.value("history-position")}});
            result = sketchy::describe(doc);
            result["historyNavigation"] = navigation;
        }
        if (!recoveryReport.isEmpty())
            result["recoveryReport"] = recoveryReport;
        if (!importReport.isEmpty())
            result["importReport"] = importReport;
        if (parser.isSet("output")) {
            sketchy::saveDocument(doc, parser.value("output"));
            result["saved"] = parser.value("output");
        }
        std::cout << QJsonDocument(result).toJson().toStdString();
        return 0;
    } catch (const sketchy::InspectionError &e) {
        std::cerr << QJsonDocument(QJsonObject{{"status", "failed"},
                                               {"code", QString::fromStdString(e.code())},
                                               {"error", e.what()}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return 1;
    } catch (const sketchy::PlanarError &e) {
        std::cerr << QJsonDocument(QJsonObject{{"status", "failed"},
                                               {"code", QString::fromStdString(e.code())},
                                               {"error", e.what()}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return 1;
    } catch (const std::exception &e) {
        const auto failure = sketchy::automationFailure(e);
        std::cerr << QJsonDocument(QJsonObject{{"status", "failed"},
                                               {"code", failure["code"]},
                                               {"error", failure["message"]}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return 1;
    }
}
