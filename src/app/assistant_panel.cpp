#include "app/assistant_panel.hpp"
#include "app/unit_display.hpp"
#include "automation/commands.hpp"
#include "automation/entity_info.hpp"
#include "integrations/chatgpt_auth.hpp"
#include "integrations/credential_store.hpp"
#include "integrations/ollama_provider.hpp"
#include "integrations/openai_provider.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
namespace sketchy {
namespace {
using Phase = AssistantTask::Phase;
QLabel *label(const QString &text, const QString &name = {}) {
    auto *value = new QLabel(text);
    value->setTextFormat(Qt::PlainText);
    value->setWordWrap(true);
    value->setObjectName(name);
    value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return value;
}
QPushButton *button(const QString &text, const QString &name) {
    auto *value = new QPushButton(text);
    value->setObjectName(name);
    return value;
}
QString json(const QJsonObject &value) {
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Indented));
}
bool active(Phase phase) {
    return phase == Phase::Ready || phase == Phase::AwaitingProvider || phase == Phase::Backoff ||
           phase == Phase::AwaitingClarification || phase == Phase::PreviewReady ||
           phase == Phase::OutcomeUnknown;
}
QString activityName(const QString &name) {
    if (name == "assistant.ask_user")
        return "Asked for clarification";
    if (name == "transaction.begin")
        return "Started a private proposal";
    if (name == "transaction.apply")
        return "Validated changes in the private proposal";
    if (name == "transaction.preview")
        return "Sealed the proposal for review";
    if (name == "transaction.inspect")
        return "Inspected the proposed model";
    if (name == "transaction.diff")
        return "Compared proposed changes";
    if (name == "transaction.abort")
        return "Discarded private staging";
    if (name.startsWith("measure."))
        return "Measured model geometry";
    if (name == "selection.get")
        return "Inspected the current selection";
    return "Inspected model data";
}
} // namespace
struct AssistantPanel::Impl {
    AssistantPanel &owner;
    Document &document;
    Viewport &view;
    HostServices services;
    OpenAiCredentialStore credentials;
    ChatGptAuth chatgpt;
    bool pendingPlan{};
    std::unique_ptr<NativeAssistantSession> session;
    std::unique_ptr<AssistantNetworkProvider> provider;
    Document::SaveStamp scope, pendingScope;
    std::shared_ptr<const Document::PreparedEdit> preview;
    std::optional<AssistantTask::Options> pending;
    QString pendingProvider, error, shownQuestion, lastResult, taskPrompt;
    bool pendingDirect{}, taskDirect{}, fenced{}, refreshing{};
    QPointer<QDialog> setup, consent;
    QLabel *chip{}, *state{}, *contextLabel{}, *summary{}, *measure{}, *setupStatus{},
        *requestLabel{}, *explanationHeading{};
    QWidget *setupCard{}, *taskControls{}, *questionCard{}, *banner{};
    QVBoxLayout *questionLayout{};
    QPlainTextEdit *composer{}, *activity{}, *explanation{}, *raw{};
    QCheckBox *selection{}, *context{}, *destructive{}, *shared{}, *activityToggle{};
    QComboBox *mode{};
    QListWidget *changes{};
    QPushButton *send{}, *stopButton{}, *applyButton{}, *discard{}, *refine{}, *undo{},
        *reconcileButton{}, *settings{};
    QPushButton *bannerApply{}, *bannerDiscard{}, *manageUsage{};
    QTimer refreshTimer;
    std::vector<Id> changedBodies;
    Impl(AssistantPanel &owner, Document &document, Viewport &view, HostServices services)
        : owner(owner), document(document), view(view), services(std::move(services)),
          credentials(&owner), chatgpt(this->services.network, &owner), refreshTimer(&owner) {
        if (!this->services.credentialExecutable.isEmpty()) {
            credentials.setExecutable(this->services.credentialExecutable);
            chatgpt.setCredentialExecutable(this->services.credentialExecutable);
        }
        owner.setObjectName("assistantContainer");
        owner.setMinimumWidth(240);
        owner.setFocusPolicy(Qt::StrongFocus);
        auto *layout = new QVBoxLayout(&owner);
        layout->setContentsMargins(8, 8, 8, 8);
        chip = label({}, "assistantProvider");
        layout->addWidget(chip);
        manageUsage = button("Manage ChatGPT usage", "assistantManageUsage");
        layout->addWidget(manageUsage);
        QObject::connect(manageUsage, &QPushButton::clicked, &owner, [] {
            QDesktopServices::openUrl(QUrl("https://chatgpt.com/settings/usage"));
        });
        auto *top = new QHBoxLayout;
        auto *disclosure = button("What is sent", "assistantDisclosure");
        settings = button("Preferences…", "assistantPreferences");
        top->addWidget(disclosure);
        top->addWidget(settings);
        layout->addLayout(top);
        QObject::connect(disclosure, &QPushButton::clicked, &owner,
                         [this] { showDisclosure(false); });
        QObject::connect(settings, &QPushButton::clicked, &owner,
                         [this] { safe([this] { showSetup(); }); });
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *body = new QWidget;
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        setupCard = new QWidget;
        auto *setupLayout = new QVBoxLayout(setupCard);
        setupLayout->addWidget(label("Choose a provider to use the assistant. You can keep "
                                     "modeling and saving without one."));
        auto *configure = button("Set up assistant", "assistantSetup");
        setupLayout->addWidget(configure);
        QObject::connect(configure, &QPushButton::clicked, settings, &QPushButton::click);
        bodyLayout->addWidget(setupCard);
        taskControls = new QWidget;
        auto *taskLayout = new QVBoxLayout(taskControls);
        taskLayout->setContentsMargins(0, 0, 0, 0);
        state = label("Ready", "assistantState");
        taskLayout->addWidget(state);
        requestLabel = label({}, "assistantOriginalRequest");
        taskLayout->addWidget(requestLabel);
        activity = new QPlainTextEdit;
        activity->setObjectName("assistantActivity");
        activity->setReadOnly(true);
        activity->setMaximumBlockCount(300);
        activity->setMaximumHeight(110);
        explanation = new QPlainTextEdit;
        explanation->setObjectName("assistantExplanation");
        explanation->setReadOnly(true);
        explanation->setMaximumHeight(110);
        explanation->setAccessibleName("Unverified assistant explanation");
        explanationHeading = label("Assistant explanation · unverified");
        taskLayout->addWidget(explanationHeading);
        taskLayout->addWidget(explanation);
        questionCard = new QWidget;
        questionCard->setObjectName("assistantQuestion");
        questionLayout = new QVBoxLayout(questionCard);
        questionLayout->setContentsMargins(0, 0, 0, 0);
        taskLayout->addWidget(questionCard);
        summary = label({}, "assistantChangeSummary");
        taskLayout->addWidget(summary);
        changes = new QListWidget;
        changes->setObjectName("assistantChanges");
        changes->setAccessibleName("Proposed entity changes");
        changes->setMaximumHeight(140);
        taskLayout->addWidget(changes);
        measure = label({}, "assistantMeasurements");
        taskLayout->addWidget(measure);
        QObject::connect(changes, &QListWidget::currentRowChanged, &owner,
                         [this](int row) { safe([this, row] { inspect(row); }); });
        auto *actions = new QHBoxLayout;
        applyButton = button("Apply", "assistantApply");
        discard = button("Discard", "assistantDiscard");
        refine = button("Refine / re-plan", "assistantRefine");
        actions->addWidget(applyButton);
        actions->addWidget(discard);
        layout->addLayout(actions);
        layout->addWidget(refine);
        reconcileButton = button("Reconcile outcome", "assistantReconcile");
        taskLayout->addWidget(reconcileButton);
        undo = button("Undo assistant change", "assistantUndo");
        taskLayout->addWidget(undo);
        QObject::connect(applyButton, &QPushButton::clicked, &owner,
                         [this] { safe([this] { apply(); }); });
        QObject::connect(discard, &QPushButton::clicked, &owner,
                         [this] { safe([this] { stop(); }); });
        QObject::connect(reconcileButton, &QPushButton::clicked, &owner,
                         [this] { safe([this] { reconcile(); }); });
        QObject::connect(refine, &QPushButton::clicked, &owner, [this] {
            safe([this] {
                stop();
                this->owner.focusComposer();
            });
        });
        QObject::connect(undo, &QPushButton::clicked, &owner, [this] {
            safe([this] {
                if (!undoCurrent())
                    throw std::runtime_error(
                        "Other edits followed this task; use History to inspect them.");
                this->document.undo();
                this->view.refresh();
                emit this->owner.modelChanged();
                refresh();
            });
        });
        activityToggle = new QCheckBox("Show validated steps");
        taskLayout->addWidget(activityToggle);
        taskLayout->addWidget(activity);
        QObject::connect(activityToggle, &QCheckBox::toggled, &owner,
                         [this](bool shown) { activity->setVisible(shown); });
        auto *diagnostics = new QCheckBox("Show raw activity");
        taskLayout->addWidget(diagnostics);
        raw = new QPlainTextEdit;
        raw->setObjectName("assistantRawActivity");
        raw->setReadOnly(true);
        raw->setMaximumHeight(180);
        raw->hide();
        taskLayout->addWidget(raw);
        QObject::connect(diagnostics, &QCheckBox::toggled, raw, &QWidget::setVisible);
        bodyLayout->addWidget(taskControls);
        bodyLayout->addStretch();
        scroll->setWidget(body);
        layout->insertWidget(2, scroll, 1);
        selection = new QCheckBox("Attach selection (up to 7 entities)");
        selection->setObjectName("assistantSelectionContext");
        selection->setChecked(true);
        context = new QCheckBox("Attach editing context");
        context->setObjectName("assistantEditingContext");
        context->setChecked(true);
        layout->addWidget(selection);
        layout->addWidget(context);
        contextLabel = label({}, "assistantContext");
        layout->addWidget(contextLabel);
        composer = new QPlainTextEdit;
        composer->setObjectName("assistantComposer");
        composer->setPlaceholderText("Ask or describe a change…");
        composer->setAccessibleName("Assistant request");
        composer->setMinimumHeight(48);
        composer->setMaximumHeight(80);
        layout->addWidget(composer);
        mode = new QComboBox;
        mode->setObjectName("assistantMode");
        mode->addItems({"Preview first", "Apply directly"});
        layout->addWidget(mode);
        destructive = new QCheckBox("Allow deleting entities for this request");
        destructive->setObjectName("assistantAllowDeletion");
        shared = new QCheckBox("Allow shared-definition edits for this request");
        shared->setObjectName("assistantAllowShared");
        bodyLayout->insertWidget(bodyLayout->count() - 1, destructive);
        bodyLayout->insertWidget(bodyLayout->count() - 1, shared);
        auto *bottom = new QHBoxLayout;
        send = button("Send", "assistantSend");
        stopButton = button("Stop", "assistantStop");
        bottom->addWidget(send);
        bottom->addWidget(stopButton);
        layout->addLayout(bottom);
        QObject::connect(send, &QPushButton::clicked, &owner,
                         [this] { safe([this] { submit(composer->toPlainText()); }); });
        QObject::connect(stopButton, &QPushButton::clicked, &owner,
                         [this] { safe([this] { stop(); }); });
        auto *applyShortcut = new QShortcut(QKeySequence("Ctrl+Return"), &owner);
        applyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        QObject::connect(applyShortcut, &QShortcut::activated, applyButton, &QPushButton::click);
        auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), &owner);
        escape->setContext(Qt::WidgetWithChildrenShortcut);
        QObject::connect(escape, &QShortcut::activated, &owner,
                         [this] { emit this->owner.focusViewport(); });
        banner = new QWidget(&view);
        banner->setObjectName("assistantPreviewBanner");
        auto *bannerLayout = new QHBoxLayout(banner);
        bannerLayout->setContentsMargins(6, 4, 6, 4);
        bannerLayout->addWidget(label("Preview — not in your model yet"));
        bannerApply = button("Apply", "assistantBannerApply");
        bannerDiscard = button("Discard", "assistantBannerDiscard");
        bannerLayout->addWidget(bannerApply);
        bannerLayout->addWidget(bannerDiscard);
        QObject::connect(bannerApply, &QPushButton::clicked, applyButton, &QPushButton::click);
        QObject::connect(bannerDiscard, &QPushButton::clicked, discard, &QPushButton::click);
        banner->hide();
        refreshTimer.setInterval(100);
        QObject::connect(&refreshTimer, &QTimer::timeout, &owner, [this] {
            if (this->owner.isVisible() || provider || pending)
                safe([this] { refresh(); });
        });
        refreshTimer.start();
        QObject::connect(&credentials, &OpenAiCredentialStore::changed, &owner,
                         [this] { safe([this] { credentialChanged(); }); });
        QObject::connect(&chatgpt, &ChatGptAuth::authorizationRequested, &owner,
                         [this](const QUrl &url) {
                             if (!QDesktopServices::openUrl(url)) {
                                 chatgpt.cancel();
                                 error = "Could not open the system browser. Check your default "
                                         "browser and try again.";
                                 refresh();
                             }
                         });
        QObject::connect(&chatgpt, &ChatGptAuth::credentialReady, &owner,
                         [this](const QByteArray &token) { safe([&] { start(token); }); });
        QObject::connect(&chatgpt, &ChatGptAuth::changed, &owner, [this] {
            // credentialReady follows changed on success, in the same event turn.
            QTimer::singleShot(0, &this->owner, [this] {
                if (pending && pendingPlan && !chatgpt.busy()) {
                    pending.reset();
                    error = chatgpt.status();
                    refresh();
                }
            });
        });
        refresh();
    }
    ~Impl() {
        refreshTimer.stop();
        delete setup.data();
        delete consent.data();
        provider.reset();
        session.reset();
        view.setAssistantPreview({});
        delete banner;
    }
    void safe(const std::function<void()> &fn) {
        try {
            fn();
        } catch (const std::exception &e) {
            pending.reset();
            error = QString::fromUtf8(e.what()).left(2048);
            state->setText(error);
            if (setupStatus)
                setupStatus->setText(error);
        }
    }
    QString configuredProvider() const {
        return QSettings("SketchyUp", "SketchyUp").value("assistant/provider", "OpenAI").toString();
    }
    bool usesPlan() const {
        return QSettings("SketchyUp", "SketchyUp")
                   .value("assistant/openaiAuth", "api")
                   .toString() == "chatgpt";
    }
    QString configuredModel() const {
        const QSettings s("SketchyUp", "SketchyUp");
        return configuredProvider() == "Ollama"
                   ? s.value("assistant/localModel", "qwen3:4b-instruct").toString()
                   : s.value(usesPlan() ? "assistant/chatgptModel" : "assistant/openaiModel")
                         .toString();
    }
    bool configured() const {
        return configuredProvider() != "None" && !configuredModel().isEmpty();
    }
    bool uncertain() const { return provider && provider->task().phase() == Phase::OutcomeUnknown; }
    bool busy() const { return pending || (provider && active(provider->task().phase())); }
    bool undoCurrent() const {
        if (!provider || provider->task().result().value("applied") != true || !document.canUndo())
            return false;
        const auto history = document.history(0, 1);
        return history.position &&
               document.history(history.position - 1, 1).entries.front().metadata.taskId ==
                   provider->task().result().value("taskId").toString().toStdString();
    }
    QJsonObject query(const QString &name) const {
        return {{"apiVersion", 1},
                {"documentId", QString::fromStdString(document.identity())},
                {"expectedRevision", QString::number(document.revision())},
                {"query", name}};
    }
    AssistantTask::Options options(const QString &prompt) const {
        AssistantTask::Options result;
        result.prompt = prompt;
        result.provider = configuredProvider();
        result.model = configuredModel();
        result.remote = result.provider == "OpenAI";
        result.remoteContextApproved =
            !result.remote ||
            QSettings("SketchyUp", "SketchyUp").value("assistant/consentOpenAI", false).toBool();
        result.clarificationAvailable = true;
        result.limits.seconds = 300;
        if (result.remote && usesPlan()) {
            result.limits.outputTokens = 8192;
            result.limits.totalReportedTokens = 262144;
        }
        if (selection->isChecked()) {
            int count{};
            for (auto entity : view.selectionState().entities()) {
                if (++count > 7)
                    break;
                const auto kind = entity.kind == SelectionKind::Body   ? "body"
                                  : entity.kind == SelectionKind::Face ? "face"
                                  : entity.kind == SelectionKind::Edge ? "edge"
                                                                       : "guide";
                auto request = query("entity.describe");
                request["target"] = inspectionReference(document, entity.body, kind, entity.entity);
                result.context.append(request);
            }
        }
        if (context->isChecked()) {
            const auto id = view.selectionState().context();
            auto request = query(id ? "entity.describe" : "document.describe");
            if (id)
                request["target"] = inspectionReference(document, id);
            result.context.append(request);
        }
        const QStringList routine{"assembly.room",
                                  "assembly.window.resize",
                                  "geometry.face",
                                  "geometry.rectangle",
                                  "geometry.circle",
                                  "geometry.polygon",
                                  "geometry.polyline",
                                  "geometry.wire",
                                  "geometry.extrude_isolated",
                                  "geometry.offset",
                                  "geometry.sweep",
                                  "geometry.intersect",
                                  "geometry.push_pull",
                                  "geometry.translate",
                                  "geometry.transform_selection",
                                  "entity.position",
                                  "entity.dimensions",
                                  "entity.properties",
                                  "group.create",
                                  "group.selection",
                                  "component.create",
                                  "component.instance",
                                  "component.make_unique",
                                  "component.edit_instance",
                                  "material.assign",
                                  "material.color",
                                  "material.create",
                                  "material.edit"};
        result.allowedCommands = routine;
        if (destructive->isChecked())
            result.allowedCommands.append({"geometry.delete", "geometry.erase_selection",
                                           "geometry.erase_face", "geometry.erase_edge",
                                           "component.replace", "group.explode"});
        if (shared->isChecked())
            result.allowedCommands.append("component.edit");
        return result;
    }
    void ensureSession() {
        if (session && document.owns(scope))
            return;
        provider.reset();
        session.reset();
        const auto root =
            services.outcomeRoot.isEmpty()
                ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                      .filePath("assistant-outcomes")
                : services.outcomeRoot;
        const auto path = QDir(root).filePath(QString::fromStdString(document.identity()) + "/" +
                                              QUuid::createUuid().toString(QUuid::WithoutBraces));
        session = std::make_unique<NativeAssistantSession>(
            document, path, &view.selectionState(), [this] { return view.inspectionBusy(); },
            services.transactions);
        scope = document.saveStamp();
    }
    void submit(const QString &prompt) {
        if (busy())
            throw std::runtime_error("Stop or resolve the current assistant task first.");
        if (!configured()) {
            showSetup();
            return;
        }
        if (prompt.trimmed().isEmpty() || prompt.size() > 4096)
            throw std::runtime_error("Enter a request of 1–4096 characters.");
        if (view.inspectionBusy())
            throw std::runtime_error("Finish the current modeling gesture first.");
        provider.reset();
        preview.reset();
        view.setAssistantPreview({});
        lastResult.clear();
        error.clear();
        pending = options(prompt);
        pendingScope = document.saveStamp();
        pendingDirect = mode->currentIndex() == 1;
        pendingProvider = pending->provider;
        pendingPlan = pending->remote && usesPlan();
        if (pending->remote && !pending->remoteContextApproved) {
            showDisclosure(true);
            refresh();
            return;
        }
        connectPending();
    }
    void connectPending() {
        if (!pending)
            return;
        if (!document.isCurrentSnapshot(pendingScope)) {
            pending.reset();
            throw std::runtime_error(
                "The model changed during setup. Send again on the current model.");
        }
        if (pendingPlan)
            chatgpt.prepare(
                QSettings("SketchyUp", "SketchyUp").value("assistant/chatgptAccount").toString(),
                pending->model);
        else if (pending->remote)
            credentials.lookup();
        else
            start({});
        refresh();
    }
    void start(QByteArray key) {
        if (!pending) {
            key.fill('\0');
            return;
        }
        if (!document.isCurrentSnapshot(pendingScope)) {
            pending.reset();
            key.fill('\0');
            throw std::runtime_error(
                "The model changed while connecting. Send again on the current model.");
        }
        ensureSession();
        auto task = std::make_unique<AssistantTask>(session->backend(), *pending);
        taskDirect = pendingDirect;
        taskPrompt = pending->prompt;
        if (pendingProvider == "OpenAI")
            provider = std::make_unique<OpenAiProvider>(std::move(task), key, services.network,
                                                        nullptr, pendingPlan);
        else {
            QSettings s("SketchyUp", "SketchyUp");
            OllamaConfiguration config;
            config.endpoint =
                QUrl(s.value("assistant/localEndpoint", "http://127.0.0.1:11434").toString());
            config.model = pending->model;
            config.contextTokens = s.value("assistant/localContext", 32768).toInt();
            config.threads = s.value("assistant/localThreads", 8).toInt();
            provider = std::make_unique<OllamaProvider>(std::move(task), config, services.network);
        }
        key.fill('\0');
        pending.reset();
        destructive->setChecked(false);
        shared->setChecked(false);
        QObject::connect(
            provider.get(), &AssistantNetworkProvider::changed, &owner,
            [this] { safe([this] { refresh(); }); }, Qt::QueuedConnection);
        provider->start();
        refresh();
    }
    void credentialChanged() {
        using P = OpenAiCredentialStore::Phase;
        const auto phase = credentials.phase();
        if (phase == P::Available) {
            start(credentials.takeCredential());
            return;
        }
        if (phase == P::Working || phase == P::Idle)
            return;
        if (setupStatus)
            setupStatus->setText(
                phase == P::Stored    ? "Credential stored in the OS credential facility."
                : phase == P::Cleared ? "Credential removed."
                                      : "Credential operation did not complete. Check that the OS "
                                        "credential facility is available and unlocked.");
        if (pending) {
            pending.reset();
            error = phase == P::Missing
                        ? "No OpenAI credential stored. Open Preferences to add one."
                        : "OpenAI credential unavailable. Check the OS credential facility and try "
                          "again.";
            refresh();
        }
    }
    void showDisclosure(bool grant) {
        if (consent) {
            if (!grant) {
                consent->raise();
                return;
            }
            // Replace an informational disclosure with the actual permission card.
            delete consent.data();
        }
        auto *dialog = new QDialog(&owner);
        consent = dialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName("assistantConsent");
        dialog->setWindowTitle("What is sent");
        auto *layout = new QVBoxLayout(dialog);
        layout->addWidget(label(
            "Your prompt, attached entity/context descriptions, and tool results are sent to the "
            "selected provider. Advertised tools can inspect bounded data elsewhere in this "
            "document, including names, hierarchy, geometry and properties. Removing an attachment "
            "omits its initial data; tools may still inspect the document and current selection. "
            "No screenshots or "
            "image assets are attached. Credentials are used for authentication, never included in "
            "model context. Text in models is treated as data."));
        const auto disclosed = provider ? provider->task().disclosure() : QJsonObject{};
        layout->addWidget(label((pending    ? pending->provider
                                 : provider ? disclosed.value("provider").toString()
                                            : configuredProvider()) +
                                " · " +
                                (pending    ? pending->model
                                 : provider ? disclosed.value("model").toString()
                                            : configuredModel())));
        if (provider && !pending)
            layout->addWidget(
                label("Current task initial queries: " +
                      QString::fromUtf8(QJsonDocument(disclosed.value("initialQueries").toArray())
                                            .toJson(QJsonDocument::Compact))));
        layout->addWidget(label(
            "Next request selection attachment: " +
            QString(selection->isChecked() ? "enabled (up to 7 entities)" : "off") +
            "\nEditing-context attachment: " + QString(context->isChecked() ? "enabled" : "off")));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
        if (grant) {
            auto *allow = buttons->addButton("Allow OpenAI requests", QDialogButtonBox::AcceptRole);
            allow->setObjectName("assistantConsentAllow");
            QObject::connect(allow, &QPushButton::clicked, &owner, [this, dialog] {
                safe([this, dialog] {
                    QSettings("SketchyUp", "SketchyUp").setValue("assistant/consentOpenAI", true);
                    if (pending)
                        pending->remoteContextApproved = true;
                    dialog->accept();
                    connectPending();
                });
            });
            QObject::connect(dialog, &QDialog::rejected, &owner, [this] {
                pending.reset();
                refresh();
            });
        }
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog->resize(440, 360);
        dialog->show();
    }
    void showSetup();
    void refresh();
    void showQuestion(const QJsonObject &card);
    void showPreview(const QJsonObject &result);
    void inspect(int row);
    void apply() {
        if (!provider)
            return;
        if (view.inspectionBusy() || QApplication::activeModalWidget())
            throw std::runtime_error("Finish the current gesture or dialog before applying.");
        const auto before = document.revision();
        provider->apply();
        if (document.revision() != before) {
            view.refresh();
            emit owner.modelChanged();
        }
        refresh();
    }
    void reconcile() {
        if (!provider)
            return;
        const auto before = document.revision();
        provider->reconcile();
        if (document.revision() != before) {
            view.refresh();
            emit owner.modelChanged();
        }
        refresh();
    }
    void stop() {
        pending.reset();
        credentials.cancel();
        if (chatgpt.busy())
            chatgpt.cancel();
        if (consent)
            consent->reject();
        if (provider)
            provider->cancel();
        preview.reset();
        view.setAssistantPreview({});
        error.clear();
        refresh();
    }
    void closeSession() {
        if (uncertain())
            throw std::runtime_error(
                "Reconcile the assistant outcome before replacing or closing this model.");
        stop();
        if (uncertain())
            throw std::runtime_error("Assistant cancellation needs outcome reconciliation.");
        provider.reset();
        if (session)
            session->close();
        session.reset();
        lastResult.clear();
        refresh();
    }
};
void AssistantPanel::Impl::showSetup() {
    if (busy())
        throw std::runtime_error(
            "Stop or finish the current task before changing provider settings.");
    if (setup) {
        setup->raise();
        return;
    }
    auto *dialog = new QDialog(&owner);
    setup = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("assistantPreferencesDialog");
    dialog->setWindowTitle("Assistant preferences");
    auto *outer = new QVBoxLayout(dialog);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *body = new QWidget;
    auto *form = new QFormLayout(body);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *providerChoice = new QComboBox;
    providerChoice->setObjectName("assistantProviderChoice");
    providerChoice->addItems({"OpenAI", "Ollama", "None"});
    providerChoice->setCurrentText(configuredProvider());
    auto *openaiModel = new QLineEdit;
    openaiModel->setObjectName("assistantOpenAIModel");
    openaiModel->setMaxLength(128);
    auto *localModel = new QLineEdit;
    localModel->setObjectName("assistantLocalModel");
    localModel->setMaxLength(128);
    auto *endpoint = new QLineEdit;
    endpoint->setObjectName("assistantLocalEndpoint");
    endpoint->setMaxLength(256);
    auto *tokens = new QSpinBox;
    tokens->setRange(4096, 65536);
    auto *threads = new QSpinBox;
    threads->setRange(1, 32);
    QSettings s("SketchyUp", "SketchyUp");
    openaiModel->setText(s.value("assistant/openaiModel").toString());
    localModel->setText(s.value("assistant/localModel", "qwen3:4b-instruct").toString());
    endpoint->setText(s.value("assistant/localEndpoint", "http://127.0.0.1:11434").toString());
    tokens->setValue(s.value("assistant/localContext", 32768).toInt());
    threads->setValue(s.value("assistant/localThreads", 8).toInt());
    form->addRow("Provider", providerChoice);
    auto *authChoice = new QComboBox;
    authChoice->setObjectName("assistantOpenAIAuth");
    authChoice->addItem("ChatGPT subscription", "chatgpt");
    authChoice->addItem("API key (separate API billing)", "api");
    authChoice->setCurrentIndex(usesPlan() ? 0 : 1);
    form->addRow("OpenAI connection", authChoice);
    auto *planCard = new QWidget;
    auto *planLayout = new QVBoxLayout(planCard);
    planLayout->setContentsMargins(0, 0, 0, 0);
    planLayout->addWidget(
        label("Use your eligible ChatGPT plan for assistant requests. Sign in in your browser; the "
              "session is stored in the OS credential facility."));
    auto *accounts = new QComboBox;
    accounts->setObjectName("assistantChatGPTAccount");
    auto fillAccounts = [this, accounts](const QString &selected) {
        QSignalBlocker block(accounts);
        accounts->clear();
        for (const auto &entry : chatgpt.accounts()) {
            const auto item = entry.toObject();
            const auto client = item.value("client_id").toString();
            accounts->addItem(item.value("email").toString() + " · " + client, client);
        }
        accounts->addItem("Add another account or workspace…", QString{});
        accounts->setCurrentIndex(std::max(0, accounts->findData(selected)));
    };
    fillAccounts(s.value("assistant/chatgptAccount").toString());
    planLayout->addWidget(label("Each task is limited to five minutes, 16 provider turns and "
                                "262,144 reported tokens, including cached input."));
    planLayout->addWidget(accounts);
    auto *signIn = button("Continue with ChatGPT", "assistantChatGPTSignIn");
    auto *signOut = button("Sign out", "assistantChatGPTSignOut");
    auto *models = new QComboBox;
    models->setObjectName("assistantChatGPTModel");
    const auto previousModel = s.value("assistant/chatgptModel").toString();
    if (!previousModel.isEmpty())
        models->addItem(previousModel + " (refresh to verify)", previousModel);
    auto *reload = button("Refresh available models", "assistantChatGPTModels");
    auto *cancelSignIn = button("Cancel sign-in / connection", "assistantChatGPTCancel");
    auto *planStatus = label(chatgpt.status(), "assistantChatGPTStatus");
    for (QWidget *w : std::initializer_list<QWidget *>{signIn, signOut, models, reload,
                                                       cancelSignIn, planStatus})
        planLayout->addWidget(w);
    auto *usage = button("Manage ChatGPT usage", "assistantChatGPTUsage");
    planLayout->addWidget(usage);
    QObject::connect(usage, &QPushButton::clicked, dialog,
                     [] { QDesktopServices::openUrl(QUrl("https://chatgpt.com/settings/usage")); });
    auto updatePlan = [this, accounts, signIn, signOut, models, reload, cancelSignIn, planStatus] {
        const bool working = chatgpt.busy();
        accounts->setEnabled(!working);
        signIn->setEnabled(!working);
        signOut->setEnabled(!working && !accounts->currentData().toString().isEmpty());
        reload->setEnabled(!working && !accounts->currentData().toString().isEmpty());
        models->setEnabled(!working);
        cancelSignIn->setEnabled(working);
        planStatus->setText(chatgpt.status());
        if (!working && !chatgpt.models().isEmpty()) {
            const auto old = models->currentData().toString();
            models->clear();
            for (const auto &entry : chatgpt.models()) {
                const auto m = entry.toObject();
                models->addItem(m.value("display_name").toString() + " · " +
                                    m.value("slug").toString(),
                                m.value("slug").toString());
            }
            auto index = models->findData(old);
            if (index < 0)
                index = models->findData("gpt-6.1-sol");
            models->setCurrentIndex(std::max(0, index));
        }
    };
    QObject::connect(&chatgpt, &ChatGptAuth::changed, dialog, updatePlan);
    QObject::connect(&chatgpt, &ChatGptAuth::registrationSaved, dialog, fillAccounts);
    QObject::connect(&chatgpt, &ChatGptAuth::connected, dialog,
                     [this, dialog, fillAccounts, updatePlan](const QString &client) {
                         fillAccounts(client);
                         updatePlan();
                         QSettings prefs("SketchyUp", "SketchyUp");
                         if (!prefs.value("assistant/chatgptWelcomed", false).toBool()) {
                             prefs.setValue("assistant/chatgptWelcomed", true);
                             auto *welcome = new QMessageBox(
                                 QMessageBox::Information, "You're using your ChatGPT plan",
                                 "Eligible assistant requests use your ChatGPT plan. You can "
                                 "manage SketchyUp's access and usage in ChatGPT Settings.",
                                 QMessageBox::Ok, dialog);
                             welcome->setAttribute(Qt::WA_DeleteOnClose);
                             welcome->button(QMessageBox::Ok)->setText("Got it");
                             welcome->open();
                         }
                     });
    QObject::connect(signIn, &QPushButton::clicked, dialog,
                     [this, accounts] { chatgpt.signIn(accounts->currentData().toString()); });
    QObject::connect(signOut, &QPushButton::clicked, dialog, [this, accounts, models] {
        models->clear();
        chatgpt.signOut(accounts->currentData().toString());
    });
    QObject::connect(reload, &QPushButton::clicked, dialog,
                     [this, accounts] { chatgpt.loadModels(accounts->currentData().toString()); });
    QObject::connect(cancelSignIn, &QPushButton::clicked, dialog, [this] { chatgpt.cancel(); });
    QObject::connect(accounts, &QComboBox::currentIndexChanged, dialog, [models, updatePlan] {
        models->clear();
        updatePlan();
        models->clear();
    });
    QObject::connect(dialog, &QDialog::finished, &owner, [this] {
        if (chatgpt.busy())
            chatgpt.cancel();
    });
    updatePlan();
    form->addRow(planCard);
    form->addRow("OpenAI model ID", openaiModel);
    auto *key = new QLineEdit;
    key->setObjectName("assistantCredential");
    key->setEchoMode(QLineEdit::Password);
    key->setMaxLength(4096);
    key->setPlaceholderText("Enter a new key to store it");
    form->addRow("OpenAI API key", key);
    auto *store = button("Store key in OS credential facility", "assistantStoreCredential");
    form->addRow(store);
    auto *clear = button("Remove stored OpenAI key", "assistantClearCredential");
    form->addRow(clear);
    setupStatus = label("Credentials are never stored in preferences or documents.",
                        "assistantCredentialStatus");
    form->addRow(setupStatus);
    QObject::connect(store, &QPushButton::clicked, dialog, [this, key] {
        safe([this, key] {
            auto value = key->text().toUtf8();
            key->clear();
            credentials.store(std::move(value));
        });
    });
    QObject::connect(clear, &QPushButton::clicked, dialog,
                     [this] { safe([this] { credentials.clear(); }); });
    QObject::connect(dialog, &QObject::destroyed, &owner, [this] { setupStatus = nullptr; });
    form->addRow("Local endpoint", endpoint);
    form->addRow("Local model", localModel);
    form->addRow("Context tokens", tokens);
    form->addRow("CPU threads", threads);
    form->addRow(
        label("Experimental local profile: Ollama 0.35.1, qwen3:4b-instruct, 32,768 context "
              "tokens, 8 CPU threads. The measured CPU corpus did not complete within five "
              "minutes. No automatic installation, model download or cloud fallback."));
    auto updateAuth = [form, authChoice, planCard, openaiModel, key, store, clear] {
        const bool plan = authChoice->currentData() == "chatgpt";
        planCard->setVisible(plan);
        form->setRowVisible(openaiModel, !plan);
        form->setRowVisible(key, !plan);
        store->setVisible(!plan);
        clear->setVisible(!plan);
    };
    QObject::connect(authChoice, &QComboBox::currentIndexChanged, dialog, updateAuth);
    updateAuth();
    scroll->setWidget(body);
    outer->addWidget(scroll);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(
        buttons, &QDialogButtonBox::accepted, dialog,
        [this, dialog, providerChoice, openaiModel, localModel, endpoint, tokens, threads,
         authChoice, accounts, models] {
            safe([=, this] {
                if (busy() || chatgpt.busy())
                    throw std::runtime_error("Finish the current task before saving settings.");
                if (providerChoice->currentText() == "OpenAI" &&
                    authChoice->currentData() == "api" &&
                    !QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$")
                         .match(openaiModel->text())
                         .hasMatch())
                    throw std::runtime_error("Enter the explicit OpenAI model ID to use.");
                if (providerChoice->currentText() == "Ollama")
                    validateOllamaConfiguration({QUrl(endpoint->text()), localModel->text(),
                                                 tokens->value(), threads->value()});
                if (providerChoice->currentText() == "OpenAI" &&
                    authChoice->currentData() == "chatgpt" &&
                    (accounts->currentData().toString().isEmpty() ||
                     models->currentData().toString().isEmpty()))
                    throw std::runtime_error(
                        "Connect a ChatGPT account and select an available model.");
                QSettings prefs("SketchyUp", "SketchyUp");
                prefs.setValue("assistant/openaiAuth", authChoice->currentData());
                prefs.setValue("assistant/chatgptAccount", accounts->currentData());
                prefs.setValue("assistant/chatgptModel", models->currentData());
                prefs.setValue("assistant/provider", providerChoice->currentText());
                prefs.setValue("assistant/openaiModel", openaiModel->text());
                prefs.setValue("assistant/localModel", localModel->text());
                prefs.setValue("assistant/localEndpoint", endpoint->text());
                prefs.setValue("assistant/localContext", tokens->value());
                prefs.setValue("assistant/localThreads", threads->value());
                error.clear();
                dialog->accept();
                refresh();
            });
        });
    dialog->resize(500, 620);
    dialog->show();
}
void AssistantPanel::Impl::showQuestion(const QJsonObject &card) {
    const auto id = card.value("id").toString();
    if (id == shownQuestion)
        return;
    shownQuestion = id;
    while (auto *item = questionLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    questionCard->setVisible(!id.isEmpty());
    if (id.isEmpty())
        return;
    questionLayout->addWidget(label(card.value("question").toString()));
    for (const auto &value : card.value("choices").toArray()) {
        const auto choice = value.toObject();
        auto *answer = button(QString(choice.value("label").toString()).replace("&", "&&"),
                              "assistantAnswer_" + choice.value("id").toString());
        questionLayout->addWidget(answer);
        QObject::connect(answer, &QPushButton::clicked, &owner, [this, id, choice] {
            safe([this, id, choice] {
                provider->answer(id, choice.value("id").toString());
                refresh();
            });
        });
    }
    if (card.value("allowFreeText") == true) {
        auto *text = new QLineEdit;
        text->setObjectName("assistantAnswerText");
        text->setMaxLength(1024);
        questionLayout->addWidget(text);
        auto *answer = button("Answer", "assistantAnswerTextSend");
        questionLayout->addWidget(answer);
        QObject::connect(answer, &QPushButton::clicked, &owner, [this, id, text] {
            safe([this, id, text] {
                provider->answer(id, {}, text->text());
                refresh();
            });
        });
        auto *pick = button("Use current viewport selection", "assistantAnswerPick");
        questionLayout->addWidget(pick);
        QObject::connect(pick, &QPushButton::clicked, &owner, [this, id] {
            safe([this, id] {
                QJsonArray selected;
                for (auto entity : view.selectionState().entities()) {
                    const auto kind = entity.kind == SelectionKind::Body   ? "body"
                                      : entity.kind == SelectionKind::Face ? "face"
                                      : entity.kind == SelectionKind::Edge ? "edge"
                                                                           : "guide";
                    selected.append(
                        inspectionReference(document, entity.body, kind, entity.entity));
                    if (selected.size() > 4)
                        throw std::runtime_error(
                            "Pick at most four targets to answer this question.");
                }
                if (selected.empty()) {
                    emit owner.focusViewport();
                    throw std::runtime_error(
                        "Select a target in the viewport, then use the selection to answer.");
                }
                const auto text =
                    QString::fromUtf8(QJsonDocument(selected).toJson(QJsonDocument::Compact));
                provider->answer(id, {}, text);
                refresh();
            });
        });
    }
}
void AssistantPanel::Impl::showPreview(const QJsonObject &result) {
    const auto next = session->previewEdit(result.value("preview").toObject());
    if (preview == next)
        return;
    preview = next;
    view.setAssistantPreview(preview);
    const auto &after = preview->snapshot();
    changes->clear();
    changedBodies.clear();
    measure->clear();
    int added{}, removed{}, updated{}, unchanged{};
    auto row = [&](Id id, const QString &action, const Body &body) {
        if (changedBodies.size() >= 200)
            return;
        changedBodies.push_back(id);
        changes->addItem(action + " #" + QString::number(id) + " · " +
                         QString::fromStdString(body.name).left(160));
    };
    for (const auto &[id, before] : document.bodies()) {
        if (!after.bodies().contains(id)) {
            ++removed;
            row(id, "Remove", *before);
        } else if (*before != *after.bodies().at(id) ||
                   document.worldTransform(id) != after.worldTransform(id)) {
            ++updated;
            row(id, "Change", *after.bodies().at(id));
        } else
            ++unchanged;
    }
    for (const auto &[id, body] : after.bodies())
        if (!document.bodies().contains(id)) {
            ++added;
            row(id, "Add", *body);
        }
    summary->setText(QString("Proposed change · revision %1\n%2 added · %3 changed · %4 removed "
                             "objects\n%5 objects verified unchanged.")
                         .arg(document.revision())
                         .arg(added)
                         .arg(updated)
                         .arg(removed)
                         .arg(unchanged));
    summary->setToolTip(
        "Counts compare body records and world transforms. Up to 200 changed "
        "objects are listed. Measurements include descendants and hidden geometry.");
    if (document.materials() != after.materials() || document.tags() != after.tags() ||
        document.definitions() != after.definitions() || document.assets() != after.assets())
        summary->setText(summary->text() +
                         "\nMaterials, tags, definitions or assets also change. Review validated "
                         "commands in raw activity; hatching covers geometry only.");
    changes->setFixedHeight(std::min(140, std::max(38, int(changedBodies.size()) * 36 + 4)));
    changes->setVisible(!changedBodies.empty());
    if (!changedBodies.empty())
        changes->setCurrentRow(0);
}
void AssistantPanel::Impl::inspect(int row) {
    if (!preview || !document.canApply(*preview) || row < 0 || size_t(row) >= changedBodies.size())
        return;
    const auto id = changedBodies[size_t(row)];
    const auto &after = preview->snapshot();
    auto description = [&](const Document &source) {
        return source.bodies().contains(id)
                   ? entityDescription(source, {id, SelectionKind::Body, 0})
                   : QJsonObject{};
    };
    const auto before = description(document), proposed = description(after);
    auto dimensions = [&](const QJsonObject &value) {
        if (value.isEmpty())
            return QString("absent");
        const auto d = value.value("world")
                           .toObject()
                           .value("bounds")
                           .toObject()
                           .value("dimensions")
                           .toArray();
        if (d.size() != 3)
            return QString("no finite bounds");
        return displayLength(d[0].toDouble(), document.displayUnits()) + " × " +
               displayLength(d[1].toDouble(), document.displayUnits()) + " × " +
               displayLength(d[2].toDouble(), document.displayUnits());
    };
    auto quantity = [&](const QJsonObject &value, const char *name, int power) {
        const auto number = value.value("world").toObject().value(name);
        return number.isDouble() ? displayMeasure(number.toDouble(), power, document.displayUnits())
                                 : QString("not available");
    };
    measure->setText(
        "Measured preview #" + QString::number(id) + "\nWorld bounds: " + dimensions(before) +
        " → " + dimensions(proposed) + "\nArea: " + quantity(before, "area", 2) + " → " +
        quantity(proposed, "area", 2) + "\nSolid volume: " + quantity(before, "volume", 3) + " → " +
        quantity(proposed, "volume", 3));
    view.setAssistantPreviewFocus(id);
    raw->setPlainText(json({{"task", provider->task().result()},
                            {"activity", provider->task().transcript()},
                            {"before", before},
                            {"proposed", proposed}}));
}
void AssistantPanel::Impl::refresh() {
    if (refreshing)
        return;
    refreshing = true;
    struct Reset {
        bool &value;
        ~Reset() { value = false; }
    } reset{refreshing};
    // Validate a waiting task before acquiring a display handle. This preserves
    // the engine's Stale state instead of turning a human edit into Discard.
    if (provider && (provider->task().phase() == Phase::PreviewReady ||
                     provider->task().phase() == Phase::AwaitingClarification))
        (void)provider->task().nextRequest();
    const bool unknown = uncertain();
    if (fenced != unknown) {
        fenced = unknown;
        emit owner.fenceChanged(fenced);
    }
    const bool working = busy();
    const bool ready = configured();
    manageUsage->setVisible(configuredProvider() == "OpenAI" && usesPlan());
    setupCard->setVisible(!ready);
    taskControls->setVisible(ready || provider != nullptr || !error.isEmpty());
    composer->setEnabled(ready && !working);
    send->setEnabled(ready && !working);
    settings->setEnabled(!working);
    stopButton->setEnabled(working && !unknown);
    mode->setEnabled(!working);
    selection->setEnabled(!working);
    context->setEnabled(!working);
    destructive->setEnabled(!working);
    shared->setEnabled(!working);
    for (auto *widget :
         std::initializer_list<QWidget *>{composer, mode, selection, context, destructive, shared,
                                          send, stopButton, contextLabel})
        widget->setVisible(ready && !working);
    contextLabel->setVisible(ready && !working);
    stopButton->setVisible(working && !unknown);
    contextLabel->setText(view.selectionSummary() +
                          QString(" · editing context #%1").arg(view.selectionState().context()));
    const auto disclosure = provider ? provider->task().disclosure() : QJsonObject{};
    const auto name = provider ? disclosure.value("provider").toString() : configuredProvider();
    const auto model = provider ? disclosure.value("model").toString() : configuredModel();
    chip->setText(!ready && !provider
                      ? "Assistant · no provider configured"
                      : (name == "OpenAI" ? "Remote · " : "Local · ") + name + " / " +
                            (model.isEmpty() ? "not configured" : model) +
                            (name == "OpenAI" && usesPlan() ? " · Using ChatGPT plan" : ""));
    requestLabel->setText(provider ? "You: " + taskPrompt : QString{});
    auto result = provider ? provider->task().result() : QJsonObject{};
    const auto phase = provider ? provider->task().phase() : Phase::Ready;
    const bool staged = provider && phase == Phase::PreviewReady;
    applyButton->setVisible(staged);
    discard->setVisible(staged);
    stopButton->setVisible(working && !unknown && !staged);
    refine->setVisible(provider && !unknown);
    applyButton->setEnabled(staged);
    discard->setEnabled(staged);
    refine->setEnabled(provider && !unknown);
    reconcileButton->setVisible(unknown);
    undo->setVisible(result.value("applied") == true);
    undo->setEnabled(undoCurrent());
    changes->setVisible(staged && !changedBodies.empty());
    summary->setVisible(staged);
    measure->setVisible(staged);
    showQuestion(phase == Phase::AwaitingClarification ? result.value("clarification").toObject()
                                                       : QJsonObject{});
    if (staged) {
        try {
            showPreview(result);
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
            provider->cancel();
            preview.reset();
            view.setAssistantPreview({});
            refreshing = false;
            refresh();
            return;
        }
    } else if (preview) {
        preview.reset();
        view.setAssistantPreview({});
    }
    banner->setVisible(view.hasAssistantPreview());
    banner->setGeometry(8, 64, std::max(200, view.width() - 16), 50);
    banner->raise();
    bannerApply->setEnabled(staged);
    bannerDiscard->setEnabled(staged);
    QString status = "Ready";
    if (pending)
        status = consent ? "Review what will be sent before connecting."
                         : "Retrieving the OpenAI credential from the OS facility…";
    else if (provider)
        switch (phase) {
        case Phase::Ready:
        case Phase::AwaitingProvider:
        case Phase::Backoff:
            status = provider->status();
            break;
        case Phase::AwaitingClarification:
            status = "Answer the question to continue. The task deadline still applies.";
            break;
        case Phase::PreviewReady:
            status = "Preview — not in your model yet. Apply adds one undo entry.";
            break;
        case Phase::Completed:
            status = result.value("applied") == true ? "Change committed · one undo entry."
                                                     : "Response complete. No edit was applied.";
            break;
        case Phase::Canceled:
            status = "Task stopped. No edit was committed.";
            break;
        case Phase::Stale:
            status = "The model changed while this was prepared. Nothing was applied. Re-plan on "
                     "the current model.";
            break;
        case Phase::Failed:
            status = "Task failed. No edit was committed. " +
                     result.value("error").toObject().value("message").toString();
            break;
        case Phase::OutcomeUnknown:
            status = "Outcome unknown. Reconcile before editing or closing this model.";
            break;
        }
    state->setText(error.isEmpty() ? status : error + "\n" + status);
    const auto serialized = json(result);
    if (serialized != lastResult) {
        lastResult = serialized;
        explanation->setPlainText(result.value("unverifiedModelText").toString());
        QStringList steps;
        if (provider) {
            QMap<QString, QString> calls;
            for (const auto &entry : provider->task().transcript()) {
                const auto message = entry.toObject();
                if (message.value("role") == "assistant")
                    for (const auto &call : message.value("toolCalls").toArray())
                        calls[call.toObject().value("id").toString()] =
                            call.toObject().value("name").toString();
                if (message.value("role") == "tool")
                    steps.append((message.value("isError") == true ? "Rejected: " : "✓ ") +
                                 activityName(calls.value(message.value("callId").toString())));
            }
            raw->setPlainText(
                json({{"task", result}, {"activity", provider->task().transcript()}}));
        }
        activity->setPlainText(steps.join('\n'));
        if (staged && preview)
            inspect(changes->currentRow());
    }
    activityToggle->setVisible(!activity->toPlainText().isEmpty());
    activity->setVisible(activityToggle->isChecked() && !activity->toPlainText().isEmpty());
    explanation->setVisible(!explanation->toPlainText().isEmpty());
    explanationHeading->setVisible(!explanation->toPlainText().isEmpty());
    if (staged && preview && taskDirect && !QApplication::activeModalWidget() &&
        !view.inspectionBusy()) {
        taskDirect = false;
        const auto id = result.value("taskId");
        QTimer::singleShot(0, &owner, [this, id] {
            safe([this, id] {
                if (provider && provider->task().result().value("taskId") == id &&
                    provider->task().phase() == Phase::PreviewReady)
                    apply();
            });
        });
    }
}
AssistantPanel::AssistantPanel(Document &document, Viewport &view, HostServices services,
                               QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this, document, view, std::move(services))) {}
