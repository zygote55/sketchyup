// R100.f2 (UX_DESIGN §18.1 to §18.3, §18.6, §18.7): the desktop side of styles. The Styles tab's
// Edge stroke section (each control one labelled document.style edit, the schema-31 caption, the
// dimmed Profile width and the missing-texture state), the Style library (refresh bounds, search,
// rows, Apply, Save with a taken name), File > Import style… and File > Export style… with their
// refusals (§18.6), and the File menu's order.
#include "app/organization_panel.hpp"
#include "app/surface_format.hpp"
#include "app/style_dialogs.hpp"
#include "app/styles_panel.hpp"
#include "app/window.hpp"
#include "core/assets.hpp"
#include "io/assets.hpp"
#include "io/model_style_io.hpp"
#include "io/style_bundle.hpp"
#include "io/texture_image.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSurfaceFormat>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>

using namespace sketchy;

namespace {

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

QByteArray texturePng() {
    return encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 255}));
}

void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write a fixture");
    file.close();
}

QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read a file");
    return file.readAll();
}

// A style with an edge stroke, whose texture is `texture` (zero for none).
ModelStyle strokeStyle(double width, double opacity, EdgeDash dash, Id texture = 0) {
    ModelStyle style;
    style.edgeStroke = EdgeStroke{width, opacity, dash, texture};
    return style;
}

// A library file: a textured style (Present), or a plain one (None).
QByteArray texturedFile(const QString &name, const ModelStyle &style, Id id) {
    AssetRecord texture{id, "Weave", "image/png", assetPayload(texturePng())};
    return encodeStyleBundle({{name, "", {}}, style, texture, StyleTextureStatus::Present});
}

QByteArray plainFile(const QString &name, const ModelStyle &style) {
    return encodeStyleBundle({{name, "", {}}, style, {}, StyleTextureStatus::None});
}

// A `.sketchylib`-envelope file built by hand, so a damaged or foreign file can be made: the
// header (magic, manifest length, payload length, zero thumbnail length), the manifest, the payload.
QByteArray envelope(const QJsonObject &manifest, const QByteArray &payload) {
    const auto text = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QByteArray bytes(24, '\0');
    std::copy_n("SKYLIB\0\1", 8, bytes.begin());
    qToLittleEndian<quint32>(quint32(text.size()), bytes.data() + 8);
    qToLittleEndian<quint64>(quint64(payload.size()), bytes.data() + 12);
    return bytes + text + payload;
}

// A style manifest with the given style record and texture descriptor (QJsonValue::Null for none).
QJsonObject manifest(const QString &kind, const QString &name, const QJsonObject &style,
                     const QJsonValue &texture) {
    return {{"bundleVersion", 1},
            {"kind", kind},
            {"name", name},
            {"description", ""},
            {"labels", QJsonArray{}},
            {"style", style},
            {"texture", texture}};
}

QJsonObject textureDescriptor(Id id, bool missing, const QByteArray &payload,
                              const QString &sha256) {
    return {{"id", QString::number(id)},
            {"name", "Weave"},
            {"mediaType", "image/png"},
            {"missing", missing},
            {"bytes", missing ? 0 : double(payload.size())},
            {"sha256", missing ? QJsonValue::Null : QJsonValue(sha256)}};
}

QString sha256Of(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// The label of the newest applied edit (history lists the oldest first).
std::string lastLabel(const Document &doc) {
    const auto position = doc.history().position;
    return doc.history(position - 1, 1).entries.front().label;
}

// Runs a blocking action (a dialog's exec) while `step` is called every 10 ms. `step` returns true
// when the dialogs it drives are done. A check that fails inside `step` is kept and rethrown once the
// action returns, since Qt does not carry an exception out of a timer. A watchdog closes whatever is
// open after 15 s, so a dialog that never appears fails the test instead of hanging the run.
void drive(const std::function<void()> &action, const std::function<bool()> &step) {
    QTimer timer;
    timer.setInterval(10);
    QElapsedTimer clock;
    clock.start();
    std::string failure;
    bool timedOut = false;
    auto closeOpen = [] {
        while (auto *modal = QApplication::activeModalWidget())
            QTest::keyClick(modal, Qt::Key_Escape);
    };
    QObject::connect(&timer, &QTimer::timeout, [&] {
        try {
            if (step()) {
                timer.stop();
                return;
            }
        } catch (const std::exception &error) {
            failure = error.what();
            timer.stop();
            closeOpen();
            return;
        }
        if (clock.elapsed() > 15000) {
            timer.stop();
            timedOut = true;
            closeOpen();
        }
    });
    timer.start();
    action();
    timer.stop();
    if (!failure.empty())
        throw std::runtime_error(failure);
    if (timedOut) {
        // Name the windows that were open, so a stalled flow shows where it stopped.
        QStringList open;
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->isVisible())
                open << widget->objectName() + "/" + widget->windowTitle();
        throw std::runtime_error(("A dialog the test drives did not open; open: " +
                                  open.join(", ")).toStdString());
    }
}

