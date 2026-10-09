#include "drm_memory.hpp"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using namespace sketchy::benchmark;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QByteArray record(const QByteArray &memory, const QByteArray &device = "0000:03:00.0",
                  const QByteArray &client = "7") {
    return "pos: 0\nflags: 02000002\ndrm-driver: amdgpu\ndrm-pdev: " + device +
           "\ndrm-client-id: " + client + "\n" + memory;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        check(drmBytes("0") == 0 && drmBytes("17") == 17 && drmBytes("2 KiB") == 2048 &&
                  drmBytes("3\tMiB") == 3145728,
              "Standard byte units preserve exact values");
        for (const auto &value : {"", "-1", "+1", "1.5", "1 GiB", "1 KiB extra",
                                  "18446744073709551616", "18446744073709551615 MiB"})
            check(!drmBytes(value), "Malformed or overflowing counts are unavailable");
        const auto first = record("drm-total-vram: 4 MiB\ndrm-resident-vram: 2 MiB\n"
                                  "drm-memory-vram: 2048 KiB\ndrm-shared-vram: 1 MiB\n"
                                  "drm-active-vram: 512 KiB\ndrm-purgeable-vram: 0\n");
        auto report = drmMemoryRecords({first, first, "pos: 0\nflags: 0\n"});
        auto clients = report["clients"].toArray();
        check(report["available"].toBool() && !report["partial"].toBool() && clients.size() == 1 &&
                  report["duplicateDescriptors"] == 1,
              "Duplicate descriptors count the DRM client once");
        auto region = clients.first().toObject()["regions"].toObject()["vram"].toObject();
        check(region["totalBytes"] == "4194304" && region["residentBytes"] == "2097152" &&
                  region["sharedBytes"] == "1048576" && region["activeBytes"] == "524288" &&
                  region["purgeableBytes"] == "0" && report["residentUpperBoundBytes"] == "2097152",
              "Categories stay separate and legacy aliases do not double count");
        report = drmMemoryRecords({first, record("drm-memory-vram: 1 KiB\n", "0000:04:00.0"),
                                   record("drm-resident-gtt: 2 KiB\n", "0000:03:00.0", "8")});
        check(report["clients"].toArray().size() == 3 &&
                  report["residentUpperBoundBytes"] == "2100224",
              "Distinct devices and clients retain independent regions");
        report = drmMemoryRecords({record("drm-memory-vram: 1 KiB\n"
                                          "drm-resident-vram: 2 KiB\n")});
        check(report["partial"].toBool() && !report["available"].toBool() &&
                  report["residentUpperBoundBytes"].isNull(),
              "Conflicting aliases cannot become a valid memory sample");
        for (const auto &memory : {"drm-resident-vram: -1\n", "drm-resident-vram: 1 GiB\n",
                                   "drm-resident-: 2 KiB\n", "drm-total-vram: nonsense\n"}) {
            report = drmMemoryRecords({record(memory)});
            check(report["partial"].toBool() && report["invalidClients"] == 1,
                  "Malformed standardized counters are reported as incomplete");
        }
        report = drmMemoryRecords({"drm-driver: xe\ndrm-total-system: 3 KiB\n"});
        check(report["partial"].toBool() && !report["available"].toBool(),
              "A missing client identity cannot be safely deduplicated");
        report = drmMemoryRecords({record("drm-total-vram: 1 KiB\n")});
        check(report["available"].toBool() && report["residentUpperBoundBytes"].isNull(),
              "Absent resident accounting stays unknown rather than zero");
        report = drmMemoryRecords({record("drm-resident-vram: 0\n")});
        check(report["available"].toBool() && report["residentUpperBoundBytes"] == "0",
              "An explicitly reported zero differs from unavailable telemetry");
        report = drmMemoryRecords({first, record("drm-total-vram: 3 MiB\n", "0000:03:00.0", "8")});
        check(report["available"].toBool() && report["partial"].toBool() &&
                  report["residentAccountingIncomplete"].toBool() &&
                  report["residentUpperBoundBytes"].isNull() &&
                  report["clients"].toArray().size() == 2,
              "A client without resident counts cannot produce a complete aggregate");
        report = drmMemoryRecords({record("drm-resident-vram: 1 MiB\n"
                                          "drm-total-gtt: 2 MiB\n")});
        check(report["partial"].toBool() && report["residentUpperBoundBytes"].isNull(),
              "A region without resident counts keeps the aggregate unavailable");
        report = drmMemoryRecords({first, record("", "0000:03:00.0", "8")});
        check(report["partial"].toBool() && report["residentUpperBoundBytes"].isNull(),
              "A recognized DRM client without memory telemetry remains incomplete");
        report = drmMemoryRecords({record("drm-resident-vram: 18446744073709551615\n"),
                                   record("drm-resident-vram: 1\n", "0000:03:00.0", "8")});
        check(report["aggregateOverflow"].toBool() && report["partial"].toBool() &&
                  report["residentUpperBoundBytes"].isNull(),
              "Aggregate overflow cannot wrap into an apparent budget pass");
        report = drmMemoryRecords({first}, 1);
        check(report["available"].toBool() && report["partial"].toBool(),
              "Read failures remain visible alongside valid client data");
        report = drmMemoryRecords({"pos: 0\nflags: 0\nunrelated: not collected\n"});
        check(!report["available"].toBool() && report["clients"].toArray().isEmpty() &&
                  report["residentUpperBoundBytes"].isNull() &&
                  !report["releaseAcceptance"].toBool(),
              "No DRM data is not evidence of zero graphics memory or acceptance");
        std::cout << "DRM units, descriptor identity, separate categories, aliases and unavailable "
                     "or corrupt accounting passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
