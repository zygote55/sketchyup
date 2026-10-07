// Opt-in component-construction measurement; timings are not a CTest pass gate.
#include "core/components.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <numbers>
#include <sys/resource.h>
using namespace sketchy;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        bool valid = argc == 1;
        const int count = argc == 1 ? 1000 : QString::fromLocal8Bit(argv[1]).toInt(&valid);
        if (argc > 2 || !valid || count < 1 || count > 1000)
            throw std::runtime_error("Instance count must be 1..1000");
        QElapsedTimer timer;
        timer.start();
        Document doc;
        std::vector<Vec3> loop;
        for (int i = 0; i < 26; ++i) {
            const auto angle = 2 * std::numbers::pi * i / 26;
            loop.push_back({std::cos(angle), std::sin(angle), 0});
        }
        const auto body = doc.addFace({loop});
        doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
        const auto component = createComponent(doc, body, "26-sided benchmark prism");
        const int columns = int(std::ceil(std::sqrt(count)));
        for (int i = 1; i < count; ++i)
            placeComponent(doc, component.definition,
                           Transform::translation({4.0 * (i % columns), 4.0 * (i / columns), 0}));
        const double constructionMs = timer.nsecsElapsed() / 1e6;
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) != 0)
            throw std::runtime_error("Cannot read benchmark RSS");
        auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
        json["documentId"] = "0000000000000000000000000000082a";
        doc = decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact));
        const auto bytes = encodeContainer(doc);
        size_t triangles{};
        for (const auto &[id, record] : doc.bodies()) {
            (void)id;
            triangles += record->surface.triangles().size();
        }
        if (triangles != size_t(count) * 100 || doc.instances().size() != size_t(count))
            throw std::runtime_error("Unexpected canonical benchmark geometry");
        QJsonObject report{
            {"fixtureVersion", 1},
            {"instances", count},
            {"triangles", qint64(triangles)},
            {"bodies", qint64(doc.bodies().size())},
            {"fixtureSha256",
             QString::fromLatin1(
                 QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
            {"nativeBytes", bytes.size()},
            {"constructionMs", constructionMs},
            {"constructionPeakRssBytes", qint64(usage.ru_maxrss) * 1024},
            {"timingScope", "Public face/extrude/component creation and placement; excludes "
                            "serialization, viewport and presentation"},
            {"releaseAcceptance", false}};
        std::cout << QJsonDocument(report).toJson().toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
