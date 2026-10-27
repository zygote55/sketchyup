#pragma once
#include "app/settled_dialog.hpp"
#include "app/viewport.hpp"
#include "io/style_bundle.hpp"
#include <QDialog>
#include <QPixmap>
#include <QSize>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace sketchy {

// R100.f2 (UX_DESIGN §18.1, §18.2, §18.6): the style dialogs and the helpers the Styles tab and the
// File menu share. Every write is a new file in a folder the person chose. An existing file is
// never replaced, and a refusal says what did not happen.

// The settings in words, as the library rows and the dialogs show them: "2.0 px · 60 % · Dashed",
// or "Edge stroke off" when the record has no stroke.
QString styleSettingsWords(const ModelStyle &style);
QString edgeDashWord(EdgeDash dash);
// The number of settings two style records differ in: the 14 fields, and the edge stroke's fields.
// Turning the stroke on or off counts once. The status line uses it ("3 settings changed").
int changedStyleSettings(const ModelStyle &before, const ModelStyle &after);
// The status line after a style is applied (UX_DESIGN §18.2): "Applied “A”: edge stroke on, 3
// settings changed. Undo: Apply style “A”.", or the neutral result when the style was already there.
QString styleApplyOutcome(const QString &name, const ModelStyle &before, const ModelStyle &after,
                          const QString &status);
// A sample of the stroke's own settings, drawn at `size` (UX_DESIGN §18.2): the width, opacity, dash
// and edge colour. It is not a render, and a texture is not drawn in it.
QPixmap styleStrokeSample(const ModelStyle &style, const QSize &size);

// The name rule of a library file is the catalog library rule (ADR 0174 §4, R100.d): 1 to 64
// letters, digits, spaces, dots, underscores and hyphens. Returns the refusal, or an empty string.
QString styleLibraryNameError(const QString &name);
// The file a name takes in `folder`.
QString styleLibraryPath(const QString &folder, const QString &name);
// The style's name from its file path: the file name without its `.sketchystyle` suffix.
QString styleNameFromPath(const QString &path);
// The name itself when its file is free in `folder`, else "name 2", "name 3" and so on.
QString suggestFreeStyleName(const QString &folder, const QString &name);
// The style folder the person chose (UX_DESIGN §18.1): the preference, else styleLibraryFolder().
QString styleLibraryPreference();
void setStyleLibraryPreference(const QString &folder);

// File → Import style… (UX_DESIGN §18.2): the file's facts, a preview of its stroke, and the two
// outcomes. Nothing changes until the primary button is pressed. A file that cannot be used shows
// the refusal of §18.6 instead, with "Choose another file…", which closes with kChooseAnother.
class StyleImportDialog : public Settled<QDialog> {
    Q_OBJECT
  public:
    static constexpr int kChooseAnother = 2;
    StyleImportDialog(Document &doc, Viewport &view, const QString &path,
                      QWidget *parent = nullptr);
    // The status line text for the outcome that happened, once the dialog is accepted.
    QString outcome() const { return outcome_; }

  protected:
    void showEvent(QShowEvent *event) override;

  private:
    Document &doc_;
    Viewport &view_;
    QString path_, outcome_;
    QByteArray bytes_;
    StyleBundle bundle_;
    bool usable_{};
    QRadioButton *apply_{}, *save_{};
    QLabel *nameLabel_{}, *error_{}, *status_{};
    QLineEdit *name_{};
    QPushButton *primary_{};
    void apply();
    void save();
};

// File → Export style… (UX_DESIGN §18.2): what the file will hold, with counts of what a style never
// carries. Export to file… refuses a missing edge texture and an existing name (§18.6).
class StyleExportDialog : public Settled<QDialog> {
    Q_OBJECT
  public:
    StyleExportDialog(Document &doc, QWidget *parent = nullptr);
    // The file written, once the dialog is accepted.
    QString exportedPath() const { return path_; }

  private:
    Document &doc_;
    QString path_;
    QLineEdit *name_{}, *description_{};
    QLabel *error_{};
    void exportFile();
};

// Styles tab → Save to library… (UX_DESIGN §18.2): copies the current style into the library folder
// under a new name. The name rule and the taken name are checked in the dialog, before any write.
class StyleSaveDialog : public Settled<QDialog> {
    Q_OBJECT
  public:
    StyleSaveDialog(Document &doc, const QString &folder, QWidget *parent = nullptr);
    // The file written, once the dialog is accepted.
    QString savedPath() const { return path_; }

  private:
    Document &doc_;
    QString folder_, path_;
    QLineEdit *name_{}, *description_{};
    QLabel *error_{}, *file_{};
    QPushButton *save_{};
};

} // namespace sketchy
