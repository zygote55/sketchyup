#include "automation/extension_sandbox_policy.hpp"
#include "automation/extension_worker.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
// The sanitizer builds: LeakSanitizer's exit check would stop the worker's threads with ptrace,
// which the sandbox refuses (ADR 0183 section 3). The host gives the worker a clean environment, so
// this is the worker's own setting, detect_leaks=0 (as the .skp helper's).
extern "C" const char *__lsan_default_options() { return "detect_leaks=0"; }
#endif

int main(int argc, char **argv) {
    // R109 S4 (ADR 0183 section 4.1): the sandbox is installed before Qt starts and before the
    // request is read. A layer that cannot be installed refuses here, with exit 70 and nothing
    // parsed.
    if (const auto failure = applyExtensionSandbox())
        return refuseExtensionSandbox(*failure);
    QCoreApplication app(argc, argv);
    try {
        if (argc != 1)
            throw std::runtime_error("Extension worker accepts stdin only");
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly))
            throw std::runtime_error("Cannot read extension worker input");
        QByteArray bytes;
        while (true) {
            const auto part = input.read(8192);
            if (part.isEmpty()) {
                if (input.error() != QFileDevice::NoError)
                    throw std::runtime_error("Extension worker input read failed");
                break;
            }
            bytes += part;
            if (bytes.size() > extensionWorkerRequestLimit)
                throw std::runtime_error("Extension worker input exceeds 384 KiB");
        }
        const auto output = resolveExtensionWorkerRequest(bytes);
        std::cout.write(output.constData(), output.size());
        std::cout << '\n';
        return 0;
    } catch (const std::exception &error) {
        const auto output =
            QJsonDocument(QJsonObject{{"protocol", 1},
                                      {"ok", false},
                                      {"error", QString::fromUtf8(error.what()).left(256)}})
                .toJson(QJsonDocument::Compact);
        std::cout.write(output.constData(), output.size());
        std::cout << '\n';
        return 1;
    }
}
