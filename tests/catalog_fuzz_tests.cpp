// R100.c (ADR 0174 section 10): the catalog decoder's libFuzzer target runs on its seeds twice with
// the same result. This test calls the same harness entry point without libFuzzer, so the
// determinism check runs in every build, not only in the Clang fuzz configuration.
#include "catalog_fixtures.hpp"
#include "fuzz/parser_harness.hpp"
#include <QCoreApplication>
#include <QFile>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
// Returns whether the input is accepted, after checking that a second run agrees.
bool runTwice(const QByteArray &input) {
    const bool first = fuzz::parseInput(QStringLiteral("catalog"), input, QString());
    const bool second = fuzz::parseInput(QStringLiteral("catalog"), input, QString());
    check(first == second, "the same catalog input gave different results");
    return first;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        std::vector<QByteArray> seeds;
        const auto generated = catalog_fixture::fixtures();
        for (auto it = generated.constBegin(); it != generated.constEnd(); ++it)
            seeds.push_back(it.value());
        for (const auto *name :
             {"basic", "missing-dependency", "tampered-entry", "tampered-asset"}) {
            QFile file(QStringLiteral(SOURCE_DIR "/tests/fixtures/catalog/") +
                       QString::fromLatin1(name) + QStringLiteral(".sketchycat"));
            check(file.open(QIODevice::ReadOnly), "committed catalog seed exists");
            seeds.push_back(file.readAll());
        }
        std::size_t accepted{}, rejected{};
        for (const auto &seed : seeds)
            (runTwice(seed) ? accepted : rejected) += 1;
        // Mutations of the valid seed: every truncation step, and a flipped byte in the first
        // 512 bytes (the envelope header and the start of the manifest).
        const auto &good = seeds.front();
        for (qsizetype cut = 0; cut < good.size(); cut += 97)
            (runTwice(good.left(cut)) ? accepted : rejected) += 1;
        for (qsizetype i = 0; i < std::min<qsizetype>(good.size(), 512); ++i) {
            auto mutated = good;
            mutated[i] = char(mutated[i] ^ 0x40);
            (runTwice(mutated) ? accepted : rejected) += 1;
        }
        check(accepted >= seeds.size() && rejected > 0,
              "seeds are accepted and mutations rejected");
        std::cout << "catalog fuzz seeds ran twice with the same result: " << accepted
                  << " accepted, " << rejected << " rejected\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
