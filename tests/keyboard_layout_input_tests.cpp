// Real keyboard-layout fixture. Keys are injected as physical X keycodes through
// XTest, so the X server (or Weston nested in it) applies the active XKB layout.
// QTest key events would bypass the layout entirely.
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QLocale>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstdlib>
#include <iostream>
#include <map>
#include <xcb/xcb.h>
#include <xcb/xtest.h>
using namespace sketchy;
namespace {
void check(bool value, const QString &message) {
    if (!value)
        throw std::runtime_error(message.toStdString());
}
// Linux evdev codes; the X keycode is the evdev code plus eight.
enum Code : uint8_t {
    Escape = 1, Key1 = 2, Key2, Key3, Key4, Key5, Key6, Key7, Key8, Key9, Key0, Minus, Equal,
    KeyQ = 16, KeyW, KeyE, KeyR, KeyT, KeyY, KeyU, KeyI, KeyO, KeyP, LeftBrace, RightBrace,
    Enter = 28, Control = 29, KeyA = 30, KeyS, KeyD, KeyF, KeyG, KeyH, KeyJ, KeyK, KeyL,
    Semicolon, Apostrophe, Grave, Shift, Backslash, KeyZ = 44, KeyX, KeyC, KeyV, KeyB, KeyN,
    KeyM, Comma, Dot, Slash, Space = 57, Iso102 = 86, AltGr = 100, Up = 103, Right = 106
};
struct Stroke {
    std::vector<Code> modifiers;
    Code key;
};
Stroke plain(Code key) { return {{}, key}; }
Stroke shifted(Code key) { return {{Shift}, key}; }
Stroke third(Code key) { return {{AltGr}, key}; }
// Where each layout puts the characters this fixture types. These tables are written
// from the printed keycaps, independently of the keymap the application receives.
std::map<QChar, Stroke> layoutTable(const QString &layout) {
    const Code digits[] = {Key0, Key1, Key2, Key3, Key4, Key5, Key6, Key7, Key8, Key9};
    std::map<QChar, Stroke> keys;
    const bool azerty = layout == "fr";
    for (int digit = 0; digit < 10; ++digit)
        keys[QChar('0' + digit)] = azerty ? shifted(digits[digit]) : plain(digits[digit]);
    for (const auto &[letter, code] :
         {std::pair{'a', KeyA}, {'c', KeyC}, {'l', KeyL}, {'m', KeyM}, {'q', KeyQ}, {'r', KeyR},
          {'w', KeyW}, {'y', KeyY}, {'z', KeyZ}})
        keys[QChar(letter)] = plain(code);
    if (layout == "us") {
        keys['.'] = plain(Dot);
        keys[','] = plain(Comma);
        keys['['] = plain(LeftBrace);
        keys[']'] = plain(RightBrace);
        keys['<'] = shifted(Comma);
        keys['>'] = shifted(Dot);
        keys['\''] = plain(Apostrophe);
        keys['"'] = shifted(Apostrophe);
        keys[';'] = plain(Semicolon);
    } else if (layout == "de") {
        keys['y'] = plain(KeyZ); // QWERTZ
        keys['z'] = plain(KeyY);
        keys['.'] = plain(Dot);
        keys[','] = plain(Comma);
        keys['['] = third(Key8);
        keys[']'] = third(Key9);
        keys['<'] = plain(Iso102);
        keys['>'] = shifted(Iso102);
        keys['\''] = shifted(Backslash);
        keys['"'] = shifted(Key2);
        keys[';'] = shifted(Comma);
    } else if (azerty) {
        keys['a'] = plain(KeyQ);
        keys['q'] = plain(KeyA);
        keys['z'] = plain(KeyW);
        keys['w'] = plain(KeyZ);
        keys['m'] = plain(Semicolon);
        keys['.'] = shifted(Comma);
        keys[','] = plain(KeyM);
        keys['['] = third(Key5);
        keys[']'] = third(Minus);
        keys['<'] = plain(Iso102);
        keys['>'] = shifted(Iso102);
        keys['\''] = plain(Key4);
        keys['"'] = plain(Key3);
        keys[';'] = plain(Comma);
    } else
        throw std::runtime_error("SKETCHYUP_TEST_KEYBOARD_LAYOUT must be us, de or fr");
    return keys;
}
class Keyboard {
  public:
    explicit Keyboard(const QString &layout) : keys_(layoutTable(layout)) {
        const auto display = qgetenv("SKETCHYUP_TEST_XTEST_DISPLAY");
        int screen = 0;
        connection_ = xcb_connect(display.isEmpty() ? nullptr : display.constData(), &screen);
        check(!xcb_connection_has_error(connection_), "XTest display connection");
        auto iterator = xcb_setup_roots_iterator(xcb_get_setup(connection_));
        for (int i = 0; i < screen; ++i)
            xcb_screen_next(&iterator);
        root_ = iterator.data->root;
        auto *version = xcb_test_get_version_reply(
            connection_, xcb_test_get_version(connection_, 2, 2), nullptr);
        check(version, "X server provides XTest");
        std::free(version);
    }
    ~Keyboard() { xcb_disconnect(connection_); }
    // The layout the X server actually applies, as published by setxkbmap.
    QString serverLayout() {
        auto *atom = xcb_intern_atom_reply(
            connection_, xcb_intern_atom(connection_, true, 16, "_XKB_RULES_NAMES"), nullptr);
        check(atom && atom->atom, "X server publishes XKB rule names");
        auto *property = xcb_get_property_reply(
            connection_,
            xcb_get_property(connection_, false, root_, atom->atom, XCB_ATOM_STRING, 0, 1024),
            nullptr);
        std::free(atom);
        check(property, "XKB rule names readable");
        const auto bytes = QByteArray(static_cast<const char *>(xcb_get_property_value(property)),
                                      xcb_get_property_value_length(property));
        std::free(property);
        const auto names = bytes.split('\0'); // rules, model, layout, variant, options
        return names.size() > 2 ? QString::fromLatin1(names[2]) : QString();
    }
    // Weston's x11 backend receives keys only while its output window has X focus.
    void focusLargestTopLevel() {
        auto *tree =
            xcb_query_tree_reply(connection_, xcb_query_tree(connection_, root_), nullptr);
        check(tree, "X window tree readable");
        xcb_window_t best = XCB_NONE;
        int bestArea = 0;
        const auto *children = xcb_query_tree_children(tree);
        for (int i = 0; i < xcb_query_tree_children_length(tree); ++i) {
            auto *attributes = xcb_get_window_attributes_reply(
                connection_, xcb_get_window_attributes(connection_, children[i]), nullptr);
            auto *geometry = xcb_get_geometry_reply(
                connection_, xcb_get_geometry(connection_, children[i]), nullptr);
            if (attributes && geometry && attributes->map_state == XCB_MAP_STATE_VIEWABLE &&
                geometry->width * geometry->height > bestArea) {
                best = children[i];
                bestArea = geometry->width * geometry->height;
            }
            std::free(attributes);
            std::free(geometry);
        }
        std::free(tree);
        check(best != XCB_NONE, "Compositor output window is mapped");
        xcb_set_input_focus(connection_, XCB_INPUT_FOCUS_POINTER_ROOT, best, XCB_CURRENT_TIME);
        sync();
    }
    void stroke(const Stroke &value) {
        for (auto modifier : value.modifiers)
            send(XCB_KEY_PRESS, modifier);
        send(XCB_KEY_PRESS, value.key);
        send(XCB_KEY_RELEASE, value.key);
        for (auto it = value.modifiers.rbegin(); it != value.modifiers.rend(); ++it)
            send(XCB_KEY_RELEASE, *it);
        sync();
    }
    Stroke character(QChar value) const {
        const auto found = keys_.find(value);
        check(found != keys_.end(), QString("Fixture has no key for %1").arg(value));
        return found->second;
    }