// A click that runs after the current event, so a file dialog the button opens is driven by the
// timer and is not nested inside the timer's own callback.
void queueClick(QAbstractButton *button) {
    QMetaObject::invokeMethod(button, "click", Qt::QueuedConnection);
}

// Accepts a Qt file chooser on `path`, as a person would type the path. Once per chooser: it is
// marked handled, so a later tick does not accept it again.
void choose(QFileDialog &chooser, const QString &path) {
    if (chooser.property("handled").toBool())
        return;
    chooser.setProperty("handled", true);
    auto *name = chooser.findChild<QLineEdit *>("fileNameEdit");
    check(name, "The file chooser has a file name field");
    name->setText(path);
    QMetaObject::invokeMethod(&chooser, "accept", Qt::DirectConnection);
}

// Sets the model's style as the Styles tab does, so the tab refreshes (its Viewport::changed).
void setModelStyle(Window &window, const ModelStyle &style) {
    window.viewport()->applyModelStyle(style);
}

// Shows the Styles tab and returns its panel.
QWidget &stylesTab(Window &window) {
    window.findChild<QAction *>("view.styles")->trigger();
    auto *panel = window.findChild<QWidget *>("stylesPanel");
    check(panel, "The Styles tab exists");
    return *panel;
}

template <class T> T *field(QWidget &root, const char *name) {
    auto *widget = root.findChild<T *>(name);
    check(widget, name);
    return widget;
}

QString statusText(Window &window) { return window.findChild<QLabel *>("statusMessage")->text(); }

// The Styles tab's library rows, by name, found by their first line.
QListWidgetItem *row(QListWidget &list, const QString &name) {
    for (int i = 0; i < list.count(); ++i)
        if (list.item(i)->text().startsWith(name + "\n"))
            return list.item(i);
    throw std::runtime_error("A library row is listed");
}

// Each Edge stroke control is one labelled undo step, and undo returns the record before it.
void edgeStrokeControls() {
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    auto &panel = stylesTab(window);
    auto *stroke = field<QCheckBox>(panel, "styleEdgeStroke");
    auto *width = field<QLineEdit>(panel, "styleEdgeWidth");
    auto *opacity = field<QLineEdit>(panel, "styleEdgeOpacity");
    auto *dash = field<QComboBox>(panel, "styleEdgeDash");
    auto *texture = field<QComboBox>(panel, "styleEdgeTexture");
    check(!stroke->isChecked() && !width->isEnabled() && !opacity->isEnabled() &&
              !dash->isEnabled() && !texture->isEnabled(),
          "With the stroke off, its fields are off");
    const auto asset = createAsset(doc, "Weave", "image/png", assetPayload(texturePng()));

    // Turning the stroke on copies the profile width (2 px by default) and is one labelled step.
    auto total = doc.history().total;
    stroke->click();
    check(doc.style().edgeStroke && doc.style().edgeStroke->width == doc.style().profileWidth,
          "Turning the stroke on copies the profile width");
    check(doc.history().total == total + 1 && lastLabel(doc) == "Turn on edge stroke",
          "Turn on edge stroke is one labelled edit");
    check(width->isEnabled() && width->text() == "2", "The width shows the stroke's width");

    // Width: Enter commits one labelled edit.
    total = doc.history().total;
    width->selectAll();
    QTest::keyClicks(width, "2.5");
    QTest::keyClick(width, Qt::Key_Return);
    check(doc.style().edgeStroke->width == 2.5 && doc.history().total == total + 1 &&
              lastLabel(doc) == "Set edge width 2.5 px",
          "Width is one labelled edit");

    // Opacity is typed as a percentage and stored as a fraction.
    total = doc.history().total;
    opacity->selectAll();
    QTest::keyClicks(opacity, "60");
    QTest::keyClick(opacity, Qt::Key_Return);
    check(std::abs(doc.style().edgeStroke->opacity - 0.6) < 1e-12 &&
              lastLabel(doc) == "Set edge opacity 60 %" && doc.history().total == total + 1,
          "Opacity is one labelled edit in percent");

    // Dash: the activated choice is one labelled edit.
    total = doc.history().total;
    dash->setCurrentIndex(1);
    emit dash->activated(1);
    check(doc.style().edgeStroke->dash == EdgeDash::Dashed &&
              lastLabel(doc) == "Set edge dash Dashed" && doc.history().total == total + 1,
          "Dash is one labelled edit");

    // Texture: choosing the model's PNG is one labelled edit; None clears it.
    total = doc.history().total;
    const auto index = texture->findData(QVariant::fromValue<qulonglong>(asset));
    check(index >= 0, "The model's PNG is listed as an edge texture");
    texture->setCurrentIndex(index);
    emit texture->activated(index);
    check(doc.style().edgeStroke->texture == asset &&
              lastLabel(doc) == "Set edge texture “Weave”" && doc.history().total == total + 1,
          "Texture is one labelled edit");
    const auto none = texture->findData(QVariant::fromValue<qulonglong>(0));
    texture->setCurrentIndex(none);
    emit texture->activated(none);
    check(!doc.style().edgeStroke->texture && lastLabel(doc) == "Clear edge texture",
          "None clears the texture in one labelled edit");

    // An invalid width is refused under the field, names the value, and changes nothing.
    total = doc.history().total;
    width->selectAll();
    QTest::keyClicks(width, "9");
    QTest::keyClick(width, Qt::Key_Return);
    auto *error = field<QLabel>(panel, "styleEdgeError");
    check(error->isVisible() && error->text().startsWith("Invalid: Edge width 9 px is outside 1 to 8"),
          "An invalid width is refused under its field");
    check(doc.history().total == total && doc.style().edgeStroke->width == 2.5,
          "A refused width changes nothing");

    // Esc restores the field to the document's value.
    width->selectAll();
    QTest::keyClicks(width, "7");
    QTest::keyClick(width, Qt::Key_Escape);
    check(width->text() == "2.5" && !error->isVisible(), "Esc restores the width");

    // Turning the stroke off removes it in one labelled edit, and undo restores it.
    total = doc.history().total;
    stroke->click();
    check(!doc.style().edgeStroke && lastLabel(doc) == "Turn off edge stroke" &&
              doc.history().total == total + 1,
          "Turn off edge stroke removes the stroke in one edit");
    doc.undo();
    check(doc.style().edgeStroke && doc.style().edgeStroke->width == 2.5,
          "Undo restores the stroke that was turned off");
}

