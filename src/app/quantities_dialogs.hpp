#pragma once
// R101.e2 (UX_DESIGN §19.6, §19.8, §19.10): the Quantities panel's dialogs. The name field of Save
// as schedule and Rename, the Delete question, and the export report sheet. The words are the
// design's; the refusals come from the engine and are turned into those words by the panel.
#include "core/model.hpp"
#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <set>

class QLabel;
class QLineEdit;
class QPushButton;

namespace sketchy {

// The store's name rules (§19.6): 1 to 64 characters, letters, digits, spaces and . _ ( ) -,
// starting and ending with a letter or a digit. Empty when the name follows them, otherwise the
// words the field shows.
QString scheduleNameProblem(const QString &name);

// Save as schedule… and Rename…: a name field that counts and checks as the person types, the list
// of what the schedule stores, and the note that the selection is never stored (§19.2). Enter saves
// and Esc cancels. A refusal from the commit is shown in the dialog, which stays open.
class ScheduleNameDialog : public QDialog {
    Q_OBJECT
  public:
    ScheduleNameDialog(QWidget *parent, bool rename, const QString &current,
                       const std::set<QString> &taken, const QStringList &stored,
                       const QString &limitNote = {});
    QString name() const;
    void showError(const QString &text);

  private:
    QLineEdit *name_{};
    QLabel *count_{}, *problem_{}, *error_{};
    QPushButton *save_{};
    std::set<QString> taken_;
    void check();
};

// Delete… (§19.6): "Delete the schedule Walls, old?" with Cancel as the default. Returns true when
// the person confirms.
bool confirmScheduleDelete(QWidget *parent, const QString &name);

// One row of the export report's "Needs your attention" column (§19.8): a not-solid group, its
// words, its count and the bodies behind it.
struct ExportAttention {
    QString words;
    QString detail;
    qulonglong count{};
    std::set<Id> bodies;
};

// The export report sheet (§19.8): the banner, what was written, the items that need attention with
// Select in model, and the notices. Close is the default. The bodies the person chose to select are
// read back with selectedBodies() after the sheet closes.
class ExportReportDialog : public QDialog {
    Q_OBJECT
  public:
    ExportReportDialog(QWidget *parent, const QString &path, const QStringList &written,
                       const std::vector<ExportAttention> &attention, const QStringList &notices);
    const std::set<Id> &selectedBodies() const { return selected_; }

  private:
    std::set<Id> selected_;
};

} // namespace sketchy
