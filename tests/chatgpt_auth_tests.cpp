#include "integrations/chatgpt_auth.hpp"
#include "integrations/openai_provider.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <iostream>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
template <class F> void wait(F done) {
    QElapsedTimer time;
    time.start();
    while (!done() && time.elapsed() < 10000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    check(done(), "Async test deadline");
}
QByteArray json(QJsonObject o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
QByteArray b64(QByteArray b) {
    return b.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "Fixture write");
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}
QByteArray read(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "Fixture read");
    return f.readAll();
}
QString quote(QString text) { return "'" + text.replace("'", "'\\''") + "'"; }
struct Signing {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{nullptr, EVP_PKEY_free};
    QJsonObject jwks;
    Signing() {
        auto *ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
        EVP_PKEY *raw{};
        check(ctx && EVP_PKEY_keygen_init(ctx) == 1 &&
                  EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) == 1 &&
                  EVP_PKEY_generate(ctx, &raw) == 1,
              "Generate fixture RSA key");
        EVP_PKEY_CTX_free(ctx);
        key.reset(raw);
        auto number = [&](const char *name) {
            BIGNUM *bn{};
            check(EVP_PKEY_get_bn_param(key.get(), name, &bn) == 1, "Public key param");
            QByteArray bytes(BN_num_bytes(bn), '\0');
            BN_bn2bin(bn, reinterpret_cast<unsigned char *>(bytes.data()));
            BN_free(bn);
            return QString::fromLatin1(b64(bytes));
        };
        jwks = {{"keys", QJsonArray{QJsonObject{{"kid", "fixture"},
                                                {"kty", "RSA"},
                                                {"alg", "RS256"},
                                                {"use", "sig"},
                                                {"n", number(OSSL_PKEY_PARAM_RSA_N)},
                                                {"e", number(OSSL_PKEY_PARAM_RSA_E)}}}}};
    }
    QByteArray sign(QJsonObject claims,
                    QJsonObject header = {{"alg", "RS256"}, {"kid", "fixture"}}) {
        const auto input = b64(json(header)) + '.' + b64(json(claims));
        auto *md = EVP_MD_CTX_new();
        size_t n{};
        check(md && EVP_DigestSignInit(md, nullptr, EVP_sha256(), nullptr, key.get()) == 1 &&
                  EVP_DigestSign(md, nullptr, &n,
                                 reinterpret_cast<const unsigned char *>(input.data()),
                                 input.size()) == 1,
              "Sign fixture");
        QByteArray signature(n, '\0');
        check(EVP_DigestSign(md, reinterpret_cast<unsigned char *>(signature.data()), &n,
                             reinterpret_cast<const unsigned char *>(input.data()),
                             input.size()) == 1,
              "Sign bytes");
        EVP_MD_CTX_free(md);
        signature.resize(n);
        return input + '.' + b64(signature);
    }
};
class Reply : public QNetworkReply {
    QByteArray bytes;
    qsizetype offset{};