// Colors and details dims Profile width while the stroke is on (UX_DESIGN §18.3, gap 2).
void profileWidthDimmed() {
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    setModelStyle(window, strokeStyle(3, 1, EdgeDash::Solid));
    auto &panel = stylesTab(window);
    bool checked = false;
    drive([&] { QTest::mouseClick(field<QPushButton>(panel, "styleCustomize"), Qt::LeftButton); },
          [&] {
              auto *dialog = window.findChild<QDialog *>("styleDialog");
              if (!dialog || !dialog->isVisible())
                  return false;
              check(!field<QLineEdit>(*dialog, "styleProfileWidth")->isEnabled(),
                    "Profile width is dimmed while the stroke is on");
              check(field<QLabel>(*dialog, "styleProfileWidthNote")->text() ==
                        "Set by the edge stroke",
                    "Profile width says it is set by the edge stroke");
              checked = true;
              dialog->reject();
              return true;
          });
    check(checked, "The Colors and details dialog was checked");
    setModelStyle(window, ModelStyle{});
    drive([&] { QTest::mouseClick(field<QPushButton>(panel, "styleCustomize"), Qt::LeftButton); },
          [&] {
              auto *dialog = window.findChild<QDialog *>("styleDialog");
              if (!dialog || !dialog->isVisible())
                  return false;
              check(field<QLineEdit>(*dialog, "styleProfileWidth")->isEnabled(),
                    "Profile width is editable while the stroke is off");
              dialog->reject();
              return true;
          });
}

// A texture the model does not hold, or holds without bytes, draws as a missing state: the field,
// the caption and the tooltip (which is the accessible description) all say so.
void missingTextureState() {
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    const auto lost = createAsset(doc, "Edge texture 2", "image/png");
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Dashed, lost));
    auto &panel = stylesTab(window);
    auto *texture = field<QComboBox>(panel, "styleEdgeTexture");
    const auto index = texture->findData(QVariant::fromValue<qulonglong>(lost));
    check(index >= 0 && texture->itemText(index).startsWith("⚠ Edge texture 2 · Missing"),
          "The missing texture is listed with its warning word");
    check(!texture->model()->index(index, 0).flags().testFlag(Qt::ItemIsEnabled),
          "A missing texture cannot be chosen");
    auto *note = field<QLabel>(panel, "styleEdgeTextureNote");
    check(note->isVisible() && note->text().startsWith("Missing:"),
          "The missing caption says how the edges draw");
    const auto tip = QString("Edge texture 2 is missing from this model (asset %1). Edges that use "
                             "it draw hatched until it is restored. No other texture is used.")
                         .arg(lost);
    check(texture->toolTip() == tip && texture->accessibleDescription() == tip,
          "The missing tooltip is the accessible description");
    check(window.findChild<QPushButton *>("edgeTextureChip")->isVisible(),
          "The status chip shows the missing texture");
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Dashed));
    check(!note->isVisible() && texture->toolTip().isEmpty(), "No stroke, no missing state");
}

