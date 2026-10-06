#include "automation/recipe.hpp"
#include "core/entity_measure.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray encode(QJsonObject object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
QJsonObject ref(QString step, QString path) { return {{"$ref", step + "#" + path}}; }
QJsonObject op(QString name, QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["operation"] = name;
    return fields;
}
QJsonObject step(QString id, QJsonObject request) { return {{"id", id}, {"request", request}}; }
QJsonObject recipe(QJsonArray steps) { return {{"apiVersion", 1}, {"steps", steps}}; }
template <class F> void rejects(QString code, F action) {
    try {
        action();
    } catch (const std::exception &error) {
        check(automationFailure(error)["code"] == code, error.what());
        return;
    }
    throw std::runtime_error("Expected " + code.toStdString());
}
QJsonArray lines(const QByteArray &bytes) {
    QJsonArray rows;
    for (const auto &line : bytes.split('\n')) {
        if (line.isEmpty())
            continue;
        const auto parsed = QJsonDocument::fromJson(line);
        check(parsed.isObject(), "Every recipe output line must be a JSON object");
        rows.append(parsed.object());
    }
    return rows;
}
struct Run {
    int code;
    QJsonArray rows;
    QJsonObject error;
};
Run cli(QStringList args, int timeoutMs = 30000) {
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    for (const auto *name : {"DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM"})
        env.remove(name);
    process.setProcessEnvironment(env);
    process.start(QStringLiteral(CLI_PATH), args);
    check(process.waitForStarted(10000), "Start recipe CLI");
    process.closeWriteChannel();
    const bool finished = process.waitForFinished(timeoutMs);
    if (!finished || process.exitStatus() != QProcess::NormalExit) {
        const auto reason = finished ? QString("crashed (exit %1)").arg(process.exitCode())
                                     : QString("timed out after %1 ms").arg(timeoutMs);
        if (!finished) {
            process.kill();
            process.waitForFinished(5000);
        }
        throw std::runtime_error(
            (QString("Recipe CLI %1: %2\nstderr:\n%3\nlast output:\n%4")
                 .arg(reason, args.join(' '),
                      QString::fromUtf8(process.readAllStandardError().right(16384)),
                      QString::fromUtf8(process.readAllStandardOutput().right(4096))))
                .toStdString());
    }
    return {process.exitCode(), lines(process.readAllStandardOutput()),
            QJsonDocument::fromJson(process.readAllStandardError()).object()};
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write recipe fixture");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QFile schema(QStringLiteral(SOURCE_DIR "/docs/api/recipe-v1.json"));
        check(schema.open(QIODevice::ReadOnly) &&
                  QJsonDocument::fromJson(schema.readAll()).object() == recipeCapabilities(),
              "Recipe schema matches installed discovery");
        QFile example(QStringLiteral(SOURCE_DIR "/examples/transaction-face-recipe.json"));
        check(example.open(QIODevice::ReadOnly), "Read shipped recipe");
        const auto exampleBytes = example.readAll();
        const auto exampleObject = QJsonDocument::fromJson(exampleBytes).object();
        QTemporaryDir files;
        check(files.isValid(), "Recipe test directory");
        for (int iteration = 0; iteration < 2; ++iteration) {
            const auto model = files.path() + "/face-" + QString::number(iteration) + ".sketchyup";
            const auto outcomes = files.path() + "/outcomes-" + QString::number(iteration);
            const auto result = cli({"--recipe", example.fileName(), "--new", "--output", model,
                                     "--outcomes", outcomes});
            check(result.code == 0 && result.rows.size() == 8, "Shipped headless recipe succeeds");
            for (const auto &row : result.rows)
                check(row.toObject()["ok"] == true, "Every recipe step succeeds");
            const auto staged = result.rows[3].toObject()["result"].toObject()["data"].toObject();
            const auto measured = result.rows[7].toObject()["result"].toObject()["data"].toObject();
            check(std::abs(staged["area"].toDouble() - 6) < tolerance &&
                      std::abs(measured["area"].toDouble() - 6) < tolerance,
                  "Typed references measure private and committed geometry identically");
            const auto saved = loadDocument(model);
            check(saved.revision() == 1 && saved.bodies().size() == 1,
                  "Recipe saves one composed edit");
            const auto inspectionPath = files.path() + "/measure.json";
            write(inspectionPath,
                  encode({{"apiVersion", 1},
                          {"documentId", QString::fromStdString(saved.identity())},
                          {"expectedRevision", "1"},
                          {"query", "measure.entity"},
                          {"space", "world"},
                          {"target", inspectionReference(saved, saved.bodies().begin()->first)}}));
            const auto reopened = cli({"--input", model, "--inspect-file", inspectionPath});
            check(reopened.code == 0 &&
                      std::abs(reopened.rows[0].toObject()["data"].toObject()["area"].toDouble() -
                               6) < tolerance,
                  "Saved recipe reopens and measures without a display or provider");
        }
        for (int variant : {0, 1, 2, 3, 4, 5, 6}) {
            const bool resize = variant == 1 || variant == 2, hosted = variant == 2,
                       furnished = variant >= 5, cabinet = variant == 6,
                       staircase = variant >= 4, roofed = variant == 3 || staircase;
            const QString name = furnished ? (cabinet ? "cabinet-recipe-v1.json" : "table-recipe-v1.json")
                                 : staircase ? "stair-recipe-v1.json"
                                 : roofed   ? "roof-recipe-v1.json"
                                 : hosted ? "hosted-room-recipe-v1.json"
                                 : resize ? "room-window-resize-recipe-v1.json"
                                          : "room-recipe-v1.json";
            const auto model = files.path() + "/" + name + ".sketchyup";
            const auto result =
                cli({"--recipe", QStringLiteral(SOURCE_DIR "/examples/") + name, "--new",
                     "--output", model, "--outcomes", files.path() + "/" + name + "-outcomes"});
            check(result.code == 0 && result.rows.size() == (hosted   ? 17
                                                             : resize ? 16
                                                                      : 8),
                  "Shipped modeling recipe completes all steps");
            for (const auto &row : result.rows)
                check(row.toObject()["ok"] == true, "Modeling recipe step succeeds");
            const auto saved = loadDocument(model);
            const auto authored = result.rows[2]
                                      .toObject()["result"]
                                      .toObject()["createdIds"]
                                      .toObject()["recipeOperations"]
                                      .toArray()[0]
                                      .toObject();
            const auto windows = authored["windows"].toArray();
            check(saved.hostedComponents().hosts.size() == (hosted ? 1 : 0) &&
                      saved.hostedComponents().attachments.size() == (hosted ? 2 : 0),
                  "Only the explicitly adopted recipe persists general host relationships");
            const auto wall = authored["wall"].toString().toULongLong();
            const auto wallVolume =
                measureEntity(saved, {wall, SelectionKind::Body, 0}).local.volume;
            check(wallVolume && std::abs(*wallVolume - (resize ? 9.848 : 9.888)) < tolerance,
                  "Saved legacy and adopted recipes have independently expected wall volumes");
            check(saved.revision() == (resize ? 2 : 1) &&
                      saved.bodies().size() == (furnished ? (cabinet ? 27 : 23) : staircase ? 13 : roofed ? 11 : 9) &&
                      saved.definitions().size() == (furnished ? (cabinet ? 3 : 2) : resize ? 2 : 1),
                  "Installed recipe saves exact transaction count and component scope");
            if (roofed) {
                const auto roofReport = result.rows[2]
                                            .toObject()["result"]
                                            .toObject()["createdIds"]
                                            .toObject()["recipeOperations"]
                                            .toArray()[1]
                                            .toObject();
                const auto roof = roofReport["roof"].toString().toULongLong();
                const auto volume =
                    measureEntity(saved, {roof, SelectionKind::Body, 0}).local.volume;
                check(volume && std::abs(*volume - 4.554) < 1e-5,
                      "Saved roof recipe has independently expected material volume");
            }
            if (staircase) {
                const auto stairReport = result.rows[2].toObject()["result"].toObject()["createdIds"].toObject()["recipeOperations"].toArray()[2].toObject();
                const auto stairs = stairReport["stairs"].toString().toULongLong();
                const auto volume = measureEntity(saved, {stairs, SelectionKind::Body, 0}).world.volume;
                check(volume && std::abs(*volume - 4.368) < 1e-5,
                      "Saved stair recipe has independently expected material volume");
            }
            if (furnished) {
                const auto furniture = result.rows[2].toObject()["result"].toObject()["createdIds"].toObject()["recipeOperations"].toArray()[3].toObject();
                const auto assembly = furniture["assembly"].toString().toULongLong();
                const auto measured = measureEntity(saved,{assembly,SelectionKind::Body,0});
                check(measured.local.bounds && length(measured.local.bounds->dimensions()-(cabinet?Vec3{.9,.4,1.2}:Vec3{1.2,.8,.75}))<1e-6,
                      "Saved furniture retains its exact envelope");
                check(std::abs(furniture["memberVolumeSum"].toDouble()-(cabinet?.059705856:.0455))<1e-6,
                      "Saved furniture has independently expected member material volume sum");
            }
            for (int index = 0; index < 2; ++index) {
                const Id window = windows[index].toObject()["body"].toString().toULongLong();
                const auto measured = measureEntity(saved, {window, SelectionKind::Body, 0});
                check(measured.local.bounds &&
                          std::abs(measured.local.bounds->dimensions().x -
                                   (resize && index == 0 ? 1.4 : 1.2)) < tolerance,
                      "Saved recipe preserves sibling and requested window width");
            }
        }
        {
            const auto model=files.path()+"/site.sketchyup";
            // This complete study replays and measures several assemblies through
            // staging/seal/commit. Sanitizer instrumentation exceeds the small
            // recipe harness's 30-second deadline; all result checks still run.
            const auto result=cli({"--recipe",QStringLiteral(SOURCE_DIR "/examples/site-recipe-v1.json"),
                                   "--new","--output",model,"--outcomes",files.path()+"/site-outcomes"},
                                  360000);
            check(result.code==0 && result.rows.size()==11,"Shipped site recipe completes all eleven steps");
            for(const auto &row:result.rows) check(row.toObject()["ok"]==true,"Site recipe step succeeds");
            const auto discovered=result.rows[4].toObject()["result"].toObject()["data"].toObject();
            check(discovered["total"].toInt()==1,"Site recipe discovers its sole new root without guessing IDs");
            const auto target=discovered["items"].toArray()[0].toObject()["ref"].toObject()["body"].toString().toULongLong();
            const auto saved=loadDocument(model);
            check(saved.revision()==1 && saved.bodies().size()==38 && saved.definitions().size()==4,
                  "Site recipe publishes the complete shared-component study as one task");
            check(length(saved.worldTransform(target).point({})-Vec3{100000.125,200000.25,12.5})<1e-7,
                  "Saved site recipe retains exact converted world coordinates");
            const QJsonValue staged=result.rows[6].toObject()["result"].toObject()["data"];
            const QJsonValue committed=result.rows[10].toObject()["result"].toObject()["data"];
            check(staged==committed,"Site measurements agree before and after commit");
        }
        const auto info = step("info", op("session.describe"));
        rejects("INVALID_REQUEST", [&] { AutomationRecipe::parse("[]"); });
        rejects("UNSUPPORTED_VERSION", [&] {
            AutomationRecipe::parse(encode({{"apiVersion", 2}, {"steps", QJsonArray{info}}}));
        });
        rejects("INVALID_REQUEST", [&] { AutomationRecipe::parse(encode(recipe({info, info}))); });
        rejects("INVALID_REFERENCE", [&] {
            AutomationRecipe::parse(
                encode(recipe({step("first", {{"value", ref("future", "/result")}}), info})));
        });
        rejects("INVALID_REFERENCE", [&] {
            AutomationRecipe::parse(
                encode(recipe({info, step("bad", {{"value", ref("info", "/bad~2escape")}})})));
        });
        rejects("LIMIT_EXCEEDED",
                [&] { AutomationRecipe::parse(QByteArray(recipeInputBytes + 1, ' ')); });
        QJsonArray tooMany;
        for (int i = 0; i < 1001; ++i)
            tooMany.append(step(QString::number(i), op("session.describe")));
        rejects("LIMIT_EXCEEDED", [&] { AutomationRecipe::parse(encode(recipe(tooMany))); });
        const auto malformed = files.path() + "/malformed.json";
        write(malformed, encode(recipe({info, info})));
        const auto unopened = files.path() + "/unopened.sketchyup";
        const auto invalid = cli({"--recipe", malformed, "--new", "--output", unopened,
                                  "--outcomes", files.path() + "/unopened-outcomes"});
        check(invalid.code == 1 && invalid.error["code"] == "INVALID_REQUEST" &&
                  !QFile::exists(unopened),
              "Malformed recipe fails before creating model");
        {
            auto broken = exampleObject;
            auto steps = broken["steps"].toArray();
            steps.append(step(
                "stale", op("transaction.begin", {{"documentId", ref("info", "/result/documentId")},
                                                  {"expectedRevision", "0"}})));
            steps.append(step("never", op("session.describe")));
            broken["steps"] = steps;
            const auto file = files.path() + "/partial.json",
                       model = files.path() + "/partial.sketchyup";
            write(file, encode(broken));
            const auto result = cli({"--recipe", file, "--new", "--output", model, "--outcomes",
                                     files.path() + "/partial-outcomes"});
            check(result.code == 1 && result.rows.size() == 9 &&
                      result.rows.last().toObject()["error"].toObject()["code"] ==
                          "STALE_REVISION" &&
                      loadDocument(model).revision() == 1,
                  "Later failure preserves earlier commit/save and stops remaining steps");
        }
        {
            auto steps = exampleObject["steps"].toArray();
            while (steps.size() > 5)
                steps.removeLast();
            steps.append(step("bad", op("transaction.status",
                                        {{"documentId", ref("info", "/missing")},
                                         {"requestId", ref("seal", "/result/requestId")},
                                         {"payloadHash", ref("seal", "/result/payloadHash")}})));
            const auto file = files.path() + "/abort.json",
                       model = files.path() + "/abort.sketchyup",
                       outcomes = files.path() + "/abort-outcomes";
            write(file, encode(recipe(steps)));
            const auto result =
                cli({"--recipe", file, "--new", "--output", model, "--outcomes", outcomes});
            check(result.code == 1 && result.rows.size() == 6 &&
                      result.rows.last().toObject()["error"].toObject()["code"] ==
                          "INVALID_REFERENCE",
                  "Missing response field fails explicitly");
            const auto sealed = result.rows[4].toObject()["result"].toObject();
            AutomationSession reopened({model, {}, outcomes});
            const auto status = reopened.execute(
                op("transaction.status", {{"documentId", sealed["documentId"]},
                                          {"requestId", sealed["requestId"]},
                                          {"payloadHash", sealed["payloadHash"]}}));
            check(status["status"] == "aborted" && loadDocument(model).revision() == 0,
                  "Recipe failure cleans up accepted uncommitted work");
        }
        {
            // Referencing a large registry repeatedly is rejected during expansion,
            // before constructing an arbitrarily large materialized request.
            QJsonArray refs;
            for (int i = 0; i < 1000; ++i)
                refs.append(ref("schema", "/result/transactions/operations/0"));
            const auto program = AutomationRecipe::parse(
                encode(recipe({step("schema", op("session.capabilities")),
                               step("large", op("session.describe", {{"padding", refs}}))})));
            AutomationSession session({{},
                                       files.path() + "/bounded.sketchyup",
                                       files.path() + "/bounded-outcomes",
                                       true});
            QBuffer output;
            output.open(QIODevice::WriteOnly);
            check(program.run(session, output) == 1 &&
                      lines(output.data()).last().toObject()["error"].toObject()["code"] ==
                          "LIMIT_EXCEEDED",
                  "Reference expansion is bounded before effects");
        }
        {
            const auto program = AutomationRecipe::parse(
                encode(recipe({info, step("next", op("session.describe"))})));
            AutomationSession session(
                {{}, files.path() + "/budget.sketchyup", files.path() + "/budget-outcomes", true});
            QBuffer output;
            output.open(QIODevice::WriteOnly);
            check(
                program.run(session, output,
                            {recipeBudgetBytes, size_t(sessionResponseBytes + 16 * 1024)}) == 1 &&
                    lines(output.data()).size() == 2 &&
                    lines(output.data()).last().toObject()["error"].toObject()["code"] ==
                        "LIMIT_EXCEEDED",
                "Output reservation rejects the next step before exceeding the configured budget");
        }
        {
            const auto program = AutomationRecipe::parse(
                encode(recipe({step("schema", op("session.capabilities")), info})));
            AutomationSession session({{},
                                       files.path() + "/retained.sketchyup",
                                       files.path() + "/retained-outcomes",
                                       true});
            QBuffer output;
            output.open(QIODevice::WriteOnly);
            check(program.run(session, output,
                              {size_t(2 * sessionResponseBytes + 4096), recipeBudgetBytes}) == 1 &&
                      lines(output.data()).size() == 2 &&
                      lines(output.data()).last().toObject()["error"].toObject()["code"] ==
                          "LIMIT_EXCEEDED",
                  "Prior responses are charged before accepting another recipe step");
        }
        {
            // Literal tags leave user data untouched instead of treating it as a reference.
            const auto program = AutomationRecipe::parse(
                encode(recipe({step("literal", {{"$literal", op("session.describe")}})})));
            AutomationSession session({{},
                                       files.path() + "/literal.sketchyup",
                                       files.path() + "/literal-outcomes",
                                       true});
            QBuffer output;
            output.open(QIODevice::WriteOnly);
            check(program.run(session, output) == 0 &&
                      lines(output.data())[0].toObject()["ok"] == true,
                  "Literal escape suppresses template expansion");
        }
        std::cout << "Headless recipes, typed references, deterministic measurements, partial "
                     "outcomes and budgets passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
