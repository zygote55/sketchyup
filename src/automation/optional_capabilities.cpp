#include "automation/optional_capabilities.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
namespace sketchy {
namespace {
bool helperPresent(const QString &name) {
    const auto directory = QCoreApplication::applicationDirPath();
    for (const auto &path : {directory + "/" + name, directory + "/../lib/sketchyup/" + name}) {
        const QFileInfo info(path);
        if (info.isFile() && info.isExecutable())
            return true;
    }
    return false;
}
QJsonObject helper(bool present, QString probe, QString alternative) {
    return {{"supported", true},
            {"executablePresent", present},
            {"readiness", present ? "not-verified" : "unavailable"},
            {"discovery", probe},
            {"alternative", alternative}};
}
} // namespace
QJsonObject optionalCapabilities() {
    return {
        {"apiVersion", 1},
        {"scope", "headless-process"},
        {"sideEffects", "none"},
        {"blender", helper(!QStandardPaths::findExecutable("blender").isEmpty(),
                           "PATH presence only; desktop executable overrides are not read",
                           "Use native viewport feedback or export GLB for an external renderer")},
        {"textGeometry", helper(helperPresent("sketchyup-text-worker"),
                                "Adjacent or installed application helper; fonts are not probed",
                                "Keep existing geometry; use plain geometry when acceptable")},
        {"extensions", helper(helperPresent("sketchyup-extension-worker"),
                              "Adjacent or installed application helper; packages are not loaded",
                              "Use the same public modeling commands directly")},
        {"providers",
         QJsonObject{{"readiness", "not-probed"},
                     {"credentialsRead", false},
                     {"networkRequests", false},
                     {"alternative", "Use manual tools or deterministic local recipes"}}},
        {"editor",
         QJsonObject{
             {"selectionAvailable", false},
             {"viewCaptureAvailable", false},
             {"alternative",
              "Use bounded geometry queries or connect to an explicitly launched native editor"}}},
        {"notes",
         QJsonArray{
             "Presence is not compatible execution or authentication",
             "Operational validation remains required before claiming success",
             "No executable paths, user configuration or credential contents are returned"}}};
}
} // namespace sketchy
