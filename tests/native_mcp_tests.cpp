#include "app/native_mcp.hpp"
#include "app/window.hpp"
#include "automation/local_mcp.hpp"
#include "automation/mcp.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>
#include <future>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QJsonObject rpc(int id, QString method, QJsonObject params = {}) {
    params["_meta"] = QJsonObject{{"io.modelcontextprotocol/protocolVersion", mcpProtocolVersion},
                                  {"io.modelcontextprotocol/clientCapabilities", QJsonObject{}}};
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}
QJsonObject query(const Document &doc, QString name, QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["query"] = name;
    fields["documentId"] = QString::fromStdString(doc.identity());
    fields["expectedRevision"] = QString::number(doc.revision());
    return fields;
}
struct Client {
    QProcess process;
    explicit Client(const QString &path) {
        process.start(QCoreApplication::applicationDirPath() + "/sketchyup-cli",
                      {"--mcp-connect", path});
        check(QTest::qWaitFor([&] { return process.state() == QProcess::Running; }),
              "Bridge started");
    }
    ~Client() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished();
        }
    }
    void send(QJsonObject request) {
        const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        check(process.write(bytes) == bytes.size(), "Bridge input accepted");
    }
    QJsonObject read() {
        check(QTest::qWaitFor([&] { return process.canReadLine(); }, 10000),
              ("Bridge reply: " + process.readAllStandardError()).constData());
        QJsonParseError error;
        auto result = QJsonDocument::fromJson(process.readLine(), &error);
        check(error.error == QJsonParseError::NoError && result.isObject(), "Valid MCP reply");
        return result.object();
    }
    QJsonObject invoke(QString name, QJsonObject request) {
        send(rpc(1, "tools/call", {{"name", name}, {"arguments", request}}));
        return read();
    }
    void close() {
        process.closeWriteChannel();
        check(QTest::qWaitFor([&] { return process.state() == QProcess::NotRunning; }, 10000),
              "Bridge EOF exits");
        check(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
              "Graceful bridge exit");
    }
};
QJsonObject content(const QJsonObject &reply) {
    check(!reply.contains("error") && !reply["result"].toObject()["isError"].toBool(),
          "Successful tool reply");
    return reply["result"].toObject()["structuredContent"].toObject();
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication app(argc, argv);
    if (app.arguments().contains("--print-capabilities")) {
        std::cout << QJsonDocument(nativeMcpCapabilities()).toJson().toStdString();
        return 0;
    }
    QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", 0);
    Window window;
    try {
        window.openPath("examples/m4-room-study.sketchyup");
        window.resize(1280, 850);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Native window exposed");
        auto &view = *window.viewport();
        auto &doc = window.document();
        window.activateWindow();
        check(QTest::qWaitFor(
                  [&] { return QGuiApplication::focusWindow() == window.windowHandle(); }),
              "Native focus");
        view.setFocus();
        check(QTest::qWaitFor([&] { return view.hasFocus() && view.rendererReady(); }),
              "Viewport ready");
        Id room{}, selected{}, second{};
        for (const auto &[id, body] : doc.bodies()) {
            if (body->name == "Room study")
                room = id;
            if (body->name == "Window A")
                selected = id;
            if (body->name == "Window B")
                second = id;
        }
        check(room && selected && second, "Fixture targets exist");
        view.enterContext(room);
        view.selectEntities({{selected, SelectionKind::Body, 0}});
        view.fit();
        QCoreApplication::processEvents();
        const auto bytes = encodeContainer(doc);
        const auto stamp = doc.saveStamp();
        const auto selection = view.selectionState().entities();
        QFile schema("docs/api/mcp-native-2026-07-28.json");
        check(schema.open(QIODevice::ReadOnly) &&
                  QJsonDocument::fromJson(schema.readAll()).object() == nativeMcpCapabilities(),
              "Published native MCP schema matches executable");
        const auto unsafe = files.filePath("unsafe");
        QDir().mkdir(unsafe);
        QFile::setPermissions(unsafe, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner | QFileDevice::ReadOther);
        bool denied = false;
        try {
            checkedMcpSocketPath(unsafe + "/endpoint", false);
        } catch (const InspectionError &) {
            denied = true;
        }
        check(denied, "Nonprivate socket parent rejected");
        const auto occupied = files.filePath("occupied");
        QFile marker(occupied);
        check(marker.open(QIODevice::WriteOnly), "Create existing endpoint marker");
        marker.write("preserve");
        marker.close();
        denied = false;
        try {
            NativeMcpService existing(view, occupied);
        } catch (const InspectionError &) {
            denied = true;
        }
        check(denied && marker.open(QIODevice::ReadOnly) && marker.readAll() == "preserve",
              "Existing endpoint preserved");
        auto wrongThread = std::async(std::launch::async, [&] {
            try {
                NativeMcpService wrong(view, files.filePath("wrong-thread.sock"));
            } catch (const InspectionError &error) {
                return error.code() == "WRONG_THREAD";
            }
            return false;
        });
        check(wrongThread.get(), "Wrong-thread native binding rejected before reading document");
        const auto path = files.filePath("native.sock");
        NativeMcpService service(view, path);
        Client client(path);
        client.send(rpc(1, "tools/list"));
        const auto catalog = client.read()["result"].toObject()["tools"].toArray();
        check(catalog == nativeMcpCapabilities()["tools"].toArray(),
              "Native discovery matches actual catalog");
        for (const auto &tool : catalog) {
            const auto name = tool.toObject()["name"].toString();
            check(!name.startsWith("transaction.") && name != "document.save" &&
                      name != "view.capture",
                  "No unscoped write or large capture tools");
        }
        const auto state = content(client.invoke(
            "session.describe", {{"apiVersion", 1}, {"operation", "session.describe"}}));
        check(state["selectionAvailable"] == true && state["mutationAvailable"] == false,
              "Honest native session capabilities");
        const auto selectedData =
            content(client.invoke("selection.get", query(doc, "selection.get")))["data"].toObject();
        check(selectedData["total"] == 1, "Real native selection over stdio bridge");
        const auto ref = selectedData["items"].toArray()[0].toObject()["ref"].toObject();
        const auto measured = content(client.invoke(
            "measure.entity",
            query(doc, "measure.entity", {{"target", ref}, {"space", "local"}})))["data"]
                                  .toObject();
        check(std::abs(measured["bounds"].toObject()["dimensions"].toArray()[0].toDouble() - 1.2) <
                  tolerance,
              "Real selected window measures 1.2 m");
        const auto camera =
            content(client.invoke("view.describe", query(doc, "view.describe")))["data"].toObject();
        check(camera["selectedCount"] == 1 && camera["clipFromWorld"].toArray().size() == 16,
              "Real camera metadata");
        auto stale = query(doc, "selection.get");
        stale["expectedRevision"] = "0";
        check(client.invoke("selection.get", stale)["result"]
                      .toObject()["structuredContent"]
                      .toObject()["code"] == "STALE_REVISION",
              "Stale query rejected");
        auto wrong = query(doc, "selection.get");
        wrong["documentId"] = "other";
        check(client.invoke("selection.get", wrong)["result"].toObject()["isError"] == true,
              "Wrong document rejected");
        check(client.invoke("document.save", {{"apiVersion", 1}, {"operation", "document.save"}})
                  .contains("error"),
              "Write tool rejected");
        auto confused = query(doc, "view.capture");
        confused["operation"] = "selection.get";
        check(client.invoke("selection.get", confused).contains("error"),
              "Mixed discriminator cannot bypass catalog");
        check(encodeContainer(doc) == bytes && doc.isCurrentSnapshot(stamp) &&
                  view.selectionState().entities() == selection,
              "Inspection preserves model, history and selection");
        client.send(rpc(2, "resources/list"));
        const auto uri = client.read()["result"]
                             .toObject()["resources"]
                             .toArray()[0]
                             .toObject()["uri"]
                             .toString();
        client.send(
            rpc(7, "subscriptions/listen",
                {{"notifications", QJsonObject{{"resourceSubscriptions", QJsonArray{uri}}}}}));
        check(client.read()["method"] == "notifications/subscriptions/acknowledged",
              "Resource subscription acknowledged");
        view.selectEntities({{second, SelectionKind::Body, 0}});
        check(client.read()["method"] == "notifications/resources/updated",
              "Manual selection change notifies despite unchanged count and revision");
        client.close();
        check(client.read()["id"] == 7, "EOF completes outstanding subscription");
        check(window.isVisible() && encodeContainer(doc) == bytes && service.listening(),
              "Disconnect leaves editor and model alive");
        check(QTest::qWaitFor([&] { return service.clientCount() == 0; }),
              "Disconnected client reclaimed");
        Client first(path), another(path);
        check(QTest::qWaitFor([&] { return service.clientCount() == 2; }), "Two clients admitted");
        Client excess(path);
        check(QTest::qWaitFor([&] { return excess.process.state() == QProcess::NotRunning; }),
              "Third client rejected");
        first.close();
        another.close();
        check(QTest::qWaitFor([&] { return service.clientCount() == 0; }),
              "Client slots reclaimed");
        // Raw same-user peer checks the service bound independently of the bridge.
        QLocalSocket raw;
        raw.connectToServer(path);
        check(QTest::qWaitFor([&] { return raw.state() == QLocalSocket::ConnectedState; }),
              "Raw peer connected");
        raw.write(QByteArray(sessionWireBytes + 2, ' '));
        check(QTest::qWaitFor([&] { return raw.state() == QLocalSocket::UnconnectedState; }),
              "Oversized unterminated wire closed");
        check(QTest::qWaitFor([&] { return service.clientCount() == 0; }),
              "Oversized peer reclaimed");
        QLocalSocket slow;
        slow.setReadBufferSize(1);
        slow.connectToServer(path);
        check(QTest::qWaitFor([&] { return slow.state() == QLocalSocket::ConnectedState; }),
              "Slow peer connected");
        const auto listRequest =
            QJsonDocument(rpc(1, "tools/list")).toJson(QJsonDocument::Compact) + '\n';
        for (int i = 0; i < 200; ++i)
            slow.write(listRequest);
        QTest::qWait(150);
        {
            Client healthy(path);
            const auto health = content(healthy.invoke(
                "session.describe", {{"apiVersion", 1}, {"operation", "session.describe"}}));
            check(health["documentId"] == QString::fromStdString(doc.identity()),
                  "Backpressured client does not block GUI or another client");
            healthy.close();
        }
        slow.abort();
        check(QTest::qWaitFor([&] { return service.clientCount() == 0; }),
              "Slow reader disconnect reclaimed");
        // EOF without a final newline still delivers the final request before closing.
        {
            Client finalLine(path);
            finalLine.process.write(
                QJsonDocument(rpc(44, "server/discover")).toJson(QJsonDocument::Compact));
            finalLine.close();
            check(finalLine.read()["id"] == 44, "Unterminated final request survives bridge EOF");
        }
        check(QTest::qWaitFor([&] { return service.clientCount() == 0; }),
              "Final-line client reclaimed");
        Client replacement(path);
        check(QTest::qWaitFor([&] { return service.clientCount() == 1; }),
              "Replacement test attached");
        window.openPath("examples/m4-room-study.sketchyup");
        check(QTest::qWaitFor([&] {
                  return !service.listening() &&
                         replacement.process.state() == QProcess::NotRunning;
              }),
              "Reopening same identity closes old document session scope");
        check(window.isVisible(), "Scope replacement does not close window");
        std::cout << "Native MCP selection, scope, subscriptions, EOF and resource bounds passed; "
                     "platform="
                  << QGuiApplication::platformName().toStdString()
                  << " DPR=" << window.devicePixelRatioF() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
