#include "io/document_io.hpp"
#include <QCborValue>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <iostream>
#include <sys/resource.h>
using namespace sketchy;
Document fixture(int count) {
    Document doc;
    std::map<Id, BodyPtr> bodies;
    for (int i = 0; i < count; ++i) {
        auto body = std::make_shared<Body>();
        body->id = i + 1;
        body->name = "Wall ring " + std::to_string(i);
        body->color = {.7f, .6f, .5f};
        auto face =
            body->surface.addFace({{{0, 0, 0}, {6, 0, 0}, {6, 4, 0}, {0, 4, 0}},
                                   {{.2, .2, 0}, {.2, 3.8, 0}, {5.8, 3.8, 0}, {5.8, .2, 0}}});
        body->surface.extrude(face, 2.7);
        body->surface.translate({double(i % 20) * 8, double(i / 20) * 6, 0});
        bodies.emplace(body->id, body);
    }
    doc.restore("00000000000000000000000000000001", count + 1, std::move(bodies));
    return doc;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QJsonArray results;
        for (int count : {1, 100, 1000}) {
            auto doc = fixture(count);
            const auto canonical = encodeDocument(doc);
            const auto tree = QJsonDocument::fromJson(canonical).object();
            for (const auto &codec : {QString("json"), QString("cbor"), QString("compressed-json"),
                                      QString("container-v2")}) {
                QElapsedTimer timer;
                QByteArray encoded;
                QJsonObject decoded;
                double encodeMs = 0, decodeMs = 0;
                for (int repeat = 0; repeat < 5; ++repeat) {
                    timer.start();
                    if (codec == "container-v2")
                        encoded = encodeContainer(doc);
                    else if (codec == "json")
                        encoded = QJsonDocument(tree).toJson(QJsonDocument::Compact);
                    else if (codec == "cbor")
                        encoded = QCborValue::fromJsonValue(tree).toCbor();
                    else
                        encoded = qCompress(QJsonDocument(tree).toJson(QJsonDocument::Compact), 6);
                    encodeMs += timer.nsecsElapsed() / 1e6;
                    timer.restart();
                    if (codec == "container-v2")
                        decoded = QJsonDocument::fromJson(encodeDocument(decodeContainer(encoded)))
                                      .object();
                    else if (codec == "json")
                        decoded = QJsonDocument::fromJson(encoded).object();
                    else if (codec == "cbor")
                        decoded = QCborValue::fromCbor(encoded).toJsonValue().toObject();
                    else
                        decoded = QJsonDocument::fromJson(qUncompress(encoded)).object();
                    decodeMs += timer.nsecsElapsed() / 1e6;
                    if (decoded != tree)
                        throw std::runtime_error("Codec lost document fields");
                }
                timer.start();
                auto restored =
                    decodeDocument(QJsonDocument(decoded).toJson(QJsonDocument::Compact));
                const auto validateMs = timer.nsecsElapsed() / 1e6;
                if (encodeDocument(restored) != canonical)
                    throw std::runtime_error("Document roundtrip differs");
                results.append(QJsonObject{
                    {"fixtureSha256",
                     QString::fromLatin1(
                         QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex())},
                    {"bodies", count},
                    {"codec", codec},
                    {"bytes", encoded.size()},
                    {"encodeMs", encodeMs / 5},
                    {"decodeMs", decodeMs / 5},
                    {"validateMs", validateMs}});
            }
        }
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
#ifdef NDEBUG
        const auto buildMode = "release";
#else
        const auto buildMode = "debug";
#endif
        std::cout << QJsonDocument(QJsonObject{{"results", results},
                                               {"peakRssKiB", qint64(usage.ru_maxrss)},
                                               {"build", buildMode},
                                               {"compiler", __VERSION__},
                                               {"qt", qVersion()},
                                               {"platform", QSysInfo::prettyProductName()},
                                               {"kernel", QSysInfo::kernelVersion()}})
                         .toJson(QJsonDocument::Indented)
                         .toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
