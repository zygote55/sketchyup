#include "text/sandbox_policy.hpp"
#include "text/text_worker.hpp"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
// The sanitizer builds. LeakSanitizer checks for leaks at exit by stopping the helper's threads
// with ptrace, which the sandbox refuses (ADR 0183 section 3). The host gives the helper its own
// environment, so ASAN_OPTIONS cannot be relied on: this is the sanitized helper's own setting,
// detect_leaks=0 (as the .skp helper's).
extern "C" const char *__lsan_default_options() { return "detect_leaks=0"; }
#endif

int main(int argc, char **argv) {
    // R109 S4 (ADR 0183 section 5): the user's font folders and their fontconfig file come as
    // arguments. They are checked before the sandbox grants them. A bad argument is refused with
    // a reply and exit 1, not with the sandbox's exit 70: the sandbox has not refused anything.
    TextFontGrants fonts;
    if (const auto refusal = parseTextFontArguments(argc, argv, fonts)) {
        const auto reply = QJsonDocument(QJsonObject{{"protocol", 1},
                                                     {"ok", false},
                                                     {"error", QString::fromStdString(*refusal)}})
                               .toJson(QJsonDocument::Compact);
        std::cout.write(reply.constData(), reply.size());
        std::cout << '\n';
        return 1;
    }
    // R109 S3 (ADR 0183 section 4.1): the sandbox is installed before Qt starts and before the
    // request is read. A layer that cannot be installed refuses here, with exit 70 and nothing
    // parsed.
    if (const auto failure = applyTextSandbox(fonts))
        return refuseTextSandbox(*failure);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    qputenv("QT_IM_MODULE", "compose");
    // Qt is given the program name only: the font arguments are the host's, and Qt takes none.
    int qtArgc = 1;
    QGuiApplication app(qtArgc, argv);
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