  private:
    void send(uint8_t type, Code code) {
        xcb_test_fake_input(connection_, type, uint8_t(code + 8), XCB_CURRENT_TIME, XCB_NONE, 0,
                            0, 0);
    }
    void sync() {
        // A round trip guarantees the server has processed every injected event.
        std::free(xcb_get_input_focus_reply(connection_, xcb_get_input_focus(connection_),
                                            nullptr));
    }
    std::map<QChar, Stroke> keys_;
    xcb_connection_t *connection_{};
    xcb_window_t root_{};
};
template <class Condition> void await(Condition condition, const QString &message) {
    if (!QTest::qWaitFor(condition, 3000))
        throw std::runtime_error(
            (message + "; focus=" +
             (QApplication::focusWidget() ? QApplication::focusWidget()->objectName()
                                          : QString("none")))
                .toStdString());
}
QString toolName(Viewport::Tool tool) { return QString::number(int(tool)); }
struct Fixture {
    Window &window;
    Keyboard &keys;
    Viewport *view = window.viewport();
    QLineEdit *field = window.findChild<QLineEdit *>("measurements");
    void shortcut(QChar character, Viewport::Tool expected) {
        keys.stroke(keys.character(character));
        await([&] { return view->tool() == expected; },
              QString("Typed '%1' selects tool %2 (now %3)")
                  .arg(character)
                  .arg(toolName(expected), toolName(view->tool())));
    }
    void escapeToSelect() {
        for (int i = 0; view->tool() != Viewport::Tool::Select && i < 3; ++i) {
            keys.stroke(plain(Escape));
            QTest::qWait(50);
        }
        await([&] { return view->tool() == Viewport::Tool::Select; }, "Escape returns to Select");
    }
    // Starts from viewport focus: the first character opens Measurements.
    void measure(const QString &text) {
        check(view->hasFocus(), "Measurement entry starts in the viewport");
        for (int i = 0; i < text.size(); ++i) {
            keys.stroke(keys.character(text[i]));
            await([&] { return field->hasFocus() && field->text() == text.left(i + 1); },
                  QString("Layout types '%1' (field has '%2')")
                      .arg(text.left(i + 1), field->text()));
        }
        keys.stroke(plain(Enter));
        await([&] { return view->hasFocus(); },
              QString("Measurements accepts %1: %2").arg(text, field->accessibleDescription()));
    }
};
bool hasWire(const Document &doc, Vec3 delta) {
    for (const auto &[id, body] : doc.bodies())
        for (const auto &wire : body->surface.wires) {
            const auto actual = body->surface.vertices.at(wire[1]) -
                                body->surface.vertices.at(wire[0]);
            if (length(actual - delta) < 1e-7 || length(actual + delta) < 1e-7)
                return true;
        }
    return false;
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir files;
    if (!files.isValid())
        return 2;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    app.setOrganizationName("SketchyUp");
    app.setApplicationName("SketchyUp");
    // Locale is a separate axis from layout: fix '.' decimals and ',' lists.
    QLocale::setDefault(QLocale::c());
    QSettings().setValue("defaultUnits", "m");
    QSettings().setValue("recoverySeconds", 0);
    const auto layout = qEnvironmentVariable("SKETCHYUP_TEST_KEYBOARD_LAYOUT");
    Window window;
    try {
        Keyboard keys(layout);
        check(keys.serverLayout() == layout,
              QString("X server layout is %1, expected %2").arg(keys.serverLayout(), layout));
        window.resize(1200, 850);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Layout window exposed");
        if (QGuiApplication::platformName() != "xcb")
            keys.focusLargestTopLevel();
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window, 5000), "Layout window activated");
        Fixture f{window, keys};
        check(f.field, "Measurements exists");
        // Initial focus is fixture setup; everything afterwards is physical keys.
        f.view->setFocus();
        await([&] { return f.view->hasFocus(); }, "Initial viewport focus");

