#pragma once
// Benchmark-only Linux DRM accounting; never scans another process.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace sketchy::benchmark {
inline std::optional<quint64> drmBytes(const QString &text) {
    const auto parts = text.simplified().split(' ');
    if (parts.empty() || parts.size() > 2 || parts[0].isEmpty())
        return {};
    for (const auto c : parts[0])
        if (c < '0' || c > '9')
            return {};
    quint64 scale = 1;
    if (parts.size() == 2) {
        if (parts[1] == "KiB")
            scale = 1024;
        else if (parts[1] == "MiB")
            scale = 1024 * 1024;
        else
            return {};
    }
    bool ok{};
    const auto value = parts[0].toULongLong(&ok);
    if (!ok || value > std::numeric_limits<quint64>::max() / scale)
        return {};
    return value * scale;
}

inline QJsonObject drmMemoryRecords(const QList<QByteArray> &records, int readFailures = 0) {
    QJsonArray clients;
    std::set<std::tuple<QString, QString, QString>> seen;
    int duplicates{}, invalid{}, memoryClients{};
    std::optional<quint64> residentUpperBound;
    bool aggregateOverflow{}, residentAccountingIncomplete{};
    for (const auto &bytes : records) {
        QString driver, device, client;
        std::map<QString, std::map<QString, quint64>> regions;
        bool malformed{};
        for (const auto &raw : bytes.split('\n')) {
            const auto colon = raw.indexOf(':');
            if (colon < 0)
                continue;
            const auto key = QString::fromLatin1(raw.left(colon));
            const auto value = QString::fromLatin1(raw.mid(colon + 1)).trimmed();
            if (key == "drm-driver")
                driver = value;
            else if (key == "drm-pdev")
                device = value;
            else if (key == "drm-client-id") {
                const auto id = drmBytes(value);
                if (!id || value.contains(' ') || value.contains('\t'))
                    malformed = true;
                else
                    client = QString::number(*id);
            } else {
                const std::pair<const char *, const char *> prefixes[] = {
                    {"drm-total-", "totalBytes"},       {"drm-shared-", "sharedBytes"},
                    {"drm-resident-", "residentBytes"}, {"drm-memory-", "residentBytes"},
                    {"drm-active-", "activeBytes"},     {"drm-purgeable-", "purgeableBytes"}};
                for (const auto &[prefix, category] : prefixes) {
                    if (!key.startsWith(prefix))
                        continue;
                    const auto region = key.mid(qsizetype(qstrlen(prefix)));
                    const auto count = drmBytes(value);
                    if (region.isEmpty() ||
                        std::any_of(region.begin(), region.end(),
                                    [](QChar c) { return c.isSpace(); }) ||
                        !count) {
                        malformed = true;
                        break;
                    }
                    auto &values = regions[region];
                    const auto previous = values.find(category);
                    if (previous != values.end() && previous->second != *count)
                        malformed = true;
                    values[category] = *count;
                    break;
                }
            }
        }
        if (driver.isEmpty())
            continue;
        if (malformed || client.isEmpty()) {
            ++invalid;
            continue;
        }
        if (!seen.emplace(driver, device, client).second) {
            ++duplicates;
            continue;
        }
        QJsonObject memory;
        residentAccountingIncomplete |= regions.empty();
        for (const auto &[region, values] : regions) {
            residentAccountingIncomplete |= !values.contains("residentBytes");
            QJsonObject categories;
            for (const auto &[category, count] : values) {
                // Decimal strings preserve uint64 precision in JSON.
                categories[category] = QString::number(count);
                if (category == "residentBytes" && !aggregateOverflow) {
                    if (!residentUpperBound)
                        residentUpperBound = 0;
                    if (count > std::numeric_limits<quint64>::max() - *residentUpperBound) {
                        aggregateOverflow = true;
                        residentUpperBound.reset();
                    } else {
                        *residentUpperBound += count;
                    }
                }
            }
            memory[region] = categories;
        }
        memoryClients += !regions.empty();
        clients.append(QJsonObject{
            {"driver", driver}, {"device", device}, {"clientId", client}, {"regions", memory}});
    }
    if (residentAccountingIncomplete)
        residentUpperBound.reset();
    const bool available = memoryClients > 0;
    const bool partial =
        readFailures > 0 || invalid > 0 || aggregateOverflow || residentAccountingIncomplete;
    return {{"available", available},
            {"partial", partial},
            {"clients", clients},
            {"duplicateDescriptors", duplicates},
            {"invalidClients", invalid},
            {"readFailures", readFailures},
            {"aggregateOverflow", aggregateOverflow},
            {"residentAccountingIncomplete", residentAccountingIncomplete},
            {"residentUpperBoundBytes", residentUpperBound
                                            ? QJsonValue(QString::number(*residentUpperBound))
                                            : QJsonValue(QJsonValue::Null)},
            {"scope", "Kernel-reported DRM client buffer objects of this process. Duplicate file "
                      "descriptors are counted once; shared buffers across distinct clients or "
                      "regions may overlap, so the resident sum is an upper bound. Categories "
                      "overlap and must not be added to each other or to RSS. Driver internals "
                      "and allocations outside this interface remain unaccounted."},
            {"releaseAcceptance", false}};
}

inline QJsonObject processDrmMemory() {
#ifdef __linux__
    QList<QByteArray> records;
    int failures{};
    const QDir directory("/proc/self/fdinfo");
    const auto names = directory.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    if (names.size() > 4096)
        return drmMemoryRecords({}, 1);
    for (const auto &name : names) {
        // Ignore ordinary files and the transient directory descriptor created
        // by enumeration; do not collect their fdinfo or expose their paths.
        if (!QFileInfo("/proc/self/fd/" + name).symLinkTarget().startsWith("/dev/dri/"))
            continue;
        QFile file(directory.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            ++failures;
            continue;
        }
        const auto bytes = file.read(16385);
        if (file.error() != QFileDevice::NoError || bytes.size() > 16384) {
            ++failures;
            continue;
        }
        records.append(bytes);
    }
    return drmMemoryRecords(records, failures);
#else
    return drmMemoryRecords({});
#endif
}
} // namespace sketchy::benchmark
