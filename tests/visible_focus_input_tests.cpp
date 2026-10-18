// R084.q: every keyboard-focusable control class used by the application shows a
// visible focus indicator (WCAG 2.4.7) whose colour has at least 3:1 contrast with the
// adjacent colours (WCAG 1.4.11), in both themes and at two interface text sizes. The
// indicator is measured from pixels painted by the real main window and the real dialogs.
#include "app/interface_preferences.hpp"
#include "app/parameters_scope_dialog.hpp"
#include "app/sheet_evaluation.hpp"
#include "app/sheet_export_dialog.hpp"
#include "app/sheet_workspace.hpp"
#include "app/sheets_tray.hpp"
#include "app/surface_format.hpp"
#include "app/theme.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/drawing_sheets.hpp"
#include "core/model_style.hpp"
#include "io/document_io.hpp"
#include <QAbstractButton>
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
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
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
// Widget classes whose focus indicator the audit requires to be exercised. The tool rail
// itself is no longer a focus target (R084.v); its buttons are QToolButtons.
const char *const kFamilies[] = {"QLineEdit",
                                 "QPushButton",
                                 "QComboBox",
                                 "QCheckBox",
                                 "QSpinBox",
                                 "QDoubleSpinBox",
                                 "QPlainTextEdit",
                                 "QListWidget",
                                 "QTreeWidget",
                                 "QTabBar",
                                 "QToolButton",
                                 "QTimeEdit",
                                 "QDateEdit",
                                 "QKeySequenceEdit",
                                 "QScrollArea",
                                 "QLabel",
                                 "sketchy::AssistantPanel",
                                 "sketchy::Viewport"};