AssistantPanel::~AssistantPanel() = default;
void AssistantPanel::showSetup() {
    impl_->safe([this] { impl_->showSetup(); });
}
void AssistantPanel::focusComposer() {
    if (impl_->uncertain())
        impl_->reconcileButton->setFocus();
    else if (impl_->composer->isEnabled())
        impl_->composer->setFocus();
    else if (impl_->questionCard->isVisible()) {
        for (auto *choice : impl_->questionCard->findChildren<QPushButton *>())
            if (choice->isVisible() && choice->isEnabled()) {
                choice->setFocus();
                break;
            }
    } else if (impl_->applyButton->isEnabled())
        impl_->applyButton->setFocus();
    else if (impl_->stopButton->isEnabled())
        impl_->stopButton->setFocus();
    else
        impl_->settings->setFocus();
}
void AssistantPanel::refresh() {
    impl_->safe([this] { impl_->refresh(); });
}
bool AssistantPanel::uncertain() const { return impl_->uncertain(); }
void AssistantPanel::closeSession() { impl_->closeSession(); }
void AssistantPanel::submit(const QString &prompt) { impl_->submit(prompt); }
void AssistantPanel::apply() { impl_->apply(); }
void AssistantPanel::stop() { impl_->stop(); }
void AssistantPanel::reconcile() { impl_->reconcile(); }
QJsonObject AssistantPanel::result() const {
    return impl_->provider ? impl_->provider->task().result() : QJsonObject{};
}
} // namespace sketchy