// The Style library: rows show the states of §18.2, search matches names and descriptions, Apply
// applies one labelled step and says so, and a file that cannot be read cannot be selected.
void libraryStates() {
    QTemporaryDir folder;
    QSettings("SketchyUp", "SketchyUp").remove("styleFolder");
    check(folder.isValid(), "A temporary folder");
    setStyleLibraryPreference(folder.path());
    write(folder.filePath("Style A.sketchystyle"),
          texturedFile("Style A", strokeStyle(2, .6, EdgeDash::Dashed, 4), 4));
    write(folder.filePath("Style B.sketchystyle"), plainFile("Style B", ModelStyle{}));
    write(folder.filePath("Style C.sketchystyle"),
          envelope(manifest("style", "Style C",
                            encodeModelStyle(strokeStyle(3, 1, EdgeDash::DashDot, 4)),
                            textureDescriptor(4, true, {}, {})),
                   {}));
    write(folder.filePath("Broken.sketchystyle"), QByteArray("garbage"));
    write(folder.filePath("Notes.txt"), QByteArray("not a style"));

    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    auto &panel = stylesTab(window);
    auto *list = field<QListWidget>(panel, "styleLibrary");
    auto *apply = field<QPushButton>(panel, "styleApplyLibrary");
    auto *count = window.findChild<QLabel *>("styleLibraryCount");
    check(list->count() == 4, "The folder lists its style files and the one it cannot read");
    check(row(*list, "Style A")->text().contains("2.0 px · 60 % · Dashed · texture stored · ✓ Stored"),
          "A stored style shows its settings in words and the ✓ Stored state");
    check(row(*list, "Style C")->text().contains("⚠ Texture missing from file · cannot apply"),
          "A missing texture is listed with its warning and cannot be applied");
    check(row(*list, "Broken")->text().contains("✕ Cannot read:") &&
              !row(*list, "Broken")->flags().testFlag(Qt::ItemIsSelectable),
          "A file that cannot be read is listed with its reason and cannot be selected");
    check(window.findChild<QLabel *>("styleLibraryNotice")->text().contains("Skipped 1 file that is not a style file"),
          "The scan reports the file it skipped and why");
    check(count->text() == "4 styles", "The count names the listed styles");

    // Search matches names and descriptions.
    auto *search = field<QLineEdit>(panel, "styleSearch");
    search->setText("style b");
    check(list->count() == 1 && list->item(0)->text().startsWith("Style B"),
          "Search narrows the rows to the match");
    search->clear();
    check(list->count() == 4, "Clearing the search lists every style");

    // A missing texture cannot be applied, and nothing changes.
    list->setCurrentItem(row(*list, "Style C"));
    check(!apply->isEnabled(), "Apply is off for a style whose texture is missing");
    const auto history = doc.history().total;
    list->setCurrentItem(row(*list, "Style A"));
    check(apply->isEnabled(), "Apply is on for a stored style");
    apply->click();
    const auto applied = doc.style();
    check(applied.edgeStroke && applied.edgeStroke->dash == EdgeDash::Dashed &&
              std::abs(applied.edgeStroke->opacity - .6) < 1e-12,
          "Apply sets the stored style on the model");
    check(doc.history().total == history + 1 && lastLabel(doc) == "Apply style “Style A”",
          "Apply is one labelled step");
    check(statusText(window).startsWith("Applied “Style A”: edge stroke on, "),
          "Apply says what changed in the status line");
    check(doc.assets().size() == 1, "Apply adds the style's texture to the model");

    // Applying the same style again is the neutral result, with no edit.
    const auto again = doc.history().total;
    apply->click();
    check(doc.history().total == again && statusText(window) ==
              "This model already has this style. Nothing changed.",
          "A style equal to the model's style is a neutral result");

    // Enter applies the selected row (keyboard reachability).
    list->setCurrentItem(row(*list, "Style B"));
    QTest::keyClick(list, Qt::Key_Return);
    check(doc.style() == ModelStyle{} && lastLabel(doc) == "Apply style “Style B”",
          "Enter on a row applies it");

    // Refresh re-reads the folder; a file added since then appears.
    write(folder.filePath("Style D.sketchystyle"), plainFile("Style D", ModelStyle{}));
    field<QPushButton>(panel, "styleRefresh")->click();
    check(list->count() == 5 && count->text() == "5 styles", "Refresh lists the new style");
}

// Refresh stays inside the ADR 0132 bounds: a style past the 128 listed is skipped and counted.
void libraryBounds() {
    QTemporaryDir folder;
    setStyleLibraryPreference(folder.path());
    for (int i = 0; i < int(styleScanCandidateLimit) + 2; ++i)
        write(folder.filePath(QString("Style %1.sketchystyle").arg(i, 3, 10, QChar('0'))),
              plainFile(QString("Style %1").arg(i, 3, 10, QChar('0')), ModelStyle{}));
    const auto library = scanStyleDirectory(folder.path());
    check(library.entries.size() == styleScanCandidateLimit && library.skippedPastCandidates == 2,
          "A folder lists at most 128 styles and counts the rest");
    check(library.notices.join("\n").contains("Skipped 2 styles past the 128 listed"),
          "The notice names how many styles were skipped and why");
}

