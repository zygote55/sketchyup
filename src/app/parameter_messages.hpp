#pragma once
#include "core/model.hpp"
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QRectF>
#include <QString>
#include <exception>
#include <functional>
class QPainter;
namespace sketchy {
// ADR 0166 §5 (R094.e): the message table. Every component parameter refusal the desktop shows
// is worded here, from the error's code and its `details` (definition, body, parameter, driver,
// offset, excerpt, cycle, referrers, bound/limit/value), never by parsing the sentence it shows.
// The core message is the fallback and the tooltip.
enum class ParameterMessageTone { Failure, Warning };
// The action a message offers beside its sentence (§5's Action column).
enum class ParameterMessageAction {
    None,
    UseUnit,      // "Use 120 mm": the formula field's one-click unit fix
    ShowDriver,   // "Show the driver": opens the definition and selects the driver
    SelectLocked, // "Select #1": closes the context and selects the locked placement
    CloseContext, // PARAMETER_TEMPLATE_SCOPE: the definition closes
};
// Where the message is shown; some codes read differently at a value field and in a form.
enum class ParameterMessagePlace { Value, Strip, Formula, Definition, Driver };
struct ParameterMessageContext {
    ParameterMessagePlace place{ParameterMessagePlace::Value};
    // Names placements, members and drivers; without it ids stay as the core wrote them.
    const Document *document{};
    DisplayUnit units{DisplayUnit::Meters};
    int precision{};
    // The parameter the field or editor edits (an entry of component_parameters.describe).
    QJsonObject parameter;
    QString typed;    // a value field's text
    QJsonValue value; // what `typed` reads as, in SI units
    QString formula;  // a formula field's text
    QString fix;      // the unit fix's literal ("120 mm") when one checks clean
    Id placement{};   // the placement a value field edits
    // Names a parameter of the definition by its label.
    std::function<QString(const QString &)> label;
    // The driver form: the member's name, whether it is a text, and the child parameter chosen.
    QString member;
    bool textMember{};
    QString childParameter;
};
struct ParameterMessage {
    QString sentence; // empty when there is nothing to show
    QString detail;   // the core message, for the tooltip
    ParameterMessageTone tone{ParameterMessageTone::Failure};
    ParameterMessageAction action{ParameterMessageAction::None};
    QString actionText; // "Show the driver", "Select #1", "Use 120 mm"
    Id target{};        // the driver or the placement the action names
    Id definition{};    // the driver's definition
};
// A refused command or query as {code, message, details}. executeBatch's plain
// "STALE_REVISION" becomes that code.
QJsonObject parameterRefusal(const std::exception &error);
// The §5 sentence, tone and action for `error` (a refusal, or an evaluate error).
ParameterMessage parameterMessage(const QJsonObject &error, const ParameterMessageContext &context);
// "Stretch Top": a driver by its kind and member; empty when the document has no such driver.
QString parameterDriverName(const Document &doc, Id definition, Id driver);

// A form's or strip's message line (ADR 0166 §5): the sentence with its mark drawn in the left
// margin in the text colour (the theme's failure or warning token), a crossed circle for an error
// and a padlock for a warning, so colour is never the only carrier.
class ParameterMessageLabel : public QLabel {
  public:
    explicit ParameterMessageLabel(QWidget *parent = nullptr);
    void setTone(ParameterMessageTone tone);
    ParameterMessageTone tone() const { return tone_; }

  protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    ParameterMessageTone tone_{ParameterMessageTone::Failure};
    void indent();
};
// Draws the mark of `tone` in `rect` with `pen`'s colour (a crossed circle, or a padlock).
void paintParameterMark(QPainter &painter, const QRectF &rect, ParameterMessageTone tone);

// The sentence a refusal shows under a value field (the rows of §5 that apply to placement
// values). `typed` is the text the user entered and `value` what it reads as.
QString parameterErrorSentence(const QJsonObject &error, const QString &typed,
                               const QJsonObject &parameter, DisplayUnit units, int precision,
                               const QJsonValue &value = {});
// The sentence a formula field shows (the rows of §5 that apply to formulas): `text` is the
// formula; `label` names a parameter by its label (for cycles). When `fix` is not empty it is
// the one-click unit fix's literal ("120 mm") and the sentence offers it.
QString formulaErrorSentence(const QJsonObject &error, const QString &text,
                             const std::function<QString(const QString &)> &label,
                             const QString &fix = {});
} // namespace sketchy
