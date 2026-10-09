#include "app/window.hpp"
#include "core/edge_appearance.hpp"
#include "integrations/credential_store.hpp"
#include "integrations/openai_provider.hpp"
#include "io/document_io.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <deque>
#include <iostream>
#include <source_location>
#include <utility>
using namespace sketchy;
using Phase = AssistantTask::Phase;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void nativeFocus(QWidget &widget) {
    check(QTest::qWaitForWindowExposed(&widget), "Assistant interaction window is exposed");
    widget.activateWindow();
    check(QTest::qWaitFor([&] {
              return QGuiApplication::focusWindow() == widget.windowHandle();
          }, 5000),
          "Assistant interaction window receives native keyboard focus");
}
QByteArray json(QJsonObject value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
void write(QString path, QByteArray bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write fixture");
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
template <class F>
void wait(F done, int limit = 10000, std::source_location where = std::source_location::current()) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < limit) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    if (!done()) {
        for (auto *widget : QApplication::allWidgets())
            if (auto *value = qobject_cast<QLabel *>(widget);
                value && (value->objectName() == "assistantState" ||
                          value->objectName() == "assistantCredentialStatus"))
                std::cerr << value->objectName().toStdString() << ": "
                          << value->text().toStdString() << '\n';
        throw std::runtime_error("Async test deadline at line " + std::to_string(where.line()));
    }
}
QJsonObject completed(QJsonArray output) {
    return {{"status", "completed"},
            {"output", output},
            {"usage", QJsonObject{{"input_tokens", 100}, {"output_tokens", 40}}}};
}
QJsonObject message(QString text = "The model is unchanged.") {
    return {
        {"type", "message"},
        {"id", "msg_fixture"},
        {"status", "completed"},
        {"role", "assistant"},
        {"phase", "final_answer"},
        {"content", QJsonArray{QJsonObject{
                        {"type", "output_text"}, {"text", text}, {"annotations", QJsonArray{}}}}}};
}
struct Response {
    int code{200}, delay{1};
    QByteArray bytes{json(completed(QJsonArray{message()}))};
    QNetworkReply::NetworkError error{QNetworkReply::NoError};
    QByteArray retryAfter;
};
class Reply : public QNetworkReply {
    QByteArray bytes_;
    qsizetype offset_{};
    bool ready_{};
    int &aborts_;