// Save to library refuses a name that is taken, suggests a free one, and never replaces a file.
void saveToLibrary() {
    QTemporaryDir folder;
    setStyleLibraryPreference(folder.path());
    write(folder.filePath("Style B.sketchystyle"), plainFile("Style B", ModelStyle{}));
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    setModelStyle(window, strokeStyle(2, .5, EdgeDash::Dotted));
    auto &panel = stylesTab(window);
    const auto original = read(folder.filePath("Style B.sketchystyle"));
    bool sawTaken = false, saved = false;
    drive([&] { field<QPushButton>(panel, "styleSaveLibrary")->click(); },
          [&] {
              auto *dialog = window.findChild<QDialog *>("saveStyleDialog");
              if (!dialog || !dialog->isVisible())
                  return saved;
              auto *name = field<QLineEdit>(*dialog, "libraryStyleName");
              auto *save = field<QPushButton>(*dialog, "libraryStyleSave");
              if (!sawTaken) {
                  name->setText("Style B");
                  check(!save->isEnabled() &&
                            field<QLabel>(*dialog, "libraryStyleError")->text().startsWith("✕ A style named “Style B” is already"),
                        "A taken name is refused in the dialog");
                  check(field<QLabel>(*dialog, "libraryStyleFile")->text().startsWith("Style B 2.sketchystyle"),
                        "The dialog suggests a free name");
                  sawTaken = true;
                  name->setText("Style D");
                  check(save->isEnabled(), "A free name can be saved");
                  save->click();
                  saved = true;
                  return false;
              }
              return false;
          });
    check(sawTaken && saved, "The save dialog was driven");
    check(QFileInfo::exists(folder.filePath("Style D.sketchystyle")),
          "The new style file is written to the library");
    check(read(folder.filePath("Style B.sketchystyle")) == original,
          "The taken file is never replaced");
    check(decodeStyleBundle(read(folder.filePath("Style D.sketchystyle"))).style.edgeStroke->dash ==
              EdgeDash::Dotted,
          "The saved file carries the model's stroke");
}

// File > Import style…: the file's facts, Apply (one labelled step, then the same-style result),
// and Save to the library (a new file only).
void importApplyAndSave() {
    QTemporaryDir files;
    QTemporaryDir folder;
    setStyleLibraryPreference(folder.path());
    const auto source = files.filePath("Import A.sketchystyle");
    write(source, texturedFile("Imported A", strokeStyle(3, .4, EdgeDash::Solid, 9), 9));
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    auto *importAction = window.findChild<QAction *>("file.importStyle");
    check(importAction && importAction->shortcut().isEmpty(), "Import style has no shortcut");

    // Apply to this model is the default outcome.
    bool checkedFacts = false;
    drive([&] { importAction->trigger(); }, [&] {
        if (auto *chooser = window.findChild<QFileDialog *>("styleImportFileDialog");
            chooser && chooser->isVisible()) {
            choose(*chooser, source);
            return false;
        }
        auto *dialog = window.findChild<QDialog *>("importStyleDialog");
        if (!dialog || !dialog->isVisible())
            return false;
        check(field<QRadioButton>(*dialog, "importApply")->isChecked(), "Apply is the default outcome");
        check(field<QLabel>(*dialog, "importStroke")->text() == "3.0 px · 40 % · Solid",
              "The file's stroke is shown in words");
        check(field<QLabel>(*dialog, "importTexture")->text().contains("stored in the file"),
              "The texture is stored in the file");
        checkedFacts = true;
        field<QPushButton>(*dialog, "importPrimary")->click();
        return true;
    });
    check(checkedFacts, "The Import style dialog was checked");
    check(doc.style().edgeStroke && doc.style().edgeStroke->dash == EdgeDash::Solid &&
              std::abs(doc.style().edgeStroke->opacity - .4) < 1e-12,
          "Apply sets the imported style on the model");
    check(lastLabel(doc) == "Apply style “Imported A”", "Import apply is labelled");
    check(statusText(window).startsWith("Applied “Imported A”: edge stroke on, "),
          "Import apply says what changed");

    // The same style again is the neutral result and changes nothing.
    const auto total = doc.history().total;
    drive([&] { importAction->trigger(); }, [&] {
        if (auto *chooser = window.findChild<QFileDialog *>("styleImportFileDialog");
            chooser && chooser->isVisible()) {
            choose(*chooser, source);
            return false;
        }
        auto *dialog = window.findChild<QDialog *>("importStyleDialog");
        if (!dialog || !dialog->isVisible())
            return false;
        field<QPushButton>(*dialog, "importPrimary")->click();
        return true;
    });
    check(doc.history().total == total && statusText(window) ==
              "This model already has this style. Nothing changed.",
          "An import of the same style is the neutral result");

    // Save to the style library: a new file, and the name rule applies.
    bool saved = false;
    drive([&] { importAction->trigger(); }, [&] {
        if (auto *chooser = window.findChild<QFileDialog *>("styleImportFileDialog");
            chooser && chooser->isVisible()) {
            choose(*chooser, source);
            return false;
        }
        auto *dialog = window.findChild<QDialog *>("importStyleDialog");
        if (!dialog || !dialog->isVisible())
            return false;
        field<QRadioButton>(*dialog, "importSave")->click();
        auto *name = field<QLineEdit>(*dialog, "importName");
        check(name->isEnabled() && field<QPushButton>(*dialog, "importPrimary")->text() == "Save to library",
              "Save names the outcome on the primary button");
        name->setText("Kept A");
        field<QPushButton>(*dialog, "importPrimary")->click();
        saved = true;
        return true;
    });
    check(saved && QFileInfo::exists(folder.filePath("Kept A.sketchystyle")),
          "Save to the library writes a new file");
    check(doc.history().total == total, "Saving to the library does not change the model");
    check(read(folder.filePath("Kept A.sketchystyle")) == read(source),
          "The library file is the file as read, byte for byte");
}

