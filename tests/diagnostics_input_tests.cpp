#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/components.hpp"
#include "core/face_orientation.hpp"
#include "core/groups.hpp"
#include "geometry/diagnostics.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QWindow>
#include <iostream>
using namespace sketchy;
namespace {
int focusedReportClosures{};
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void events() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}
QPushButton *button(QDialog *report, const char *name) {
    auto *result = report->findChild<QPushButton *>(name);
    check(result, "Diagnostic button exists");
    return result;
}
void click(QDialog *report, const char *name) {
    auto *target = button(report, name);
    if (!target->isEnabled())
        throw std::runtime_error(std::string("Diagnostic button disabled: ") + name);
    auto *window = qobject_cast<Window *>(report->parentWidget());
    QTest::mouseClick(target, Qt::LeftButton);
    events();
    if (QString::fromLatin1(name) == "diagnosticsRepair")
        check(QTest::qWaitFor(
                  [&] {
                      return !window->viewport()->findChild<QObject *>("diagnosticRepairHandoff");
                  },
                  6000),
              "Repair focus handoff completes");
    events();
}
QDialog *open(Window &window, Id body) {
    window.viewport()->setTool(Viewport::Tool::Select);
    window.viewport()->setSelection(body);
    auto *action = window.findChild<QAction *>("geometry.diagnose");
    check(action && action->isEnabled(), "Native diagnostics action enabled");
    action->trigger();
    events();
    auto *report = window.findChild<QDialog *>("geometryDiagnosticsReport");
    check(report && report->isVisible(), "Native report visible");
    const QPointer<QWindow> native = report->windowHandle();
    QObject::connect(report, &QDialog::finished, &window, [native] {
        if (native && QGuiApplication::focusWindow() == native)
            ++focusedReportClosures;
    });
    report->activateWindow();
    check(QTest::qWaitFor(
              [&] {
                  return QApplication::activeWindow() == report &&
                         QGuiApplication::focusWindow() == report->windowHandle();
              },
              5000),
          "Diagnostic report receives its own native focus");
    return report;
}
void choose(QDialog *report, const char *code) {
    auto *rows = report->findChild<QTreeWidget *>("diagnosticsFindings");
    for (int i = 0; i < rows->topLevelItemCount(); ++i) {
        auto *item = rows->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == code) {
            rows->setCurrentItem(item);
            events();
            return;
        }
    }
    throw std::runtime_error(std::string("Missing native finding ") + code);
}
Id cube(Document &doc, double x = 0) {
    const auto body = doc.addFace({{{x, 0, 0}, {x + 2, 0, 0}, {x + 2, 2, 0}, {x, 2, 0}}});
    doc.extrude(body, 5, 2);
    return body;
}
void invert(Document &doc, Id body, Id context = 0) {
    SelectionSet faces;
    for (const auto &[id, face] : doc.bodies().at(body)->surface.faces)
        faces.insert({body, SelectionKind::Face, id});
    reverseSelectedFaces(doc, faces, context);
}
void focus(Window &window) {
    window.activateWindow();
    check(QTest::qWaitFor(
              [&] {
                  return QApplication::activeWindow() == &window &&
                         QGuiApplication::focusWindow() == window.windowHandle();
              },
              5000),
          "Model window receives native focus");
    window.viewport()->setFocus();
    events();
}
void close(QDialog *report) {
    QPointer<QWindow> native = report->windowHandle();
    report->close();
    events();
    check(QTest::qWaitFor([&] { return native.isNull(); }, 2000),
          "Closing diagnostics destroys its native window");
    QGuiApplication::sync();
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    window.show();
    try {
        check(QTest::qWaitForWindowExposed(&window), "Diagnostics window exposed");
        auto &doc = window.document();
        auto &view = *window.viewport();
        const auto capture = qEnvironmentVariable("SKETCHYUP_DIAGNOSTICS_CAPTURE");
        if (!capture.isEmpty())
            QDir().mkpath(capture);
        auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        view.refresh();
        view.fit();
        auto *report = open(window, body);
        choose(report, "open_boundary");
        const auto before = encodeContainer(doc);
        const auto originalSelection = view.selectionState().entities();
        click(report, "diagnosticsFrame");
        check(encodeContainer(doc) == before &&
                  view.selectionState().entities() == originalSelection,
              "Frame changes camera only");
        click(report, "diagnosticsSelect");
        check(view.selectionState().entities().size() == 5 && encodeContainer(doc) == before,
              "Open sheet selects four edges and its face without history changes");
        check(!button(report, "diagnosticsRepair")->isVisible(),
              "Open sheet has no inferred repair");
        if (!capture.isEmpty())
            check(report->grab().save(capture + "/open-report.png"), "Capture native report");
        doc.move(body, {1, 0, 0});
        // Trigger before timer delivery: handler must itself reject stale data.
        button(report, "diagnosticsSelect")->click();
        events();
        check(!button(report, "diagnosticsSelect")->isEnabled(),
              "Stale report rejects action synchronously");
        click(report, "diagnosticsRefresh");
        check(button(report, "diagnosticsSelect")->isEnabled(),
              "Explicit refresh renews current report");
        setEntityState(doc, body, {}, true);
        view.refresh();
        click(report, "diagnosticsRefresh");
        check(!button(report, "diagnosticsSelect")->isEnabled() &&
                  button(report, "diagnosticsFrame")->isEnabled(),
              "Locked findings remain frameable but cannot select or edit");
        close(report);

        doc = Document{};
        body = cube(doc);
        const auto unrelated = cube(doc, 6);
        invert(doc, body);
        view.refresh();
        view.fit();
        const auto original = doc.bodies().at(body), other = doc.bodies().at(unrelated);
        const auto base = encodeContainer(doc);
        const auto depth = doc.history().total;
        report = open(window, body);
        choose(report, "inverted_shells");
        check(button(report, "diagnosticsRepair")->isEnabled(),
              "Complete inverted shell offers preview");
        const auto assistantPreview = std::make_shared<const Document::PreparedEdit>(
            doc.prepareEdit([&](Document &draft) { draft.move(body, {0, .5, 0}); }));
        view.setAssistantPreview(assistantPreview);
        button(report, "diagnosticsRepair")->click();
        events();
        check(view.hasAssistantPreview() && !view.previewValid() &&
                  !button(report, "diagnosticsRepair")->isEnabled() && encodeContainer(doc) == base,
              "Diagnostic repair cannot replace an active assistant preview");
        view.setAssistantPreview({});
        check(
            QTest::qWaitFor([&] { return button(report, "diagnosticsRepair")->isEnabled(); }, 5000),
            "Clearing the assistant preview restores diagnostic repair availability");
        click(report, "diagnosticsRepair");
        focus(window);
        check(view.tool() == Viewport::Tool::Orientation && view.previewValid() &&
                  encodeContainer(doc) == base,
              "Diagnostic repair is an immutable orientation preview");
        if (!capture.isEmpty())
            check(view.grabFramebuffer().save(capture + "/repair-preview.png"),
                  "Capture repair arrows");
        QTest::keyClick(&view, Qt::Key_Escape);
        events();
        check(encodeContainer(doc) == base && doc.history().total == depth,
              "Escape leaves no repair history");
        report = open(window, body);
        choose(report, "inverted_shells");
        click(report, "diagnosticsRepair");
        focus(window);
        QTest::keyClick(&view, Qt::Key_Return);
        events();
        check(doc.history().total == depth + 1 && doc.bodies().at(unrelated) == other &&
                  diagnoseGeometry(doc.bodies().at(body)->surface, doc.bodies().at(body)->topology)
                      .findings.empty(),
              "Apply repairs the inverted shell in one edit without deleting unrelated geometry");
        doc.undo();
        view.refresh();
        check(*doc.bodies().at(body) == *original, "Undo restores exact inverted body");
        doc.redo();
        view.refresh();
        reverseSelectedFaces(doc, {{body, SelectionKind::Face, 5}}, 0);
        view.refresh();
        report = open(window, body);
        choose(report, "inconsistent_winding");
        auto *reference = report->findChild<QComboBox *>("diagnosticsReference");
        check(reference->isVisible() && !button(report, "diagnosticsRepair")->isEnabled(),
              "Orient requires an explicit reference choice");
        int index = -1;
        for (int i = 1; i < reference->count(); ++i)
            if (reference->itemData(i).toULongLong() != 5) {
                index = i;
                break;
            }
        check(index > 0, "Unchanged neighboring reference available");
        reference->setCurrentIndex(index);
        events();
        click(report, "diagnosticsRepair");
        focus(window);
        QTest::keyClick(&view, Qt::Key_Return);
        events();
        check(diagnoseGeometry(doc.bodies().at(body)->surface, doc.bodies().at(body)->topology)
                  .findings.empty(),
              "Explicit neighboring reference repairs one reversed face");

        view.setTool(Viewport::Tool::Select);
        doc = Document{};
        body = cube(doc);
        auto old = doc.bodies().at(body);
        auto orphan = std::make_shared<Body>(*old);
        const auto vertex = orphan->surface.vertex({20, 30, 40});
        orphan->topology = Topology::rebuild(orphan->surface, old->topology);
        doc.apply({"Orphan fixture", {{body, old, orphan}}}, doc.revision());
        view.refresh();
        report = open(window, body);
        choose(report, "loose_vertices");
        check(!button(report, "diagnosticsSelect")->isEnabled() &&
                  button(report, "diagnosticsFrame")->isEnabled(),
              "Isolated vertex can frame without pretending it is a selectable edge");
        for (bool orthographic : {false, true}) {
            view.setOrthographic(orthographic);
            click(report, "diagnosticsFrame");
            const auto vertexPoint = orphan->surface.vertices.at(vertex);
            const auto point = view.project(vertexPoint);
            check(std::abs(point.x() - view.width() * .5) < 2 &&
                      std::abs(point.y() - view.height() * .5) < 2,
                  "Frame locates orphan vertex at camera center");
            const auto camera = view.inferenceCamera();
            const auto &m = camera.clipFromWorld;
            const auto z =
                m[2] * vertexPoint.x + m[6] * vertexPoint.y + m[10] * vertexPoint.z + m[14];
            const auto w =
                m[3] * vertexPoint.x + m[7] * vertexPoint.y + m[11] * vertexPoint.z + m[15];
            check(
                z > -w + std::abs(w) * 1e-6 && z < w,
                "Isolated framed point lies inside both perspective and orthographic clip planes");
        }
        view.setOrthographic(false);
        doc = Document{};
        cube(doc);
        view.refresh();
        QTest::qWait(250);
        check(!button(report, "diagnosticsRefresh")->isEnabled() &&
                  !button(report, "diagnosticsFrame")->isEnabled(),
              "Replacement document cannot reuse old numeric references");
        close(report);
        view.setTool(Viewport::Tool::Select);
        doc = Document{};
        body = cube(doc);
        invert(doc, body);
        const auto group = createGroup(doc, {body}, "Repair context");
        view.refresh();
        view.enterContext(group);
        report = open(window, body);
        choose(report, "inverted_shells");
        view.leaveContext();
        button(report, "diagnosticsRepair")->click();
        events();
        check(!view.previewValid() && !button(report, "diagnosticsRepair")->isEnabled(),
              "Context change rejects stale repair before starting preview");
        click(report, "diagnosticsRefresh");
        check(!button(report, "diagnosticsRepair")->isEnabled(),
              "Outside-context geometry cannot repair");
        close(report);
        view.setTool(Viewport::Tool::Select);
        doc = Document{};
        body = cube(doc);
        invert(doc, body);
        const auto root = createGroup(doc, {body}, "Shared repair");
        const auto component = createComponent(doc, root);
        const auto instance =
            placeComponent(doc, component.definition,
                           Transform::translation({8, 0, 0}) * Transform::scaling({-1, 2, .5}))
                .instance;
        const auto sibling = doc.instances().at(instance)->members.at(body);
        view.refresh();
        view.enterContext(root);
        const auto sharedBefore = encodeContainer(doc);
        report = open(window, body);
        choose(report, "inverted_shells");
        click(report, "diagnosticsRepair");
        focus(window);
        check(view.previewValid() && encodeContainer(doc) == sharedBefore,
              "Shared repair preview remains private");
        QTest::keyClick(&view, Qt::Key_Return);
        events();
        for (auto id : {body, sibling})
            check(diagnoseGeometry(doc.bodies().at(id)->surface, doc.bodies().at(id)->topology)
                      .findings.empty(),
                  "Shared repair updates reflected placement through canonical scope");
        doc.undo();
        view.refresh();
        makeComponentUnique(doc, root);
        view.refresh();
        const auto siblingBefore = doc.bodies().at(sibling);
        report = open(window, body);
        choose(report, "inverted_shells");
        click(report, "diagnosticsRepair");
        focus(window);
        QTest::keyClick(&view, Qt::Key_Return);
        events();
        check(doc.bodies().at(sibling) == siblingBefore &&
                  diagnoseGeometry(doc.bodies().at(body)->surface, doc.bodies().at(body)->topology)
                      .findings.empty(),
              "Make Unique isolates diagnostic repair");
        check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
              "Diagnostic repair persists exactly");
        view.setTool(Viewport::Tool::Select);
        doc = Document{};
        auto many = std::make_shared<Body>();
        many->id = 1;
        for (int i = 0; i < 12; ++i) {
            Surface box;
            const double x = i * 3;
            const auto base = box.addFace({{{x, 0, 0}, {x + 1, 0, 0}, {x + 1, 1, 0}, {x, 1, 0}}});
            box.extrude(base, 1);
            for (const auto &[id, face] : box.faces) {
                std::vector<std::vector<Vec3>> loops;
                for (const auto &loop : face.loops) {
                    loops.emplace_back();
                    for (auto vertex : loop)
                        loops.back().push_back(box.vertices.at(vertex));
                    std::reverse(loops.back().begin(), loops.back().end());
                }
                many->surface.addFace(loops);
            }
        }
        many->topology = Topology::rebuild(many->surface, {});
        doc.apply({"Truncated fixture", {{1, nullptr, many}}}, doc.revision());
        view.refresh();
        report = open(window, 1);
        choose(report, "inverted_shells");
        check(!button(report, "diagnosticsRepair")->isVisible() &&
                  report->findChild<QPlainTextEdit *>("diagnosticsDetails")
                      ->toPlainText()
                      .contains("sample"),
              "Truncated inverted sample never offers whole-shell repair");
        close(report);
        view.setTool(Viewport::Tool::Select);
        doc = Document{};
        body = cube(doc);
        invert(doc, body);
        view.refresh();
        report = open(window, body);
        choose(report, "inverted_shells");
        // Delay actual deletion independently of visibility. Repair must wait
        // for the native report lifetime, not assume a 20 ms timer is enough.
        report->setAttribute(Qt::WA_DeleteOnClose, false);
        button(report, "diagnosticsRepair")->click();
        QTest::qWait(150);
        check(!view.previewValid() && view.findChild<QObject *>("diagnosticRepairHandoff"),
              "Hidden but retained report cannot hand off an orientation preview");
        delete report;
        check(QTest::qWaitFor([&] { return !view.findChild<QObject *>("diagnosticRepairHandoff"); },
                              6000) &&
                  view.previewValid(),
              "Actual report destruction completes guarded handoff");
        QTest::keyClick(&view, Qt::Key_Escape);
        events();
        report = open(window, body);
        choose(report, "inverted_shells");
        button(report, "diagnosticsRepair")->click();
        doc.move(body, {.25, 0, 0});
        const auto duringHandoff = encodeContainer(doc);
        check(QTest::qWaitFor([&] { return !view.findChild<QObject *>("diagnosticRepairHandoff"); },
                              6000) &&
                  !view.previewValid() && encodeContainer(doc) == duringHandoff,
              "Model change during focus handoff cannot stage or publish a stale repair");
        report = open(window, body);
        choose(report, "inverted_shells");
        button(report, "diagnosticsRepair")->click();
        QTest::keyClick(&view, Qt::Key_Escape);
        check(QTest::qWaitFor([&] { return !view.findChild<QObject *>("diagnosticRepairHandoff"); },
                              6000) &&
                  !view.previewValid() && encodeContainer(doc) == duringHandoff,
              "Escape cancels a pending focus handoff before preview creation");
        events();
        for (bool escape : {false, true}) {
            report = open(window, body);
            QPointer<QDialog> closing = report;
            int finished = 0;
            QObject::connect(report, &QDialog::finished, &window, [&](int result) {
                check(result == QDialog::Rejected, "Report close keeps its rejection result");
                ++finished;
            });
            if (escape)
                QTest::keyClick(report, Qt::Key_Escape);
            else {
                auto *buttons = report->findChild<QDialogButtonBox *>();
                check(buttons, "Report close button exists");
                QTest::mouseClick(buttons->button(QDialogButtonBox::Close), Qt::LeftButton);
            }
            // A second close request must not bypass the pending native drain.
            if (closing)
                closing->close();
            check(QTest::qWaitFor([&] { return closing.isNull(); }, 2000) && finished == 1,
                  "Close button and Escape finish once after native report teardown");
        }
        check(focusedReportClosures == 0,
              "Report releases native focus before finished permits surface deletion");
        std::cout << "Native diagnostic findings, selection/frame, explicit repairs, Undo and "
                     "staleness passed; DPR "
                  << view.devicePixelRatioF() << std::endl;
        doc = Document{};
        view.refresh();
        QGuiApplication::sync();
        if (QGuiApplication::platformName().startsWith("wayland")) {
            QTimer::singleShot(0, &window,
                               [&] { doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}); });
            window.close();
            check(QTest::qWaitFor([&] { return window.windowHandle()->isVisible(); }, 2000) &&
                      window.isVisible() && !doc.bodies().empty(),
                  "An intervening model edit cancels deferred shutdown and remaps the window");
            doc = Document{};
            view.refresh();
        }
        unsigned lastClosed = 0;
        QObject::connect(&app, &QGuiApplication::lastWindowClosed, &window, [&] { ++lastClosed; });
        QTimer::singleShot(0, &window, [&] {
            // A repeated close during native drain cannot destroy the surface early.
            QTimer::singleShot(0, &window, [&] { window.close(); });
            window.close();
        });
        QTimer::singleShot(5000, &app, [&] { app.exit(2); });
        const auto exitCode = app.exec();
        if (exitCode || lastClosed != 1 || window.isVisible())
            std::cerr << "Shutdown result: exit=" << exitCode << " lastClosed=" << lastClosed
                      << " visible=" << window.isVisible() << '\n';
        check(exitCode == 0 && lastClosed == 1 && !window.isVisible(),
              "Final native close emits lastWindowClosed once and quits the application");
        events();
        QGuiApplication::sync();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        std::cerr << "tool=" << int(window.viewport()->tool())
                  << " preview=" << window.viewport()->previewValid()
                  << " phase=" << int(window.viewport()->interactionPhase())
                  << " revision=" << window.document().revision() << '\n';
        if (auto *report = window.findChild<QDialog *>("geometryDiagnosticsReport"))
            std::cerr << "report visible=" << report->isVisible() << " notice="
                      << report->findChild<QLabel *>("diagnosticsNotice")->text().toStdString()
                      << '\n';
        return 1;
    }
}
