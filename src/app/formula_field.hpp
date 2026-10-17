#pragma once
#include <QJsonObject>
#include <QWidget>
#include <functional>
#include <map>
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
namespace sketchy {
// The sentence a formula field shows for an error (ADR 0166 §5, the rows that apply to formula
// fields): `error` is a component_parameters.evaluate error or a refusal's {code, message,
// details}; `text` is the formula; `label` names a parameter by its label (for cycles). When
// `fix` is not empty it is the one-click unit fix's literal ("120 mm") and the sentence offers
// it. The core message is the fallback.
QString formulaErrorSentence(const QJsonObject &error, const QString &text,
                             const std::function<QString(const QString &)> &label,
                             const QString &fix = {});

// One formula or default-expression field (ADR 0166 §3.5), shared by the parameter editor
// and the driver editor: a monospace line edit of at most 1,024 ASCII characters, checked as
// you type. Each edit calls `evaluate` at once (syntax, names and units, no geometry); 300 ms
// after the last edit `dryRun` checks the actual command (result type, cycles, bounds and
// regeneration). An error shows the red border, a caret under the character it names and
// the sentence; for a bare number that needs a unit the field offers "Use 120 mm" when the
// fixed text checks clean. The document keeps its last valid formula until the field is
// fixed; Esc restores it and Enter asks to commit.
class FormulaField : public QWidget {
    Q_OBJECT
  public:
    // Each check returns the error ({code, message, details}) or an empty object.
    using Check = std::function<QJsonObject(const QString &text)>;
    explicit FormulaField(QWidget *parent = nullptr);
    QLineEdit *edit() const { return edit_; }
    QLabel *caret() const { return caret_; }
    QPushButton *fixButton() const { return fix_; }
    void setAccessibleName(const QString &name);
    void setChecks(Check evaluate, Check dryRun);
    // The units the one-click fix tries, in order ("mm", "deg").
    void setFixUnits(const QStringList &units) { units_ = units; }
    // Names parameters by label in sentences.
    void setLabels(std::function<QString(const QString &)> label) { label_ = std::move(label); }
    // Writes the sentence for errors the owner words itself (bounds); empty for the default.
    using Sentence = std::function<QString(const QJsonObject &error, const QString &text)>;
    void setSentence(Sentence sentence) { sentence_ = std::move(sentence); }
    // Shows the document's text (the last valid formula) unless the user is editing.
    void setStored(const QString &text, bool force = false);
    QString stored() const { return stored_; }
    QString text() const;
    bool edited() const { return edited_; }
    // Runs both checks now; returns the error sentence (empty when the text checks clean).
    QString checkNow();
    // Shows an error from elsewhere (a refused commit) at this field.
    void showError(const QJsonObject &error);
    void clearError();
    QString error() const { return error_; }
    // Shows the stored text again and clears the error (Esc).
    void restore();
    // The character the caret marker points at (0-based), or -1 while it is hidden.
    int caretIndex() const { return caretIndex_; }

  signals:
    void textEdited();
    void commitRequested(); // Enter
    void escaped();         // Esc with nothing to restore
    void checked();         // a check finished (the error may have changed)

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    QLineEdit *edit_{};
    QWidget *caretRow_{}, *errorRow_{};
    QLabel *caret_{}, *errorText_{};
    QPushButton *fix_{};
    QTimer *dryRunTimer_{};
    Check evaluate_, dryRun_;
    std::function<QString(const QString &)> label_;
    Sentence sentence_;
    QStringList units_{"mm"};
    QString stored_, accepted_, error_, fixed_;
    QJsonObject shownError_;
    bool edited_{}, syncing_{};
    int caretIndex_{-1};
    void typed();
    void runEvaluate();
    void runDryRun();
    void present(const QJsonObject &error);
    void placeCaret();
    // A text with a unit after the bare number the error points at, which checks clean.
    QString unitFix(const QJsonObject &error, QString &literal) const;
};
} // namespace sketchy
