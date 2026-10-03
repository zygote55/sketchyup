#include "automation/commands.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <random>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    unsigned seed = 0;
    try {
        QTemporaryDir files;
        check(files.isValid(), "Temporary model directory");
        for (seed = 1; seed <= 24; ++seed) {
            std::mt19937 rng(seed);
            double w = 4 + double(rng() % 100) / 10, d = 4 + double(rng() % 100) / 10,
                   h = .5 + double(rng() % 100) / 50;
            Document doc;
            const auto body = doc.addFace({{{0, 0, 0}, {w, 0, 0}, {w, d, 0}, {0, d, 0}}});
            auto face = doc.bodies().at(body)->surface.faces.begin()->first;
            doc.splitEdge(body, doc.bodies().at(body)->topology.edges.begin()->first, .5);
            doc.pushPull(body, face, h);
            doc.insertEdges(body, {0, 0, h}, {0, 0, 1},
                            {{{{1, 1, h}, {w - 1, 1, h}}},
                             {{{w - 1, 1, h}, {w - 1, d - 1, h}}},
                             {{{w - 1, d - 1, h}, {1, d - 1, h}}},
                             {{{1, d - 1, h}, {1, 1, h}}}});
            Id selected = 0;
            for (const auto &[id, f] : doc.bodies().at(body)->surface.faces)
                if (doc.bodies().at(body)->surface.normal(id).z > .99 && f.loops.size() == 1 &&
                    std::abs(doc.bodies().at(body)->surface.area(id) - (w - 2) * (d - 2)) < 1e-7)
                    selected = id;
            check(selected != 0, "Selected inset region");
            auto before = doc.bodies().at(body);
            QJsonObject request{
                {"apiVersion", 1},
                {"documentId", QString::fromStdString(doc.identity())},
                {"expectedRevision", QString::number(doc.revision())},
                {"commands", QJsonArray{QJsonObject{{"command", "geometry.push_pull"},
                                                    {"body", QString::number(body)},
                                                    {"face", QString::number(selected)},
                                                    {"distance", -h}}}}};
            auto preview = previewBatch(doc, request);
            auto committed = executeBatch(doc, request);
            check(preview["changes"] == committed["changes"], "Preview/commit topology maps");
            double volume = 0;
            const auto &surface = doc.bodies().at(body)->surface;
            for (auto t : surface.triangles())
                volume += dot(t.a, cross(t.b, t.c)) / 6;
            check(std::abs(volume - (w * d - (w - 2) * (d - 2)) * h) < 1e-6, "Cut volume");
            for (auto e : surface.edges())
                check(e.faces.size() == 2, "Split/extrude/cut radial incidence");
            const auto committedBytes = encodeDocument(doc);
            auto after = doc.bodies().at(body);
            const auto path = files.filePath(QString::number(seed) + ".sketchyup");
            saveDocument(doc, path);
            auto reopened = loadDocument(path);
            check(encodeDocument(reopened) == committedBytes && !doc.dirty() && !reopened.dirty(),
                  "Durable save/reopen exact records");
            doc.undo();
            check(doc.bodies().at(body)->surface.faces == before->surface.faces &&
                      doc.bodies().at(body)->topology.edges == before->topology.edges,
                  "Cut undo exact IDs");
            check(doc.dirty(), "Undo after save is dirty");
            doc.redo();
            check(doc.bodies().at(body)->surface.faces == after->surface.faces &&
                      doc.bodies().at(body)->topology.edges == after->topology.edges &&
                      !doc.dirty(),
                  "Redo returns saved content");
            saveDocument(doc, path);
            check(encodeDocument(loadDocument(path + ".bak")) == encodeDocument(reopened),
                  "Previous valid model retained");
        }
        std::cout << "24 seeded split/extrude/cut/preview/save/reopen/undo/redo/backup sequences "
                     "passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Geometry persistence seed=" << seed << ": " << e.what() << '\n';
        return 1;
    }
}
