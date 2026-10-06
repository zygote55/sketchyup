#include "automation/inspection_session.hpp"
#include "automation/mcp.hpp"
#include "core/face_orientation.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(const char *code, F operation) {
    try {
        operation();
    } catch (const InspectionError &error) {
        check(error.code() == code, error.what());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
QJsonObject request(const Document &doc, Id body) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"query", "geometry.diagnose"},
            {"target", inspectionReference(doc, body)}};
}
QJsonObject run(const Document &doc, Id body) {
    const auto result = inspectDocument(doc, request(doc, body));
    check(QJsonDocument(result).toJson(QJsonDocument::Compact).size() <= inspectionResponseBytes,
          "Diagnostic response respects wire budget");
    return result["data"].toObject();
}
QJsonObject finding(const QJsonObject &data, const QString &code) {
    for (const auto &entry : data["findings"].toArray())
        if (entry.toObject()["code"] == code)
            return entry.toObject();
    throw std::runtime_error("Missing finding: " + code.toStdString());
}
Id cube(Document &doc) {
    const auto body = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
    return body;
}
void guardedRead() {
    Document doc;
    const auto body = cube(doc), group = createGroup(doc, {body}, "Context");
    doc.transform(group, Transform::scaling({-2, 3, .5}));
    const auto before = encodeContainer(doc);
    const auto stamp = doc.saveStamp();
    auto data = run(doc, body);
    check(data["findings"].toArray().empty() && data["analysisComplete"] == true &&
              data["materialVolume"] == 1 && data["space"] == "local" &&
              data["scope"] == "body_record" && data["includesDescendants"] == false,
          "Reflection does not invert native geometry; volume and scope explicitly local");
    check(run(doc, group)["solidStatus"] == "empty",
          "Group record never impersonates its descendants");
    auto bad = request(doc, body);
    bad["target"] = inspectionReference(doc, body, "face", 5);
    rejects("INVALID_TARGET", [&] { inspectDocument(doc, bad); });
    bad = request(doc, body);
    bad["target"] = inspectionReference(doc, body);
    auto target = bad["target"].toObject();
    target["contextPath"] = QJsonArray{};
    bad["target"] = target;
    rejects("CONTEXT_MISMATCH", [&] { inspectDocument(doc, bad); });
    bad = request(doc, body);
    bad["documentId"] = "foreign";
    rejects("WRONG_DOCUMENT", [&] { inspectDocument(doc, bad); });
    bad = request(doc, body);
    bad["space"] = "world";
    rejects("INVALID_REQUEST", [&] { inspectDocument(doc, bad); });
    check(encodeContainer(doc) == before && doc.isCurrentSnapshot(stamp),
          "Reads/rejections are immutable");

    SelectionSet faces;
    for (const auto &[id, face] : doc.bodies().at(body)->surface.faces)
        faces.insert({body, SelectionKind::Face, id});
    auto stale = request(doc, body);
    reverseSelectedFaces(doc, faces, group);
    rejects("STALE_REVISION", [&] { inspectDocument(doc, stale); });
    setEntityState(doc, group, true, true);
    data = run(doc, body);
    const auto inverted = finding(data, "inverted_shells");
    check(inverted["count"] == 6 && inverted["countExact"] == true &&
              inverted["reverseShellsEligible"] == true && inverted["truncated"] == false,
          "Hidden locked geometry remains inspectable without granting edit permission");
    for (const auto &value : inverted["references"].toArray()) {
        const auto ref = value.toObject();
        check(ref["kind"] == "face" && ref["body"] == QString::number(body) &&
                  ref["contextPath"] == QJsonArray{QString::number(group)} &&
                  ref["documentId"] == QString::fromStdString(doc.identity()),
              "Findings retain typed identity and hierarchy");
    }
    check(run(decodeContainer(encodeContainer(doc)), body) == data,
          "Diagnostics survive reopen exactly");
}
void snapshotsAndMcp() {
    Document doc;
    const auto body = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    InspectionSession session;
    QJsonObject begin{{"apiVersion", 1},
                      {"documentId", QString::fromStdString(doc.identity())},
                      {"expectedRevision", QString::number(doc.revision())},
                      {"query", "snapshot.begin"}};
    const auto captured = session.execute(doc, begin);
    auto query = request(doc, body);
    const QJsonValue baseline = session.execute(doc, query)["data"];
    query["snapshotId"] = captured["snapshotId"];
    doc.extrude(body, 5, 1);
    const auto result = session.execute(doc, query);
    check(result["data"] == baseline && result["differsFromLiveRevision"] == true &&
              run(doc, body)["findings"].toArray().empty(),
          "Snapshot diagnostics retain open boundaries after live geometry becomes solid");

    QTemporaryDir files;
    check(files.isValid(), "Private test files");
    saveDocument(doc, files.filePath("model.sketchyup"));
    AutomationSession automation(
        {files.filePath("model.sketchyup"), {}, files.filePath("outcomes"), false});
    McpServer server(automation);
    QJsonObject rpc{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "tools/call"},
        {"params",
         QJsonObject{
             {"name", "geometry.diagnose"},
             {"arguments", request(doc, body)},
             {"_meta",
              QJsonObject{{"io.modelcontextprotocol/protocolVersion", mcpProtocolVersion},
                          {"io.modelcontextprotocol/clientCapabilities", QJsonObject{}}}}}}};
    const auto messages = server.handle(rpc);
    const auto response = messages[0].toObject()["result"].toObject();
    check(response["isError"] == false &&
              response["structuredContent"].toObject()["data"] == run(doc, body),
          "MCP dispatch exposes identical read-only diagnostic report");
}
void byteBound() {
    Document doc;
    Edit edit{"Large nested diagnostics", {}};
    Id parent = 0;
    for (Id i = 0; i < 127; ++i) {
        auto group = std::make_shared<Body>();
        group->id = 10000000000000000000ULL + i;
        group->kind = BodyKind::Group;
        group->parent = parent;
        parent = group->id;
        edit.changes.push_back({group->id, nullptr, group});
    }
    auto body = std::make_shared<Body>();
    body->id = parent + 1;
    body->parent = parent;
    for (int i = 0; i < 100; ++i) {
        const double x = i * 3;
        body->surface.addFace({{{x, 0, 0}, {x + 1, 0, 0}, {x, 1, 0}}});
        const auto a = body->surface.vertex({x, 3, 0}), b = body->surface.vertex({x + 1, 3, 0});
        body->surface.wires.push_back({a, b});
        body->surface.vertex({x, 6, 0});
    }
    body->topology = Topology::rebuild(body->surface, {});
    edit.changes.push_back({body->id, nullptr, body});
    doc.apply(std::move(edit), doc.revision());
    const auto before = encodeContainer(doc);
    const auto data = run(doc, body->id);
    bool byteTruncated = false;
    for (const auto &value : data["findings"].toArray()) {
        const auto row = value.toObject();
        check(row["countExact"] == true && row["truncated"] == true &&
                  row["reverseShellsEligible"] == false && row["references"].toArray().size() <= 64,
              "Truncation retains exact counts without repair permission");
        byteTruncated |= row["references"].toArray().size() < 64;
    }
    check(
        byteTruncated && finding(data, "open_boundary")["count"] == 300 &&
            finding(data, "loose_edges")["count"] == 100 &&
            finding(data, "loose_vertices")["count"] == 100,
        "Deep uint64 context paths truncate reference bytes while preserving every scalar finding");
    check(encodeContainer(doc) == before, "Large inspection leaves document bytes unchanged");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        guardedRead();
        snapshotsAndMcp();
        byteBound();
        std::cout << "Guarded diagnostic inspection, snapshots, MCP and deep-context byte bounds "
                     "passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
