#include "child_process.hpp"
#include "io/new_file.hpp"
#include "new_file_fault_hooks.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cerrno>
#include <csignal>
#include <functional>
#include <iostream>
using namespace sketchy;
namespace {
using namespace new_file_faults;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read publication fixture");
    return file.readAll();
}
std::string failure(const std::function<void()> &action) {
    try {
        action();
    } catch (const std::exception &error) {
        return error.what();
    }
    throw std::runtime_error("Expected publication failure");
}
struct Inject {
    explicit Inject(Fault value) {
        fault = value;
        writes = syncs = 0;
    }
    ~Inject() { fault = Fault::None; }
};
} // namespace
// The payload every publication writes.
QByteArray testPayload() { return QByteArray(128 * 1024, char(0x5a)); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // A killed writer: --child <fault> <output>. The injected fault kills it at its boundary.
    if (const auto child = childArguments()) {
        Inject injection(Fault(child->at(0).toInt()));
        publishNewFile(child->at(1), testPayload());
        return 2;
    }
    try {
        QTemporaryDir root;
        check(root.isValid(), "Publication scratch");
        const auto payload = testPayload();
        const auto original = root.filePath("original");
        publishNewFile(original, "original bytes");
        for (const auto mode : {Fault::NoSpace, Fault::ShortWrite, Fault::FileSync, Fault::Link,
                                Fault::DirectorySync, Fault::RetrySync, Fault::RacingWriter}) {
            const auto folder = root.filePath(QString::number(int(mode)));
            check(QDir().mkdir(folder), "Create isolated fault destination");
            const auto output = folder + "/result";
            std::string error;
            {
                Inject injection(mode);
                if (mode == Fault::RetrySync)
                    publishNewFile(output, payload);
                else
                    error = failure([&] { publishNewFile(output, payload); });
            }
            const bool published = mode == Fault::DirectorySync || mode == Fault::RetrySync;
            if (published)
                check(read(output) == payload, "Complete output retained after publication");
            else if (mode == Fault::RacingWriter)
                check(read(output) == "racing writer", "Concurrent writer is never replaced");
            else
                check(!QFileInfo::exists(output), "Prepublication failure leaves no destination");
            if (mode == Fault::DirectorySync)
                check(error.find("published") != std::string::npos &&
                          error.find("uncertain") != std::string::npos,
                      "Postpublication durability uncertainty is explicit");
            if (mode == Fault::RetrySync)
                check(syncs >= 3, "Interrupted file sync is retried");
            if (mode == Fault::NoSpace || mode == Fault::ShortWrite)
                check(writes > 0, "Write fault was exercised");
            const auto entries =
                QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
            check(entries.size() == ((published || mode == Fault::RacingWriter) ? 1 : 0),
                  "Handled failure cleans staging files without deleting published data");
            check(read(original) == "original bytes",
                  "Unrelated original survives every injected fault");
        }
        // Killing a writer models process interruption, not sudden storage power loss.
        for (const auto mode : {Fault::KillBeforePublication, Fault::KillAfterPublication}) {
            const auto output = root.filePath(QString("killed-%1").arg(int(mode)));
            const int status = runChild({QString::number(int(mode)), output});
            check(killedBy(status, SIGKILL), "Child stops at the intended publication boundary");
            if (mode == Fault::KillBeforePublication)
                check(!QFileInfo::exists(output),
                      "Killed staging writer exposes no partial destination");
            else
                check(read(output) == payload, "Killed published writer leaves a complete output");
        }
        const auto linkPath = root.filePath("existing-link");
        check(::symlink(QFile::encodeName(original).constData(),
                        QFile::encodeName(linkPath).constData()) == 0,
              "Create symlink fixture");
        failure([&] { publishNewFile(linkPath, payload); });
        failure([&] { publishNewFile(original, payload); });
        check(read(original) == "original bytes",
              "Existing original and linked targets are never replaced");
        std::cout << "New-file publication: full/partial ENOSPC, file/directory fsync, EINTR, link "
                     "failure, racing writer, killed writers and existing targets passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
