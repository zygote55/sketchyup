#pragma once
#include "app/parameter_messages.hpp"
#include "app/settled_dialog.hpp"
#include <QDialog>
#include <functional>
class QLabel;
class QPushButton;
namespace sketchy {
// ADR 0166 §4.1 (R094.e): "*Placement* has its own values". Double-click, Enter or Edit
// definition… on a placement that shows its own values asks how to edit its definition:
// Cancel, Make unique and edit, or Edit definition at defaults (the default button, accent
// filled with the inset focus ring). A nested placement whose values come from its parent offers
// Cancel and Make unique and edit only. Esc is Cancel. It settles before closing on Wayland
// (Settled, R091.h); the window's ModalReturn hands activation back when it closes.
class ParametersScopeDialog final : public Settled<QDialog> {
  public:
    enum Choice { Cancel = QDialog::Rejected, EditAtDefaults = QDialog::Accepted, MakeUnique = 2 };
    // `placement` names the placement ("Dining table #3"); `values` lists the values that
    // differ from the defaults ("Width 1200 mm and Shelves 2"), `count` how many; `parent`
    // names the placement that supplies a nested placement's values (empty when top level).
    ParametersScopeDialog(const QString &placement, const QString &values, int count,
                          const QString &parent, QWidget *owner = nullptr);
    QPushButton *cancelButton() const { return cancel_; }
    QPushButton *uniqueButton() const { return unique_; }
    QPushButton *defaultsButton() const { return defaults_; }
    QString text() const;
    // Checks a choice before the dialog closes (a dry run of its command); a refusal is shown
    // in the dialog, which stays open (ADR 0166 §5: the dialog that issued the command).
    void setCheck(std::function<ParameterMessage(int choice)> check) { check_ = std::move(check); }
    ParameterMessageLabel *errorLabel() const { return error_; }
    // "Width 1200 mm and Shelves 2": a list joined with commas and a final "and".
    static QString listed(const QStringList &items);

  private:
    QLabel *heading_{}, *text_{}, *footnote_{};
    ParameterMessageLabel *error_{};
    QPushButton *cancel_{}, *unique_{}, *defaults_{};
    std::function<ParameterMessage(int)> check_;
    void choose(int choice);
};
} // namespace sketchy
