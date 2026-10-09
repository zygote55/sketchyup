#include "automation/transaction_coordinator.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <array>
#include <charconv>
#include <iostream>
#include <random>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read sequence fixture");
    return file.readAll();
}
unsigned number(const char *text) {
    unsigned result{};
    const auto end = text + std::char_traits<char>::length(text);
    const auto parsed = std::from_chars(text, end, result);
    check(parsed.ec == std::errc{} && parsed.ptr == end, "Expected unsigned seed");
    return result;
}
struct Receipt {
    QString id, hash;
    QJsonObject outcome;
};
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    unsigned seed{}, step{}, mode{};
    try {
        check(argc == 1 || (argc == 3 && std::string(argv[1]) == "--seed"),
              "Usage: transaction_sequence_tests [--seed N]");
        const unsigned first = argc == 3 ? number(argv[2]) : 0x8300;
        const unsigned count = argc == 3 ? 1 : 8;
        QTemporaryDir files;
        check(files.isValid(), "Private transaction sequence directory");
        std::array<unsigned, 8> coverage{};
        unsigned committed{}, aborted{}, reconciled{}, restarted{};
        for (unsigned index = 0; index < count; ++index) {
            seed = first + index;
            std::mt19937 random(seed);
            Document original;
            const auto body = original.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
            const auto neighbor = original.addFace({{{20, 0, 0}, {21, 0, 0}, {20, 1, 0}}});
            const auto untouched = *original.bodies().at(neighbor);
            const auto source = files.filePath(QString::number(seed) + "-original.sketchyup");
            saveDocument(original, source);
            const auto originalFile = read(source);
            const auto root = files.filePath(QString::number(seed) + "-outcomes");
            bool armed{};
            auto phase = OutcomeStore::Phase::BeforeWrite;
            TransactionCoordinator::Options options;
            options.fault = [&](auto observed) {
                if (armed && phase == observed)
                    throw std::runtime_error("sequence publication interruption");
            };
            auto actor = std::make_unique<TransactionCoordinator>(original, root, options);
            std::vector<Receipt> receipts;
            Vec3 expected{};
            for (step = 0; step < 32; ++step) {
                mode = step < 8 ? step : random() % 8;
                ++coverage[mode];
                const Vec3 delta{double(1 + random() % 8) / 8, double(int(random() % 9) - 4) / 8,
                                 0};
                const auto before = encodeContainer(actor->document());
                const auto beforeBody = *actor->document().bodies().at(body);
                const auto revision = actor->document().revision();
                QJsonObject batch{
                    {"apiVersion", 1},
                    {"documentId", QString::fromStdString(actor->document().identity())},
                    {"expectedRevision", QString::number(revision)},
                    {"commands",
                     QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                            {"body", QString::number(body)},
                                            {"delta", QJsonArray{delta.x, delta.y, delta.z}}}}}};
                const auto prepared = actor->prepare(batch);
                Receipt receipt{
                    prepared["requestId"].toString(), prepared["payloadHash"].toString(), {}};
                actor->preview(receipt.id, receipt.hash);
                check(encodeContainer(actor->document()) == before,
                      "Preparation and preview preserve live state");
                if (mode == 1) {
                    receipt.outcome = actor->cancel(receipt.id, receipt.hash);
                    check(receipt.outcome["status"] == "aborted" &&
                              encodeContainer(actor->document()) == before,
                          "Cancellation preserves exact live state");
                    ++aborted;
                } else if (mode == 2) {
                    actor->edit([&](Document &doc) {
                        doc.move(body, {0, 0, .25});
                        doc.undo();
                    });
                    const auto intervening = encodeContainer(actor->document());
                    receipt.outcome = actor->commit(receipt.id, receipt.hash);
                    check(receipt.outcome["status"] == "aborted" &&
                              receipt.outcome["result"].toObject()["reason"] == "STALE_REVISION" &&
                              encodeContainer(actor->document()) == intervening &&
                              *actor->document().bodies().at(body) == beforeBody,
                          "Edit/undo invalidates staging without replay or geometry loss");
                    ++aborted;
                } else {
                    if (mode >= 3) {
                        const std::array phases{
                            OutcomeStore::Phase::BeforeWrite, OutcomeStore::Phase::AfterWrite,
                            OutcomeStore::Phase::AfterFileSync, OutcomeStore::Phase::AfterRename,
                            OutcomeStore::Phase::AfterDirectorySync};
                        phase = phases.at(mode - 3);
                        armed = true;
                        bool rejected{};
                        try {
                            actor->commit(receipt.id, receipt.hash);
                        } catch (const std::runtime_error &) {
                            rejected = true;
                        }
                        armed = false;
                        check(rejected && encodeContainer(actor->document()) == before,
                              "Interrupted commit never publishes an unconfirmed live edit");
                        if (mode >= 6) {
                            check(actor->uncertain() &&
                                      actor->status(receipt.id, receipt.hash)["status"] ==
                                          "unknown",
                                  "Replacement interruption exposes an unknown outcome");
                            bool blocked{};
                            try {
                                actor->edit([&](Document &doc) { doc.move(body, {50, 0, 0}); });
                            } catch (const InspectionError &error) {
                                blocked = error.code() == "OUTCOME_UNKNOWN";
                            }
                            check(blocked && encodeContainer(actor->document()) == before,
                                  "Unknown outcomes prevent unrelated mutation");
                            receipt.outcome = actor->reconcile();
                            ++reconciled;
                        } else {
                            check(!actor->uncertain() &&
                                      actor->status(receipt.id, receipt.hash)["status"] ==
                                          "pending",
                                  "Pre-replacement interruption remains safely retryable");
                        }
                    }
                    if (receipt.outcome.isEmpty())
                        receipt.outcome = actor->commit(receipt.id, receipt.hash);
                    check(receipt.outcome["status"] == "committed" &&
                              actor->document().revision() == revision + 1 && !actor->uncertain(),
                          "Confirmed or reconciled transaction publishes exactly one revision");
                    expected = expected + delta;
                    ++committed;
                    const auto afterBody = *actor->document().bodies().at(body);
                    actor->edit([](Document &doc) { doc.undo(); });
                    check(*actor->document().bodies().at(body) == beforeBody &&
                              actor->status(receipt.id, receipt.hash) == receipt.outcome,
                          "Undo restores exact content without erasing durable receipt");
                    actor->edit([](Document &doc) { doc.redo(); });
                    check(*actor->document().bodies().at(body) == afterBody,
                          "Redo restores exactly one confirmed edit");
                }
                const auto completed = encodeContainer(actor->document());
                check(actor->commit(receipt.id, receipt.hash) == receipt.outcome &&
                          actor->cancel(receipt.id, receipt.hash) == receipt.outcome &&
                          encodeContainer(actor->document()) == completed,
                      "Duplicate commit and late cancel are immutable receipt lookups");
                receipts.push_back(receipt);
                const auto actual = actor->document().worldTransform(body).point({0, 0, 0});
                check(length(actual - expected) < 1e-12 &&
                          *actor->document().bodies().at(neighbor) == untouched,
                      "Independent translation oracle and untouched neighbor agree");
                check(encodeContainer(decodeContainer(completed)) == completed,
                      "Every transition survives authoritative native validation");
                if (step % 8 == 7) {
                    auto checkpoint = actor->document();
                    const auto path =
                        files.filePath(QString::number(seed) + "-checkpoint.sketchyup");
                    saveDocument(checkpoint, path);
                    check(encodeContainer(loadDocument(path)) == completed &&
                              read(source) == originalFile,
                          "Explicit save/reopen preserves state and original source");
                    actor.reset();
                    actor = std::make_unique<TransactionCoordinator>(std::move(checkpoint), root,
                                                                     options);
                    for (const auto &prior : receipts)
                        check(actor->status(prior.id, prior.hash) == prior.outcome,
                              "Restart preserves all prior terminal receipts");
                    check(encodeContainer(actor->document()) == completed,
                          "Restart never replays already committed commands");
                    ++restarted;
                }
            }
        }
        QJsonArray counts;
        for (const auto value : coverage) {
            check(value > 0, "All sequence paths covered");
            counts.append(int(value));
        }
        std::cout << QJsonDocument(QJsonObject{{"firstSeed", qint64(first)},
                                               {"seeds", int(count)},
                                               {"stepsPerSeed", 32},
                                               {"modeCounts", counts},
                                               {"committed", int(committed)},
                                               {"aborted", int(aborted)},
                                               {"reconciled", int(reconciled)},
                                               {"restarted", int(restarted)}})
                         .toJson()
                         .toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Transaction sequence seed=" << seed << " step=" << step << " mode=" << mode
                  << ": " << error.what() << "\nReplay: transaction_sequence_tests --seed " << seed
                  << '\n';
        return 1;
    }
}
