// R107.e2 (UX_DESIGN §22): the user guide viewer, offscreen. It reads the repository's copy of
// USER_GUIDE.md, so the section the GeoTIFF page asks for is the real one. The checks: the guide
// opens scrolled to the section with the cursor on its heading; a missing or unreadable file shows
// one line and Close; a section the guide does not have opens at the top with a line that says so;
// every section the application opens resolves; the text is read only, links are not followed, Tab
// leaves the text for Close, and Esc and Enter close the window; the heading slug follows GitHub's
// rule and only headings count.
#include "app/user_guide_viewer.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCursor>

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace sketchy;

namespace {

void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}

// The repository's copy of the guide, as the tests read it.
QString repositoryGuide() {
    return QString::fromStdString(std::string(SOURCE_DIR) + "/docs/USER_GUIDE.md");
}

QPushButton *closeButton(UserGuideViewer &viewer) {
    auto *close = viewer.findChild<QPushButton *>("userGuideClose");
    check(close != nullptr, "The viewer has a Close button");
    return close;
}

// Shows the window and waits until it is exposed and active, so that layout and focus are real.
void showAndActivate(UserGuideViewer &viewer) {
    viewer.show();
    check(QTest::qWaitForWindowExposed(&viewer, 5000), "The viewer shows");
    viewer.activateWindow();
    QTest::qWaitFor([&] { return viewer.isActiveWindow(); }, 2000);
}

// The guide opens with the cursor on the section's heading, and the heading is at the top of the
// view (UX §22: "scrolled to its section ... at the top of the view").
void opensAtTheSection() {
    UserGuideViewer viewer(repositoryGuide(), kTerrainDownloadSection, nullptr);
    check(viewer.loaded() && viewer.sectionFound(),
          "The repository's guide opens and has the section");
    check(viewer.objectName() == "userGuideViewer" &&
              viewer.windowTitle() == "SketchyUp user guide" &&
              viewer.accessibleName() == "SketchyUp user guide",
          "The window is titled and named SketchyUp user guide");
    check(!viewer.isModal(), "The window is not modal");
    check(viewer.text() && viewer.text()->isReadOnly(), "The text is read only");
    check(viewer.text()->accessibleName() == "User guide, read only", "The text is named");
    check(closeButton(viewer)->accessibleName() == "Close the user guide", "Close is named");
    showAndActivate(viewer);
    check(!viewer.message()->isVisible() && viewer.text()->isVisible(),
          "A found section shows no line and the text");
    const QTextCursor cursor = viewer.text()->textCursor();
    check(cursor.block().text() == "Terrain from a downloaded elevation file" &&
              cursor.block().blockFormat().headingLevel() == 2,
          "The cursor is on the section's heading: " + cursor.block().text().toStdString());
    const int scroll = viewer.text()->verticalScrollBar()->value();
    check(scroll > 0, "The view is scrolled down to the section");
    // The heading's line is the first one in the view: its caret sits within the top two lines.
    const QRect caret = viewer.text()->cursorRect(cursor);
    const int line = viewer.text()->fontMetrics().lineSpacing();
    check(caret.top() >= 0 && caret.top() < 2 * line,
          "The heading is at the top of the view: caret top " + std::to_string(caret.top()) +
              ", line " + std::to_string(line));
}

// Every section the application opens is a heading of the installed guide.
void everyAppSectionResolves() {
    const std::vector<QString> sections{kTerrainDownloadSection};
    for (const auto &section : sections) {
        UserGuideViewer viewer(repositoryGuide(), section, nullptr);
        check(viewer.sectionFound(), "The guide has the section " + section.toStdString());
    }
}

// The slug is GitHub's: lower case, punctuation removed, spaces made hyphens; only headings count.
void slugAndHeadings() {
    QTemporaryDir dir;
    check(dir.isValid(), "A temporary folder");
    const QString path = dir.filePath("USER_GUIDE.md");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Text), "The test guide is written");
    file.write("# Title\n\n"
               "Terrain from a downloaded elevation file\n\n"
               "## Terrain: a *test*, with `code` & more!\n\n"
               "text\n\n"
               "## Résumé tables\n\n"
               "### Under_score-heading\n");
    file.close();

    UserGuideViewer heading(path, "terrain-a-test-with-code--more", nullptr);
    check(heading.loaded() && heading.sectionFound(),
          "A heading with markup and punctuation has GitHub's slug");
    UserGuideViewer accented(path, "résumé-tables", nullptr);
    check(accented.sectionFound(), "Letters outside ASCII are kept in the slug");
    UserGuideViewer underscore(path, "under_score-heading", nullptr);
    check(underscore.sectionFound(), "Underscores are kept in the slug");
    UserGuideViewer paragraph(path, "terrain-from-a-downloaded-elevation-file", nullptr);
    check(!paragraph.sectionFound(), "A paragraph with the same words is not a section");
    check(!paragraph.message()->isHidden() &&
              paragraph.message()->text().startsWith("This section"),
          "A section that is not there says so: " + paragraph.message()->text().toStdString());
}

