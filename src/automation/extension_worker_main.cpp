#include "automation/extension_worker.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
int main(int argc, char **argv) {
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
