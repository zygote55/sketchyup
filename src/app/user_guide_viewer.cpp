#include "app/user_guide_viewer.hpp"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

namespace sketchy {
namespace {
// GitHub's heading slug: lower case, punctuation removed (letters, digits, hyphens and underscores
// stay), and each space made a hyphen. "Terrain from a downloaded elevation file" is
// "terrain-from-a-downloaded-elevation-file".
QString headingSlug(const QString &heading) {
    QString slug;
    for (const QChar c : heading.toLower()) {
        if (c.isLetterOrNumber() || c == u'-' || c == u'_')
            slug += c;
        else if (c.isSpace())
            slug += u'-';
    }
    return slug;
}

// The first heading block whose slug is `section`; an invalid block when there is none. Only
// heading blocks count, so a paragraph with the same words is not a section.
QTextBlock findHeading(const QTextDocument &document, const QString &section) {
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() > 0 && headingSlug(block.text()) == section)
            return block;
    }
    return {};
}
} // namespace

QString installedUserGuidePath() {
    return QDir::cleanPath(QCoreApplication::applicationDirPath() +
                           "/../share/doc/sketchyup/USER_GUIDE.md");
}

UserGuideViewer::UserGuideViewer(const QString &path, const QString &section, QWidget *parent)
    : QDialog(parent), path_(path), section_(section) {
    setObjectName("userGuideViewer");
    setWindowTitle("SketchyUp user guide");
    setAccessibleName("SketchyUp user guide");
    resize(640, 560);

    auto *layout = new QVBoxLayout(this);
    message_ = new QLabel;
    message_->setObjectName("userGuideMessage");
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    message_->hide();
    layout->addWidget(message_);

    text_ = new QTextBrowser;
    text_->setObjectName("userGuideText");
    text_->setAccessibleName("User guide, read only");
    // Links are shown as text, and none is followed: the installed copy holds only this guide
    // and the support matrix, so most links would not resolve (UX §22).
    text_->setOpenLinks(false);
    text_->setOpenExternalLinks(false);
    // Tab leaves the text for Close, and Up, Down, Page Up, Page Down, Home and End scroll it.
    text_->setTabChangesFocus(true);
    text_->installEventFilter(this);
    layout->addWidget(text_, 1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    close_ = new QPushButton("Close");
    close_->setObjectName("userGuideClose");
    close_->setAccessibleName("Close the user guide");
    close_->setDefault(true);
    buttons->addWidget(close_);
    layout->addLayout(buttons);
    connect(close_, &QPushButton::clicked, this, &QWidget::close);

    QFile file(path_);
    QByteArray bytes;
    if (file.open(QIODevice::ReadOnly)) {
        bytes = file.readAll();
        loaded_ = file.error() == QFileDevice::NoError;
    }
    if (!loaded_) {
        message_->setText(QStringLiteral("The user guide could not be shown. Its file, "
                                         "USER_GUIDE.md, should be in %1. Reinstall SketchyUp to "
                                         "restore it.")
                              .arg(QDir::toNativeSeparators(QFileInfo(path_).absolutePath())));
        message_->show();
        text_->hide();
        return;
    }

    text_->setMarkdown(QString::fromUtf8(bytes));
    const QTextBlock heading = findHeading(*text_->document(), section_);
    sectionFound_ = heading.isValid();
    if (sectionFound_) {
        sectionPosition_ = heading.position();
    } else {
        message_->setText("This section is not in the installed guide. It opens at the top.");
        message_->show();
    }
}

// A plain Tab leaves the text for Close. In this Qt the focus chain keeps Tab in the text: the
// text's viewport comes next in the chain and gives the focus back to the text, so the window
// moves the focus itself (the chain was read in the test build; Backtab already reaches Close).
bool UserGuideViewer::eventFilter(QObject *watched, QEvent *event) {
    if (watched == text_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<const QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier) {
            close_->setFocus(Qt::TabFocusReason);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void UserGuideViewer::done(int result) {
    QDialog::done(result);
    // Closed by Close, Esc or the window's close box alike: the next open is a new window, at its
    // section again.
    deleteLater();
}

void UserGuideViewer::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    // The text is laid out at the width the window has when it shows, so the section's position is
    // known only now.
    if (scrolled_ || !sectionFound_)
        return;
    scrolled_ = true;
    scrollTo(text_->document()->findBlock(sectionPosition_));
}

// The cursor moves to the heading, and the heading's block is scrolled to the top of the view
// (ADR 0181, R107.b2 section, "Finding the section").
void UserGuideViewer::scrollTo(const QTextBlock &heading) {
    text_->setTextCursor(QTextCursor(heading));
    const qreal top = text_->document()->documentLayout()->blockBoundingRect(heading).top();
    text_->verticalScrollBar()->setValue(qRound(top));
}

} // namespace sketchy
