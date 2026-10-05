#include "integrations/chatgpt_auth.hpp"
#include "integrations/credential_store.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <functional>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/rand.h>
#include <optional>
namespace sketchy {
namespace {
constexpr auto issuer = "https://auth.openai.com";
constexpr auto tokenEndpoint = "https://auth.openai.com/api/accounts/oauth/token";
constexpr auto resource = "https://api.openai.com/v1";
void require(bool ok, const char *message = "ChatGPT sign-in could not be verified. Try again.") {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray json(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
QJsonObject parse(const QByteArray &bytes) {
    require(bytes.size() <= 1024 * 1024);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject());
    return doc.object();
}
QByteArray base64(const QByteArray &bytes) {
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QByteArray unbase64(const QByteArray &bytes) {
    const auto decoded = QByteArray::fromBase64Encoding(
        bytes, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    require(bool(decoded) && base64(decoded.decoded) == bytes);
    return decoded.decoded;
}
QString randomValue() {
    QByteArray bytes(32, '\0');
    require(RAND_bytes(reinterpret_cast<unsigned char *>(bytes.data()), bytes.size()) == 1);
    return QString::fromLatin1(base64(bytes));
}
bool validClient(const QString &client) {
    return QRegularExpression("^oaiapp_[A-Za-z0-9_-]{1,200}$").match(client).hasMatch();
}
bool validSecret(const QString &token) {
    return token.size() >= 8 && token.size() <= 16384 &&
           QRegularExpression("^[!-~]+$").match(token).hasMatch();
}
QString field(const QJsonObject &object, const char *name) { return object.value(name).toString(); }
QJsonArray savedAccounts() {
    return QJsonDocument::fromJson(
               QSettings("SketchyUp", "SketchyUp").value("assistant/chatgptAccounts").toByteArray())
        .array();
}
void saveAccount(const QJsonObject &account) {
    auto saved = savedAccounts();
    bool found{};
    for (qsizetype i = 0; i < saved.size(); ++i)
        if (saved[i].toObject().value("client_id") == account.value("client_id")) {
            saved[i] = account;
            found = true;
            break;
        }
    if (!found) {
        require(saved.size() < 32, "Too many saved ChatGPT accounts.");
        saved.append(account);
    }
    QSettings prefs("SketchyUp", "SketchyUp");
    prefs.setValue("assistant/chatgptAccounts",
                   QJsonDocument(saved).toJson(QJsonDocument::Compact));
    prefs.sync();
    require(prefs.status() == QSettings::NoError, "Could not save ChatGPT account settings.");
}
QJsonObject accountFor(const QString &client) {
    for (const auto &value : savedAccounts())
        if (field(value.toObject(), "client_id") == client)
            return value.toObject();
    return {};
}
} // namespace
QJsonObject verifyChatGptIdentity(const QByteArray &jwt, const QJsonObject &jwks,
                                  const QString &client, const QString &nonce, qint64 now) {
    require(jwt.size() <= 16384 && validClient(client));
    const auto parts = jwt.split('.');
    require(parts.size() == 3);
    const auto header = parse(unbase64(parts[0]));
    require(header.value("alg") == "RS256" && !field(header, "kid").isEmpty() &&
            !header.contains("crit"));
    QJsonObject key;
    const auto keys = jwks.value("keys").toArray();
    require(keys.size() <= 64);
    for (auto value : keys) {
        const auto candidate = value.toObject();
        if (candidate.value("kid") == header.value("kid")) {
            require(key.isEmpty());
            key = candidate;
        }
    }
    require(key.value("kty") == "RSA" && (!key.contains("alg") || key.value("alg") == "RS256") &&
            (!key.contains("use") || key.value("use") == "sig"));
    const auto n = unbase64(field(key, "n").toLatin1());
    const auto e = unbase64(field(key, "e").toLatin1());
    require(n.size() >= 256 && n.size() <= 1024 && e.size() > 0 && e.size() <= 8);
    auto bn = std::unique_ptr<BIGNUM, decltype(&BN_free)>(
        BN_bin2bn(reinterpret_cast<const unsigned char *>(n.data()), n.size(), nullptr), BN_free);
    auto be = std::unique_ptr<BIGNUM, decltype(&BN_free)>(
        BN_bin2bn(reinterpret_cast<const unsigned char *>(e.data()), e.size(), nullptr), BN_free);
    auto builder = std::unique_ptr<OSSL_PARAM_BLD, decltype(&OSSL_PARAM_BLD_free)>(
        OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
    require(bn && be && builder &&
            OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_N, bn.get()) == 1 &&
            OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_E, be.get()) == 1);
    auto params = std::unique_ptr<OSSL_PARAM, decltype(&OSSL_PARAM_free)>(
        OSSL_PARAM_BLD_to_param(builder.get()), OSSL_PARAM_free);
    auto context = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>(
        EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY *raw{};
    require(context && params && EVP_PKEY_fromdata_init(context.get()) == 1 &&
            EVP_PKEY_fromdata(context.get(), &raw, EVP_PKEY_PUBLIC_KEY, params.get()) == 1);
    auto publicKey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(raw, EVP_PKEY_free);
    auto md =
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    const auto signature = unbase64(parts[2]), signedBytes = parts[0] + '.' + parts[1];
    require(md &&
            EVP_DigestVerifyInit(md.get(), nullptr, EVP_sha256(), nullptr, publicKey.get()) == 1 &&
            EVP_DigestVerify(md.get(), reinterpret_cast<const unsigned char *>(signature.data()),
                             signature.size(),
                             reinterpret_cast<const unsigned char *>(signedBytes.data()),
                             signedBytes.size()) == 1);
    const auto claims = parse(unbase64(parts[1]));
    const auto aud = claims.value("aud");
    require(claims.value("iss") == issuer &&
            (aud == client || (aud.isArray() && aud.toArray().contains(client))) &&
            (!claims.contains("azp") || claims.value("azp") == client) &&
            (!aud.isArray() || aud.toArray().size() <= 1 || claims.value("azp") == client) &&
            claims.value("exp").isDouble() && claims.value("exp").toDouble() > double(now - 5) &&
            claims.value("iat").isDouble() && claims.value("iat").toDouble() <= double(now + 5) &&
            (!claims.contains("nbf") || (claims.value("nbf").isDouble() &&
                                         claims.value("nbf").toDouble() <= double(now + 5))) &&
            claims.value("nonce") == nonce && !nonce.isEmpty() && !field(claims, "sub").isEmpty() &&
            field(claims, "sub").size() <= 512);
    return claims;
}
struct ChatGptAuth::Impl {
    ChatGptAuth &owner;
    QNetworkAccessManager *network;
    // Destroy the helper before releasing the cross-process session lock.
    std::unique_ptr<QLockFile> lock;
    OpenAiCredentialStore store;
    QTcpServer server;
    QTimer deadline;
    QPointer<QNetworkReply> reply;
    QByteArray bytes;
    QString action, client, state, nonce, verifier, redirect, model, clearedStatus,
        message{"Not connected"};
    QJsonObject account, record, received;
    QJsonArray catalog;
    bool working{}, refreshing{}, revocationConfirmed{}, revocationRetried{};
    quint64 generation{};
    std::function<void()> stored;
    Impl(ChatGptAuth &owner, QNetworkAccessManager *manager)
        : owner(owner), network(manager ? manager : new QNetworkAccessManager(&owner)),
          store(&owner), server(&owner), deadline(&owner) {
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &owner,
                         [this] { fail("ChatGPT operation timed out. Try again."); });
        QObject::connect(&server, &QTcpServer::newConnection, &owner, [this] { acceptSocket(); });
        QObject::connect(&store, &OpenAiCredentialStore::changed, &owner,
                         [this] { guarded([this] { credentialChanged(); }); });
    }
    ~Impl() { stop(); }
    void stop() {
        ++generation;
        working = false;
        deadline.stop();
        server.close();
        for (auto *socket : server.findChildren<QTcpSocket *>()) {
            socket->abort();
            socket->deleteLater();
        }
        if (reply) {
            QObject::disconnect(reply, nullptr, &owner, nullptr);
            reply->abort();
            reply->deleteLater();
            reply = nullptr;
        }
        store.cancel();
        if (store.phase() != OpenAiCredentialStore::Phase::Working)
            lock.reset();
        record = {};
        received = {};
        bytes.fill('\0');
        bytes.clear();
        verifier.clear();
        nonce.clear();
        state.clear();
        stored = {};
    }
    void finish(QString status) {
        stop();
        message = std::move(status);
        emit owner.changed();
    }
    void fail(QString status) { finish(std::move(status)); }
    void guarded(const std::function<void()> &fn) {
        try {
            fn();
        } catch (const std::exception &e) {
            fail(QString::fromUtf8(e.what()));
        }
    }
    void begin(QString operation, QString selected) {
        require(!working && store.phase() != OpenAiCredentialStore::Phase::Working,
                "Finish the current ChatGPT operation first.");
        require(selected.isEmpty() || validClient(selected));
        const auto path =
            QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                .filePath("SketchyUp/SketchyUp");
        require(QDir().mkpath(path), "Could not open ChatGPT session lock directory.");
        lock = std::make_unique<QLockFile>(QDir(path).filePath("chatgpt-session.lock"));
        lock->setStaleLockTime(0);
        require(lock->tryLock(),
                "ChatGPT sign-in or renewal is active in another SketchyUp window.");
        action = std::move(operation);
        client = std::move(selected);
        account = accountFor(client);
        require(client.isEmpty() || !account.isEmpty(), "Choose a saved ChatGPT account.");
        working = true;
        refreshing = false;
        revocationConfirmed = false;
        revocationRetried = false;
        clearedStatus.clear();
        catalog = {};
        record = {};
        received = {};
        message = "Connecting to ChatGPT…";
        deadline.start(120000);
        emit owner.changed();
    }
    void request(const QUrl &url, std::optional<QByteArray> body, QByteArray bearer,
                 std::function<void(const QJsonObject &)> done, bool emptyOk = false) {
        require(!reply && url.scheme() == "https" &&
                (url.host() == "auth.openai.com" || url.host() == "api.openai.com"));
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
        req.setTransferTimeout(30000);
        req.setRawHeader("Accept", "application/json");
        if (!bearer.isEmpty())
            req.setRawHeader("Authorization", "Bearer " + bearer);
        if (body)
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        reply = body ? network->post(req, *body) : network->get(req);
        reply->setReadBufferSize(1024 * 1024 + 1);
        bytes.clear();
        auto drain = [this] {
            if (!reply)
                return;
            const auto chunk = reply->readAll();
            require(chunk.size() <= 1024 * 1024 - bytes.size());
            bytes += chunk;
        };
        QObject::connect(reply, &QNetworkReply::readyRead, &owner,
                         [this, drain] { guarded(drain); });
        QObject::connect(reply, &QNetworkReply::finished, &owner, [this, drain, done, emptyOk] {
            guarded([this, drain, done, emptyOk] {
                if (!reply)
                    return;
                drain();
                auto *completed = reply.data();
                reply = nullptr;
                completed->deleteLater();
                const auto code =
                    completed->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (action == "signout" && emptyOk) {
                    revocationConfirmed =
                        code == 200 && completed->error() == QNetworkReply::NoError;
                    bytes.fill('\0');
                    bytes.clear();
                    if (!revocationConfirmed && !revocationRetried &&
                        (code >= 500 || completed->error() != QNetworkReply::NoError)) {
                        revocationRetried = true;
                        QTimer::singleShot(300, &owner, [this, expected = generation] {
                            if (working && expected == generation)
                                guarded([this] { revoke(); });
                        });
                        return;
                    }
                    store.clear();
                    return;
                }
                if (refreshing && completed->url() == QUrl(tokenEndpoint) && code != 200) {
                    const auto payload = QJsonDocument::fromJson(bytes).object();
                    const auto rawError = payload.value("error");
                    const auto error = rawError.isString()
                                           ? rawError.toString()
                                           : rawError.toObject().value("code").toString();
                    if (QStringList{"invalid_grant", "invalid_refresh_token", "token_expired",
                                    "refresh_token_expired", "refresh_token_invalidated",
                                    "refresh_token_reused"}
                            .contains(error)) {
                        clearedStatus = "ChatGPT session expired or was revoked. Continue with "
                                        "ChatGPT to sign in again.";
                        bytes.fill('\0');
                        bytes.clear();
                        store.clear();
                        return;
                    }
                }
                require(code == 200 && completed->error() == QNetworkReply::NoError,
                        "ChatGPT connection failed. Check connectivity, plan access, or sign in "
                        "again.");
                const auto result = emptyOk && bytes.isEmpty() ? QJsonObject{} : parse(bytes);
                bytes.fill('\0');
                bytes.clear();
                done(result);
            });
        });
    }
    void form(const QString &endpoint, QUrlQuery data,
              std::function<void(const QJsonObject &)> done, bool emptyOk = false) {
        QByteArray encoded;
        for (const auto &item : data.queryItems(QUrl::FullyDecoded)) {
            if (!encoded.isEmpty())
                encoded += '&';
            encoded +=
                QUrl::toPercentEncoding(item.first) + '=' + QUrl::toPercentEncoding(item.second);
        }
        request(QUrl(endpoint), encoded, {}, std::move(done), emptyOk);
    }
    void authorize() {
        require(server.listen(QHostAddress::LocalHost, 0),
                "Could not start browser sign-in callback.");
        server.setMaxPendingConnections(4);
        redirect = QString("http://127.0.0.1:%1/auth/callback").arg(server.serverPort());
        state = randomValue();
        nonce = randomValue();
        verifier = randomValue();
        QSettings prefs("SketchyUp", "SketchyUp");
        auto host = prefs.value("assistant/chatgptHostId").toString();
        if (host.isEmpty()) {
            host = "urn:uuid:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            prefs.setValue("assistant/chatgptHostId", host);
            prefs.sync();
            require(prefs.status() == QSettings::NoError);
        }
        QUrlQuery query;
        query.addQueryItem("client_id", client.isEmpty() ? "dynamic_agent_client" : client);
        if (client.isEmpty())
            query.addQueryItem("agent_name_hint", "SketchyUp");
        query.addQueryItem("ext_agent_host_id", host);
        if (!account.isEmpty() && account.value("plan_enabled") == false)
            query.addQueryItem("prompt", "consent");
        query.addQueryItem("response_type", "code");
        query.addQueryItem("redirect_uri", redirect);
        query.addQueryItem("resource", resource);
        query.addQueryItem(
            "scope",
            "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct");
        query.addQueryItem("state", state);
        query.addQueryItem("nonce", nonce);
        query.addQueryItem("code_challenge_method", "S256");
        query.addQueryItem("code_challenge",
                           QString::fromLatin1(base64(QCryptographicHash::hash(
                               verifier.toLatin1(), QCryptographicHash::Sha256))));
        // Hints are optional. Omitting them keeps retained ID tokens out of browser URLs.
        QUrl url("https://auth.openai.com/api/accounts/authorize");
        url.setQuery(query);
        deadline.start(600000);
        message = "Finish signing in in your browser. No API key is needed.";
        emit owner.changed();
        emit owner.authorizationRequested(url);
    }
    void acceptSocket() {
        while (auto *socket = server.nextPendingConnection()) {
            if (server.findChildren<QTcpSocket *>().size() > 8) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            socket->setReadBufferSize(16385);
            auto buffer = std::make_shared<QByteArray>();
            QTimer::singleShot(10000, socket, [socket] {
                socket->abort();
                socket->deleteLater();
            });
            QObject::connect(socket, &QTcpSocket::readyRead, &owner, [this, socket, buffer] {
                *buffer += socket->readAll();
                if (buffer->size() > 16384) {
                    socket->abort();
                    socket->deleteLater();
                    return;
                }
                if (!buffer->contains("\r\n\r\n"))
                    return;
                QObject::disconnect(socket, &QTcpSocket::readyRead, &owner, nullptr);
                const auto first = buffer->left(buffer->indexOf("\r\n")).split(' ');
                bool accepted = false;
                if (working && server.isListening() && first.size() == 3 && first[0] == "GET" &&
                    first[1].startsWith("/auth/callback?")) {
                    const QUrl url = QUrl::fromEncoded(first[1], QUrl::StrictMode);
                    const QUrlQuery query(url);
                    bool unique = true;
                    for (const auto &key : {"state", "code", "client_id", "error"})
                        unique &= query.allQueryItemValues(key).size() <= 1;
                    accepted = unique && url.path() == "/auth/callback" &&
                               query.queryItemValue("state", QUrl::FullyDecoded) == state;
                    if (accepted) {
                        // Consume exactly once before any asynchronous operation.
                        server.close();
                        state.clear();
                        QTimer::singleShot(0, &owner, [this, query, expected = generation] {
                            if (working && expected == generation)
                                guarded([this, query] { callback(query); });
                        });
                    }
                }
                socket->write(
                    accepted
                        ? "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nCache-Control: "
                          "no-store\r\nConnection: close\r\n\r\nReturn to SketchyUp to finish "
                          "connecting."
                        : "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nCache-Control: "
                          "no-store\r\nConnection: close\r\n\r\nInvalid sign-in callback.");
                socket->disconnectFromHost();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            });
        }
    }
    void callback(const QUrlQuery &query) {
        require(working && action == "signin");
        require(!query.hasQueryItem("error"), "ChatGPT sign-in was declined. You can try again.");
        const auto issued = query.queryItemValue("client_id", QUrl::FullyDecoded);
        require(client.isEmpty() ? validClient(issued) : (issued.isEmpty() || issued == client));
        if (client.isEmpty())
            client = issued;
        const auto code = query.queryItemValue("code", QUrl::FullyDecoded);
        require(!code.isEmpty() && code.size() <= 4096);
        deadline.start(120000);
        QUrlQuery data;
        data.addQueryItem("grant_type", "authorization_code");
        data.addQueryItem("client_id", client);
        data.addQueryItem("code", code);
        data.addQueryItem("code_verifier", verifier);
        data.addQueryItem("redirect_uri", redirect);
        data.addQueryItem("resource", resource);
        form(tokenEndpoint, data, [this](const QJsonObject &result) {
            received = result;
            request(
                QUrl("https://auth.openai.com/.well-known/jwks.json"), {}, {},
                [this](const QJsonObject &jwks) {
                    const auto identity =
                        verifyChatGptIdentity(field(received, "id_token").toLatin1(), jwks, client,
                                              nonce, QDateTime::currentSecsSinceEpoch());
                    require(
                        account.isEmpty() || account.value("subject") == identity.value("sub"),
                        "The signed-in ChatGPT account does not match the selected registration.");
                    account = {{"client_id", client},
                               {"subject", identity.value("sub")},
                               {"email", field(identity, "email").left(320)},
                               {"plan_enabled", false}};
                    // Keep the validated registration even if plan permission is declined.
                    saveAccount(account);
                    emit owner.registrationSaved(client);
                    acceptTokens(received);
                    account["plan_enabled"] = true;
                    persist([this] {
                        saveAccount(account);
                        fetchModels([this] {
                            const auto connectedClient = client;
                            finish("Connected. Eligible assistant requests use your ChatGPT plan.");
                            emit owner.connected(connectedClient);
                        });
                    });
                });
        });
    }
    void acceptTokens(const QJsonObject &result) {
        const auto scopes = field(result, "scope").split(' ', Qt::SkipEmptyParts);
        require(
            result.value("token_type") == "Bearer" && validSecret(field(result, "access_token")) &&
                validSecret(field(result, "refresh_token")) &&
                scopes.contains("chatgpt.tokens.use.direct") && scopes.contains("resource.invoke"),
            "ChatGPT plan use was not granted. Sign in again and enable plan access.");
        const auto lifetime = result.value("expires_in").toInt();
        require(lifetime >= 360 && lifetime <= 86400);
        if (!refreshing)
            require(validSecret(field(result, "id_token")));
        // Refresh ID tokens do not replace the validated sign-in identity or its retained hint.
        record["access_token"] = result.value("access_token");
        record["refresh_token"] = result.value("refresh_token");
        if (!refreshing)
            record["id_token"] = result.value("id_token");
        record["scope"] = field(result, "scope");
        record["client_id"] = client;
        record["subject"] = account.value("subject");
        record["expires_at"] = double(QDateTime::currentSecsSinceEpoch() + lifetime);
        record["earliest_refresh_at"] = result.value("earliest_refresh_at");
        received = {};
    }
    void persist(std::function<void()> next) {
        stored = std::move(next);
        store.setChatGptAccount(client);
        store.store(base64(json(record)));
    }
    void lookup() {
        store.setChatGptAccount(client);
        store.lookup();
    }
    void credentialChanged() {
        using P = OpenAiCredentialStore::Phase;
        if (!working) {
            if (store.phase() != P::Working) lock.reset();
            return;
        }
        const auto phase = store.phase();
        if (phase == P::Working || phase == P::Idle)
            return;
        if (phase == P::Stored) {
            auto done = std::move(stored);
            require(bool(done));
            done();
            return;
        }
        if (phase == P::Cleared) {
            if (!clearedStatus.isEmpty()) {
                finish(clearedStatus);
                return;
            }
            finish(revocationConfirmed ? "Signed out of ChatGPT."
                                       : "Signed out locally. Remote revocation was not confirmed; "
                                         "disconnect SketchyUp in ChatGPT Settings.");
            return;
        }
        if (phase == P::Missing && action == "signout") {
            finish("No local ChatGPT session stored.");
            return;
        }
        require(phase == P::Available,
                "ChatGPT session unavailable. Unlock the OS credential facility or sign in again.");
        auto encoded = store.takeCredential();
        record = parse(unbase64(encoded));
        encoded.fill('\0');
        require(record.value("client_id") == client &&
                record.value("subject") == account.value("subject"));
        if (action == "signout") {
            revoke();
            return;
        }
        require(validSecret(field(record, "access_token")) &&
                validSecret(field(record, "refresh_token")) &&
                field(record, "scope").split(' ').contains("chatgpt.tokens.use.direct"));
        if (record.value("expires_at").toDouble() <=
            double(QDateTime::currentSecsSinceEpoch() + 330)) {
            refreshing = true;
            QUrlQuery data;
            data.addQueryItem("grant_type", "refresh_token");
            data.addQueryItem("client_id", client);
            data.addQueryItem("refresh_token", field(record, "refresh_token"));
            data.addQueryItem("resource", resource);
            form(tokenEndpoint, data, [this](const QJsonObject &result) {
                acceptTokens(result);
                persist([this] { ready(); });
            });
        } else
            ready();
    }
    void ready() {
        fetchModels([this] {
            if (action == "prepare") {
                bool allowed{};
                for (auto entry : catalog)
                    allowed |= field(entry.toObject(), "slug") == model;
                require(allowed, "The selected model is unavailable to this ChatGPT account. "
                                 "Refresh models in Preferences.");
                const auto token = field(record, "access_token").toLatin1();
                finish("Using ChatGPT plan");
                emit owner.credentialReady(token);
            } else
                finish("ChatGPT models refreshed.");
        });
    }
    void fetchModels(std::function<void()> done) {
        request(QUrl("https://api.openai.com/v1/models"), {},
                field(record, "access_token").toLatin1(), [this, done](const QJsonObject &result) {
                    require(result.value("models").isArray());
                    const auto models = result.value("models").toArray();
                    require(models.size() <= 512);
                    catalog = {};
                    for (auto entry : models) {
                        const auto m = entry.toObject();
                        if (m.value("visibility") != "list")
                            continue;
                        require(QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$")
                                    .match(field(m, "slug"))
                                    .hasMatch());
                        catalog.append(
                            QJsonObject{{"slug", m.value("slug")},
                                        {"display_name", field(m, "display_name").left(256)}});
                    }
                    require(!catalog.isEmpty(), "No models available for this ChatGPT account.");
                    done();
                });
    }
    void revoke() {
        require(validSecret(field(record, "refresh_token")));
        request(QUrl("https://auth.openai.com/.well-known/openid-configuration"), {}, {},
                [this](const QJsonObject &discovery) {
                    require(discovery.value("issuer") == issuer &&
                            discovery.value("revocation_endpoint") ==
                                "https://auth.openai.com/api/accounts/oauth/revoke");
                    QUrlQuery data;
                    data.addQueryItem("token", field(record, "refresh_token"));
                    data.addQueryItem("token_type_hint", "refresh_token");
                    data.addQueryItem("client_id", client);
                    form(
                        field(discovery, "revocation_endpoint"), data, [](const QJsonObject &) {},
                        true);
                });
    }
};
ChatGptAuth::ChatGptAuth(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, network)) {}
ChatGptAuth::~ChatGptAuth() = default;
QJsonArray ChatGptAuth::accounts() const { return savedAccounts(); }
QJsonArray ChatGptAuth::models() const { return impl_->catalog; }
QString ChatGptAuth::status() const { return impl_->message; }
bool ChatGptAuth::busy() const { return impl_->working; }
void ChatGptAuth::signIn(const QString &client) {
    impl_->guarded([&] {
        impl_->begin("signin", client);
        impl_->authorize();
    });
}
void ChatGptAuth::loadModels(const QString &client) {
    impl_->guarded([&] {
        impl_->begin("models", client);
        impl_->lookup();
    });
}
void ChatGptAuth::prepare(const QString &client, const QString &model) {
    impl_->guarded([&] {
        impl_->begin("prepare", client);
        impl_->model = model;
        impl_->lookup();
    });
}
void ChatGptAuth::signOut(const QString &client) {
    impl_->guarded([&] {
        impl_->begin("signout", client);
        impl_->lookup();
    });
}
void ChatGptAuth::cancel() { impl_->finish("ChatGPT operation canceled."); }
void ChatGptAuth::setCredentialExecutable(QString path) {
    impl_->store.setExecutable(std::move(path));
}
} // namespace sketchy
