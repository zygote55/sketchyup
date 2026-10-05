#include "integrations/provider_transport.hpp"
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPointer>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray json(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
QJsonObject parse(const QByteArray &bytes) {
    checkAutomationDepth(bytes);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(),
            "Invalid provider JSON");
    return document.object();
}
bool running(AssistantTask::Phase phase) {
    return phase == AssistantTask::Phase::Ready ||
           phase == AssistantTask::Phase::AwaitingProvider ||
           phase == AssistantTask::Phase::Backoff || phase == AssistantTask::Phase::PreviewReady;
}
} // namespace
struct AssistantNetworkProvider::Impl {
    AssistantNetworkProvider &owner;
    std::unique_ptr<AssistantTask> task;
    Protocol protocol;
    QByteArray bytes;
    QJsonObject pendingRequest;
    bool probing{};
    size_t preflightIndex{};
    QNetworkAccessManager *manager;
    QPointer<QNetworkReply> pending;
    QTimer poll, deadline;
    QString attempt, status{"Ready"};
    bool started{}, processing{};
    AssistantTask::Phase last;
    Impl(AssistantNetworkProvider &owner, std::unique_ptr<AssistantTask> task, Protocol protocol,
         QNetworkAccessManager *manager)
        : owner(owner), task(std::move(task)), protocol(std::move(protocol)), manager(manager),
          poll(&owner), deadline(&owner), last(AssistantTask::Phase::Ready) {
        require(bool(this->task), "Provider requires a task");
        const auto disclosure = this->task->disclosure();
        require(disclosure.value("remote") == this->protocol.remote &&
                    disclosure.value("provider") == this->protocol.provider,
                "Provider task does not match its disclosure policy");
        require(this->protocol.request && this->protocol.decode && this->protocol.accepted &&
                    this->protocol.timeoutMs >= 1000 && this->protocol.timeoutMs <= 120000,
                "Invalid provider protocol");
        const auto &key = this->protocol.key;
        require(!this->protocol.remote ||
                    (key.size() >= 8 && key.size() <= 4096 && !key.contains('\r') &&
                     !key.contains('\n') && !key.contains('\0')),
                "Configure a valid API credential");
        require(this->protocol.remote || key.isEmpty(),
                "Local provider must not receive a credential");
        if (!this->manager)
            this->manager = new QNetworkAccessManager(&owner);
        if (!this->protocol.remote)
            this->manager->setProxy(QNetworkProxy::NoProxy);
        require(this->manager->thread() == owner.thread(),
                "Provider network manager must share owner thread");
        poll.setInterval(20);
        deadline.setSingleShot(true);
        QObject::connect(&poll, &QTimer::timeout, &owner, [this] { guarded([this] { pump(); }); });
        QObject::connect(&deadline, &QTimer::timeout, &owner, [this] {
            guarded([this] {
                fail(AssistantTask::ProviderFailure::Timeout, "Provider request timed out");
            });
        });
    }
    ~Impl() {
        abort();
        protocol.key.fill('\0');
    }
    void checkOwner() const {
        require(QThread::currentThread() == owner.thread(), "Provider requires owner thread");
    }
    void abort() {
        deadline.stop();
        if (pending) {
            auto *reply = pending.data();
            pending = nullptr;
            QObject::disconnect(reply, nullptr, &owner, nullptr);
            reply->abort();
            reply->deleteLater();
        }
        bytes.clear();
    }
    void notify() {
        last = task->phase();
        emit owner.changed();
    }
    void fail(AssistantTask::ProviderFailure failure, QString message, int retryAfter = 0) {
        abort();
        task->providerFailed(attempt, failure, retryAfter);
        status = std::move(message);
        notify();
    }
    void guarded(const std::function<void()> &operation) {
        try {
            operation();
        } catch (const std::exception &) {
            abort();
            poll.stop();
            task->cancel();
            status = "Local document unavailable; provider task stopped";
            notify();
        }
    }
    void drain() {
        if (!pending)
            return;
        const auto chunk = pending->readAll();
        if (chunk.size() > 1024 * 1024 - bytes.size()) {
            fail(AssistantTask::ProviderFailure::Fatal,
                 "Provider response exceeded its byte limit");
            return;
        }
        bytes += chunk;
    }
    void finished() {
        if (!pending)
            return;
        drain();
        if (!pending)
            return;
        auto *reply = pending.data();
        const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool delayValid{};
        const auto seconds = reply->rawHeader("Retry-After").toInt(&delayValid);
        const int retryAfter = delayValid ? std::clamp(seconds, 0, 10) * 1000 : 0;
        if (code == 429) {
            fail(AssistantTask::ProviderFailure::RateLimited,
                 "Provider rate limit; retrying within the task budget", retryAfter);
            return;
        }
        if (code == 408 || code == 504) {
            fail(AssistantTask::ProviderFailure::Timeout, "Provider request timed out");
            return;
        }
        if (code >= 500 || (!code && reply->error() != QNetworkReply::NoError)) {
            fail(AssistantTask::ProviderFailure::Unavailable, "Provider connection unavailable");
            return;
        }
        if (code != 200 || reply->error() != QNetworkReply::NoError) {
            fail(AssistantTask::ProviderFailure::Fatal,
                 code == 401 || code == 403
                     ? "Provider rejected the credential or account access"
                     : "Provider rejected the request; check model configuration");
            return;
        }
        const auto response = bytes;
        abort();
        if (probing) {
            try {
                const auto &probe = protocol.preflights.at(preflightIndex);
                require(bool(probe.validate), "Missing model capability validation");
                probe.validate(parse(response));
                ++preflightIndex;
            } catch (const std::exception &) {
                fail(AssistantTask::ProviderFailure::Fatal,
                     "Local runtime or model does not support the configured profile");
                return;
            }
            task->nextRequest();
            if (task->phase() == AssistantTask::Phase::AwaitingProvider)
                prepare();
            else
                notify();
            return;
        }
        try {
            const auto translated = protocol.decode(response);
            if (task->accept(attempt, translated))
                protocol.accepted();
            status = "Provider response received";
            notify();
        } catch (const std::exception &) {
            task->providerFailed(attempt, AssistantTask::ProviderFailure::Fatal);
            status = "Provider returned an incomplete or invalid response";
            notify();
        }
    }
    void send(const QByteArray &body, const QUrl &endpoint, int timeout, bool get = false) {
        require(body.size() <= 8 * 1024 * 1024, "Provider request exceeds wire budget");
        QNetworkRequest network(endpoint);
        network.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if (protocol.remote)
            network.setRawHeader("Authorization", "Bearer " + protocol.key);
        network.setRawHeader("Accept", "application/json");
        network.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        network.setTransferTimeout(timeout);
        pending = get ? manager->get(network) : manager->post(network, body);
        bytes.clear();
        pending->setReadBufferSize(1024 * 1024 + 1);
        QObject::connect(pending, &QNetworkReply::readyRead, &owner,
                         [this] { guarded([this] { drain(); }); });
        QObject::connect(pending, &QNetworkReply::finished, &owner,
                         [this] { guarded([this] { finished(); }); });
        deadline.start(timeout);
        status = probing ? "Checking local model capabilities" : "Waiting for " + protocol.provider;
        notify();
    }
    void prepare() {
        try {
            probing = preflightIndex < protocol.preflights.size();
            if (probing) {
                const auto &probe = protocol.preflights[preflightIndex];
                send(probe.body ? json(*probe.body) : QByteArray{}, probe.endpoint, 20000,
                     !probe.body.has_value());
            } else {
                send(json(protocol.request(pendingRequest)), protocol.endpoint, protocol.timeoutMs);
            }
        } catch (const std::exception &) {
            fail(AssistantTask::ProviderFailure::Fatal,
                 "Provider request could not be prepared within configured limits");
        }
    }
    void pump() {
        checkOwner();
        if (processing)
            return;
        processing = true;
        const auto reset = qScopeGuard([this] { processing = false; });
        auto request =
            task->nextRequest(); // Also checks revision/deadline while a request is in flight.
        if (!running(task->phase())) {
            abort();
            poll.stop();
        }
        if (task->phase() != last)
            notify();
        if (!request || task->phase() != AssistantTask::Phase::AwaitingProvider)
            return;
        attempt = request->value("attemptId").toString();
        pendingRequest = *request;
        prepare();
    }
};
AssistantNetworkProvider::AssistantNetworkProvider(std::unique_ptr<AssistantTask> task,
                                                   Protocol protocol,
                                                   QNetworkAccessManager *manager, QObject *parent)
    : QObject(parent),
      impl_(std::make_unique<Impl>(*this, std::move(task), std::move(protocol), manager)) {}
AssistantNetworkProvider::~AssistantNetworkProvider() = default;
void AssistantNetworkProvider::start() {
    impl_->checkOwner();
    require(!impl_->started, "Provider task can start only once");
    impl_->started = true;
    impl_->poll.start();
    impl_->guarded([this] { impl_->pump(); });
}
void AssistantNetworkProvider::cancel() {
    impl_->checkOwner();
    impl_->task->cancel();
    impl_->abort();
    impl_->poll.stop();
    impl_->status = "Provider request stopped";
    impl_->notify();
}
void AssistantNetworkProvider::apply() {
    impl_->checkOwner();
    impl_->task->apply();
    impl_->notify();
}
void AssistantNetworkProvider::reconcile() {
    impl_->checkOwner();
    impl_->task->reconcile();
    impl_->notify();
}
AssistantTask &AssistantNetworkProvider::task() {
    impl_->checkOwner();
    return *impl_->task;
}
QString AssistantNetworkProvider::status() const {
    impl_->checkOwner();
    return impl_->status;
}
} // namespace sketchy