  public:
    Reply(QNetworkRequest req, QByteArray payload, int status, QObject *parent)
        : QNetworkReply(parent), bytes(std::move(payload)) {
        setRequest(req);
        setUrl(req.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(1, this, [this] {
            if (!isFinished()) {
                emit readyRead();
                setFinished(true);
                emit finished();
            }
        });
    }
    void abort() override {
        if (!isFinished()) {
            setFinished(true);
            emit finished();
        }
    }
    qint64 bytesAvailable() const override {
        return bytes.size() - offset + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *out, qint64 size) override {
        const auto n = std::min<qint64>(size, bytes.size() - offset);
        if (!n)
            return -1;
        memcpy(out, bytes.data() + offset, n);
        offset += n;
        return n;
    }
};
struct Network : QNetworkAccessManager {
    Signing signing;
    QUrl authUrl;
    QString subject{"fixture-user"}, access{"access-fixture-only"}, refresh{"refresh-fixture-only"};
    bool badNonce{}, badScope{}, rejectRefresh{};
    int tokens{}, renewals{}, catalogs{}, revokeCode{200};
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req,
                                 QIODevice *out) override {
        check(req.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() ==
                  QNetworkRequest::ManualRedirectPolicy,
              "No auth redirects");
        const QUrlQuery form(QString::fromUtf8(out ? out->readAll() : QByteArray{}));
        auto get = [&](QString key) { return form.queryItemValue(key, QUrl::FullyDecoded); };
        const auto path = req.url().path();
        QByteArray response;
        int code = 200;
        if (path == "/api/accounts/oauth/token") {
            ++tokens;
            check(op == PostOperation, "Form POST");
            check(get("client_id").startsWith("oaiapp_") &&
                      get("resource") == "https://api.openai.com/v1" &&
                      !form.hasQueryItem("client_secret"),
                  "Issued public client and resource");
            if (get("grant_type") == "refresh_token") {
                ++renewals;
                check(get("refresh_token") == refresh && !form.hasQueryItem("scope"),
                      "Rotating refresh and retained scope");
                refresh += "-rotated";
            } else {
                const QUrlQuery auth(authUrl);
                check(get("code") == "code+fixture" &&
                          get("redirect_uri") ==
                              auth.queryItemValue("redirect_uri", QUrl::FullyDecoded),
                      "Exact callback and form encoding");
                check(QString::fromLatin1(b64(QCryptographicHash::hash(
                          get("code_verifier").toLatin1(), QCryptographicHash::Sha256))) ==
                          auth.queryItemValue("code_challenge"),
                      "PKCE S256");
            }
            const auto now = QDateTime::currentSecsSinceEpoch();
            const auto claims = QJsonObject{
                {"iss", "https://auth.openai.com"},
                {"aud", get("client_id")},
                {"sub", subject},
                {"email", "fixture@example.invalid"},
                {"iat", double(now)},
                {"exp", double(now + 3600)},
                {"nonce", badNonce ? "wrong" : QUrlQuery(authUrl).queryItemValue("nonce")}};
            response = json(
                {{"access_token", access},
                 {"refresh_token", refresh},
                 {"token_type", "Bearer"},
                 {"expires_in", 3600},
                 {"scope", badScope
                               ? "openid"
                               : "openid offline_access resource.invoke chatgpt.tokens.use.direct"},
                 {"id_token", QString::fromLatin1(signing.sign(claims))}});
            if (get("grant_type") == "refresh_token" && rejectRefresh) {
                code = 400;
                response = json({{"error", "invalid_grant"}});
            }
        } else if (path == "/.well-known/jwks.json")
            response = json(signing.jwks);
        else if (path == "/v1/models") {
            ++catalogs;
            check(op == GetOperation &&
                      req.rawHeader("Authorization") == "Bearer " + access.toLatin1(),
                  "OAuth token only in header");
            response = json(
                {{"models", QJsonArray{QJsonObject{{"visibility", "hidden"}, {"slug", "hidden"}},
                                       QJsonObject{{"visibility", "list"},
                                                   {"slug", "fixture-model"},
                                                   {"display_name", "Fixture model"}}}}});
        } else if (path == "/.well-known/openid-configuration")
            response = json(
                {{"issuer", "https://auth.openai.com"},
                 {"revocation_endpoint", "https://auth.openai.com/api/accounts/oauth/revoke"}});
        else if (path == "/api/accounts/oauth/revoke") {
            check(get("token") == refresh && get("token_type_hint") == "refresh_token",
                  "Revoke renewable session");
            code = revokeCode;
        } else
            throw std::runtime_error("Unexpected network endpoint");
        return new Reply(req, response, code, this);
    }
};
int fakeSecret(QStringList args) {
    const auto root = args[2], operation = args[3], client = args.last();
    check(args.contains("ChatGPT") && args.contains("org.sketchyup.SketchyUp") &&
              client.startsWith("oaiapp_"),
          "Separate credential schema");
    const auto path = root + '/' + client;
    if (operation == "store") {
        const std::string input((std::istreambuf_iterator<char>(std::cin)), {});
        write(path, QByteArray::fromStdString(input));
        return 0;
    }
    if (operation == "clear") {
        QFile::remove(path);
        return 0;
    }
    if (!QFile::exists(path))
        return 1;
    std::cout << read(path).toStdString();
    return 0;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().contains("--secret"))
            return fakeSecret(app.arguments());
        QTemporaryDir files;
        check(files.isValid(), "Temporary fixtures");
        qputenv("XDG_DATA_HOME", files.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
        QCoreApplication::setApplicationName("SketchyUpAuthTest");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, files.path());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, files.path());
        check(QSettings("SketchyUp", "SketchyUp").fileName().startsWith(files.path()),
              "Isolated preferences required");
        const auto helper = files.path() + "/secret-fixture";
        write(helper, ("#!/bin/sh\nexec " + quote(QCoreApplication::applicationFilePath()) +
                       " --secret " + quote(files.path()) + " \"$@\"\n")
                          .toUtf8());
        Network network;
        ChatGptAuth auth(&network);
        auth.setCredentialExecutable(helper);
        QUrl browserUrl;
        QObject::connect(&auth, &ChatGptAuth::authorizationRequested, [&](const QUrl &url) {
            browserUrl = url;
            network.authUrl = url;
        });
        QString connected;
        QObject::connect(&auth, &ChatGptAuth::connected,
                         [&](const QString &client) { connected = client; });
        QByteArray credential;
        QObject::connect(&auth, &ChatGptAuth::credentialReady,
                         [&](const QByteArray &token) { credential = token; });
        QNetworkAccessManager browser;
        auto callback = [&](QString state, QString client = "oaiapp_fixture", QString error = {}) {
            const QUrlQuery authQuery(browserUrl);
            QUrl url(authQuery.queryItemValue("redirect_uri", QUrl::FullyDecoded));
            QUrlQuery q;
            q.addQueryItem("state", state);
            q.addQueryItem("client_id", client);
            q.addQueryItem("code", "code%2Bfixture");
            if (!error.isEmpty())
                q.addQueryItem("error", error);
            url.setQuery(q);
            auto *reply = browser.get(QNetworkRequest(url));
            wait([&] { return reply->isFinished(); });
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reply->deleteLater();
            return status;
        };
        auth.signIn();
        check(auth.busy() && network.tokens == 0, "No token before browser consent");
        const QUrlQuery first(browserUrl);
        check(first.queryItemValue("client_id") == "dynamic_agent_client" &&
                  first.queryItemValue("agent_name_hint") == "SketchyUp" &&
                  first.queryItemValue("code_challenge_method") == "S256",
              "Dynamic registration");
        check(callback("wrong") == 400 && auth.busy() && network.tokens == 0,
              "Wrong state cannot redeem code");
        ChatGptAuth competing(&network);
        competing.setCredentialExecutable(helper);
        competing.signIn();
        check(!competing.busy() && competing.status().contains("another SketchyUp"),
              "Cross-window session lock");
        check(callback(first.queryItemValue("state")) == 200, "Loopback callback");
        wait([&] { return !auth.busy(); });
        check(connected == "oaiapp_fixture" && auth.accounts().size() == 1 &&
                  auth.models().size() == 1,
              "Validated account with filtered model catalog");
        const auto saved = files.path() + "/oaiapp_fixture";
        auto record = QJsonDocument::fromJson(
                          QByteArray::fromBase64(read(saved), QByteArray::Base64UrlEncoding))
                          .object();
        check(record.value("subject") == network.subject &&
                  record.value("access_token") == network.access,
              "Session stored in separate credential record");
        const auto settings = QSettings("SketchyUp", "SketchyUp").fileName();
        check(!read(settings).contains(network.access.toUtf8()) &&
                  !read(settings).contains(network.refresh.toUtf8()) &&
                  !read(settings).contains("id_token"),
              "No tokens in preferences");
        auth.prepare("oaiapp_fixture", "fixture-model");
        wait([&] { return !auth.busy(); });
        check(credential == network.access.toLatin1() && network.renewals == 0,
              "Ready with current entitlement");
        credential.clear();
        record["expires_at"] = double(QDateTime::currentSecsSinceEpoch() + 10);
        write(saved, b64(json(record)));
        auth.prepare("oaiapp_fixture", "fixture-model");
        wait([&] { return !auth.busy(); });
        check(!credential.isEmpty() && network.renewals == 1 && read(saved) != b64(json(record)),
              "Renew before task and persist rotation before use");
        credential.clear();
        auth.prepare("oaiapp_fixture", "hidden");
        wait([&] { return !auth.busy(); });
        check(credential.isEmpty() && auth.status().contains("unavailable"),
              "Unknown model rejected before inference");
        const auto originalStored = read(saved);
        connected.clear();
        network.badNonce = true;
        auth.signIn("oaiapp_fixture");
        const QUrlQuery returning(browserUrl);
        check(returning.queryItemValue("client_id") == "oaiapp_fixture" &&
                  !returning.hasQueryItem("agent_name_hint") &&
                  returning.queryItemValue("state") != first.queryItemValue("state") &&
                  returning.queryItemValue("ext_agent_host_id") ==
                      first.queryItemValue("ext_agent_host_id"),
              "Stable host, issued client, fresh state");
        callback(returning.queryItemValue("state"));
        wait([&] { return !auth.busy(); });
        check(connected.isEmpty() && read(saved) == originalStored,
              "Invalid nonce preserves selected account");
        network.badNonce = false;
        network.subject = "wrong-account";
        auth.signIn("oaiapp_fixture");
        callback(QUrlQuery(browserUrl).queryItemValue("state"));
        wait([&] { return !auth.busy(); });
        check(read(saved) == originalStored && auth.status().contains("does not match"),
              "Returning account identity binding");
        network.subject = "fixture-user";
        network.badScope = true;
        auth.signIn();
        callback(QUrlQuery(browserUrl).queryItemValue("state"), "oaiapp_denied");
        wait([&] { return !auth.busy(); });
        check(!QFile::exists(files.path() + "/oaiapp_denied") && auth.accounts().size() == 2,
              "Identity without plan scope grants no inference");
        network.badScope = false;
        auth.signIn();
        callback(QUrlQuery(browserUrl).queryItemValue("state"), "oaiapp_second");
        wait([&] { return !auth.busy(); });
        check(auth.accounts().size() == 3 && QFile::exists(files.path() + "/oaiapp_second"),
              "Separate registrations even for same email and subject");
        const auto beforeDenied = network.tokens;
        auth.signIn();
        callback(QUrlQuery(browserUrl).queryItemValue("state"), "oaiapp_declined", "access_denied");
        wait([&] { return !auth.busy(); });
        check(network.tokens == beforeDenied, "Declined callback never exchanged");
        network.revokeCode = 503;
        auth.signOut("oaiapp_fixture");
        wait([&] { return !auth.busy(); });
        check(!QFile::exists(saved) && QFile::exists(files.path() + "/oaiapp_second") &&
                  auth.accounts().size() == 3 && auth.status().contains("not confirmed"),
              "Local sign-out keeps registration and other accounts; failed revocation disclosed");
        network.revokeCode = 200;
        auth.signOut("oaiapp_second");
        wait([&] { return !auth.busy(); });
        check(auth.status() == "Signed out of ChatGPT.", "Successful revocation");
        auth.signIn("oaiapp_fixture");
        callback(QUrlQuery(browserUrl).queryItemValue("state"));
        wait([&] { return !auth.busy(); });
        auto expired = QJsonDocument::fromJson(
                           QByteArray::fromBase64(read(saved), QByteArray::Base64UrlEncoding))
                           .object();
        expired["expires_at"] = double(QDateTime::currentSecsSinceEpoch());
        write(saved, b64(json(expired)));
        network.rejectRefresh = true;
        credential.clear();
        auth.prepare("oaiapp_fixture", "fixture-model");
        wait([&] { return !auth.busy(); });
        check(!QFile::exists(saved) && credential.isEmpty() && auth.status().contains("revoked") &&
                  auth.accounts().size() == 3,
              "Terminal refresh rejection clears only unusable session and retains issued "
              "registration");
        // Cryptographic negative cases are signed correctly to test claim validation independently.
        const auto now = QDateTime::currentSecsSinceEpoch();
        QJsonObject claims{{"iss", "https://auth.openai.com"},
                           {"aud", "oaiapp_fixture"},
                           {"sub", "user"},
                           {"nonce", "nonce"},
                           {"iat", double(now)},
                           {"exp", double(now + 100)}};
        auto verify = [&](QByteArray jwt) {
            return verifyChatGptIdentity(jwt, network.signing.jwks, "oaiapp_fixture", "nonce", now);
        };
        check(verify(network.signing.sign(claims)).value("sub") == "user", "Valid RS256 identity");
        for (const auto &pair :
             std::initializer_list<std::pair<QString, QJsonValue>>{{"iss", "https://other.invalid"},
                                                                   {"aud", "oaiapp_other"},
                                                                   {"nonce", "wrong"},
                                                                   {"sub", ""},
                                                                   {"exp", double(now - 10)},
                                                                   {"iat", double(now + 100)},
                                                                   {"nbf", double(now + 100)}}) {
            auto bad = claims;
            bad[pair.first] = pair.second;
            rejects([&] { verify(network.signing.sign(bad)); });
        }
        auto tampered = network.signing.sign(claims);
        tampered[tampered.lastIndexOf('.') + 1] =
            tampered[tampered.lastIndexOf('.') + 1] == 'A' ? 'B' : 'A';
        rejects([&] { verify(tampered); });
        rejects(
            [&] { verify(network.signing.sign(claims, {{"alg", "none"}, {"kid", "fixture"}})); });
        rejects(
            [&] { verify(network.signing.sign(claims, {{"alg", "RS256"}, {"kid", "unknown"}})); });
        auth.signIn();
        auth.cancel();
        check(!auth.busy(), "Cancel sign-in");
        std::cout << "ChatGPT PKCE callback, identity validation, account isolation, OS storage, "
                     "refresh, entitlement and sign-out passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