  public:
    Reply(QNetworkRequest request, Response response, int &aborts, QObject *parent)
        : QNetworkReply(parent), bytes_(std::move(response.bytes)), aborts_(aborts) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::PostOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.code);
        if (!response.retryAfter.isEmpty())
            setRawHeader("Retry-After", response.retryAfter);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (response.delay >= 0)
            QTimer::singleShot(response.delay, this, [this, error = response.error] {
                if (isFinished())
                    return;
                if (error != NoError)
                    setError(error, "DO NOT EXPOSE RAW ERROR OR CREDENTIAL");
                ready_ = true;
                emit readyRead();
                if (isFinished())
                    return;
                setFinished(true);
                emit finished();
            });
    }
    void abort() override {
        if (isFinished())
            return;
        ++aborts_;
        setError(OperationCanceledError, "aborted");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return (ready_ ? bytes_.size() - offset_ : 0) + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *data, qint64 maximum) override {
        if (!ready_)
            return 0;
        const auto count = std::min<qint64>(maximum, bytes_.size() - offset_);
        if (!count)
            return -1;
        memcpy(data, bytes_.constData() + offset_, size_t(count));
        offset_ += count;
        return count;
    }
};
class Network : public QNetworkAccessManager {
  public:
    int calls{}, aborts{};
    std::function<Response(QJsonObject, int)> respond;
    std::vector<QJsonObject> requests;
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                 QIODevice *out) override {
        check(op == PostOperation && request.url() == QUrl("https://api.openai.com/v1/responses"),
              "Fixed HTTPS Responses endpoint");
        check(request.rawHeader("Authorization") == "Bearer sk-fixture-only",
              "Credential is in authorization header");
        check(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() ==
                  QNetworkRequest::ManualRedirectPolicy,
              "Redirects are not followed");
        const auto bytes = out->readAll();
        check(!bytes.contains("sk-fixture-only"), "No credential in JSON");
        const auto body = QJsonDocument::fromJson(bytes).object();
        check(body.value("store") == false && body.value("stream") == false &&
                  body.value("parallel_tool_calls") == true,
              "Stateless requests advertise bounded read-only inspection batching");
        requests.push_back(body);
        const auto response = respond ? respond(body, calls++) : (++calls, Response{});
        return new Reply(request, response, aborts, this);
    }
};
QString alias(const QJsonObject &body, const QString &name) {
    for (auto value : body.value("tools").toArray()) {
        const auto tool = value.toObject();
        check(tool.value("strict") == false, "Original optional schema semantics retained");
        if (tool.value("description").toString().startsWith(name + ":"))
            return tool.value("name").toString();
    }
    throw std::runtime_error("Advertised tool missing");
}
QJsonObject call(const QJsonObject &body, QString name, QJsonObject args, int index) {
    return {{"type", "function_call"},
            {"id", "fc_" + QString::number(index)},
            {"call_id", "call_" + QString::number(index)},
            {"status", "completed"},
            {"name", alias(body, name)},
            {"arguments", QString::fromUtf8(json(args))}};
}
QString quote(QString text) { return "'" + text.replace("'", "'\\''") + "'"; }
int fakeSecret(QCoreApplication &app, QStringList args) {
    const auto mode = args[2];
    const auto operation = args[3];
    check(args.contains("org.sketchyup.SketchyUp") && args.contains("OpenAI"),
          "Fixed credential attributes");
    check(!args.contains("sk-fixture-only"), "No secret in argv");
    if (mode == "hang")
        return app.exec();
    if (mode == "missing")
        return 1;
    if (mode == "error") {
        std::cerr << "sk-fixture-only";
        return 1;
    }
    if (mode == "flood") {
        std::cout << std::string(9000, 'x') << std::flush;
        return 0;
    }
    if (operation == "lookup")
        std::cout << "sk-fixture-only" << std::flush;
    if (operation == "store") {
        std::string key((std::istreambuf_iterator<char>(std::cin)), {});
        check(key == "sk-fixture-only", "Credential bytes passed by stdin without newline");
    }
    return 0;
}
void modeling(Network &network, Document &doc) {
    const auto id = QString::fromStdString(doc.identity()),
               revision = QString::number(doc.revision()), nextBody = QString::number(doc.nextId());
    network.respond = [id, revision, nextBody, draft = QString{}](QJsonObject request,
                                                                  int turn) mutable {
        QJsonObject args{{"apiVersion", 1}, {"documentId", id}};
        QString operation;
        if (turn == 0) {
            operation = "transaction.begin";
            args["expectedRevision"] = revision;
        } else {
            const auto last = request.value("input").toArray().last().toObject();
            const auto receipt =
                QJsonDocument::fromJson(last.value("output").toString().toUtf8()).object();
            check(receipt.value("isError") == false, "Native tool receipt succeeded");
            if (turn == 1) {
                draft = receipt.value("data").toObject().value("transactionId").toString();
                operation = "transaction.apply";
                args["expectedVersion"] = 0;
                args["operationId"] = "face";
                args["commands"] = QJsonArray{
                    QJsonObject{{"command", "geometry.face"},
                                {"name", "Native assistant face"},
                                {"loops",
                                 QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{2, 0, 0},
                                                       QJsonArray{2, 3, 0}, QJsonArray{0, 3, 0}}}}},
                    QJsonObject{
                        {"command", "geometry.edge_appearance"},
                        {"context", "0"},
                        {"entities", QJsonArray{QJsonObject{{"body", nextBody}, {"edge", "1"}}}},
                        {"smooth", true}}};
            } else {
                operation = "transaction.preview";
                args["expectedVersion"] = 1;
            }
            args["transactionId"] = draft;
        }
        args["operation"] = operation;
        return Response{200, 1, json(completed(QJsonArray{call(request, operation, args, turn)}))};
    };
}
int main(int argc, char **argv) {
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "--fake-secret") {
        QCoreApplication app(argc, argv);
        return fakeSecret(app, app.arguments());
    }
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QApplication app(argc, argv);
    try {
        QSettings prefs("SketchyUp", "SketchyUp");
        prefs.setValue("recoverySeconds", 0);
        const auto helper = files.path() + "/secret-tool";
        write(helper, ("#!/bin/sh\nexec " + quote(QCoreApplication::applicationFilePath()) +
                       " --fake-secret normal \"$@\"\n")
                          .toUtf8());
        check(QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
              "Fake credential helper executable");
        Network network;
        AssistantPanel::HostServices services;
        services.outcomeRoot = files.path() + "/outcomes";
        services.credentialExecutable = helper;
        services.network = &network;
        Window window(nullptr, services);
        window.resize(1200, 850);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        check(QTest::qWaitFor([&] { return window.viewport()->rendererReady(); }),
              "Renderer ready");
        auto &panel = *window.assistantPanel();
        auto &doc = window.document();
        window.findChild<QAction *>("view.assistant")->trigger();
        check(panel.isVisible() && !panel.findChild<QPushButton *>("assistantSend")->isEnabled(),
              "No provider shows setup without disabling model");
        const auto body = doc.addFace({{{-3, 0, 0}, {-2, 0, 0}, {-2, 1, 0}, {-3, 1, 0}}});
        window.viewport()->refresh();
        emit window.viewport()->changed();
        const auto saved = files.path() + "/manual.sketchyup";
        saveDocument(doc, saved);
        check(loadDocument(saved).bodies().size() == 1 && network.calls == 0,
              "Manual edit/save/reopen works with no provider");
        panel.showSetup();
        auto *setup = window.findChild<QDialog *>("assistantPreferencesDialog");
        check(setup, "Preferences open");
        nativeFocus(*setup);
        auto *authChoice = setup->findChild<QComboBox *>("assistantOpenAIAuth");
        check(authChoice, "OpenAI authentication choices");
        authChoice->setCurrentIndex(0);
        check(setup->findChild<QPushButton *>("assistantChatGPTSignIn")->isVisible() &&
                  !setup->findChild<QLineEdit *>("assistantCredential")->isVisible() &&
                  network.calls == 0,
              "Subscription setup offers browser sign-in without API key or background network");
        for (const auto &[id, name] :
             {std::pair{"assistantChatGPTAccount", "ChatGPT account"},
              std::pair{"assistantChatGPTModel", "ChatGPT model"},
              std::pair{"assistantPreferencesScroll", "Assistant preferences"}}) {
            auto *control = setup->findChild<QWidget *>(id);
            check(control && control->isVisibleTo(setup), "Subscription control is visible");
            auto *accessible = QAccessible::queryAccessibleInterface(control);
            check(control->accessibleName() == QString::fromLatin1(name) && accessible &&
                      accessible->isValid(),
                  "Subscription preferences expose descriptive accessible names");
            if (auto *combo = qobject_cast<QComboBox *>(control)) {
                const auto caption = QString::fromLatin1(name);
#ifdef Q_OS_UNIX
                // Qt's Unix combo interface names the selected value and exposes the
                // control's caption through a Label relation.
                const auto accessibleName =
                    combo->currentText().isEmpty() ? caption : combo->currentText();
#else
                const auto accessibleName = caption;
#endif
                check(accessible->role() == QAccessible::ComboBox &&
                          accessible->text(QAccessible::Name) == accessibleName &&
                          accessible->text(QAccessible::Value) == combo->currentText(),
                      "Subscription selector exposes its selected value");
                bool labeled{};
                for (const auto &[related, relation] : accessible->relations(QAccessible::Label)) {
                    if (!related || !related->isValid())
                        continue;
                    auto *captionWidget = qobject_cast<QLabel *>(related->object());
                    labeled |= relation.testFlag(QAccessible::Label) && captionWidget &&
                               captionWidget->buddy() == combo &&
                               captionWidget->isVisibleTo(setup) &&
                               related->text(QAccessible::Name) == caption;
                }
                check(labeled, "Subscription selector exposes its associated caption");
            } else {
                check(accessible->text(QAccessible::Name) == QString::fromLatin1(name),
                      "Preferences scroll region exposes its accessible name");
            }
        }
        for (const auto *id : {"assistantChatGPTAccount", "assistantChatGPTModel"}) {
            auto *control = setup->findChild<QWidget *>(id);
            bool labeled{};
            for (auto *caption : setup->findChildren<QLabel *>())
                labeled |= caption->buddy() == control && caption->isVisibleTo(setup);
            check(labeled, "Subscription selector has a visible associated label");
        }
        authChoice->setCurrentIndex(1);
        setup->findChild<QLineEdit *>("assistantOpenAIModel")->setText("fixture-responses-model");
        setup->findChild<QLineEdit *>("assistantCredential")->setText("sk-fixture-only");
        setup->findChild<QPushButton *>("assistantStoreCredential")->click();
        wait([&] {
            return setup->findChild<QLabel *>("assistantCredentialStatus")
                ->text()
                .contains("Credential stored");
        });
        check(setup->findChild<QLineEdit *>("assistantCredential")->text().isEmpty(),
              "Credential entry clears after storage request");
        setup->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (const auto &key : prefs.allKeys())
            check(!prefs.value(key).toString().contains("sk-fixture-only"),
                  "No credential persisted in preferences");
        modeling(network, doc);
        nativeFocus(window);
        panel.findChild<QPushButton *>("assistantDisclosure")->click();
        nativeFocus(*window.findChild<QDialog *>("assistantConsent"));
        panel.submit("Create a 2 by 3 metre face.");
        auto *consent = window.findChild<QDialog *>("assistantConsent");
        check(consent && consent->isVisible() && network.calls == 0 &&
                  consent->findChild<QPushButton *>("assistantConsentAllow"),
              "No remote request before actual consent");
        nativeFocus(*consent);
        consent->findChild<QPushButton *>("assistantConsentAllow")->click();
        wait([&] { return panel.result().value("phase") == "preview-ready"; });
        wait([&] { return window.viewport()->hasAssistantPreview(); });
        QSet<QString> commands;
        for (const auto &value : network.requests.front()["tools"].toArray()) {
            const auto tool = value.toObject();
            if (!tool["description"].toString().startsWith("transaction.apply:"))
                continue;
            for (const auto &schema : tool["parameters"]
                                          .toObject()["properties"]
                                          .toObject()["commands"]
                                          .toObject()["items"]
                                          .toObject()["oneOf"]
                                          .toArray())
                commands.insert(schema.toObject()["properties"]
                                    .toObject()["command"]
                                    .toObject()["const"]
                                    .toString());
        }
        check(commands.contains("assert.measurement") && commands.contains("component.attach") && commands.contains("component.bind") &&
                  commands.contains("component.detach") && !commands.contains("component.glue") &&
                  !commands.contains("component.bake_host"),
              "Native assistant advertises routine attachments while retaining shared and "
              "destructive permission gates");

        check(doc.bodies().size() == 1 && window.viewport()->isEnabled(),
              "Private preview keeps manual model editable");
        const auto history = doc.history().total;
        auto *applyButton = panel.findChild<QPushButton *>("assistantApply");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Assistant window active for keyboard Apply");
        applyButton->setFocus();
        check(QTest::qWaitFor([&] { return applyButton->hasFocus(); }), "Apply has keyboard focus");
        QTest::keyClick(applyButton, Qt::Key_Return, Qt::ControlModifier);
        wait([&] { return panel.result().value("applied") == true; });
        check(doc.bodies().size() == 2 && doc.history().total == history + 1 &&
                  !window.viewport()->hasAssistantPreview(),
              "Native Apply adds exactly one undo entry and clears preview");
        for (const auto &[bodyId, body] : doc.bodies())
            if (body->name == "Native assistant face")
                check(edgeAppearance(*body, 1).smooth,
                      "Assistant command policy publishes proposed edge appearance");
        panel.findChild<QPushButton *>("assistantUndo")->click();
        check(doc.bodies().size() == 1 &&
                  !panel.findChild<QPushButton *>("assistantUndo")->isEnabled(),
              "Native assistant undo preserves previous manual face");
        network.calls = 0;
        network.requests.clear();
        modeling(network, doc);
        panel.findChild<QCheckBox *>("assistantSelectionContext")->setChecked(false);
        panel.findChild<QCheckBox *>("assistantEditingContext")->setChecked(false);
        panel.findChild<QComboBox *>("assistantMode")->setCurrentIndex(1);
        panel.submit("Create a 2 by 3 metre face directly.");
        wait([&] { return panel.result().value("applied") == true; });
        check(doc.bodies().size() == 2 && doc.history().total == history + 1,
              "Direct mode uses one composed publication after undo branch");
        check(network.requests.front()
                  .value("input")
                  .toArray()
                  .at(1)
                  .toObject()
                  .value("content")
                  .toString()
                  .contains("\"selectedContext\":[]"),
              "Removing context attachments omits initial inspection data");
        panel.findChild<QComboBox *>("assistantMode")->setCurrentIndex(0);
        network.calls = 0;
        network.requests.clear();
        modeling(network, doc);
        panel.submit("Create another face for review.");
        wait([&] { return panel.result().value("phase") == "preview-ready"; });
        doc.move(body, {0, .1, 0});
        window.viewport()->refresh();
        emit window.viewport()->changed();
        wait([&] { return panel.result().value("phase") == "stale"; });
        check(!window.viewport()->hasAssistantPreview() && panel.result().value("applied") == false,
              "Human edit invalidates a displayed preview");
        network.calls = 0;
        network.requests.clear();
        network.respond = [&](QJsonObject request, int turn) {
            if (turn == 0)
                return Response{
                    200, 1,
                    json(completed(QJsonArray{call(
                        request, "assistant.ask_user",
                        {{"question", "Which target?"},
                         {"allowFreeText", true},
                         {"choices",
                          QJsonArray{QJsonObject{{"id", "first"}, {"label", "First & only"}},
                                     QJsonObject{{"id", "other"}, {"label", "Other target"}}}}},
                        0)}))};
            const auto output = QJsonDocument::fromJson(request.value("input")
                                                            .toArray()
                                                            .last()
                                                            .toObject()
                                                            .value("output")
                                                            .toString()
                                                            .toUtf8())
                                    .object();
            check(output.value("data").toObject().value("text").toString().contains("body"),
                  "Viewport pick sends actual bounded entity references");
            return Response{};
        };
        panel.submit("Clarify which face I mean.");
        wait([&] { return panel.result().value("phase") == "awaiting-clarification"; });
        wait([&] { return panel.findChild<QPushButton *>("assistantAnswerPick") != nullptr; });
        window.viewport()->setSelection(body);
        panel.findChild<QPushButton *>("assistantAnswerPick")->click();
        wait([&] { return panel.result().value("phase") == "completed"; });
        check(network.calls == 2 && panel.result().value("applied") == false,
              "Native question resumes once without implicit Apply");
        network.calls = 0;
        network.requests.clear();
        modeling(network, doc);
        panel.submit("Preview another 2 by 3 metre face, leaving existing faces unchanged.");
        wait([&] { return panel.result().value("phase") == "preview-ready"; });
        wait([&] { return window.viewport()->hasAssistantPreview(); });
        check(panel.findChild<QLabel *>("assistantMeasurements")->text().contains("6 m²"),
              "Preview card shows actual measured area");
        check(window.viewport()->accessibleDescription().contains("2 m × 3 m × 0 m"),
              "Preview focus has actual measured length units");
        window.viewport()->fit();
        if (argc > 1)
            check(QDir().mkpath(QString::fromLocal8Bit(argv[1])),
                  "Create visual evidence directory");
        for (int width : {640, 900, 1200, 1600}) {
            window.resize(width, width == 640 ? 480 : 850);
            QTest::qWait(250);
            std::cout << "Layout requested " << width << ", actual " << window.width() << " x "
                      << window.height() << ", DPR " << window.devicePixelRatioF() << '\n';
            check(panel.isVisible() && panel.width() >= 240 && window.viewport()->width() >= 160,
                  "Panel remains usable at target width");
            check(applyButton->isVisible() && panel.rect().contains(applyButton->mapTo(
                                                  &panel, applyButton->rect().center())),
                  "Apply remains on screen without scrolling at every supported width");
            if (argc > 1) {
                const auto directory = QString::fromLocal8Bit(argv[1]);
                check(window.viewport()->grabFramebuffer().save(
                          directory + QString("/assistant-viewport-%1.png").arg(width)),
                      "Save viewport framebuffer with measured overlay");
                check(window.grab().save(directory + QString("/assistant-%1.png").arg(width)),
                      "Save native assistant layout");
                if (auto *sheet = window.findChild<QDialog *>("assistantSheet");
                    sheet && sheet->isVisible())
                    check(sheet->grab().save(directory +
                                             QString("/assistant-sheet-%1.png").arg(width)),
                          "Save native assistant sheet");
            }
            if (width == 640 && window.width() < 1100) {
                auto *sheet = window.findChild<QDialog *>("assistantSheet");
                check(sheet && sheet->isVisible() &&
                          sheet->height() <= std::max(480, window.height()),
                      "Smallest assistant sheet fits the supported minimum window height");
                sheet->activateWindow();
                check(QTest::qWaitForWindowActive(sheet), "Narrow sheet active for Escape");
                panel.setFocus();
                QTest::keyClick(&panel, Qt::Key_Escape);
                check(!sheet->isVisible(), "Escape returns from the narrow sheet to the model");
                window.findChild<QAction *>("view.assistant")->trigger();
                check(sheet->isVisible(), "Assistant action reopens the narrow sheet");
            }
        }
        panel.stop();
        check(!window.viewport()->hasAssistantPreview() && panel.result().value("applied") == false,
              "Discard clears proposal without publication");
        bool armed{};
        Network uncertainNetwork;
        auto uncertainServices = services;
        uncertainServices.network = &uncertainNetwork;
        uncertainServices.transactions.fault = [&](OutcomeStore::Phase point) {
            if (armed && point == OutcomeStore::Phase::AfterRename)
                throw std::runtime_error("injected publication failure");
        };
        Window uncertainWindow(nullptr, uncertainServices);
        uncertainWindow.resize(1200, 850);
        uncertainWindow.show();
        uncertainWindow.findChild<QAction *>("view.assistant")->trigger();
        auto &uncertainPanel = *uncertainWindow.assistantPanel();
        modeling(uncertainNetwork, uncertainWindow.document());
        uncertainPanel.submit("Create a face with a retained uncertain outcome.");
        wait([&] { return uncertainPanel.result().value("phase") == "preview-ready"; });
        armed = true;
        uncertainPanel.apply();
        check(uncertainPanel.uncertain() && !uncertainWindow.viewport()->isEnabled() &&
                  uncertainPanel.result().value("applied").isNull(),
              "Uncertain commit fences native editing without claiming no change");
        const auto beforeUnknown = encodeDocument(uncertainWindow.document());
        uncertainWindow.findChild<QAction *>("edit.undo")->trigger();
        uncertainWindow.close();
        check(uncertainWindow.isVisible() &&
                  encodeDocument(uncertainWindow.document()) == beforeUnknown,
              "Unknown outcome prevents editing and document close");
        armed = false;
        uncertainPanel.reconcile();
        check(uncertainPanel.result().value("applied") == true &&
                  uncertainWindow.viewport()->isEnabled() &&
                  uncertainWindow.document().history().total == 1,
              "Reconciliation publishes once and releases native fence");
        check(!panel.findChild<QPlainTextEdit *>("assistantRawActivity")
                   ->toPlainText()
                   .contains("sk-fixture-only"),
              "No credential in raw activity");
        std::cout << "Native assistant setup, consent, preview/direct undo, clarification, "
                     "staleness, layouts and reconciliation passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
