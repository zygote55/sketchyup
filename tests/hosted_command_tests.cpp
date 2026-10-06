#include "automation/commands.hpp"
#include "automation/hosted_commands.hpp"
#include "automation/inspection.hpp"
#include "automation/native_assistant_session.hpp"
#include "core/components.hpp"
#include "geometry/solid.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QString sid(Id id) { return QString::number(id); }
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", sid(doc.revision())},
            {"commands", commands}};
}
QJsonObject inspect(const Document &doc, QString name, QJsonObject args) {
    args["apiVersion"] = 1;
    args["documentId"] = QString::fromStdString(doc.identity());
    args["expectedRevision"] = sid(doc.revision());
    args["query"] = name;
    return inspectDocument(doc, args)["data"].toObject();
}
template <class F> void rejects(Document &doc, F run) {
    const auto bytes = encodeContainer(doc);
    const auto history = doc.historyBytes();
    try {
        run();
    } catch (const std::exception &) {
        check(encodeContainer(doc) == bytes && doc.historyBytes() == history,
              "Rejected command leaves exact bytes and history");
        return;
    }
    throw std::runtime_error("Expected hosted command rejection");
}
void volume(const Document &doc, Id host, double expected) {
    const auto &body = *doc.bodies().at(host);
    const auto result = analyzeSolidShells(body.surface, body.topology);
    check(result.report.volume && std::abs(*result.report.volume - expected) < 1e-6,
          "Independent host material volume");
}
struct Fixture {
    Document doc;
    Id host{}, face{}, root{}, definition{}, member{}, sourceFace{}, sceneMember{};
    Fixture(bool cutting = true) {
        host = doc.addFace({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}});
        doc.extrude(host, doc.bodies().at(host)->surface.faces.begin()->first, 1);
        for (const auto &[id, value] : doc.bodies().at(host)->surface.faces)
            if (doc.bodies().at(host)->surface.normal(id).z > .99)
                face = id;
        const auto source = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        sourceFace = doc.bodies().at(source)->surface.faces.begin()->first;
        const auto made = createComponent(doc, source, "Window");
        root = made.instance;
        definition = made.definition;
        member = made.movedGeometry.at(source);
        sceneMember = doc.instances().at(root)->members.at(member);
        executeBatch(doc, batch(doc, {glue(cutting)}));
    }
    QJsonObject glue(bool cutting) const {
        return {{"command", "component.glue"},
                {"definition", sid(definition)},
                {"glue", QJsonObject{{"member", sid(member)},
                                     {"face", sid(sourceFace)},
                                     {"anchor", QJsonArray{1, 1, 0}},
                                     {"tangent", QJsonArray{1, 0, 0}},
                                     {"cutsOpening", cutting}}}};
    }
    QJsonObject attach(double x = 3) const {
        return {{"command", "component.attach"},
                {"body", sid(root)},
                {"host", sid(host)},
                {"face", sid(face)},
                {"anchor", QJsonArray{x, 3, 1}},
                {"inset", .2}};
    }
    QJsonObject simple(QString name) const { return {{"command", name}, {"body", sid(root)}}; }
    QJsonObject run(QJsonObject command) { return executeBatch(doc, batch(doc, {command})); }
};
void lifecycle() {
    Fixture f;
    auto &doc = f.doc;
    const auto bytes = encodeContainer(doc);
    const auto depth = doc.history().total;
    const auto request = batch(doc, {f.attach(), QJsonObject{{"command", "geometry.translate"},
                                                             {"body", sid(f.root)},
                                                             {"delta", QJsonArray{1, 0, 0}}}});
    const auto preview = previewBatch(doc, request);
    check(encodeContainer(doc) == bytes, "Hosted command preview is private");
    const auto receipt = executeBatch(doc, request);
    check(receipt["changes"] == preview["changes"] &&
              receipt["componentOperations"] == preview["componentOperations"] &&
              doc.history().total == depth + 1,
          "Exact preview, receipt and one history item");
    volume(doc, f.host, 96);
    const auto operation = receipt["componentOperations"].toArray()[0].toObject();
    check(operation["attachment"] == attachmentDescription(doc, f.root) &&
              std::abs(doc.worldTransform(f.root).point({1, 1, 0}).x - 4) < 1e-6,
          "Attachment receipt describes final placement after a later command in the same batch");
    check(
        operation["affectedHosts"].toArray() == QJsonArray{sid(f.host)} &&
            operation["affectedAttachments"].toArray() == QJsonArray{sid(f.root)} &&
            operation["attachment"].toObject()["opening"].toObject()["reveals"].toArray().size() ==
                4,
        "Receipt exposes bounded affected records and native reveals");
    const auto description =
        inspect(doc, "entity.describe", {{"target", inspectionReference(doc, f.root)}});
    const auto attachment = description["attachment"].toObject();
    check(attachment["hostRef"] == inspectionReference(doc, f.host) &&
              attachment["faceRef"] == inspectionReference(doc, f.host, "face", f.face),
          "Attachment discovery provides typed current host and face references");
    const auto binding =
        inspect(doc, "entity.describe",
                {{"target", inspectionReference(doc, f.sceneMember, "face",
                                                f.sourceFace)}})["canonicalBinding"]
            .toObject();
    check(binding["member"] == sid(f.member) && binding["definition"] == sid(f.definition),
          "Canonical glue member is discoverable from a materialized face");
    const auto instances =
        inspect(doc, "component.instances", {{"definition", sid(f.definition)}, {"limit", 1}});
    check(instances["glue"] == f.glue(true)["glue"] && instances["items"].toArray().size() == 1,
          "Paged instance query publishes canonical glue");
    const auto first = doc.hostedComponents().hosts.at(f.host)->openings.at(f.root);
    f.run(
        {{"command", "geometry.translate"}, {"body", sid(f.root)}, {"delta", QJsonArray{1, 0, 0}}});
    check(doc.hostedComponents().hosts.at(f.host)->openings.at(f.root).jambs == first.jambs,
          "Ordinary shared move keeps native reveal identities");
    volume(doc, f.host, 96);
    const auto roundTrip = decodeContainer(encodeContainer(doc));
    check(encodeContainer(roundTrip) == encodeContainer(doc),
          "Shared command records persist exactly");
    f.run(f.simple("component.detach"));
    volume(doc, f.host, 100);
    check(doc.hostedComponents().attachments.empty(), "Detach restores opening");
    doc.undo();
    volume(doc, f.host, 96);
    f.run({{"command", "component.bake_host"}, {"body", sid(f.host)}});
    volume(doc, f.host, 96);
    check(doc.hostedComponents().hosts.empty(), "Bake retains cuts and releases records");
    doc.undo();
    f.run({{"command", "component.glue"},
           {"definition", sid(f.definition)},
           {"glue", QJsonValue::Null}});
    volume(doc, f.host, 100);
    check(doc.hostedComponents().attachments.empty() && !doc.definitions().at(f.definition)->glue,
          "Explicit glue removal releases cut and attachment together");
    doc.undo();
    check(doc.hostedComponents().attachments.contains(f.root), "Undo restores glue and attachment");
}
void metadataAndFailures() {
    Fixture f(false);
    auto &doc = f.doc;
    // Put the glue anchor at an explicit compatible current pose without binding.
    doc.move(f.root, {2, 2, 1.2});
    const QJsonObject bind{{"command", "component.bind"},
                           {"body", sid(f.root)},
                           {"host", sid(f.host)},
                           {"face", sid(f.face)},
                           {"inset", .2}};
    const auto bodies = doc.bodies();
    const auto depth = doc.history().total;
    StagingSession stage;
    const auto staged = stage.prepare(doc, batch(doc, {bind}));
    const auto changes = stage.changes(doc, staged["stageId"].toString())["changes"].toArray();
    check(changes.size() == 2 && changes[0].toObject()["kind"] == "host" &&
              changes[1].toObject()["kind"] == "attachment",
          "Paged staging describes metadata-only changes");
    stage.clear();
    const auto preview = previewBatch(doc, batch(doc, {bind}));
    const auto receipt = f.run(bind);
    check(doc.bodies() == bodies && receipt["changes"] == preview["changes"] &&
              doc.history().total == depth + 1 && doc.hostedComponents().attachments.size() == 1,
          "Metadata-only binding is a committed command with one Undo");
    doc.undo();
    check(doc.bodies() == bodies && doc.hostedComponents().attachments.empty(),
          "Metadata binding Undo");
    doc.redo();
    f.run(f.simple("component.detach"));
    for (const auto *field : {"body", "host", "face", "inset"}) {
        auto missing = bind;
        missing.remove(field);
        rejects(doc, [&] { f.run(missing); });
    }
    for (const auto &bad : QJsonArray{QJsonValue(1), QJsonValue("01"), QJsonValue("0"),
                                      QJsonValue("18446744073709551616")}) {
        auto invalid = bind;
        invalid["host"] = bad;
        rejects(doc, [&] { f.run(invalid); });
    }
    for (const auto *field : {"member", "face", "anchor", "tangent", "cutsOpening"}) {
        auto command = f.glue(true);
        auto glue = command["glue"].toObject();
        glue.remove(field);
        command["glue"] = glue;
        rejects(doc, [&] { f.run(command); });
    }
    auto invalid = f.glue(true);
    auto glue = invalid["glue"].toObject();
    glue["unexpected"] = true;
    invalid["glue"] = glue;
    rejects(doc, [&] { f.run(invalid); });
    invalid = f.glue(true);
    glue = invalid["glue"].toObject();
    glue["cutsOpening"] = 1;
    invalid["glue"] = glue;
    rejects(doc, [&] { f.run(invalid); });
    auto stale = batch(doc, {bind});
    f.run({{"command", "scene.rename"}, {"body", sid(f.root)}, {"name", "Renamed"}});
    rejects(doc, [&] { executeBatch(doc, stale); });
    rejects(doc, [&] {
        executeBatch(doc, batch(doc, {bind, QJsonObject{{"command", "geometry.translate"},
                                                        {"body", sid(f.root)},
                                                        {"delta", QJsonArray{0, 0, 1}}}}));
    });
    rejects(doc, [&] {
        f.run({{"command", "component.edit"},
               {"definition", sid(f.definition)},
               {"commands", QJsonArray{f.glue(true)}}});
    });
    f.run({{"command", "scene.state"}, {"body", sid(f.host)}, {"locked", true}});
    rejects(doc, [&] { f.run(bind); });
    doc.undo();
    f.run(bind);
    f.run({{"command", "scene.state"}, {"body", sid(f.root)}, {"locked", true}});
    rejects(doc, [&] { f.run(f.simple("component.detach")); });
}
void stagedRecordDiff() {
    Fixture f(false);
    auto &doc = f.doc;
    const auto peer = placeComponent(doc, f.definition).instance;
    attachComponent(doc, peer, f.host, f.face, {{7, 7, 1}});
    StagingSession stage;
    const auto prepared = stage.prepare(doc, batch(doc, {f.attach()}));
    const auto changes = stage.changes(doc, prepared["stageId"].toString())["changes"].toArray();
    int hosts = 0, attachments = 0;
    for (const auto &value : changes) {
        const auto change = value.toObject();
        if (change["kind"] == "host")
            ++hosts;
        if (change["kind"] == "attachment") {
            ++attachments;
            check(change["id"] == sid(f.root), "Unchanged peer attachment is absent from diff");
        }
    }
    check(hosts == 0 && attachments == 1, "Frozen unchanged host baseline is absent from diff");
}
void copiedBindings() {
    Fixture f;
    auto &doc = f.doc;
    f.run(f.attach());
    const auto original = encodeContainer(doc);
    const auto depth = doc.history().total;
    QJsonObject command{
        {"command", "geometry.array_selection"},
        {"entities",
         QJsonArray{QJsonObject{{"body", sid(f.root)}, {"kind", "context"}, {"entity", "0"}}}},
        {"mode", "linear"},
        {"delta", QJsonArray{2.5, 0, 0}},
        {"count", 1}};
    const auto preview = previewBatch(doc, batch(doc, {command}));
    check(encodeContainer(doc) == original, "Hosted array command preview is private");
    const auto receipt = f.run(command);
    check(receipt["copies"] == preview["copies"] && receipt["changes"] == preview["changes"],
          "Hosted array preview and commit expose the same copied identities and host edits");
    Id copied = 0;
    for (const auto &value : receipt["copies"].toArray()) {
        const auto record = value.toObject();
        if (record["sourceBody"] == sid(f.root))
            copied = record["body"].toString().toULongLong();
    }
    check(copied && doc.hostedComponents().attachments.at(copied)->host == f.host,
          "Shared array receipt resolves to a persistent copied attachment");
    volume(doc, f.host, 92);
    command["count"] = 2;
    executeAmend(doc, doc.amendmentStamp(), batch(doc, {command}));
    volume(doc, f.host, 88);
    check(doc.hostedComponents().attachments.size() == 3 && doc.history().total == depth + 1 &&
              !doc.bodies().contains(copied),
          "Shared array amendment retires copies in one history item");
    const auto persisted = encodeContainer(doc);
    check(encodeContainer(decodeContainer(persisted)) == persisted,
          "Array opening correspondence and bindings survive exact container persistence");
    command["count"] = 3;
    rejects(doc, [&] { executeAmend(doc, doc.amendmentStamp(), batch(doc, {command})); });
    doc.undo();
    volume(doc, f.host, 96);
    check(doc.hostedComponents().attachments.size() == 1,
          "One shared Undo removes all copied openings and bindings");
}
void nativeAssistant() {
    for (int scenario : {0, 1, 2, 3}) {
        const bool locked = scenario == 1 || scenario == 2;
        Fixture f(false);
        auto &doc = f.doc;
        doc.move(f.root, {2, 2, 1.2});
        Selection selection;
        if (locked)
            selection.lock(doc, scenario == 2 ? f.root : f.host, true);
        QTemporaryDir files;
        check(files.isValid(), "Owned durable outcome directory");
        NativeAssistantSession session(doc, files.path(), &selection);
        auto backend = session.backend();
        const auto call = backend.call;
        QJsonObject last;
        backend.call = [&](const QJsonObject &request) {
            last = call(request);
            return last;
        };
        AssistantTask::Options options;
        options.prompt = "Bind this alignment-only component at its current pose.";
        options.provider = "fixture";
        options.model = "hosted";
        options.allowedCommands = {"component.bind", "component.glue"};
        AssistantTask task(backend, options);
        int serial = 0;
        auto send = [&](QString name, QJsonObject args) {
            const auto request = task.nextRequest();
            check(request.has_value(), "Assistant requests next tool turn");
            args["apiVersion"] = 1;
            args["documentId"] = QString::fromStdString(doc.identity());
            args["operation"] = name;
            AssistantReply reply;
            reply.calls.push_back({QString::number(++serial), name, args});
            check(task.accept(request->value("attemptId").toString(), reply),
                  "Assistant accepts tool turn");
            return last;
        };
        const auto before = encodeContainer(doc);
        const auto history = doc.history().total;
        const QJsonValue draft =
            send("transaction.begin", {{"expectedRevision", sid(doc.revision())}})["transactionId"];
        const auto command = scenario == 2 ? f.glue(true)
                                           : QJsonObject{{"command", "component.bind"},
                                                         {"body", sid(f.root)},
                                                         {"host", sid(f.host)},
                                                         {"face", sid(f.face)},
                                                         {"inset", .2}};
        send("transaction.apply", {{"transactionId", draft},
                                   {"expectedVersion", 0},
                                   {"operationId", "bind"},
                                   {"commands", QJsonArray{command}}});
        send("transaction.preview", {{"transactionId", draft}, {"expectedVersion", 1}});
        check(encodeContainer(doc) == before,
              "Assistant attachment staging never mutates live document");
        if (locked) {
            check(task.phase() != AssistantTask::Phase::PreviewReady,
                  "Native editor lock rejects metadata-only attachment preview");
            task.cancel();
        } else {
            check(task.phase() == AssistantTask::Phase::PreviewReady,
                  "Assistant seals attachment preview");
            if (scenario == 3) {
                selection.lock(doc, f.host, true);
                task.apply();
                check(task.result()["applied"] != true && encodeContainer(doc) == before &&
                          doc.history().total == history,
                      "New native lock blocks metadata-only commit after preview");
                continue;
            }
            task.apply();
            check(task.result()["applied"] == true &&
                      doc.hostedComponents().attachments.size() == 1 &&
                      doc.history().total == history + 1,
                  "Durable assistant apply commits metadata-only binding once");
            task.apply();
            check(doc.history().total == history + 1,
                  "Assistant repeated apply does not duplicate history");
            doc.undo();
            check(doc.hostedComponents().attachments.empty(),
                  "Assistant binding is one ordinary Undo");
        }
    }
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        lifecycle();
        metadataAndFailures();
        stagedRecordDiff();
        copiedBindings();
        nativeAssistant();
        std::cout << "Hosted commands, inspection, atomic failures, metadata Undo and native "
                     "assistant policy passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
