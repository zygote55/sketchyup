#include "text/sandbox_policy.hpp"
#include "text/text_worker.hpp"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
int main(int argc, char **argv) {
    // R109 S3 (ADR 0183 section 4.1): the sandbox is installed before Qt starts and before the
    // request is read. A layer that cannot be installed refuses here, with exit 70 and nothing
    // parsed.
    if (const auto failure = applyTextSandbox())
        return refuseTextSandbox(*failure);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    qputenv("QT_IM_MODULE", "compose");
    QGuiApplication app(argc, argv);
    try {
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly))
            throw std::runtime_error("Cannot read text worker input");
        QByteArray bytes;
        while (true) {
            const auto part = input.read(8192);
            if (part.isEmpty()) {
                if (input.error() != QFileDevice::NoError)
                    throw std::runtime_error("Text worker input read failed");
                break;
            }
            bytes += part;
            if (bytes.size() > textWorkerRequestLimit)
                throw std::runtime_error("Text worker request exceeds 32 KiB");
        }
        QJsonParseError parse;
        const auto request = QJsonDocument::fromJson(bytes, &parse);
        if (parse.error != QJsonParseError::NoError || !request.isObject())
            throw std::runtime_error("Text worker requires one JSON request");
        const auto root = request.object();
        if (root.size() != 2 || root["protocol"] != 1 || !root["settings"].isObject())
            throw std::runtime_error("Unsupported text worker request");
        const auto geometry = shapeTextGeometry(decodeTextSettings(root["settings"].toObject()));
        const auto reply = QJsonDocument(QJsonObject{{"protocol", 1},
                                                     {"ok", true},
                                                     {"geometry", encodeTextGeometry(geometry)}})
                               .toJson(QJsonDocument::Compact);
        if (reply.size() + 1 > textWorkerResponseLimit)
            throw std::runtime_error("Text geometry exceeds response byte limit");
        std::cout.write(reply.constData(), reply.size());
        std::cout << '\n';
        return 0;
    } catch (const std::exception &error) {
        const auto reply =
            QJsonDocument(QJsonObject{{"protocol", 1},
                                      {"ok", false},
                                      {"error", QString::fromUtf8(error.what()).left(1024)}})
                .toJson(QJsonDocument::Compact);
        std::cout.write(reply.constData(), reply.size());
        std::cout << '\n';
        return 1;
    }
}