// The §18.6 refusals of Import style: each names what did not happen and changes nothing.
void importRefusals() {
    QTemporaryDir files;
    QTemporaryDir folder;
    setStyleLibraryPreference(folder.path());
    const auto good = files.filePath("Good.sketchystyle");
    write(good, plainFile("Good", strokeStyle(4, 1, EdgeDash::Dotted)));
    const auto bad = files.filePath("Bad.sketchystyle");
    write(bad, QByteArray("not a style file"));
    const auto missing = files.filePath("Missing.sketchystyle");
    write(missing, envelope(manifest("style", "Missing", encodeModelStyle(strokeStyle(2, 1, EdgeDash::Solid, 4)),
                                     textureDescriptor(4, true, {}, {})),
                            {}));
    const auto hash = files.filePath("Hash.sketchystyle");
    write(hash, envelope(manifest("style", "Hash", encodeModelStyle(strokeStyle(2, 1, EdgeDash::Solid, 4)),
                                  textureDescriptor(4, false, texturePng(), QString(64, '0'))),
                         texturePng()));
    const auto gif = files.filePath("Gif.sketchystyle");
    write(gif, envelope(manifest("style", "Gif", encodeModelStyle(strokeStyle(2, 1, EdgeDash::Solid, 4)),
                                 QJsonObject{{"id", "4"}, {"name", "Weave"}, {"mediaType", "image/gif"},
                                             {"missing", false}, {"bytes", 3},
                                             {"sha256", sha256Of(QByteArray("GIF"))}}),
                        QByteArray("GIF")));
    const auto catalog = files.filePath("Catalog.sketchystyle");
    write(catalog, envelope(manifest("catalog", "Catalog", encodeModelStyle(ModelStyle{}), QJsonValue::Null),
                            {}));
    const auto wide = files.filePath("Wide.sketchystyle");
    {
        // Built by hand: encodeModelStyle refuses a width of 9, which is what the file must hold.
        auto style = encodeModelStyle(strokeStyle(2, 1, EdgeDash::Solid));
        auto stroke = style["edgeStroke"].toObject();
        stroke["width"] = 9;
        style["edgeStroke"] = stroke;
        write(wide, envelope(manifest("style", "Wide", style, QJsonValue::Null), {}));
    }
    const auto huge = files.filePath("Huge.sketchystyle");
    {
        QByteArray header(24, '\0');
        std::copy_n("SKYLIB\0\1", 8, header.begin());
        qToLittleEndian<quint32>(2, header.data() + 8);
        qToLittleEndian<quint64>(quint64(17) * 1024 * 1024, header.data() + 12);
        write(huge, header + QByteArray("{}"));
    }

    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    const auto history = doc.history().total;
    auto *importAction = window.findChild<QAction *>("file.importStyle");

    auto refuse = [&](const QString &path, const char *title, const char *reason) {
        bool seen = false;
        drive([&] { importAction->trigger(); }, [&] {
            if (auto *chooser = window.findChild<QFileDialog *>("styleImportFileDialog");
                chooser && chooser->isVisible()) {
                choose(*chooser, path);
                return false;
            }
            auto *dialog = window.findChild<QDialog *>("importStyleRefused");
            if (!dialog || !dialog->isVisible())
                return false;
            check(field<QLabel>(*dialog, "styleRefusalReason")->text().contains(reason),
                  "The refusal names its reason");
            check(dialog->findChild<QLabel *>("styleRefusalTitle")->text() == title,
                  "The refusal has its title");
            seen = true;
            field<QPushButton>(*dialog, "importClose")->click();
            return true;
        });
        check(seen, title);
        check(doc.history().total == history, "A refusal changes nothing");
    };
    refuse(missing, "The edge texture is not in the file.", "has no bytes");
    refuse(hash, "The edge texture does not match its checksum.", "may be damaged");
    refuse(gif, "Import style refused", "not an image SketchyUp can read");
    refuse(catalog, "This file is a catalog, not a style.", "not imported as a style");
    refuse(wide, "Import style refused", "outside 1 to 8 px");
    refuse(huge, "Import refused: the texture is too large", "17.0 MiB");
    refuse(bad, "Import style refused", "not a style SketchyUp can read");
    check(folder.isValid() && QDir(folder.path()).entryList({"*.sketchystyle"}).isEmpty(),
          "Refusals write nothing to the library");

    // Choose another file reopens the chooser, and the next file is applied.
    bool reopened = false, applied = false;
    drive([&] { importAction->trigger(); }, [&] {
        if (auto *chooser = window.findChild<QFileDialog *>("styleImportFileDialog");
            chooser && chooser->isVisible()) {
            choose(*chooser, reopened ? good : bad);
            reopened = true;
            return false;
        }
        auto *dialog = window.findChild<QDialog *>("importStyleRefused");
        if (dialog && dialog->isVisible()) {
            field<QPushButton>(*dialog, "importChooseAnother")->click();
            return false;
        }
        auto *import = window.findChild<QDialog *>("importStyleDialog");
        if (import && import->isVisible()) {
            applied = true;
            field<QPushButton>(*import, "importPrimary")->click();
            return true;
        }
        return false;
    });
    check(reopened && applied, "Choose another file reopens the chooser and applies the next file");
    check(lastLabel(doc) == "Apply style “Good”", "The chosen file is applied in one labelled step");
}

