#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <memory>
namespace sketchy {
// RS256 only; signature, issuer, audience, time, nonce and subject are mandatory.
QJsonObject verifyChatGptIdentity(const QByteArray &jwt, const QJsonObject &jwks,
                                  const QString &client, const QString &nonce, qint64 now);
class ChatGptAuth : public QObject {
    Q_OBJECT
  public:
    explicit ChatGptAuth(QNetworkAccessManager *network = nullptr, QObject *parent = nullptr);
    ~ChatGptAuth() override;
    QJsonArray accounts() const; // Public metadata only. Tokens remain in Secret Service.
    QJsonArray models() const;
    QString status() const;
    bool busy() const;
    void signIn(const QString &client = {}); // Empty registers another account/workspace.
    void loadModels(const QString &client);
    void prepare(const QString &client, const QString &model);
    void signOut(const QString &client);
    void cancel();
    void setCredentialExecutable(QString executable); // Trusted test/host configuration.
  signals:
    void changed();
    void authorizationRequested(const QUrl &url); // Open in system browser; never log URL.
    void registrationSaved(const QString &client);
    void connected(const QString &client);
    void credentialReady(const QByteArray &accessToken);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