// R091.h: the classes the Sheets workspace run (--workspace=sheets) must exercise.
const char *const kSheetsFamilies[] = {"QLineEdit",           "QPushButton", "QComboBox",
                                       "QTreeWidget",         "QTabBar",     "QToolButton",
                                       "sketchy::SheetCanvas"};
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
// The dominant colour of the middle half of one edge at each of the first `count` device pixels
// in from the boundary. A two-tone ring (D-023) is read as the runs of colour along this scan, as
// its halo is 1 to 2 device pixels at a fractional scale.
std::vector<QColor> edgeScan(const QImage &image, const QRect &box, int edge, int count) {
    std::vector<QColor> scan;
    for (int offset = 0; offset < count; ++offset) {
        std::map<QRgb, int> histogram;
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
        check(!histogram.empty(), "Focus edge scan has pixels");
        scan.push_back(QColor(
            std::max_element(histogram.begin(), histogram.end(), [](const auto &a, const auto &b) {
                return a.second < b.second;
            })->first));
    }
    return scan;
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
    auto arguments = app.arguments().mid(1);
    const bool sheetsOnly = arguments.removeAll("--workspace=sheets") > 0;
    const auto reportPath = arguments.isEmpty() ? QString() : arguments.front();
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
        // R091.h: --workspace=sheets sweeps the Sheets workspace only (its own CI line, so each
        // run stays well under the Wayland harness's 60 s); without it, the Model window.
        if (sheetsOnly) {
            // R091.h: a page for the Sheets workspace sweep, with a plan view, a text and a title
            // block. Model and sheet commands go in separate batches.
            auto batch = [&](const QJsonArray &commands) {
                auto &doc = window.document();
                return executeBatch(doc, {{"apiVersion", 1},
                                          {"documentId", QString::fromStdString(doc.identity())},
                                          {"expectedRevision", QString::number(doc.revision())},
                                          {"commands", commands}});
            };
            const auto scene = batch({QJsonObject{
                {"command", "saved_scene.create"},
                {"name", "Plan"},
                {"snapshot",
                 QJsonObject{{"camera", QJsonObject{{"target", QJsonArray{4, 3, 0}},
                                                    {"yaw", -90},
                                                    {"pitch", 90},
                                                    {"distance", 20},
                                                    {"fieldOfView", 45},
                                                    {"orthographic", true}}},
                             {"section", QJsonObject{{"plane", QJsonValue()}}}}}}})["createdScenes"]
                                   .toArray()[0]
                                   .toString();
            const auto sheet = QString::number(window.document().nextDrawingSheetId());
            batch({QJsonObject{{"command", "drawing_sheet.create"},
                               {"name", "Ground floor"},
                               {"number", "A-101"},
                               {"paper", QJsonObject{{"widthMm", 420}, {"heightMm", 297}}}},
                   QJsonObject{
                       {"command", "drawing_view.create"},
                       {"name", "Plan"},
                       {"sheet", sheet},
                       {"source", QJsonObject{{"kind", "scene"}, {"scene", scene}}},
                       {"frame",
                        QJsonObject{{"xMm", 20}, {"yMm", 20}, {"widthMm", 200}, {"heightMm", 150}}},
                       {"scaleDenominator", 100}},
                   QJsonObject{{"command", "drawing_item.create"},
                               {"sheet", sheet},
                               {"kind", "text"},
                               {"data", QJsonObject{{"frame", QJsonObject{{"xMm", 240},
                                                                          {"yMm", 40},
                                                                          {"widthMm", 60},
                                                                          {"heightMm", 10}}},
                                                    {"text", "Notes"}}}},
                   QJsonObject{{"command", "drawing_item.create"},
                               {"sheet", sheet},
                               {"kind", "titleBlock"},
                               {"data", QJsonObject{{"frame", QJsonObject{{"xMm", 250},
                                                                          {"yMm", 257},
                                                                          {"widthMm", 160},
                                                                          {"heightMm", 30}}}}}}});
        }
        emit window.viewport()->changed();
        const auto content = encodeContainer(window.document());
        QJsonArray rows;
        QJsonObject coverage;
        std::set<QString> covered;
        int failures{}, measured{}, clipped{};
        double minimumAccentChange = 99;
        double minimumOutside = 99, minimumInside = 99;
        // D-023: the two-tone rings (a ring inside a halo in the control's background) of the
        // controls that border the 3D canvas, the canvas's own ring, and the scroll bars.
        double minimumRingHalo = 99, minimumHaloCanvasDark = 99, minimumRingCanvasLight = 99,
               minimumCanvasRing = 99, minimumScrollHandle = 99;
        std::set<QString> haloCovered;
        int scrollBarsMeasured{};
        const auto defaultCanvas = [] {
            const auto &rgb = ModelStyle{}.background;
            return QColor(int(std::lround(rgb[0] * 255)), int(std::lround(rgb[1] * 255)),
                          int(std::lround(rgb[2] * 255)));
        }();
        QString theme;
        int textPercent{};
        // Measures one control: unfocused vs focused painted boundary.
        auto probe = [&](QWidget &top, QWidget &widget, const QString &context) {
            // The margins are half the control, so that all of it scrolls into view and not only
            // its centre; and twice, as the first scroll may precede the content's own layout.
            for (int pass = 0; pass < 2; ++pass) {
                for (auto *parent = widget.parentWidget(); parent; parent = parent->parentWidget())
                    if (auto *scroll = qobject_cast<QScrollArea *>(parent))
                        scroll->ensureWidgetVisible(&widget, widget.width() / 2 + 1,
                                                    widget.height() / 2 + 1);
                QCoreApplication::processEvents();
            }
            if (widget.visibleRegion().boundingRect().size() != widget.size() ||
                widget.width() < 12 || widget.height() < 12) {
                ++clipped;
                if (qEnvironmentVariableIsSet("SKETCHYUP_FOCUS_VERBOSE"))
                    std::cerr << "Clipped: " << identity(&widget).toStdString() << " "
                              << widget.width() << "x" << widget.height() << " visible "
                              << widget.visibleRegion().boundingRect().width() << "x"
                              << widget.visibleRegion().boundingRect().height()
                              << " context=" << context.toStdString() << " text=" << textPercent
                              << " theme=" << theme.toStdString() << '\n';
                // D-023 (d): the scroll area it overflows, and the widest minimum width inside.
                if (qEnvironmentVariableIsSet("SKETCHYUP_FOCUS_VERBOSE"))
                    for (auto *parent = widget.parentWidget(); parent;
                         parent = parent->parentWidget())
                        if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
                            const auto at = widget.mapTo(scroll->widget(), QPoint());
                            std::cerr << "  in " << identity(scroll).toStdString() << " viewport "
                                      << scroll->viewport()->width() << "x"
                                      << scroll->viewport()->height() << " at " << at.x() << ","
                                      << at.y() << " content " << scroll->widget()->height()
                                      << " scrolled " << scroll->verticalScrollBar()->value() << "/"
                                      << scroll->verticalScrollBar()->maximum() << " min "
                                      << scroll->widget()->minimumSizeHint().width() << ":";
                            for (auto *child : scroll->widget()->findChildren<QWidget *>())
                                if (child->isVisibleTo(scroll->widget()) &&
                                    child->minimumSizeHint().width() >
                                        scroll->viewport()->width() - 8)
                                    std::cerr << ' ' << identity(child).toStdString() << " "
                                              << child->minimumSizeHint().width() << "/"
                                              << child->width();
                            std::cerr << '\n';
                            break;
                        }
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
            auto before = capture(top, widget);
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
            auto after = capture(top, widget);
            // A tab's style margins leave a gap inside its tab rectangle (the page tabs' spacing,
            // R091.h). Measure the painted tab: the box of focused pixels unlike the bar behind.
            if (qobject_cast<QTabBar *>(&widget)) {
                const auto behind = after.image.pixelColor(0, after.image.height() / 2);
                QRect painted;
                for (int y = after.box.top(); y <= after.box.bottom(); ++y)
                    for (int x = after.box.left(); x <= after.box.right(); ++x)
                        if (contrast(after.image.pixelColor(x, y), behind) > 1.2)
                            painted |= QRect(x, y, 1, 1);
                if (painted.isValid())
                    before.box = after.box = painted;
            }
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
            // R091.h: an accent-filled control (a primary button, the checked Model | Sheets
            // segment, the edit-view Done) shows focus as a 2 px inset ring in the fill's own
            // theme surface. Its rule: the ring against the fill just inside it (3 px in, past the
            // ring), and the change of its boundary colour from the unfocused state, each at
            // least 3:1. Beyond the boundary is chrome of the ring's own colour by design.
            auto *button = qobject_cast<QAbstractButton *>(&widget);
            const bool accentFilled = widget.property("accentFill").toBool() ||
                                      widget.objectName() == "sheetEditDone" ||
                                      (widget.parentWidget() &&
                                       widget.parentWidget()->objectName() == "workspaceToggle" &&
                                       button && button->isChecked());
            const auto inner = bandColor(after.image, after.box, after.valid, {}, after.scale,
                                         {accentFilled ? 3.0 : 2.0}, 0b1001);
            double change = 99;
            // D-023: a control that borders the 3D canvas has a two-tone ring: the halo is the
            // outermost run of colour, the ring the next one, then the fill inside it.
            const bool haloed = widget.property("focusHalo").toBool();
            const auto chrome = themeColors(theme == "dark");
            const bool parametric = widget.property("parametric").toBool();
            const QColor expectedHalo = parametric ? chrome.informationSurface : chrome.surface;
            double ringHalo = 99, haloCanvas = 99, ringCanvas = 99;
            bool haloIsBackground = true;
            for (int edge = 0; edge < 4; ++edge) {
                QColor unfocusedColor, focusedColor, innerColor = inner;
                if (haloed) {
                    // Qt anti-aliases the margin's rounded outer edge, so the halo is the run
                    // of the background's own colour, after at most one blended pixel.
                    const auto scanned = edgeScan(after.image, after.box, edge, 12);
                    const auto unfocusedScan = edgeScan(before.image, before.box, edge, 12);
                    int haloAt = 0;
                    while (haloAt < 4 && scanned[haloAt] != expectedHalo)
                        ++haloAt;
                    int ringAt = haloAt;
                    while (ringAt < 11 && scanned[ringAt] == expectedHalo)
                        ++ringAt;
                    int innerAt = ringAt;
                    while (innerAt < 11 && scanned[innerAt] == scanned[ringAt])
                        ++innerAt;
                    // The halo is at least one logical pixel and at most the 2 px margin.
                    check(haloAt < 4 && ringAt - haloAt >= int(after.scale) &&
                              ringAt - haloAt <= 2 * int(std::ceil(after.scale)) && innerAt < 11,
                          "A two-tone ring has a halo of one logical pixel and a ring");
                    if (qEnvironmentVariableIsSet("SKETCHYUP_FOCUS_VERBOSE")) {
                        std::cerr << "Scan " << edge << " " << identity(&widget).toStdString()
                                  << ":";
                        for (const auto &color : scanned)
                            std::cerr << ' ' << color.name().toStdString();
                        std::cerr << '\n';
                    }
                    focusedColor = scanned[ringAt];
                    unfocusedColor = unfocusedScan[ringAt];
                    outer = scanned[haloAt];
                    innerColor = scanned[innerAt];
                    haloIsBackground &= scanned[haloAt] == expectedHalo;
                    ringHalo = std::min(ringHalo, contrast(focusedColor, scanned[haloAt]));
                    haloCanvas = std::min(haloCanvas, contrast(scanned[haloAt], defaultCanvas));
                    ringCanvas = std::min(ringCanvas, contrast(focusedColor, defaultCanvas));
                    // Only the top and left edges: arrows and scroll bars lie at the others.
                    if (edge == 1 || edge == 2)
                        innerColor = QColor();
                } else {
                    unfocusedColor = edgeColor(before.image, before.box, before.scale, 0, edge);
                    focusedColor = edgeColor(after.image, after.box, after.scale, 0, edge);
                }
                differs &= unfocusedColor != focusedColor;
                ring.append(focusedColor.name());
                outside = std::min(outside, haloed ? ringHalo : contrast(focusedColor, outer));
                if (innerColor.isValid())
                    inside = std::min(inside, contrast(focusedColor, innerColor));
                change = std::min(change, contrast(focusedColor, unfocusedColor));
            }
            // The canvas's own ring against the window chrome beside it, either theme (D-023).
            double canvasRingDark = 99, canvasRingLight = 99;
            if (gl) {
                for (int edge = 0; edge < 4; ++edge) {
                    const auto edgeRing = edgeColor(after.image, after.box, after.scale, 0, edge);
                    canvasRingDark =
                        std::min(canvasRingDark, contrast(edgeRing, themeColors(true).surface));
                    canvasRingLight =
                        std::min(canvasRingLight, contrast(edgeRing, themeColors(false).surface));
                }
                outside = std::min(canvasRingDark, canvasRingLight);
                minimumCanvasRing = std::min(minimumCanvasRing, outside);
            }
            worst = QString("outside %1 inside %2").arg(outer.name(), inner.name());
            for (auto *parent = widget.parentWidget(); parent; parent = parent->parentWidget())
                worst += " < " + identity(parent) + (parent->isWindow() ? "(window)" : "");
            if (!gl && !accentFilled && !haloed)
                minimumOutside = std::min(minimumOutside, outside);
            // The two-tone ring: ring vs halo at least 3:1 and, in the dark theme, halo vs the
            // light default canvas at least 3:1. In the light theme the halo is the light chrome
            // (about 1.05:1 against the canvas, which no chrome token can change), so the ring
            // itself must have 3:1 against the canvas there.
            bool haloPasses = true;
            if (haloed) {
                haloPasses = haloIsBackground && ringHalo >= 3 &&
                             (theme == "dark" ? haloCanvas >= 3 : ringCanvas >= 3);
                minimumRingHalo = std::min(minimumRingHalo, ringHalo);
                if (theme == "dark")
                    minimumHaloCanvasDark = std::min(minimumHaloCanvasDark, haloCanvas);
                else
                    minimumRingCanvasLight = std::min(minimumRingCanvasLight, ringCanvas);
                haloCovered.insert(theme + ':' + widget.objectName() + (parametric ? ":p" : ""));
            }
            if (!gl)
                minimumInside = std::min(minimumInside, inside);
            if (accentFilled)
                minimumAccentChange = std::min(minimumAccentChange, change);
            // The 3D canvas paints its own ring on its own background (document colours); the
            // window chrome beyond it is reported but not required to meet the ratio.
            const bool passes = differs && layoutStable && inside >= 3 && haloPasses &&
                                (accentFilled ? change >= 3 : outside >= 3);
            failures += !passes;
            if (!passes)
                std::cerr << "Focus indicator failure: " << identity(&widget).toStdString()
                          << " context=" << context.toStdString()
                          << " theme=" << theme.toStdString() << " text=" << textPercent
                          << " differs=" << differs << " layoutStable=" << layoutStable
                          << " outside=" << outside << " inside=" << inside << " change=" << change
                          << " accentFilled=" << accentFilled << " haloed=" << haloed
                          << " haloIsBackground=" << haloIsBackground << " ringHalo=" << ringHalo
                          << " haloCanvas=" << haloCanvas << " ringCanvas=" << ringCanvas
                          << " canvasRing(dark,light)=" << canvasRingDark << ',' << canvasRingLight
                          << " [" << worst.toStdString() << "]\n";
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
                                    {"accentFilled", accentFilled},
                                    {"contrastChange", change},
                                    {"twoTone", haloed},
                                    {"contrastRingHalo", haloed ? ringHalo : 0.},
                                    {"contrastHaloCanvas", haloed ? haloCanvas : 0.},
                                    {"contrastRingCanvas", haloed ? ringCanvas : 0.},
                                    {"contrastCanvasRingDarkChrome", gl ? canvasRingDark : 0.},
                                    {"contrastCanvasRingLightChrome", gl ? canvasRingLight : 0.},
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
                    // Every dialog here waits for its own keyboard focus, not
                    // qWaitForWindowActive(): QWindow::isActive() counts a transient child as
                    // active with its active parent, so that passed before the compositor
                    // activated the dialog, which could then close first and have its late
                    // activation take focus from the main window (as in PR #340).
                    check(QTest::qWaitForWindowFocused(dialog), "Focus dialog active");
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
            check(QTest::qWaitForWindowFocused(&dialog), "Evidence dialog active");
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
        auto sheetsSweep = [&] {
            window.setWorkspace(Window::Workspace::Sheets);
            auto *sheets = window.sheetWorkspace();
            auto *canvas = sheets->canvas();
            auto *cache = window.sheetEvaluations();
            check(QTest::qWaitFor(
                      [&] {
                          return cache->idle() && sheets->currentSheet() &&
                                 cache->current(sheets->currentSheet());
                      },
                      20000),
                  "Sheet evaluation settles");
            window.activateWindow();
            check(QTest::qWaitForWindowActive(&window), "Window active in Sheets");
            const auto views = drawingViewsOnSheet(window.document(), sheets->currentSheet());
            const auto items = drawingItemsOnSheet(window.document(), sheets->currentSheet());
            check(views.size() == 1 && items.size() == 2, "The sweep page has a view and items");
            canvas->setSelectedView(0);
            canvas->setSelectedItem(0);
            QTest::qWait(30);
            sweep(window, "Sheets / page");
            // The accent-filled controls, every time (the sweep samples two of a class): the
            // checked Sheets segment here, Place and Done below.
            auto probeNamed = [&](const char *name, const QString &context) {
                auto *control = window.findChild<QWidget *>(name);
                check(control && control->isVisible() && control->isEnabled(),
                      "Accent-filled control shown");
                probe(window, *control, context);
            };
            probeNamed("workspaceSheets", "Sheets / Sheets segment (checked)");
            canvas->setSelectedView(views[0]);
            QTest::qWait(30);
            sweep(window, "Sheets / model view");
            canvas->setSelectedItem(items[0]);
            QTest::qWait(30);
            sweep(window, "Sheets / text");
            canvas->setSelectedItem(0);
            canvas->setSelectedView(0);
            // The sheet rail is one Tab stop like the model rail (R084.v): probe one checked
            // and one unchecked button.
            auto *rail = window.findChild<QToolBar *>("sheetRail");
            check(rail && rail->isVisible(), "Sheet rail shown");
            std::set<bool> probed;
            for (auto *button : rail->findChildren<QToolButton *>()) {
                if (!button->defaultAction() || !button->isVisibleTo(&window) ||
                    probed.contains(button->isChecked()))
                    continue;
                probed.insert(button->isChecked());
                const auto policy = button->focusPolicy();
                button->setFocusPolicy(Qt::TabFocus);
                probe(window, *button,
                      button->isChecked() ? "Sheets / sheet rail button (checked)"
                                          : "Sheets / sheet rail button");
                button->setFocusPolicy(policy);
            }
            check(probed.size() == 2, "Checked and unchecked sheet tool buttons were probed");
            // The New model view panel of the W tool.
            window.findChild<QAction *>("sheet.tool.view")->trigger();
            check(QTest::qWaitFor([&] { return window.sheetsTray()->placePanel()->isVisible(); }),
                  "New model view panel shown");
            sweep(window, "Sheets / New model view");
            probeNamed("sheetPlace", "Sheets / Place");
            window.findChild<QAction *>("sheet.tool.select")->trigger();
            // Edit-view and its banner (Save as new scene…, Done).
            canvas->setSelectedView(views[0]);
            canvas->setFocus(Qt::OtherFocusReason);
            canvas->enterEditView();
            check(canvas->editView() == views[0], "Edit-view opened");
            QTest::qWait(30);
            sweep(window, "Sheets / edit-view");
            probeNamed("sheetEditDone", "Sheets / Done");
            canvas->setFocus(Qt::OtherFocusReason);
            QTest::keyClick(canvas, Qt::Key_Escape);
            check(!canvas->editView(), "Edit-view closed");
            canvas->setSelectedView(views[0]);
            modalSweep(
                [&window] { window.findChild<QAction *>("sheet.view.chooseScene")->trigger(); },
                "chooseSceneDialog");
            // R092.b: File › Export sheets…: every control of the preflight dialog, and each
            // finding control and the accent-filled Export PDF by name (with the opt-ins set,
            // so it is enabled).
            {
                QString failure;
                bool captured{};
                QTimer::singleShot(0, &window, [&] {
                    auto *dialog =
                        dynamic_cast<SheetExportDialog *>(QApplication::activeModalWidget());
                    try {
                        check(dialog && dialog->objectName() == "sheetExportDialog",
                              "Export dialog opened");
                        check(QTest::qWaitForWindowExposed(dialog), "Export dialog exposed");
                        dialog->activateWindow();
                        check(QTest::qWaitForWindowFocused(dialog), "Export dialog active");
                        auto settled = [&] {
                            check(QTest::qWaitFor(
                                      [&] { return !dialog->checking() && dialog->preflight(); },
                                      20000),
                                  "Export preflight settles");
                        };
                        settled();
                        for (auto *allow : dialog->findChildren<QCheckBox *>())
                            if (allow->objectName().startsWith("sheetExportAllow") &&
                                allow->isVisible() && !allow->isChecked()) {
                                allow->setChecked(true);
                                settled();
                            }
                        sweep(*dialog, "Sheets / Export sheets");
                        auto *confirm = dialog->findChild<QPushButton *>("sheetExportConfirm");
                        check(confirm && confirm->isEnabled(), "Export PDF is ready to probe");
                        for (const char *name :
                             {"sheetExportConfirm", "sheetExportReview", "sheetExportGoToView",
                              "sheetExportChooseFont", "sheetExportChoose", "sheetExportCancel"})
                            if (auto *control = dialog->findChild<QWidget *>(name);
                                control && control->isVisible() && control->isEnabled())
                                probe(*dialog, *control,
                                      QString("Sheets / Export sheets / ") + name);
                        captured = true;
                    } catch (const std::exception &error) {
                        failure = QString::fromUtf8(error.what());
                    }
                    if (dialog)
                        dialog->reject();
                });
                window.findChild<QAction *>("sheet.export")->trigger();
                if (!failure.isEmpty())
                    throw std::runtime_error(failure.toStdString());
                check(captured, "Export dialog sweep completed");
                window.activateWindow();
                check(QTest::qWaitForWindowActive(&window), "Main window regains focus");
            }
            canvas->setSelectedView(0);
            window.setWorkspace(Window::Workspace::Model);
            window.activateWindow();
            check(QTest::qWaitForWindowActive(&window), "Window active after Sheets");
        };
        // D-023: a focused tool rail button. The rail's own background around it is the halo
        // (4 to 5 px of chrome lie between the button and the canvas), so the ring is measured
        // against it as for the other two-tone rings, and the gap to the canvas is checked.
        auto railHalo = [&](QToolButton &button, const QString &name) {
            button.setFocus(Qt::TabFocusReason);
            QCoreApplication::processEvents();
            const auto shot = capture(window, button);
            const auto chrome = themeColors(theme == "dark");
            const auto halo =
                bandColor(shot.image, shot.box, shot.valid, {}, shot.scale, {-1.0}, 0b1111);
            double ringHalo = 99, ringCanvas = 99;
            for (int edge = 0; edge < 4; ++edge) {
                const auto ring = edgeColor(shot.image, shot.box, shot.scale, 0, edge);
                ringHalo = std::min(ringHalo, contrast(ring, halo));
                ringCanvas = std::min(ringCanvas, contrast(ring, defaultCanvas));
            }
            const double haloCanvas = contrast(halo, defaultCanvas);
            const int gap = window.viewport()->mapTo(&window, QPoint()).x() -
                            (button.mapTo(&window, QPoint()).x() + button.width());
            const bool passes = button.hasFocus() && halo == chrome.surface && gap >= 2 &&
                                ringHalo >= 3 &&
                                (theme == "dark" ? haloCanvas >= 3 : ringCanvas >= 3);
            minimumRingHalo = std::min(minimumRingHalo, ringHalo);
            if (theme == "dark")
                minimumHaloCanvasDark = std::min(minimumHaloCanvasDark, haloCanvas);
            else
                minimumRingCanvasLight = std::min(minimumRingCanvasLight, ringCanvas);
            haloCovered.insert(theme + ":toolRailButton" + name);
            if (!passes) {
                ++failures;
                std::cerr << "Rail halo failure: " << name.toStdString()
                          << " theme=" << theme.toStdString()
                          << " halo=" << halo.name().toStdString() << " ringHalo=" << ringHalo
                          << " haloCanvas=" << haloCanvas << " ringCanvas=" << ringCanvas
                          << " gap=" << gap << " focus=" << button.hasFocus() << '\n';
            }
            rows.append(QJsonObject{{"context", "Main window / tool rail halo" + name},
                                    {"theme", theme},
                                    {"textPercent", textPercent},
                                    {"class", "QToolButton"},
                                    {"twoTone", true},
                                    {"contrastRingHalo", ringHalo},
                                    {"contrastHaloCanvas", haloCanvas},
                                    {"contrastRingCanvas", ringCanvas},
                                    {"gapToCanvas", gap},
                                    {"passes", passes}});
        };
        // D-023 (c): native scroll bars in the theme: a themed track and handle with 3:1
        // between them, the interface-text-scaled thickness, a rounded handle, no arrow buttons.
        auto scrollBars = [&] {
            const auto chrome = themeColors(theme == "dark");
            auto *host = new QWidget(&window);
            auto *layout = new QVBoxLayout(host);
            layout->setContentsMargins(0, 0, 0, 0);
            auto *vertical = new QScrollBar(Qt::Vertical);
            vertical->setFixedHeight(180);
            auto *horizontal = new QScrollBar(Qt::Horizontal);
            horizontal->setFixedWidth(180);
            for (auto *bar : {vertical, horizontal}) {
                bar->setRange(0, 100);
                bar->setPageStep(50);
                bar->setValue(0);
                layout->addWidget(bar);
            }
            host->setFixedSize(200, 180 + 70);
            host->move(12, 12);
            host->show();
            QTest::qWait(60);
            const int thickness = interfaceExtent(10, textPercent);
            for (auto *bar : {vertical, horizontal}) {
                const bool tall = bar->orientation() == Qt::Vertical;
                const auto image = bar->grab().toImage().convertToFormat(QImage::Format_RGB32);
                const auto scale = image.devicePixelRatio();
                const int length = tall ? image.height() : image.width();
                const int across = tall ? image.width() : image.height();
                auto at = [&](int along, int position) {
                    return QColor(tall ? image.pixel(position, along)
                                       : image.pixel(along, position));
                };
                int handleCount = 0, trackCount = 0, otherCount = 0, handleStart = -1;
                for (int i = 0; i < length; ++i) {
                    const auto color = at(i, across / 2);
                    if (color == chrome.muted) {
                        ++handleCount;
                        if (handleStart < 0)
                            handleStart = i;
                    } else if (color == chrome.surface)
                        ++trackCount;
                    else
                        ++otherCount;
                }
                int handleAcross = 0;
                if (handleStart >= 0)
                    for (int j = 0; j < across; ++j)
                        handleAcross += at(handleStart + handleCount / 2, j) == chrome.muted;
                const double handleTrack = contrast(chrome.muted, chrome.surface);
                const double hoverTrack = contrast(chrome.ink, chrome.surface);
                const int thick = tall ? bar->width() : bar->height();
                const bool rounded = handleStart >= 0 && at(handleStart, 0) == chrome.surface;
                const bool passes = handleTrack >= 3 && hoverTrack >= 3 && thick == thickness &&
                                    std::abs(handleAcross - thickness * scale) <= 1 &&
                                    handleCount * 5 >= length && trackCount * 5 >= length &&
                                    otherCount * 50 <= length && rounded;
                minimumScrollHandle = std::min(minimumScrollHandle, handleTrack);
                ++scrollBarsMeasured;
                if (!passes) {
                    ++failures;
                    std::cerr << "Scroll bar failure: " << (tall ? "vertical" : "horizontal")
                              << " theme=" << theme.toStdString() << " text=" << textPercent
                              << " thickness=" << thick << " (want " << thickness
                              << ") handleAcross=" << handleAcross << " handle=" << handleCount
                              << " track=" << trackCount << " other=" << otherCount << " of "
                              << length << " handleVsTrack=" << handleTrack
                              << " inkVsTrack=" << hoverTrack << " rounded=" << rounded << '\n';
                }
                rows.append(QJsonObject{
                    {"context", tall ? "Scroll bar / vertical" : "Scroll bar / horizontal"},
                    {"theme", theme},
                    {"textPercent", textPercent},
                    {"class", "QScrollBar"},
                    {"contrastHandleTrack", handleTrack},
                    {"contrastHoverTrack", hoverTrack},
                    {"thickness", thick},
                    {"passes", passes}});
                if (!captureDir.isEmpty() && textPercent == 100)
                    host->grab().save(QString("%1/scrollbars-%2.png").arg(captureDir, theme));
            }
            delete host;
        };
        // D-023 evidence: SKETCHYUP_FOCUS_WINDOW_SHOTS=dir saves the 1200x800 window with a rail
        // button focused, with the breadcrumb focused, and with a list and its scroll bars.
        auto windowShots = [&](const QString &directory) {
            window.resize(1200, 800);
            check(QTest::qWaitFor([&] { return window.width() == 1200 && window.height() == 800; }),
                  "Window sized for the screenshots");
            QTest::qWait(80);
            const auto name = [&](const char *what) {
                return QString("%1/%2-%3-%4x.png")
                    .arg(directory, what, theme, QString::number(window.devicePixelRatioF()));
            };
            auto *rail = window.findChild<QToolBar *>("toolRail");
            for (auto *button : rail->findChildren<QToolButton *>())
                if (button->defaultAction() && button->isVisibleTo(&window) &&
                    !button->isChecked()) {
                    button->setFocusPolicy(Qt::TabFocus);
                    button->setFocus(Qt::TabFocusReason);
                    QTest::qWait(60);
                    window.grab().save(name("rail"));
                    button->setFocusPolicy(Qt::NoFocus);
                    break;
                }
            auto *crumb = window.findChild<QLabel *>("contextBreadcrumb");
            crumb->setFocus(Qt::TabFocusReason);
            QTest::qWait(60);
            window.grab().save(name("breadcrumb"));
            window.viewport()->setFocus(Qt::OtherFocusReason);
            auto *host = new QWidget(&window);
            auto *hostLayout = new QVBoxLayout(host);
            auto *list = new QListWidget;
            for (int i = 0; i < 60; ++i)
                list->addItem(QString("Scroll bar sample row %1 with a line long enough to scroll "
                                      "sideways as well, so both bars show")
                                  .arg(i + 1));
            hostLayout->addWidget(list);
            host->setGeometry(420, 180, 380, 300);
            host->show();
            host->raise();
            QTest::qWait(80);
            window.grab().save(name("scrollbars"));
            delete host;
            window.resize(1280, 900);
            check(QTest::qWaitFor([&] { return window.width() == 1280; }), "Window restored");
            QTest::qWait(50);
        };
        const auto windowShotDir = qEnvironmentVariable("SKETCHYUP_FOCUS_WINDOW_SHOTS");
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
                if (sheetsOnly) {
                    // The canvas, page tabs, Pages tree and Entity info for the page, a view and
                    // an item; the sheet rail; the New model view panel; the edit-view banner;
                    // and the Choose scene dialog.
                    sheetsSweep();
                    continue;
                }
                auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
                check(tabs && tabs->count() > 0, "Model-panel tabs exist");
                // Every panel at the default size; the first three at 200% (the enlarged-text
                // layout of the same controls), which bounds the native run time.
                for (int page = 0; page < (percent == 100 ? tabs->count() : 3); ++page) {
                    tabs->setCurrentIndex(page);
                    QTest::qWait(30);
                    sweep(window, "Main window / " + tabs->tabText(page));
                }
                // Only the active tool's rail button is a Tab stop (R084.v); temporarily allow
                // Tab focus on one checked and one unchecked button to prove the rule on both.
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
                    railHalo(*button, button->isChecked() ? ":checked" : "");
                    button->setFocusPolicy(Qt::NoFocus);
                }
                check(probed.size() == 2, "Checked and unchecked tool buttons were probed");
                // D-023: the breadcrumb and the component scope banner float on the 3D canvas.
                {
                    auto *crumb = window.findChild<QLabel *>("contextBreadcrumb");
                    auto *banner = window.findChild<QLabel *>("componentScopeBanner");
                    check(crumb && banner && crumb->isVisible(), "Breadcrumb and banner exist");
                    probe(window, *crumb, "Main window / breadcrumb");
                    const bool bannerShown = banner->isVisible();
                    banner->setText("Editing the shared definition. <a href=\"close\">Close "
                                    "definition</a> (Esc)");
                    banner->setFixedWidth(420);
                    banner->move(16, crumb->geometry().bottom() + 8);
                    for (bool parametric : {false, true}) {
                        banner->setProperty("parametric", parametric);
                        banner->style()->unpolish(banner);
                        banner->style()->polish(banner);
                        banner->adjustSize();
                        banner->show();
                        banner->raise();
                        QTest::qWait(30);
                        probe(window, *banner,
                              parametric ? "Main window / component scope banner (parametric)"
                                         : "Main window / component scope banner");
                    }
                    banner->setProperty("parametric", false);
                    banner->style()->unpolish(banner);
                    banner->style()->polish(banner);
                    banner->setVisible(bannerShown);
                }
                scrollBars();
                if (!windowShotDir.isEmpty() && percent == 100)
                    windowShots(windowShotDir);
                // UX_DESIGN §3.3: the narrow layout's panel strip (a checked and an unchecked
                // button, with the overlay sheet open on Info) and the Menu button, at the
                // default text size (the Wayland run's time limit).
                if (percent == 100) {
                    window.resize(900, 900);
                    check(QTest::qWaitFor([&] { return window.width() == 900; }),
                          "Window narrowed for the panel strip");
                    QTest::qWait(50);
                    auto *strip = window.findChild<QToolBar *>("panelStrip");
                    check(strip && strip->isVisible(), "Panel strip shows at 900 px");
                    window.findChild<QAction *>("panelStrip.info")->trigger();
                    QTest::qWait(50);
                    for (const char *id : {"panelStrip.info", "panelStrip.outliner"}) {
                        auto *button = qobject_cast<QToolButton *>(
                            strip->widgetForAction(window.findChild<QAction *>(id)));
                        check(button && button->isChecked() == (QString(id) == "panelStrip.info"),
                              "Panel strip button state");
                        const auto policy = button->focusPolicy();
                        button->setFocusPolicy(Qt::TabFocus);
                        probe(window, *button,
                              button->isChecked() ? "Narrow / panel strip button (checked)"
                                                  : "Narrow / panel strip button");
                        button->setFocusPolicy(policy);
                    }
                    window.findChild<QAction *>("panelStrip.info")->trigger();
                    QTest::qWait(50);
                    probe(window, *window.findChild<QToolButton *>("menuButton"),
                          "Narrow / Menu button");
                    window.resize(1280, 900);
                    check(QTest::qWaitFor([&] { return window.width() == 1280; }),
                          "Window widened again");
                    QTest::qWait(50);
                }
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
                // R094.e (ADR 0166 §4.1): "… has its own values": every button, and the
                // accent-filled default every time (the sweep samples two of a class).
                {
                    QString failure;
                    bool captured{};
                    QTimer::singleShot(0, &window, [&] {
                        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                        try {
                            check(dialog && dialog->objectName() == "parametersScopeDialog",
                                  "The parameters dialog opened");
                            check(QTest::qWaitForWindowExposed(dialog),
                                  "Parameters dialog exposed");
                            dialog->activateWindow();
                            check(QTest::qWaitForWindowFocused(dialog), "Parameters dialog active");
                            check(QTest::qWaitFor([&] {
                                      auto *focused = QApplication::focusWidget();
                                      return focused && dialog->isAncestorOf(focused);
                                  }),
                                  "Parameters dialog owns keyboard focus");
                            sweep(*dialog, "Placement has its own values");
                            for (const char *name :
                                 {"parametersScopeCancel", "parametersScopeUnique",
                                  "parametersScopeDefaults"})
                                probe(*dialog, *dialog->findChild<QPushButton *>(name),
                                      QString("Placement has its own values / ") + name);
                            captured = true;
                        } catch (const std::exception &error) {
                            failure = QString::fromUtf8(error.what());
                        }
                        if (dialog)
                            dialog->reject();
                    });
                    ParametersScopeDialog dialog("Dining table #3", "Width 1200 mm and Shelves 2",
                                                 2, {}, &window);
                    dialog.exec();
                    if (!failure.isEmpty())
                        throw std::runtime_error(failure.toStdString());
                    check(captured, "Parameters dialog sweep completed");
                    window.activateWindow();
                    check(QTest::qWaitForWindowActive(&window), "Main window regains focus");
                }
                if (!window.assistantPanel()->isVisible())
                    window.findChild<QAction *>("view.assistant")->trigger();
                // D-023: the Assistant container's two-tone ring.
                if (window.assistantPanel()->isVisible() && window.assistantPanel()->width() >= 12)
                    probe(window, *window.assistantPanel(), "Main window / Assistant container");
                window.findChild<QPushButton *>("assistantPreferences")->click();
                auto *dialog = window.findChild<QDialog *>("assistantPreferencesDialog");
                check(dialog && QTest::qWaitForWindowExposed(dialog),
                      "Assistant preferences exposed");
                dialog->activateWindow();
                check(QTest::qWaitForWindowFocused(dialog), "Assistant preferences active");
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
            if (!sheetsOnly && !covered.contains(name))
                missing.append(name);
        for (const char *name : kSheetsFamilies)
            if (sheetsOnly && !covered.contains(name))
                missing.append(name);
        // D-023: every two-tone ring in both themes (the Model run), and the scroll bars.
        if (!sheetsOnly) {
            for (const char *mode : {"light", "dark"})
                for (const char *name :
                     {"contextBreadcrumb", "componentScopeBanner", "componentScopeBanner:p",
                      "assistantContainer", "toolRailButton", "toolRailButton:checked"})
                    if (!haloCovered.contains(QString("%1:%2").arg(mode, name)))
                        missing.append(QString("two-tone ring %1 %2").arg(theme, name));
            if (scrollBarsMeasured != 8)
                missing.append("scroll bars");
        }
        QJsonArray coveredClasses;
        for (const auto &name : covered)
            coveredClasses.append(name);
        const bool passed = failures == 0 && missing.isEmpty() && measured > 0;
        const auto bytes =
            QJsonDocument(
                QJsonObject{
                    {"rows", rows},
                    {"measuredControls", measured},
                    {"skippedClippedControls", clipped},
                    {"failures", failures},
                    {"coveredClasses", coveredClasses},
                    {"uncoveredRequiredClasses", missing},
                    {"minimumContrastOutside", minimumOutside},
                    {"minimumContrastInside", minimumInside},
                    {"minimumAccentRingChange", minimumAccentChange},
                    {"twoToneRings",
                     QJsonObject{{"minimumRingHalo", minimumRingHalo},
                                 {"minimumHaloCanvasDark", minimumHaloCanvasDark},
                                 {"minimumRingCanvasLight", minimumRingCanvasLight},
                                 {"covered", QJsonArray::fromStringList(QStringList(
                                                 haloCovered.begin(), haloCovered.end()))}}},
                    {"canvasRingMinimumContrastAgainstChrome", minimumCanvasRing},
                    {"scrollBars",
                     QJsonObject{{"measured", scrollBarsMeasured},
                                 {"minimumHandleTrackContrast", minimumScrollHandle}}},
                    {"workspace", sheetsOnly ? "sheets" : "model"},
                    {"passes", passed},
                    {"scope",
                     sheetsOnly
                         ? "Painted boundary of every Tab-focusable public control in the "
                           "Sheets workspace (canvas, page tabs, Pages, Entity info for "
                           "the page, a view and a text, sheet rail, New model view, "
                           "edit-view, Choose scene, Export sheets), in light and dark themes at "
                           "100% "
                           "and 200% interface text"
                         : "Painted boundary of every Tab-focusable public control in "
                           "the main window panels, tool rail, shortcut, drawing "
                           "plane, command, placement-values and assistant preference "
                           "dialogs, in light and dark themes at 100% and 200% interface text"},
                    {"releaseAcceptance", false}})
                .toJson();
        if (!reportPath.isEmpty()) {
            QFile report(reportPath);
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
