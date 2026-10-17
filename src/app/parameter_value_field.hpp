#pragma once
#include "app/parameter_messages.hpp"
#include "core/units.hpp"
#include <QJsonObject>
#include <QJsonValue>
#include <QWidget>
class QBoxLayout;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
namespace sketchy {
// After a row changes between side by side and stacked (in its resizeEvent), the layouts
// above it hold heights for the old arrangement: invalidates them and asks the enclosing
// scroll area to size its content again.
void relayoutAncestors(QWidget *widget);
// A value in document units for a parameter type (ADR 0166 §1.2): lengths and areas in the
// document's units and precision, angles in degrees, counts as whole numbers.
QString parameterValueText(double si, const QString &type, DisplayUnit units, int precision);
// Reads text in the Measurements box syntax for a quantity type (length, area, angle, number
// or count) in the document's units: a JSON number in SI units. Throws std::runtime_error with
// the type's hint ("Type a length, such as 1200 mm or 3'11\".") when it cannot be read.
QJsonValue parseParameterText(const QString &text, const QString &type, DisplayUnit units);
// One parameter row of Entity info's "Parameters · this placement" (ADR 0166 §1.2, §1.3):
// the label column (label and caption), the value control for the parameter's type, the
// reset button of an override row and the error line under the field. The row parses what is
// typed; the section validates, commits and resets.
// - length, area, angle, number: a line edit in the Measurements box syntax;
// - count: a spin box with − and + buttons that take no focus;
// - boolean: a check box reading "Shown" or "Hidden"; choice: a combo box of option labels;
// - derived (and every row of a nested placement): a read-only, focusable line edit.
// Esc restores the stored value; Ctrl+Backspace resets an override row while the text is
// unchanged and otherwise keeps its text meaning (delete the previous word).
class ParameterValueField : public QWidget {
    Q_OBJECT
  public:
    // `parameter` is one entry of component_parameters.describe.
    explicit ParameterValueField(const QJsonObject &parameter, QWidget *parent = nullptr);
    const QJsonObject &parameter() const { return parameter_; }
    QString name() const;
    QString label() const;
    QString type() const;
    bool derived() const;
    // A typed quantity: length, area, angle, number or count.
    bool quantity() const;
    QWidget *control() const;
    QPushButton *resetButton() const { return reset_; }
    // Shows the stored value. `source` is override, default, derived or parent; `parent`
    // names the placement that supplies a parent value. Text the user is editing is kept.
    void setStored(const QJsonObject &value, const QString &source, const QString &parent,
                   DisplayUnit units, int precision, bool readOnly);
    QJsonValue storedValue() const { return stored_["value"]; }
    QString storedDisplay() const { return stored_["display"].toString(); }
    QString source() const { return source_; }
    bool readOnly() const { return readOnly_; }
    // The value shown: what the user typed or picked, in SI units for quantities. Throws
    // std::runtime_error with the sentence when it cannot be read.
    QJsonValue value() const;
    // Reads text in the Measurements box syntax and the document's units.
    QJsonValue parse(const QString &text) const;
    // The text the user entered (for sentences).
    QString text() const;
    // The user changed the value since it was last stored.
    bool edited() const { return edited_; }
    void setEdited(bool edited) { edited_ = edited; }
    bool equalsStored(const QJsonValue &value) const;
    void setError(const QString &sentence, const QString &detail = {});
    // A §5 message (R094.e): its sentence, tone (a warning is amber with a lock, not invalid)
    // and action button ("Show the driver", "Select #1").
    void setMessage(const ParameterMessage &message);
    void clearError();
    QString error() const { return error_; }
    bool warning() const { return warning_; }
    QPushButton *actionButton() const { return action_; }
    // Shows the stored value again and clears the error (Esc).
    void restore();
    QString caption() const;

  signals:
    void textEdited();      // the text changed: live validation restarts
    void commitRequested(); // Enter, focus loss, a step, a check or a choice
    void resetRequested();  // the reset button or Ctrl+Backspace
    void focusEntered();
    // The message's action button was pressed (a ParameterMessageAction and its targets).
    void actionRequested(int action, qulonglong target, qulonglong definition);

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    QJsonObject parameter_, stored_;
    QString source_, parent_, error_;
    DisplayUnit units_{DisplayUnit::Meters};
    int precision_{};
    bool readOnly_{true}, edited_{}, syncing_{};
    QWidget *labelColumn_{}, *valueColumn_{};
    QBoxLayout *line_{};
    QLabel *caption_{}, *errorText_{};
    QWidget *errorRow_{};
    QLineEdit *edit_{};
    QSpinBox *spin_{};
    QToolButton *fewer_{}, *more_{};
    QCheckBox *check_{};
    QComboBox *choice_{};
    QPushButton *reset_{}, *action_{};
    QWidget *errorMark_{};
    ParameterMessage message_;
    bool warning_{};
    void showStored();
    void markEdited();
    void refreshState();
    QString captionText(const QString &separator) const;
};
} // namespace sketchy
