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
        std::cout << "Shortcut preferences: restart, explicit conflicts, upgrades, unknown values, "
                     "reserved keys and malformed/future versions passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