// File > Export style…: an existing name is refused and the file is left alone, a new name writes
// the style, and a missing edge texture is named before any file dialog opens.
void exportStyle() {
    QTemporaryDir folder;
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Dashed));
    auto *exportAction = window.findChild<QAction *>("file.exportStyle");
    check(exportAction, "Export style is a File entry");
    const auto existing = folder.filePath("Exists.sketchystyle");
    write(existing, QByteArray("keep me"));

    // Each button press is queued, so the file dialog it opens is driven by the timer on the next
    // tick (see queueClick). The stages run in order, one per tick.
    int stage = 0;
    bool refused = false;
    drive([&] { exportAction->trigger(); }, [&] {
        auto *dialog = window.findChild<QDialog *>("exportStyleDialog");
        auto *chooser = window.findChild<QFileDialog *>("exportStyleFileDialog");
        if (stage == 0 && dialog && dialog->isVisible()) {
            field<QLineEdit>(*dialog, "exportName")->setText("Exported");
            queueClick(field<QPushButton>(*dialog, "exportPrimary"));
            stage = 1;
        } else if (stage == 1 && chooser && chooser->isVisible()) {
            choose(*chooser, existing);
            stage = 2;
        } else if (stage == 2 && dialog && dialog->isVisible()) {
            refused = field<QLabel>(*dialog, "exportError")
                          ->text()
                          .startsWith("✕ That file already exists. An existing file is never replaced");
            queueClick(field<QPushButton>(*dialog, "exportCancel"));
            stage = 3;
        }
        return false;
    });
    check(refused && stage == 3, "An existing name is refused in the dialog");
    check(read(existing) == QByteArray("keep me"), "An existing file is never replaced");

    // A new name writes the style, and the file reads back with the model's stroke.
    const auto target = folder.filePath("New style.sketchystyle");
    stage = 0;
    drive([&] { exportAction->trigger(); }, [&] {
        auto *dialog = window.findChild<QDialog *>("exportStyleDialog");
        auto *chooser = window.findChild<QFileDialog *>("exportStyleFileDialog");
        if (stage == 0 && dialog && dialog->isVisible()) {
            queueClick(field<QPushButton>(*dialog, "exportPrimary"));
            stage = 1;
        } else if (stage == 1 && chooser && chooser->isVisible()) {
            choose(*chooser, target);
            stage = 2;
        }
        return false;
    });
    check(QFileInfo::exists(target), "A new name writes the style file");
    check(decodeStyleBundle(read(target)).style.edgeStroke->dash == EdgeDash::Dashed,
          "The exported file carries the model's stroke");

    // A missing edge texture is named when the dialog opens, and no file dialog or file follows.
    const auto lost = createAsset(doc, "Lost", "image/png");
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Solid, lost));
    const auto refusedTarget = folder.filePath("Refused.sketchystyle");
    bool namedAtOpen = false, chooserOpened = false;
    stage = 0;
    drive([&] { exportAction->trigger(); }, [&] {
        auto *dialog = window.findChild<QDialog *>("exportStyleDialog");
        if (auto *chooser = window.findChild<QFileDialog *>("exportStyleFileDialog");
            chooser && chooser->isVisible())
            chooserOpened = true;
        if (stage == 0 && dialog && dialog->isVisible()) {
            namedAtOpen = field<QLabel>(*dialog, "exportError")->text() ==
                          "✕ Export refused: “Lost” (asset " + QString::number(lost) +
                              ") is missing from this model. Nothing was written.";
            queueClick(field<QPushButton>(*dialog, "exportPrimary"));
            stage = 1;
        } else if (stage == 1 && dialog && dialog->isVisible()) {
            queueClick(field<QPushButton>(*dialog, "exportCancel"));
            stage = 2;
        }
        return false;
    });
    check(namedAtOpen, "The missing texture is named in the dialog");
    check(!chooserOpened && !QFileInfo::exists(refusedTarget),
          "A missing texture stops the export before any file dialog, and writes nothing");
}