        // Shortcuts follow the typed character, wherever the layout puts it. On
        // AZERTY the US "Q" position types 'a'; on QWERTZ 'z' and 'y' swap.
        f.shortcut('l', Viewport::Tool::Line);
        f.shortcut('r', Viewport::Tool::Rectangle);
        f.shortcut('c', Viewport::Tool::Circle);
        f.shortcut('m', Viewport::Tool::Move);
        f.shortcut('q', Viewport::Tool::Rotate);
        f.shortcut('a', Viewport::Tool::CenterArc);
        f.shortcut('z', Viewport::Tool::Zoom);
        keys.stroke(plain(Space));
        await([&] { return f.view->tool() == Viewport::Tool::Select; }, "Space selects Select");
        // 'y' and 'w' are unbound. On QWERTZ 'y', and on AZERTY 'w', sit on the US
        // "Z" key: matching by key position would select Zoom.
        keys.stroke(keys.character('y'));
        keys.stroke(keys.character('w'));
        QTest::qWait(150);
        check(f.view->tool() == Viewport::Tool::Select, "Unbound characters leave Select active");
        auto *fit = window.findChild<QAction *>("view.fit");
        auto *top = window.findChild<QAction *>("view.top");
        check(fit && top, "View actions exist");
        QSignalSpy fitted(fit, &QAction::triggered), topped(top, &QAction::triggered);
        auto shiftZ = keys.character('z');
        shiftZ.modifiers.push_back(Shift);
        keys.stroke(shiftZ);
        await([&] { return fitted.count() == 1; }, "Shift+Z fits the model");
        keys.stroke(keys.character('2'));
        await([&] { return topped.count() == 1; }, "Typed '2' selects the top view");
        check(fitted.count() == 1, "Digit does not repeat Fit");
        if (layout == "fr") {
            // The unshifted AZERTY digit key types 'é', not '2'.
            keys.stroke(plain(Key2));
            QTest::qWait(150);
            check(topped.count() == 1 && f.view->tool() == Viewport::Tool::Select,
                  "Unshifted AZERTY digit key does not trigger the '2' view shortcut");
        }

