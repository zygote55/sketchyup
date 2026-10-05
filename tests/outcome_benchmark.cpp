#include "core/assets.hpp"
#include "io/outcome_store.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        if (!files.isValid())
            throw std::runtime_error("Cannot create benchmark directory");
        QJsonArray rows;
        auto measure = [&](QString name, Document doc) {
            const auto identity = QString::fromStdString(doc.identity());
            const auto source = encodeContainer(doc);
            OutcomeStore store(files.path() + "/" + name, identity);
            QJsonArray samples;
            for (int i = 0; i < 5; ++i) {
                auto candidate = doc;
                candidate.setDisplayUnits(doc.displayUnits() == DisplayUnit::Meters
                                              ? DisplayUnit::Millimeters
                                              : DisplayUnit::Meters);
                const auto hash = QString::fromLatin1(
                    QCryptographicHash::hash(QByteArray::number(i), QCryptographicHash::Sha256)
                        .toHex());
                QElapsedTimer timer;
                timer.start();
                const auto id = store.begin(doc, hash)["requestId"].toString();
                const auto beginMs = double(timer.nsecsElapsed()) / 1000000;
                timer.restart();
                store.commit(id, hash, doc, candidate,
                             {{"revision", QString::number(candidate.revision())}});
                const auto commitMs = double(timer.nsecsElapsed()) / 1000000;
                doc = std::move(candidate);
                samples.append(QJsonObject{{"beginMs", beginMs}, {"commitMs", commitMs}});
            }
            rows.append(QJsonObject{
                {"fixture", name},
                {"modelBytes", double(source.size())},
                {"modelSha256",
                 QString::fromLatin1(
                     QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex())},
                {"checkpointBytes", double(QFileInfo(store.directory() + "/OUTCOMES").size())},
                {"samples", samples}});
        };
        measure("empty", Document{});
        measure("room",
                loadDocument(QStringLiteral(SOURCE_DIR "/examples/m4-room-study.sketchyup")));
        Document sample;
        sample.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        Document many;
        std::map<Id, BodyPtr> bodies;
        for (Id i = 1; i <= 1000; ++i) {
            auto body = std::make_shared<Body>(*sample.bodies().begin()->second);
            body->id = i;
            bodies.emplace(i, std::move(body));
        }
        many.restore(many.identity(), 1001, std::move(bodies));
        measure("1000-faces", std::move(many));
        Document resource;
        createAsset(
            resource, "Owned benchmark payload", "application/octet-stream",
            std::make_shared<const AssetPayload>(std::vector<std::uint8_t>(8 * 1024 * 1024, 17)));
        measure("8MiB-asset", std::move(resource));
        std::cout << QJsonDocument(rows).toJson().toStdString();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
