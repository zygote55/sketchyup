#include "app/recovery_controller.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void wait(RecoveryController &controller) {
    check(QTest::qWaitFor([&] { return !controller.busy(); }, 10000),
          "Background recovery completed");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        auto root = temporary.filePath("recovery");
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        const auto identity = doc.identity();
        QString originalKey;
        {
            RecoveryController controller(doc, root, [] { return RecoveryContext{}; });
            controller.setInterval(0);
            controller.checkpoint();
            const auto captured = doc.revision();
            doc.move(1, {1, 0, 0});
            wait(controller);
            check(controller.error().isEmpty() && controller.durable() &&
                      controller.durable()->revision == captured && doc.dirty(),
                  "Background acknowledgement protects only the captured revision");
            originalKey = controller.durable()->key;
            controller.checkpoint();
            wait(controller);
            check(controller.durable()->revision == doc.revision(),
                  "Subsequent checkpoint advances acknowledged recovery");
            controller.setInterval(1);
            check(controller.interval() == 5, "Minimum interval bounded");
            controller.setInterval(10000);
            check(controller.interval() == 3600, "Maximum interval bounded");
            controller.setInterval(0);
        }
        auto recovered = readRecovery(root, originalKey);
        check(recovered.document && recovered.document->dirty() &&
                  recovered.document->identity() == identity,
              "Destruction retains recoverable work and releases active lock");
        doc = std::move(*recovered.document);
        {
            RecoveryController controller(doc, root, [] { return RecoveryContext{}; });
            controller.setInterval(0);
            controller.adopt(recovered.info);
            check(readRecovery(root, originalKey).verified,
                  "Original recovery retained before replacement");
            controller.checkpoint();
            wait(controller);
            check(controller.error().isEmpty() && controller.durable() &&
                      controller.durable()->key != originalKey &&
                      !QFileInfo::exists(QDir(root).filePath(originalKey)),
                  "Durable replacement retires adopted recovery only after acknowledgement");
            check(controller.discard().isEmpty() && listRecoveries(root).empty(),
                  "Explicit discard cleans current session");
        }
        const auto bad = temporary.filePath("obstructed");
        QFile obstacle(bad);
        check(obstacle.open(QIODevice::WriteOnly), "Create obstructed root");
        obstacle.close();
        {
            RecoveryController controller(doc, bad, [] { return RecoveryContext{}; });
            controller.setInterval(0);
            controller.checkpoint();
            wait(controller);
            check(!controller.error().isEmpty() && !controller.durable() && doc.dirty(),
                  "Storage failure claims no protection and preserves edits");
            check(QFile::remove(bad), "Remove obstruction");
            controller.checkpoint();
            wait(controller);
            check(controller.error().isEmpty() && controller.durable(),
                  "Explicit retry recovers after failure");
            controller.discard();
        }
        {
            RecoveryController controller(doc, root, [] { return RecoveryContext{}; });
            controller.setInterval(5);
            check(QTest::qWaitFor([&] { return controller.durable().has_value(); }, 8000),
                  "Configured timer writes recovery automatically");
            const auto before = controller.durable()->key;
            Document next;
            next.addFace({{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}}});
            doc = std::move(next);
            controller.checkpoint();
            wait(controller);
            check(controller.durable()->documentId == QString::fromStdString(doc.identity()) &&
                      readRecovery(root, before).verified,
                  "Document replacement isolates acknowledgement and retains un-discarded prior "
                  "work");
            doc.markSaved();
            check(QTest::qWaitFor([&] { return !controller.durable().has_value(); }, 8000),
                  "Returning to saved state retires stale recovery");
            discardRecovery(root, before);
        }
        std::cout
            << "Background recovery captured revisions, active locks, adoption, failure/retry, "
               "timer settings, document replacement and clean-state cleanup passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