        // Numeric entry: brackets need AltGr on QWERTZ/AZERTY, digits need Shift on
        // AZERTY, and the foot/inch marks sit on different keys in every layout.
        f.shortcut('r', Viewport::Tool::Rectangle);
        f.measure("[0,0,0]");
        check(f.view->operationAnchor() == std::optional<Vec3>{{0, 0, 0}},
              "Typed absolute coordinate anchors the rectangle");
        f.measure("2'6\",1.5");
        auto &doc = window.document();
        check(doc.bodies().size() == 1, "Typed imperial rectangle commits");
        const auto body = doc.bodies().begin()->second;
        const auto area = body->surface.area(body->surface.faces.begin()->first);
        check(std::abs(area - 0.762 * 1.5) < 1e-8,
              QString("Rectangle 2'6\" by 1.5 m (area %1)").arg(area));

        // Inference locks use the arrow keys; the length follows the locked axis.
        f.escapeToSelect();
        f.shortcut('l', Viewport::Tool::Line);
        f.measure("[10,0,0]");
        keys.stroke(plain(Up));
        f.measure("2");
        check(hasWire(doc, {0, 0, 2}), "Up-arrow blue lock places a two metre vertical line");
        f.escapeToSelect();
        f.shortcut('l', Viewport::Tool::Line);
        f.measure("[20,0,0]");
        f.measure("<0,1,0>");
        check(hasWire(doc, {0, 1, 0}), "Typed relative coordinate places the line endpoint");
        f.escapeToSelect();

        // Ctrl+Z follows the 'z' character too.
        const auto position = doc.history(0, 1).position;
        auto undo = keys.character('z');
        undo.modifiers.push_back(Control);
        keys.stroke(undo);
        await([&] { return doc.history(0, 1).position == position - 1; }, "Ctrl+Z undoes");
        auto redo = undo;
        redo.modifiers.push_back(Shift);
        keys.stroke(redo);
        await([&] { return doc.history(0, 1).position == position; }, "Ctrl+Shift+Z redoes");

        // With the locale that usually accompanies the layout, a value may start with
        // the decimal separator: ",5" in German or French means one half.
        const auto locale = layout == "de"   ? QLocale(QLocale::German, QLocale::Germany)
                            : layout == "fr" ? QLocale(QLocale::French, QLocale::France)
                                             : QLocale::c();
        QLocale::setDefault(locale);
        const auto point = locale.decimalPoint();
        const auto list = point == "," ? QString(";") : QString(",");
        f.shortcut('r', Viewport::Tool::Rectangle);
        f.measure("[30" + list + "0" + list + "0]");
        f.measure(point + "5" + list + "2");
        bool half = false;
        for (const auto &[id, candidate] : doc.bodies())
            for (const auto &[face, record] : candidate->surface.faces)
                half = half || std::abs(candidate->surface.area(face) - 1) < 1e-9;
        check(half, "Leading locale decimal separator gives a 0.5 by 2 rectangle");
        f.escapeToSelect();
        doc.markSaved();
        std::cout << "Keyboard layout " << layout.toStdString() << " on "
                  << QGuiApplication::platformName().toStdString()
                  << ": character shortcuts, Shift/Ctrl shortcuts, digit view shortcut, AltGr "
                     "brackets, foot/inch marks, decimal/list separators, relative "
                     "coordinates, arrow lock and leading locale decimal separator passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        window.document().markSaved();
        return 1;
    }
}
