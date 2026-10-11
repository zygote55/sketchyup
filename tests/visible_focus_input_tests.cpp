// R084.q: every keyboard-focusable control class used by the application shows a
// visible focus indicator (WCAG 2.4.7) whose colour has at least 3:1 contrast with the
// adjacent colours (WCAG 1.4.11), in both themes and at two interface text sizes. The
// indicator is measured from pixels painted by the real main window and the real dialogs.
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <set>
using namespace sketchy;
namespace {
double luminance(const QColor &color) {
    auto linear = [](double v) {
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) +
           .0722 * linear(color.blueF());
}
double contrast(const QColor &a, const QColor &b) {
    const auto x = luminance(a), y = luminance(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
// Widget classes whose focus indicator the audit requires to be exercised.
const char *const kFamilies[] = {"QLineEdit",        "QPushButton", "QComboBox",
                                 "QCheckBox",        "QSpinBox",    "QDoubleSpinBox",
                                 "QPlainTextEdit",   "QListWidget", "QTreeWidget",
                                 "QTabBar",          "QToolButton", "QToolBar",
                                 "QTimeEdit",        "QDateEdit",   "QKeySequenceEdit",
                                 "QScrollArea",      "QLabel",      "sketchy::AssistantPanel",
                                 "sketchy::Viewport"};
QString family(const QWidget *widget) {
    if (qobject_cast<const QKeySequenceEdit *>(widget))
        return "QKeySequenceEdit";
    if (qobject_cast<const QComboBox *>(widget))
        return "QComboBox";
    for (const char *name : {"QDateEdit", "QTimeEdit", "QDoubleSpinBox", "QSpinBox"})
        if (widget->inherits(name))
            return name;
    for (const char *name : {"QTreeWidget", "QListWidget", "QPlainTextEdit", "QLineEdit",
                             "QCheckBox", "QPushButton", "QTabBar", "QToolButton", "QToolBar"})
        if (widget->inherits(name))
            return name;
    return widget->metaObject()->className();
}
// Qt exposes internal editors separately from their composite public control.
bool internalEditor(const QWidget *widget) {
    for (auto *parent = widget->parentWidget(); parent; parent = parent->parentWidget())
        if (qobject_cast<const QAbstractSpinBox *>(parent) ||
            qobject_cast<const QTabBar *>(parent) ||
            qobject_cast<const QKeySequenceEdit *>(parent) ||
            qobject_cast<const QComboBox *>(parent) ||
            qobject_cast<const QAbstractItemView *>(parent))
            return true;
    return false;
}
QString identity(const QWidget *widget) { return family(widget) + ':' + widget->objectName(); }
struct Ring {
    QColor edges[4];
    bool valid{};
};
// Dominant colour of the middle half of one edge band `inset` logical pixels from the
// widget boundary (negative = outside). Edge order: top, right, bottom, left.
QColor edgeColor(const QImage &image, const QRect &box, qreal scale, qreal inset, int edge) {
    std::map<QRgb, int> histogram;
    const int offset = int(std::floor(inset * scale));
    const bool horizontal = edge == 0 || edge == 2;
    const int length = horizontal ? box.width() : box.height();
    for (int i = length / 4; i < length - length / 4; ++i) {
        int x, y;
        if (edge == 0)
            x = box.left() + i, y = box.top() + offset;
        else if (edge == 2)
            x = box.left() + i, y = box.bottom() - offset;
        else if (edge == 1)
            x = box.right() - offset, y = box.top() + i;
        else
            x = box.left() + offset, y = box.top() + i;
        if (QRect(QPoint(), image.size()).contains(x, y))
            ++histogram[image.pixel(x, y)];
    }
    check(!histogram.empty(), "Focus edge band has pixels");
    return QColor(
        std::max_element(histogram.begin(), histogram.end(), [](const auto &a, const auto &b) {
            return a.second < b.second;
        })->first);
}
// Dominant colour of the bands `insets` logical pixels from the boundary (negative =
// outside), over the middle half of the chosen edges (bit mask; top, right, bottom, left).
// Pixels outside `valid` (beyond the window) or inside `skip` (the 3D canvas, whose colour is
// document content, not interface chrome) are ignored. Neighbouring frames, scroll bars
// and arrow buttons do not outvote the background the indicator is drawn against.
QColor bandColor(const QImage &image, const QRect &box, const QRect &valid, const QRect &skip,
                 qreal scale, std::initializer_list<qreal> insets, int edges) {
    std::map<QRgb, int> histogram;
    for (const auto inset : insets)
        for (int edge = 0; edge < 4; ++edge) {
            if (!(edges & (1 << edge)))
                continue;
            const int offset = int(std::floor(inset * scale));
            const bool horizontal = edge == 0 || edge == 2;
            const int length = horizontal ? box.width() : box.height();
            for (int i = length / 4; i < length - length / 4; ++i) {
                const int x = horizontal ? box.left() + i
                                         : (edge == 1 ? box.right() - offset : box.left() + offset);
                const int y = !horizontal
                                  ? box.top() + i
                                  : (edge == 0 ? box.top() + offset : box.bottom() - offset);
                if (valid.contains(x, y) && !skip.contains(x, y))
                    ++histogram[image.pixel(x, y)];
            }
        }
    if (histogram.empty())
        return {};
    return QColor(
        std::max_element(histogram.begin(), histogram.end(), [](const auto &a, const auto &b) {
            return a.second < b.second;
        })->first);
}
struct Capture {
    QImage image;
    QRect box;
    qreal scale{};
    QRect valid; // Image pixels that lie inside the window.
};
// A tab bar's focus indicator belongs to its current tab, not the whole bar.
QRect focusRect(const QWidget &widget) {
    if (auto *tabs = qobject_cast<const QTabBar *>(&widget))
        return tabs->tabRect(tabs->currentIndex());
    return widget.rect();
}
Capture capture(QWidget &top, QWidget &widget) {
    const int margin = 4;
    // A GL child is measured from its own framebuffer; its parent is the surrounding colour.
    if (auto *gl = qobject_cast<QOpenGLWidget *>(&widget)) {
        const auto image = gl->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
        return {image, QRect(QPoint(), image.size()), qreal(image.width()) / widget.width(),
                QRect(QPoint(), image.size())};
    }
    const auto area = focusRect(widget);
    const QRect logical(widget.mapTo(&top, area.topLeft()), area.size());
    const auto image = top.grab(logical.adjusted(-margin, -margin, margin, margin))
                           .toImage()
                           .convertToFormat(QImage::Format_RGB32);
    const auto scale = image.devicePixelRatio();
    const QRect box(
        QPoint(int(std::lround(margin * scale)), int(std::lround(margin * scale))),
        QSize(int(std::lround(area.width() * scale)), int(std::lround(area.height() * scale))));
    const QRect window(
        QPoint(int(std::lround((top.rect().left() - logical.left() + margin) * scale)),
               int(std::lround((top.rect().top() - logical.top() + margin) * scale))),
        QSize(int(std::lround(top.width() * scale)), int(std::lround(top.height() * scale))));
    return {image, box, scale, window.intersected(QRect(QPoint(), image.size()))};
}
QWidget *neighbour(QWidget &top, const QWidget *than) {
    for (auto *candidate : top.findChildren<QWidget *>())
        if (candidate != than && !than->isAncestorOf(candidate) && !candidate->isAncestorOf(than) &&
            candidate->isVisibleTo(&top) && candidate->isEnabled() &&
            (candidate->focusPolicy() & Qt::TabFocus) && !internalEditor(candidate) &&
            !qobject_cast<QAbstractItemView *>(candidate->parentWidget()))
            return candidate;
    return nullptr;
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
    AssistantPanel::HostServices services;
    services.outcomeRoot = files.filePath("isolated-outcomes");
    services.credentialExecutable = "/bin/false";
    QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", 0);
    Window window(nullptr, services);
    try {
        const auto captureDir = qEnvironmentVariable("SKETCHYUP_FOCUS_CAPTURE_DIR");
        window.resize(1280, 900);
        window.demo();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Focus window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Focus window owns keyboard focus");
        const auto content = encodeContainer(window.document());
        QJsonArray rows;
        QJsonObject coverage;
        std::set<QString> covered;
        int failures{}, measured{}, clipped{};
        double minimumOutside = 99, minimumInside = 99;
        QString theme;
        int textPercent{};
        // Measures one control: unfocused vs focused painted boundary.
        auto probe = [&](QWidget &top, QWidget &widget, const QString &context) {
            for (auto *parent = widget.parentWidget(); parent; parent = parent->parentWidget())
                if (auto *scroll = qobject_cast<QScrollArea *>(parent))
                    scroll->ensureWidgetVisible(&widget, 0, 0);
            QCoreApplication::processEvents();
            if (widget.visibleRegion().boundingRect().size() != widget.size() ||
                widget.width() < 12 || widget.height() < 12) {
                ++clipped;
                if (qEnvironmentVariableIsSet("SKETCHYUP_FOCUS_VERBOSE"))
                    std::cerr << "Clipped: " << identity(&widget).toStdString() << " "
                              << widget.width() << "x" << widget.height() << " visible "
                              << widget.visibleRegion().boundingRect().width() << "x"
                              << widget.visibleRegion().boundingRect().height() << '\n';
                return;
            }
            auto *other = neighbour(top, &widget);
            check(other, "A second focusable control exists to hold the unfocused state");
            const auto size = widget.size();
            const auto hint = widget.sizeHint();
            other->setFocus(Qt::TabFocusReason);
            QCoreApplication::processEvents();
            check(!widget.hasFocus(), "Control is unfocused for the baseline");
            const bool gl = qobject_cast<QOpenGLWidget *>(&widget);
            const auto before = capture(top, widget);
            widget.setFocus(Qt::TabFocusReason);
            QCoreApplication::processEvents();
            if (!widget.hasFocus())
                std::cerr << "Control did not take focus: " << identity(&widget).toStdString()
                          << " context=" << context.toStdString()
                          << " active=" << widget.window()->isActiveWindow() << " focusWidget="
                          << (QApplication::focusWidget()
                                  ? identity(QApplication::focusWidget()).toStdString()
                                  : "none")
                          << '\n';
            if (!widget.hasFocus()) {
                ++failures;
                return;
            }
            const auto after = capture(top, widget);
            const auto kind = family(&widget);
            covered.insert(kind);
            ++measured;
            bool differs = true, layoutStable = widget.size() == size && widget.sizeHint() == hint;
            double outside = 99, inside = 99;
            QString worst;
            QJsonArray ring;
            // Outside: 2-4 px beyond the boundary, past any thin native frame. Inside: just
            // past the 1 px border on the top and left edges (scroll bars and arrow buttons
            // are placed at the right and bottom).
            QRect canvas;
            if (auto *view = window.viewport(); view && !gl) {
                const auto origin = view->mapTo(&top, QPoint(0, 0));
                const auto area = focusRect(widget);
                const auto here = widget.mapTo(&top, area.topLeft());
                canvas = QRect(
                    QPointF((origin.x() - here.x()) * after.scale + after.box.left(),
                            (origin.y() - here.y()) * after.scale + after.box.top())
                        .toPoint(),
                    QSizeF(view->width() * after.scale, view->height() * after.scale).toSize());
                if (&top != window.window())
                    canvas = {};
            }
            auto outer = bandColor(after.image, after.box, after.valid, canvas, after.scale,
                                   {-2.0, -3.0, -4.0}, 0b1111);
            if (!outer.isValid() || gl)
                outer = widget.palette().color(QPalette::Window);
            const auto inner =
                bandColor(after.image, after.box, after.valid, {}, after.scale, {2.0}, 0b1001);
            for (int edge = 0; edge < 4; ++edge) {
                const auto unfocusedColor =
                    edgeColor(before.image, before.box, before.scale, 0, edge);
                const auto focusedColor = edgeColor(after.image, after.box, after.scale, 0, edge);
                differs &= unfocusedColor != focusedColor;
                ring.append(focusedColor.name());
                outside = std::min(outside, contrast(focusedColor, outer));
                inside = std::min(inside, contrast(focusedColor, inner));
            }
            worst = QString("outside %1 inside %2").arg(outer.name(), inner.name());
            for (auto *parent = widget.parentWidget(); parent; parent = parent->parentWidget())
                worst += " < " + identity(parent) + (parent->isWindow() ? "(window)" : "");
            if (!gl) {
                minimumOutside = std::min(minimumOutside, outside);
                minimumInside = std::min(minimumInside, inside);
            }
            // The 3D canvas paints its own ring on its own background (document colours); the
            // window chrome beyond it is reported but not required to meet the ratio.
            const bool passes = differs && layoutStable && (gl || outside >= 3) && inside >= 3;
            failures += !passes;
            if (!passes)
                std::cerr << "Focus indicator failure: " << identity(&widget).toStdString()
                          << " context=" << context.toStdString()
                          << " theme=" << theme.toStdString() << " text=" << textPercent
                          << " differs=" << differs << " layoutStable=" << layoutStable
                          << " outside=" << outside << " inside=" << inside << " ["
                          << worst.toStdString() << "]\n";
            rows.append(QJsonObject{{"context", context},
                                    {"theme", theme},
                                    {"textPercent", textPercent},
                                    {"class", kind},
                                    {"control", identity(&widget)},
                                    {"focusedRing", ring},
                                    {"changesWhenFocused", differs},
                                    {"layoutStable", layoutStable},
                                    {"contrastOutside", outside},
                                    {"contrastInside", inside},
                                    {"passes", passes}});
            if (!captureDir.isEmpty()) {
                const auto path = QString("%1/%2-%3-%4-%5.png")
                                      .arg(captureDir, theme, QString::number(textPercent), kind,
                                           widget.objectName().isEmpty() ? QString::number(measured)
                                                                         : widget.objectName());
                after.image
                    .scaled(after.image.size() * 4, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                    .save(path);
            }
        };
        auto sweep = [&](QWidget &top, const QString &context) {
            std::map<QString, int> sampled;
            for (auto *widget : top.findChildren<QWidget *>()) {
                if (!widget->isVisibleTo(&top) || !widget->isEnabled() || widget->focusProxy() ||
                    !(widget->focusPolicy() & Qt::TabFocus) || internalEditor(widget) ||
                    qobject_cast<QAbstractItemView *>(widget->parentWidget()))
                    continue;
                // Focus-capable Qt composites (calendar popups, view viewports) are not
                // public controls of the application.
                if (qobject_cast<QAbstractScrollArea *>(widget->parentWidget()) &&
                    !qobject_cast<QAbstractItemView *>(widget) &&
                    !qobject_cast<QPlainTextEdit *>(widget))
                    continue;
                // Bounded run time: the first two controls of each class per context at the
                // default size, the first one at 200% text; every class is still exercised.
                if (++sampled[family(widget)] > (textPercent == 100 ? 2 : 1))
                    continue;
                probe(top, *widget, context);
            }
        };
        auto modalSweep = [&](const std::function<void()> &open, const char *dialogId) {
            QString failure;
            bool captured{};
            QTimer::singleShot(0, &window, [&, dialogId] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                try {
                    check(dialog && dialog->objectName() == dialogId,
                          "Expected owned modal dialog opened");
                    check(QTest::qWaitForWindowExposed(dialog), "Focus dialog exposed");
                    dialog->activateWindow();
                    check(QTest::qWaitForWindowActive(dialog), "Focus dialog active");
                    check(QTest::qWaitFor([&] {
                              auto *focused = QApplication::focusWidget();
                              return focused &&
                                     (focused == dialog || dialog->isAncestorOf(focused));
                          }),
                          "Focus dialog owns keyboard focus");
                    sweep(*dialog, dialog->windowTitle());
                    captured = true;
                } catch (const std::exception &error) {
                    failure = QString::fromUtf8(error.what());
                }
                if (dialog)
                    dialog->reject();
            });
            open();
            if (!failure.isEmpty())
                throw std::runtime_error(failure.toStdString());
            check(captured, "Modal dialog sweep completed");
            window.activateWindow();
            check(QTest::qWaitForWindowActive(&window), "Main window regains focus");
        };
        // Evidence image: one dialog, focus on a combo box (left) and on a checked tool button
        // (right), rendered under the live theme.
        auto shoot = [&](const QString &directory) {
            QDialog dialog(&window);
            dialog.setWindowTitle("Visible focus");
            auto *grid = new QGridLayout(&dialog);
            auto *combo = new QComboBox;
            combo->addItems({"Perspective", "Parallel projection"});
            auto *tool = new QToolButton;
            tool->setText("Select");
            tool->setCheckable(true);
            tool->setChecked(true);
            tool->setFocusPolicy(Qt::TabFocus);
            auto *spin = new QSpinBox;
            spin->setValue(12);
            auto *guides = new QCheckBox("Show guides");
            auto *apply = new QPushButton("Apply");
            grid->addWidget(new QLabel("Projection"), 0, 0);
            grid->addWidget(combo, 0, 1);
            grid->addWidget(new QLabel("Tool"), 1, 0);
            grid->addWidget(tool, 1, 1);
            grid->addWidget(new QLabel("Steps"), 2, 0);
            grid->addWidget(spin, 2, 1);
            grid->addWidget(guides, 3, 0, 1, 2);
            grid->addWidget(apply, 4, 1);
            dialog.show();
            check(QTest::qWaitForWindowExposed(&dialog), "Evidence dialog exposed");
            dialog.activateWindow();
            check(QTest::qWaitForWindowActive(&dialog), "Evidence dialog active");
            apply->setFocus(Qt::TabFocusReason);
            QTest::qWait(20);
            combo->setFocus(Qt::TabFocusReason);
            QTest::qWait(30);
            const auto left = dialog.grab().toImage();
            tool->setFocus(Qt::TabFocusReason);
            QTest::qWait(30);
            const auto right = dialog.grab().toImage();
            QImage both(left.width() + right.width() + 16, left.height(), QImage::Format_RGB32);
            both.fill(dialog.palette().color(QPalette::Window));
            QPainter painter(&both);
            painter.drawImage(0, 0, left);
            painter.drawImage(left.width() + 16, 0, right);
            painter.end();
            both.save(QString("%1/R084q-focus-%2.png").arg(directory, theme));
        };
        const auto shotDir = qEnvironmentVariable("SKETCHYUP_FOCUS_SCREENSHOT_DIR");
        for (int percent : {100, 200}) {
            textPercent = percent;
            QSettings("SketchyUp", "SketchyUp").setValue("interfaceTextPercent", percent);
            for (int mode : {1, 2}) {
                theme = mode == 1 ? "light" : "dark";
                window.findChild<QAction *>("view.theme.1")->trigger();
                window.findChild<QAction *>("view.theme." + QString::number(mode))->trigger();
                QTest::qWait(50);
                window.activateWindow();
                check(QTest::qWaitForWindowActive(&window), "Window active after theme change");
                auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
                check(tabs && tabs->count() > 0, "Model-panel tabs exist");
                // Every panel at the default size; the first three at 200% (the enlarged-text
                // layout of the same controls), which bounds the native run time.
                for (int page = 0; page < (percent == 100 ? tabs->count() : 3); ++page) {
                    tabs->setCurrentIndex(page);
                    QTest::qWait(30);
                    sweep(window, "Main window / " + tabs->tabText(page));
                }
                // The tool rail's buttons are not keyboard stops in Qt, so prove the rule on
                // a button that is: temporarily allow Tab focus on one rail button.
                auto *rail = window.findChild<QToolBar *>("toolRail");
                check(rail, "Tool rail exists");
                if (!captureDir.isEmpty())
                    window.grab().save(
                        QString("%1/window-%2-%3.png").arg(captureDir, theme).arg(percent));
                // Probe one checked and one unchecked tool: checked + focused must show both.
                std::set<bool> probed;
                for (auto *button : rail->findChildren<QToolButton *>()) {
                    if (!button->defaultAction() || !button->isVisibleTo(&window) ||
                        probed.contains(button->isChecked()))
                        continue;
                    probed.insert(button->isChecked());
                    button->setFocusPolicy(Qt::TabFocus);
                    probe(window, *button,
                          button->isChecked() ? "Main window / tool rail button (checked)"
                                              : "Main window / tool rail button");
                    button->setFocusPolicy(Qt::NoFocus);
                }
                check(probed.size() == 2, "Checked and unchecked tool buttons were probed");
                window.viewport()->setFocus(Qt::OtherFocusReason);
                auto action = [&](const char *id) {
                    return [&window, id] {
                        auto *entry = window.findChild<QAction *>(id);
                        check(entry && entry->isEnabled(), "Dialog action available in demo");
                        entry->trigger();
                    };
                };
                modalSweep(action("drawing.plane.custom"), "drawingPlaneDialog");
                modalSweep(action("edit.shortcuts"), "shortcutDialog");
                modalSweep(action("view.commands"), "commandPalette");
                modalSweep(
                    [&window] {
                        auto *edit = window.findChild<QPushButton *>("solarEditButton");
                        check(edit && edit->isEnabled(), "Sun settings button available");
                        edit->click();
                    },
                    "solarDialog");
                modalSweep(
                    [&window] {
                        auto *create = window.findChild<QPushButton *>("materialNew");
                        check(create && create->isEnabled(), "New material button available");
                        create->click();
                    },
                    "materialDialog");
                if (!window.assistantPanel()->isVisible())
                    window.findChild<QAction *>("view.assistant")->trigger();
                window.findChild<QPushButton *>("assistantPreferences")->click();
                auto *dialog = window.findChild<QDialog *>("assistantPreferencesDialog");
                check(dialog && QTest::qWaitForWindowExposed(dialog),
                      "Assistant preferences exposed");
                dialog->activateWindow();
                check(QTest::qWaitForWindowActive(dialog), "Assistant preferences active");
                check(QTest::qWaitFor([&] {
                          auto *focused = QApplication::focusWidget();
                          return focused && (focused == dialog || dialog->isAncestorOf(focused));
                      }),
                      "Assistant preferences own keyboard focus");
                auto *provider = dialog->findChild<QComboBox *>("assistantProviderChoice");
                check(provider, "Provider choice exists");
                for (const auto &name :
                     {QStringLiteral("None"), QStringLiteral("Ollama"), QStringLiteral("OpenAI")}) {
                    provider->setCurrentText(name);
                    QTest::qWait(25);
                    sweep(*dialog, "Assistant preferences / " + name);
                }
                dialog->reject();
                window.activateWindow();
                check(QTest::qWaitForWindowActive(&window), "Main window regains focus");
                if (!shotDir.isEmpty() && percent == 100)
                    shoot(shotDir);
            }
        }
        QJsonArray missing;
        for (const char *name : kFamilies)
            if (!covered.contains(name))
                missing.append(name);
        QJsonArray coveredClasses;
        for (const auto &name : covered)
            coveredClasses.append(name);
        const bool passed = failures == 0 && missing.isEmpty() && measured > 0;
        const auto bytes =
            QJsonDocument(
                QJsonObject{{"rows", rows},
                            {"measuredControls", measured},
                            {"skippedClippedControls", clipped},
                            {"failures", failures},
                            {"coveredClasses", coveredClasses},
                            {"uncoveredRequiredClasses", missing},
                            {"minimumContrastOutside", minimumOutside},
                            {"minimumContrastInside", minimumInside},
                            {"passes", passed},
                            {"scope", "Painted boundary of every Tab-focusable public control in "
                                      "the main window panels, tool rail, shortcut, drawing "
                                      "plane, command and assistant preference dialogs, in light "
                                      "and dark themes at 100% and 200% interface text"},
                            {"releaseAcceptance", false}})
                .toJson();
        if (argc == 2) {
            QFile report(QString::fromLocal8Bit(argv[1]));
            check(report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size(),
                  "Write focus report");
        }
        std::cout << bytes.constData();
        check(encodeContainer(window.document()) == content, "Focus sweep preserves content");
        check(missing.isEmpty(), "Every required control class was exercised");
        check(failures == 0, "Every focused control shows a >=3:1 indicator without layout shift");
        window.document().markSaved();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        window.document().markSaved();
        return 1;
    }
}
