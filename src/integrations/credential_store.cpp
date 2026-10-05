#include "integrations/credential_store.hpp"
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool valid(const QByteArray &key) {
    if (key.size() < 8 || key.size() > 4096)
        return false;
    for (char c : key)
        if (c < 33 || c > 126)
            return false;
    return true;
}
} // namespace
struct OpenAiCredentialStore::Impl {
    OpenAiCredentialStore &owner;
    QProcess process;
    QTimer deadline, kill;
    Phase phase{Phase::Idle}, target{Phase::Failed};
    QString executable, operation;
    QByteArray input, output, credential;
    bool stopping{}, stderrSeen{};
    int errorBytes{};
    explicit Impl(OpenAiCredentialStore &owner)
        : owner(owner), process(&owner), deadline(&owner), kill(&owner) {
        deadline.setSingleShot(true);
        kill.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &owner, [this] { stop(Phase::TimedOut); });
        QObject::connect(&kill, &QTimer::timeout, &owner, [this] { process.kill(); });
        QObject::connect(&process, &QProcess::started, &owner, [this] {
            if (!input.isEmpty())
                process.write(input);
            input.fill('\0');
            input.clear();
            process.closeWriteChannel();
        });
        QObject::connect(&process, &QProcess::readyReadStandardOutput, &owner, [this] { drain(); });
        QObject::connect(&process, &QProcess::readyReadStandardError, &owner, [this] { drain(); });
        QObject::connect(&process, &QProcess::errorOccurred, &owner,
                         [this](QProcess::ProcessError error) {
                             if (error == QProcess::FailedToStart)
                                 finish(Phase::Unavailable);
                         });
        QObject::connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         &owner, [this](int code, QProcess::ExitStatus exit) {
                             drain();
                             if (stopping) {
                                 finish(target);
                                 return;
                             }
                             if (exit != QProcess::NormalExit) {
                                 finish(Phase::Failed);
                                 return;
                             }
                             if (operation == "lookup" && code == 1 && output.isEmpty() &&
                                 !stderrSeen) {
                                 finish(Phase::Missing);
                                 return;
                             }
                             if (code != 0) {
                                 finish(Phase::Failed);
                                 return;
                             }
                             if (operation == "lookup") {
                                 if (output.endsWith('\n'))
                                     output.chop(1);
                                 if (!valid(output)) {
                                     finish(Phase::Failed);
                                     return;
                                 }
                                 credential = output;
                                 finish(Phase::Available);
                             } else
                                 finish(operation == "store" ? Phase::Stored : Phase::Cleared);
                         });
    }
    ~Impl() {
        QObject::disconnect(&process, nullptr, &owner, nullptr);
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(2000);
        }
        input.fill('\0');
        output.fill('\0');
        credential.fill('\0');
    }
    void checkOwner() const {
        require(QThread::currentThread() == owner.thread(),
                "Credential store requires owner thread");
    }
    void finish(Phase result) {
        deadline.stop();
        kill.stop();
        input.fill('\0');
        input.clear();
        output.fill('\0');
        output.clear();
        phase = result;
        emit owner.changed();
    }
    void stop(Phase result) {
        if (phase != Phase::Working || stopping)
            return;
        stopping = true;
        target = result;
        deadline.stop();
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            kill.start(1000);
        } else
            finish(result);
    }
    void drain() {
        const auto bytes = process.readAllStandardOutput();
        const auto errors = process.readAllStandardError();
        stderrSeen |= !errors.isEmpty();
        if (bytes.size() > 4097 - output.size() || errors.size() > 64 * 1024 - errorBytes) {
            stop(Phase::Failed);
            return;
        }
        output += bytes;
        errorBytes += int(errors.size()); // Discard diagnostics; a helper may echo secrets.
    }
    void begin(QString action, QByteArray key = {}) {
        checkOwner();
        require(phase != Phase::Working, "Credential operation already active");
        if (action == "store")
            require(valid(key),
                    "Credential must be 8–4096 printable ASCII characters without spaces");
        credential.fill('\0');
        credential.clear();
        input = std::move(key);
        output.clear();
        errorBytes = 0;
        stderrSeen = false;
        stopping = false;
        operation = std::move(action);
        auto path =
            executable.isEmpty() ? QStandardPaths::findExecutable("secret-tool") : executable;
        const QFileInfo file(path);
        if (!file.isAbsolute() || !file.isFile() || !file.isExecutable()) {
            finish(Phase::Unavailable);
            return;
        }
        QStringList arguments{operation};
        if (operation == "store")
            arguments << "--label=SketchyUp OpenAI API key";
        arguments << "application" << "org.sketchyup.SketchyUp" << "provider" << "OpenAI"
                  << "account" << "default";
        process.setProgram(file.canonicalFilePath());
        process.setArguments(arguments);
        process.setProcessChannelMode(QProcess::SeparateChannels);
        phase = Phase::Working;
        emit owner.changed();
        if (stopping || phase != Phase::Working)
            return;
        deadline.start(60000);
        process.start();
    }
};
OpenAiCredentialStore::OpenAiCredentialStore(QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this)) {}
OpenAiCredentialStore::~OpenAiCredentialStore() = default;
void OpenAiCredentialStore::lookup() { impl_->begin("lookup"); }
void OpenAiCredentialStore::store(QByteArray key) { impl_->begin("store", std::move(key)); }
void OpenAiCredentialStore::clear() { impl_->begin("clear"); }
void OpenAiCredentialStore::cancel() {
    impl_->checkOwner();
    impl_->stop(Phase::Canceled);
}
OpenAiCredentialStore::Phase OpenAiCredentialStore::phase() const {
    impl_->checkOwner();
    return impl_->phase;
}
QByteArray OpenAiCredentialStore::takeCredential() {
    impl_->checkOwner();
    require(impl_->phase == Phase::Available, "No credential available");
    auto key = std::move(impl_->credential);
    impl_->phase = Phase::Idle;
    return key;
}
void OpenAiCredentialStore::setExecutable(QString path) {
    impl_->checkOwner();
    require(impl_->phase != Phase::Working, "Credential operation already active");
    require(path.isEmpty() || QFileInfo(path).isAbsolute(),
            "Credential executable must be absolute");
    impl_->executable = std::move(path);
}
} // namespace sketchy
