#pragma once
#include <QDialog>
#include <QString>

class QEvent;
class QLabel;
class QPushButton;
class QShowEvent;
class QTextBlock;
class QTextBrowser;

namespace sketchy {

// R107.e2 (UX_DESIGN §22): the installed user guide in a small read-only window, scrolled to one
// of its sections. It is a non-modal window that its import dialog owns, so closing the dialog
// closes it. The guide is read with QTextDocument::setMarkdown. A section is the first heading
// whose GitHub-style slug is the one asked for; the heading blocks are searched, so the window
// does not depend on named anchors the Markdown reader may not create (ADR 0181, R107.b2 section,
// "Finding the section").
//
// Closing it by Close, Esc or the close box deletes it, so a later open starts at the section
// again.
class UserGuideViewer final : public QDialog {
    Q_OBJECT

  public:
    // `section` is a heading's slug, for example "terrain-from-a-downloaded-elevation-file".
    UserGuideViewer(const QString &path, const QString &section, QWidget *parent);

    const QString &path() const { return path_; }
    const QString &section() const { return section_; }
    // False when the file could not be read: the window then shows one line and no text.
    bool loaded() const { return loaded_; }
    // False when the guide has no heading with the section's slug: the window opens at the top.
    bool sectionFound() const { return sectionFound_; }
    QTextBrowser *text() const { return text_; }
    QLabel *message() const { return message_; }

    void done(int result) override;

  protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void scrollTo(const QTextBlock &heading);

    QString path_;
    QString section_;
    bool loaded_{};
    bool sectionFound_{};
    bool scrolled_{};
    int sectionPosition_{-1}; // the heading block's position in the document
    QTextBrowser *text_{};
    QPushButton *close_{};
    QLabel *message_{};
};

// The installed guide: share/doc/sketchyup/USER_GUIDE.md, beside the program's bin folder, where
// CMakeLists.txt installs it.
QString installedUserGuidePath();

// The section the GeoTIFF page of the terrain import dialog opens (UX §20.1). Every section the
// application opens is listed here, and user_guide_viewer_tests checks that each one resolves.
inline constexpr const char *kTerrainDownloadSection = "terrain-from-a-downloaded-elevation-file";

} // namespace sketchy