// The File menu holds the two entries after their siblings, and Ctrl+K lists them by name.
void fileMenu() {
    Window window;
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    QStringList names;
    QMenu *file = nullptr;
    for (auto *action : window.menuBar()->actions())
        if (action->menu() && action->text().remove('&') == "File")
            file = action->menu();
    check(file, "The File menu exists");
    for (auto *action : file->actions())
        if (!action->isSeparator())
            names << action->text();
    const auto importTerrain = names.indexOf("Import terrain…");
    const auto exportStep = names.indexOf("Export STEP…");
    check(importTerrain >= 0 && names.value(importTerrain + 1) == "Import style…",
          "Import style… follows Import terrain…");
    check(exportStep >= 0 && names.value(exportStep + 1) == "Export style…",
          "Export style… follows Export STEP…");
    for (const auto *action : file->actions())
        if (action->text() == "Import style…" || action->text() == "Export style…")
            check(action->shortcut().isEmpty(), "The style entries have no default shortcut");
}

// The Styles tab's new controls have accessible names, and they stay reachable and legible at 200 %
// text: the application font is doubled, and every row and label still has its size.
void accessibleAndLargeText() {
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Dashed));
    auto &panel = stylesTab(window);
    for (const char *name : {"styleEdgeStroke", "styleEdgeWidth", "styleEdgeOpacity", "styleEdgeDash",
                             "styleEdgeTexture", "styleLibraryFolder", "styleSearch", "styleLibrary",
                             "styleChooseFolder", "styleRefresh", "styleApplyLibrary",
                             "styleSaveLibrary"}) {
        auto *widget = field<QWidget>(panel, name);
        check(!widget->accessibleName().isEmpty(), name);
    }
    QFont large = QApplication::font();
    large.setPointSizeF(large.pointSizeF() * 2);
    const auto normal = QApplication::font();
    QApplication::setFont(large);
    auto *library = field<QListWidget>(panel, "styleLibrary");
    check(library->isVisible() && library->height() > 0, "The library list is shown at 200 % text");
    check(field<QPushButton>(panel, "styleApplyLibrary")->sizeHint().width() > 0 &&
              field<QPushButton>(panel, "styleSaveLibrary")->sizeHint().width() > 0,
          "The library buttons keep their size at 200 % text");
    QApplication::setFont(normal);
}

// The §18.7 keyboard order of the Styles tab: from the stroke's check box, Tab reaches each control
// in turn, the library list, then Choose folder, Refresh, Apply and Save. Each is reached, in order,
// by moving the focus chain forward.
void tabOrder() {
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Dashed));
    auto &panel = stylesTab(window);
    const QStringList expected = {"styleEdgeStroke", "styleEdgeWidth", "styleEdgeOpacity",
                                  "styleEdgeDash", "styleEdgeTexture", "styleSearch", "styleLibrary",
                                  "styleChooseFolder", "styleRefresh", "styleApplyLibrary",
                                  "styleSaveLibrary"};
    QWidget *widget = field<QWidget>(panel, "styleEdgeStroke");
    QStringList reached;
    for (int step = 0; step < 400 && widget && reached.size() < expected.size(); ++step) {
        if (!widget->objectName().isEmpty() && expected.contains(widget->objectName()) &&
            (reached.isEmpty() || reached.last() != widget->objectName()))
            reached << widget->objectName();
        widget = widget->nextInFocusChain();
    }
    check(reached == expected, "The Styles tab's controls are reached in the §18.7 order");
}

} // namespace

int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    struct Test {
        const char *name;
        void (*run)();
    };
    const Test tests[] = {{"edge stroke controls", edgeStrokeControls},
                          {"profile width dimmed", profileWidthDimmed},
                          {"missing texture state", missingTextureState},
                          {"library states", libraryStates},
                          {"library bounds", libraryBounds},
                          {"save to library", saveToLibrary},
                          {"import apply and save", importApplyAndSave},
                          {"import refusals", importRefusals},
                          {"export style", exportStyle},
                          {"file menu", fileMenu},
                          {"accessible names and large text", accessibleAndLargeText},
                          {"tab order", tabOrder}};
    int failures = 0;
    for (const auto &test : tests) {
        // An optional argument names one test, for a quicker run.
        if (argc > 1 && std::string(argv[1]) != test.name)
            continue;
        try {
            test.run();
            std::cout << "ok: " << test.name << '\n';
        } catch (const std::exception &error) {
            ++failures;
            std::cerr << "FAILED: " << test.name << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
