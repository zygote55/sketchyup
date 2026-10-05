#pragma once
#include <QObject>
#include <memory>
namespace sketchy {
// Linux Secret Service via libsecret's secret-tool; no plaintext fallback.
class OpenAiCredentialStore : public QObject {
    Q_OBJECT
  public:
    enum class Phase {
        Idle,
        Working,
        Available,
        Stored,
        Cleared,
        Missing,
        Unavailable,
        Failed,
        Canceled,
        TimedOut
    };
    explicit OpenAiCredentialStore(QObject *parent = nullptr);
    ~OpenAiCredentialStore() override;
    void lookup();
    void store(QByteArray key);
    void clear();
    void cancel();
    Phase phase() const;
    QByteArray takeCredential();
    // Trusted host/test configuration; not model controlled. Must be absolute.
    void setExecutable(QString executable);
    // Separate bounded credential record for a validated ChatGPT registration.
    void setChatGptAccount(QString clientId);
  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
