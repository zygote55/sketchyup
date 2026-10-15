#include "app/shortcut_bindings.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
using namespace sketchy;
namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejected(F run) {
    bool failed = false;
    try {
        run();
    } catch (const std::runtime_error &) {
        failed = true;
    }
    check(failed, "Invalid shortcut operation must reject");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const ShortcutMap defaults{{"tool.rectangle", QKeySequence("R")},
                                   {"tool.pushPull", QKeySequence("P")},
                                   {"file.save", QKeySequence("Ctrl+S")},
                                   {"view.extra", {}}};
        ShortcutBindings bindings(defaults);
        check(bindings.effective() == defaults, "Unversioned absent preferences use defaults");
        const auto initial = bindings.encode();
        rejected([&] { bindings.assign("tool.pushPull", QKeySequence("R")); });
        check(bindings.encode() == initial, "Rejected conflict preserves original choices");
        check(bindings.conflicts("tool.pushPull", QKeySequence("R")) ==
                  QStringList{"tool.rectangle"},
              "Conflict identifies displaced action");
        bindings.assign("tool.pushPull", QKeySequence("R"), true);
        ShortcutBindings restarted(defaults, bindings.encode());
        check(restarted.effective()["tool.rectangle"].isEmpty() &&
                  restarted.effective()["tool.pushPull"] == QKeySequence("R"),
              "Explicit reassignment and unbinding survive restart");
        const auto unchanged = restarted.encode();
        for (const auto *key : {"Meta+R", "Escape", "Tab", "Return", "Up", "Ctrl+K, Ctrl+R"})
            rejected([&] { restarted.assign("tool.pushPull", QKeySequence(key)); });
        rejected([&] { restarted.assign("missing", QKeySequence("X")); });
        check(restarted.encode() == unchanged, "Invalid choices are atomic");
        QJsonObject saved{{"apiVersion", 1},
                          {"future", QJsonObject{{"opaque", 42}}},
                          {"bindings", QJsonObject{{"file.save", "Ctrl+Alt+S"},
                                                   {"future.action", "Future key syntax"}}}};
        auto newerDefaults = defaults;
        newerDefaults["view.new"] = QKeySequence("Ctrl+Alt+S");
        ShortcutBindings upgraded(newerDefaults, QJsonDocument(saved).toJson());
        check(upgraded.effective()["file.save"] == QKeySequence("Ctrl+Alt+S") &&
                  upgraded.effective()["view.new"].isEmpty() && !upgraded.notices().isEmpty(),
              "User binding wins over newly introduced default with a visible notice");
        upgraded.assign("view.extra", QKeySequence("Ctrl+Alt+E"));
        upgraded.resetKnown();
        const auto reset = QJsonDocument::fromJson(upgraded.encode()).object();
        check(reset["future"] == saved["future"] &&
                  reset["bindings"].toObject()["future.action"] == "Future key syntax" &&
                  upgraded.effective() == newerDefaults,
              "Reset preserves unknown settings and restores known defaults");
        auto duplicate = saved;
        duplicate["bindings"] = QJsonObject{{"file.save", "X"}, {"tool.rectangle", "X"}};
        ShortcutBindings collision(defaults, QJsonDocument(duplicate).toJson());
        check(collision.effective()["file.save"].isEmpty() &&
                  collision.effective()["tool.rectangle"].isEmpty() &&
                  !collision.notices().isEmpty(),
              "Conflicting saved bindings never activate ambiguously");
        check(collision.conflicts("view.extra", QKeySequence("X")).size() == 2,
              "Inactive saved conflicts still require explicit resolution");
        collision.assign("view.extra", QKeySequence("X"), true);
        ShortcutBindings resolved(defaults, collision.encode());
        check(resolved.notices().isEmpty() &&
                  resolved.effective()["view.extra"] == QKeySequence("X"),
              "One explicit reassignment resolves all conflicting saved owners");
        const auto reserved = [](const QString &id, const QKeySequence &key) -> QString {
            return id == "file.save" && key == QKeySequence("Ctrl+Shift+M")
                       ? "Shortcut belongs to a model panel"
                       : QString{};
        };
        auto protectedSaved = saved;
        protectedSaved["bindings"] =
            QJsonObject{{"file.save", "Ctrl+Shift+M"}, {"future.action", "opaque"}};
        ShortcutBindings guarded(defaults, QJsonDocument(protectedSaved).toJson(), reserved);
        check(guarded.effective()["file.save"].isEmpty() && !guarded.notices().isEmpty() &&
                  QJsonDocument::fromJson(guarded.encode()).object() == protectedSaved,
              "New panel reservation disables old global binding without destroying preferences");
        const auto guardedBefore = guarded.encode();
        rejected([&] { guarded.assign("file.save", QKeySequence("Ctrl+Shift+M"), true); });
        check(guarded.encode() == guardedBefore,
              "Explicit reassignment cannot remove panel binding");
        ShortcutBindings disjoint(defaults, {}, reserved);
        disjoint.assign("tool.rectangle", QKeySequence("Ctrl+Shift+M"));
        check(disjoint.effective()["tool.rectangle"] == QKeySequence("Ctrl+Shift+M"),
              "Guard allows disjoint viewport scope");
        guarded.assign("file.save", QKeySequence("Ctrl+Alt+S"));
        check(guarded.notices().isEmpty(), "A different global key resolves panel reservation");
        for (const auto bytes :
             {QByteArray("invalid"), QByteArray("[]"),
              QByteArray("{\"apiVersion\":2,\"bindings\":{}}"),
              QByteArray("{\"apiVersion\":1,\"bindings\":{\"file.save\":3}}"),
              QByteArray("{\"apiVersion\":1,\"bindings\":{\"file.save\":\"Escape\"}}"),
              QByteArray(128 * 1024 + 1, ' ')})
            rejected([&] { ShortcutBindings invalid(defaults, bytes); });
        // ADR 0164 §4: workspace scopes. The same key may bind once in Model and once in
        // Sheets; Global conflicts with both.
        const ShortcutMap scoped{{"tool.line", QKeySequence("L")},
                                 {"tool.pan", QKeySequence("H")},
                                 {"view.fit", QKeySequence("Shift+Z")},
                                 {"sheet.tool.pan", QKeySequence("H")},
                                 {"sheet.view.fit", QKeySequence("Shift+Z")},
                                 {"sheet.tool.zoom", QKeySequence("Z")},
                                 {"sheet.page.add", QKeySequence("Ctrl+Shift+N")},
                                 {"view.commands", QKeySequence("Ctrl+K")},
                                 {"file.save", QKeySequence("Ctrl+S")}};
        const ShortcutScopes scopes{
            {"tool.line", ShortcutScope::Model},       {"tool.pan", ShortcutScope::Model},
            {"view.fit", ShortcutScope::Model},        {"sheet.tool.pan", ShortcutScope::Sheets},
            {"sheet.view.fit", ShortcutScope::Sheets}, {"sheet.tool.zoom", ShortcutScope::Sheets},
            {"sheet.page.add", ShortcutScope::Sheets}};
        rejected([&] { ShortcutBindings unscoped(scoped); });
        ShortcutBindings workspaces(scoped, {}, {}, scopes);
        check(workspaces.effective() == scoped && workspaces.notices().isEmpty(),
              "Equal defaults in Model and Sheets are both active");
        check(workspaces.scope("view.commands") == ShortcutScope::Global &&
                  workspaces.scope("sheet.tool.pan") == ShortcutScope::Sheets,
              "Unlisted actions are Global");
        check(workspaces.conflicts("sheet.tool.zoom", QKeySequence("L")).isEmpty(),
              "A sheet tool may use the model Line key");
        check(workspaces.conflicts("sheet.tool.zoom", QKeySequence("H")) ==
                  QStringList{"sheet.tool.pan"},
              "Keys still conflict within the Sheets workspace");
        check(workspaces.conflicts("sheet.tool.zoom", QKeySequence("Ctrl+K")) ==
                  QStringList{"view.commands"},
              "A Global key conflicts with Sheets actions");
        check(workspaces.conflicts("view.commands", QKeySequence("H")) ==
                  QStringList{"sheet.tool.pan", "tool.pan"},
              "A Global action conflicts with both workspaces");
        workspaces.assign("sheet.tool.zoom", QKeySequence("L"));
        check(workspaces.effective()["sheet.tool.zoom"] == QKeySequence("L") &&
                  workspaces.effective()["tool.line"] == QKeySequence("L"),
              "Assigning a model key to a sheet tool keeps both bindings");
        rejected([&] { workspaces.assign("view.commands", QKeySequence("L")); });
        workspaces.assign("view.commands", QKeySequence("L"), true);
        check(workspaces.effective()["tool.line"].isEmpty() &&
                  workspaces.effective()["sheet.tool.zoom"].isEmpty() &&
                  workspaces.effective()["view.commands"] == QKeySequence("L"),
              "Reassigning to a Global action displaces both workspace bindings");
        // Migration: saved preferences from before scopes keep their meaning. A saved model
        // binding equal to a new Sheets default no longer conflicts; a saved Global one still
        // wins over the new default with a notice.
        const auto oldSaved =
            QJsonDocument(QJsonObject{{"apiVersion", 1},
                                      {"bindings", QJsonObject{{"tool.line", "Ctrl+Shift+N"},
                                                               {"future.action", "F13"}}}})
                .toJson();
        ShortcutBindings migrated(scoped, oldSaved, {}, scopes);
        check(migrated.effective()["tool.line"] == QKeySequence("Ctrl+Shift+N") &&
                  migrated.effective()["sheet.page.add"] == QKeySequence("Ctrl+Shift+N") &&
                  migrated.notices().isEmpty() &&
                  migrated.encode() ==
                      QJsonDocument::fromJson(oldSaved).toJson(QJsonDocument::Compact),
              "An old model binding and a new Sheets default are both active; bytes unchanged");
        const auto oldGlobal =
            QJsonDocument(QJsonObject{{"apiVersion", 1},
                                      {"bindings", QJsonObject{{"file.save", "Ctrl+Shift+N"}}}})
                .toJson();
        ShortcutBindings shadowed(scoped, oldGlobal, {}, scopes);
        check(shadowed.effective()["file.save"] == QKeySequence("Ctrl+Shift+N") &&
                  shadowed.effective()["sheet.page.add"].isEmpty() &&
                  shadowed.notices().size() == 1 &&
                  shadowed.notices().front().contains("sheet.page.add"),
              "A saved Global binding keeps winning over a new Sheets default");
        // Saved choices that collide only across workspaces become active again; ones that
        // collide within a workspace stay inactive until resolved.
        const auto crossSaved =
            QJsonDocument(
                QJsonObject{{"apiVersion", 1},
                            {"bindings", QJsonObject{{"tool.pan", "J"}, {"sheet.tool.pan", "J"}}}})
                .toJson();
        ShortcutBindings cross(scoped, crossSaved, {}, scopes);
        check(cross.effective()["tool.pan"] == QKeySequence("J") &&
                  cross.effective()["sheet.tool.pan"] == QKeySequence("J") &&
                  cross.notices().isEmpty(),
              "Saved bindings that differ in workspace are both active");
        const auto sameSaved =
            QJsonDocument(QJsonObject{{"apiVersion", 1},
                                      {"bindings", QJsonObject{{"tool.pan", "J"},
                                                               {"tool.line", "J"},
                                                               {"sheet.tool.pan", "J"}}}})
                .toJson();
        ShortcutBindings same(scoped, sameSaved, {}, scopes);
        check(same.effective()["tool.pan"].isEmpty() && same.effective()["tool.line"].isEmpty() &&
                  same.effective()["sheet.tool.pan"] == QKeySequence("J") &&
                  same.notices().size() == 1 && !same.notices().front().contains("sheet."),
              "Only the bindings that conflict within a workspace are disabled");
        std::cout << "Shortcut preferences: restart, explicit conflicts, upgrades, unknown values, "
                     "reserved keys, malformed/future versions and workspace scopes (conflicts, "
                     "migration) passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