// A file that cannot be read shows one line that names the file and its folder, and Close; the
// text is not shown; Enter on Close and Esc each close the window.
void missingGuideSaysSo() {
    QTemporaryDir dir;
    check(dir.isValid(), "A temporary folder");
    const QString path = dir.filePath("share/doc/sketchyup/USER_GUIDE.md");
    UserGuideViewer viewer(path, kTerrainDownloadSection, nullptr);
    check(!viewer.loaded() && !viewer.sectionFound(), "A missing guide is not loaded");
    showAndActivate(viewer);
    check(viewer.message()->isVisible() && !viewer.text()->isVisible(),
          "A missing guide shows the line, not the text");
    const QString line = viewer.message()->text();
    check(line.startsWith(
              "The user guide could not be shown. Its file, USER_GUIDE.md, should be in ") &&
              line.endsWith("Reinstall SketchyUp to restore it."),
          "The missing-guide line: " + line.toStdString());
    check(line.contains(QDir::toNativeSeparators(dir.filePath("share/doc/sketchyup"))),
          "The line names the folder the file should be in");
    QPushButton *close = closeButton(viewer);
    check(close->isVisible() && close->isEnabled(), "Close is shown and enabled");

    QPointer<UserGuideViewer> window = new UserGuideViewer(path, kTerrainDownloadSection, nullptr);
    window->show();
    check(QTest::qWaitForWindowExposed(window, 5000), "The second viewer shows");
    QTest::keyClick(closeButton(*window), Qt::Key_Return);
    check(QTest::qWaitFor([&] { return window.isNull(); }, 2000),
          "Enter on Close closes the window");

    window = new UserGuideViewer(path, kTerrainDownloadSection, nullptr);
    window->show();
    check(QTest::qWaitForWindowExposed(window, 5000), "The third viewer shows");
    QTest::keyClick(window, Qt::Key_Escape);
    check(QTest::qWaitFor([&] { return window.isNull(); }, 2000), "Esc closes the window");
}

// A section the guide does not have opens at the top, with the line that says so.
void missingSectionOpensAtTop() {
    UserGuideViewer viewer(repositoryGuide(), "no-such-section", nullptr);
    check(viewer.loaded() && !viewer.sectionFound(), "A guide with no such section is loaded");
    showAndActivate(viewer);
    check(viewer.message()->isVisible() &&
              viewer.message()->text() ==
                  "This section is not in the installed guide. It opens at the top.",
          "The section line: " + viewer.message()->text().toStdString());
    check(viewer.text()->verticalScrollBar()->value() == 0, "The view is at the top");
}

// Links are shown but not followed; Tab leaves the text for Close; the keys scroll the text.
void keysAndLinks() {
    UserGuideViewer viewer(repositoryGuide(), kTerrainDownloadSection, nullptr);
    check(!viewer.text()->openLinks() && !viewer.text()->openExternalLinks(),
          "Links are not followed");
    check(viewer.text()->tabChangesFocus(), "Tab leaves the text");
    showAndActivate(viewer);
    QTextBrowser *text = viewer.text();
    text->setFocus(Qt::TabFocusReason);
    QTest::qWaitFor([&] { return viewer.focusWidget() == text; }, 2000);
    check(viewer.focusWidget() == text, "The text takes focus");
    QTest::keyClick(text, Qt::Key_Tab);
    check(viewer.focusWidget() == closeButton(viewer), "Tab from the text reaches Close");

    text->setFocus(Qt::TabFocusReason);
    QTest::qWaitFor([&] { return viewer.focusWidget() == text; }, 2000);
    const int before = text->verticalScrollBar()->value();
    QTest::keyClick(text, Qt::Key_PageDown);
    const int afterPage = text->verticalScrollBar()->value();
    check(afterPage > before, "Page Down scrolls the text");
    QTest::keyClick(text, Qt::Key_End);
    check(text->verticalScrollBar()->value() == text->verticalScrollBar()->maximum(),
          "End scrolls to the end");
    QTest::keyClick(text, Qt::Key_Home);
    check(text->verticalScrollBar()->value() == 0, "Home scrolls to the top");
    check(text->isReadOnly() && !text->document()->toPlainText().isEmpty(),
          "The text is read only");
}

} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        opensAtTheSection();
        std::cout << "opens at the section\n";
        everyAppSectionResolves();
        std::cout << "every app section resolves\n";
        slugAndHeadings();
        std::cout << "heading slugs and headings\n";
        missingGuideSaysSo();
        std::cout << "missing guide\n";
        missingSectionOpensAtTop();
        std::cout << "section not in the guide\n";
        keysAndLinks();
        std::cout << "keys and links\n";
    } catch (const std::exception &error) {
        std::cerr << "user_guide_viewer_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "user guide viewer: ok\n";
    return 0;
}
