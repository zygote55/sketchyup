#include "app/parameter_messages.hpp"
#include "app/parameter_value_field.hpp"
#include "automation/inspection.hpp"
#include "core/component_parameter_records.hpp"
#include "core/parameter_expression.hpp"
#include <QEvent>
#include <QJsonArray>
#include <QPainter>
#include <QRegularExpression>
#include <QStyle>
namespace sketchy {
namespace {
using Place = ParameterMessagePlace;
QJsonObject detailsOf(const QJsonObject &error) {
    return error.contains("details") ? error["details"].toObject() : error;
}
// The reason after the core's "PARAMETER_SYNTAX at byte 7: " prefix, else the whole message.
QString reasonOf(const QJsonObject &error) {
    const auto message = error["message"].toString();
    static const QRegularExpression prefix(R"(at byte \d+: (.*)$)");
    const auto match = prefix.match(message);
    return match.hasMatch() ? match.captured(1) : message;
}
QString sentence(QString text) {
    text = text.trimmed();
    if (text.isEmpty())
        return text;
    text[0] = text[0].toUpper();
    if (!text.endsWith('.'))
        text += '.';
    return text;
}
QString kindOfUnit(const QString &unit) {
    return unit == "deg" || unit == "rad" ? "an angle" : "a length";
}
Id idOf(const QJsonValue &value) {
    return value.isString() ? value.toString().toULongLong() : Id(value.toDouble());
}
QString kindTitle(ComponentDriverKind kind) {
    switch (kind) {
    case ComponentDriverKind::Stretch:
        return "Stretch";
    case ComponentDriverKind::Position:
        return "Position";
    case ComponentDriverKind::Rotation:
        return "Rotation";
    case ComponentDriverKind::Visibility:
        return "Visibility";
    case ComponentDriverKind::Repeat:
        return "Repeat";
    case ComponentDriverKind::ChildParameter:
        return "Child parameter";
    }
    return {};
}
const ComponentDriver *driverRecord(const Document &doc, Id definition, Id driver) {
    const auto found = doc.definitions().find(definition);
    if (found == doc.definitions().end() || !found->second->parameters)
        return nullptr;
    for (const auto &record : found->second->parameters->drivers)
        if (record.id == driver)
            return &record;
    return nullptr;
}
QString memberName(const Document &doc, Id definition, Id member) {
    const auto found = doc.definitions().find(definition);
    if (found == doc.definitions().end())
        return {};
    const auto named = found->second->members.find(member);
    return named == found->second->members.end() ? QString{}
                                                 : QString::fromStdString(named->second->name);
}
QString bodyName(const Document *doc, Id body) {
    if (doc)
        if (const auto found = doc->bodies().find(body);
            found != doc->bodies().end() && !found->second->name.empty())
            return QString::fromStdString(found->second->name);
    return "Placement #" + QString::number(body);
}
// "Select #1" for "Dining table #1", else "Select Dining table".
QString selectText(const QString &name) {
    static const QRegularExpression numbered(R"((#\d+)\s*$)");
    const auto match = numbered.match(name);
    return "Select " + (match.hasMatch() ? match.captured(1) : name);
}
// The value a value field refused, in the document's units.
QString shownValue(const ParameterMessageContext &context) {
    const auto type = context.parameter["type"].toString();
    if (context.value.isDouble() && !type.isEmpty())
        return parameterValueText(context.value.toDouble(), type, context.units, context.precision);
    return context.typed.trimmed();
}
QString named(const ParameterMessageContext &context, const QString &name) {
    return context.label ? context.label(name) : name;
}
// The formula rows of §5 (syntax, names, types, cycles, other domain errors, driver targets).
QString formulaSentence(const QJsonObject &error, const ParameterMessageContext &context) {
    const auto code = error["code"].toString();
    const auto details = detailsOf(error);
    const auto reason = reasonOf(error);
    const auto &text = context.formula;
    const auto &fix = context.fix;
    const int offset = details["offset"].toInt();
    const auto at = [&](int character) { return QString(" at character %1.").arg(character); };
    if (code == "PARAMETER_SYNTAX") {
        if (reason == "empty expression")
            return "Type a formula.";
        if (reason == "unexpected end of expression") {
            if (text.count('(') > text.count(')'))
                return "A closing \")\" is missing" + at(int(text.size()) + 1);
            return "The formula ends too early" + at(int(text.size()) + 1);
        }
        if (reason.startsWith("unexpected") && offset > 0 && offset <= text.size())
            return "Unexpected \"" + text.mid(offset - 1, 1) + "\"" + at(offset);
        auto result = sentence(reason);
        result.chop(1);
        return result + (offset > 0 ? at(offset) : QString("."));
    }
    if (code == "PARAMETER_TYPE") {
        if (!fix.isEmpty()) {
            const auto unit = fix.section(' ', -1);
            const auto kind = kindOfUnit(unit == "°" ? "deg" : unit);
            const auto op = offset > 0 && offset <= text.size() ? text[offset - 1] : QChar();
            QString first;
            const auto literal = fix.section(' ', 0, 0);
            const int literalAt = int(text.indexOf(literal, std::max(0, offset - 1)));
            const bool after = literalAt >= offset;
            if (op == '-')
                first = after ? "Can't subtract a number from " + kind
                              : "Can't subtract " + kind + " from a number";
            else if (op == '+')
                first = "Can't add a number to " + kind;
            else if (op == '<' || op == '>')
                first = "Can't compare a number with " + kind;
            else if (reason.startsWith("expression is"))
                first = QString(kind == "an angle" ? "An angle" : "A length") + " needs a unit";
            else
                first = "This number needs a unit";
            return first + ". Add a unit, for example " + fix + ".";
        }
        static const QRegularExpression result(R"(^expression is (.+); the \w+ needs (.+)$)");
        if (const auto match = result.match(reason); match.hasMatch())
            return "The formula gives " + match.captured(1) + ", but " + match.captured(2) +
                   " is needed.";
        auto line = sentence(reason);
        line.chop(1);
        return line + (offset > 0 ? at(offset) : QString("."));
    }
    if (code == "PARAMETER_CYCLE") {
        QStringList chain;
        for (const auto &entry : details["cycle"].toArray())
            chain << named(context, entry.toString());
        if (!chain.isEmpty())
            chain << chain.front();
        return "These depend on each other: " + chain.join(" → ") +
               ". One of them needs a value instead of a formula.";
    }
    if (code == "PARAMETER_DOMAIN") {
        if (reason == "division by zero")
            return "Division by zero" + at(std::max(1, offset));
        if (reason == "sqrt() of a negative value")
            return "The square root of a negative value" + at(std::max(1, offset));
        if (reason.startsWith("count is not a whole number")) {
            const auto parameter = details["parameter"].toString();
            return (parameter.isEmpty() ? QString("The value") : named(context, parameter)) +
                   " must be a whole number; use round().";
        }
        auto line = sentence(reason);
        if (offset > 0) {
            line.chop(1);
            line += at(offset);
        }
        return line;
    }
    if (code == "PARAMETER_DRIVER_TARGET") {
        if (reason.contains("crosses the stretch plane"))
            return "The arc crosses the stretch plane; move the plane.";
        if (reason.contains("glue face off"))
            return "This would move the glue face off its wall.";
        if (reason.contains("stretch drives Geometry members only"))
            return context.textMember ? "Text can't be stretched."
                                      : "Only geometry can be stretched.";
        if (reason.contains("has no input parameter") && !context.member.isEmpty())
            return context.member + " has no parameter " + context.childParameter + ".";
        static const QRegularExpression driver(R"(driver #\d+: (.*)$)");
        const auto match = driver.match(reason);
        return sentence(match.hasMatch() ? match.captured(1) : reason);
    }
    return {};
}
// "Still used by Leg position · Stretch Top." from the referrers' ids.
QString referrersSentence(const QJsonArray &referrers, const ParameterMessageContext &context,
                          Id definition) {
    QStringList users;
    static const QRegularExpression driver(R"(^driver #(\d+)(?: of definition #(\d+))?$)");
    for (const auto &entry : referrers) {
        const auto user = entry.toString();
        if (user.startsWith("parameter ")) {
            users << named(context, user.mid(10));
            continue;
        }
        const auto match = driver.match(user);
        if (match.hasMatch() && context.document) {
            const Id owner =
                match.captured(2).isEmpty() ? definition : match.captured(2).toULongLong();
            const auto name =
                parameterDriverName(*context.document, owner, match.captured(1).toULongLong());
            if (!name.isEmpty()) {
                const auto found = context.document->definitions().find(owner);
                users << (match.captured(2).isEmpty() ||
                                  found == context.document->definitions().end()
                              ? name
                              : name + " of " + QString::fromStdString(found->second->name));
                continue;
            }
        }
        users << user;
    }
    return "Still used by " + users.join(" · ") + ".";
}
// PARAMETER_REGENERATION: "At 300 mm Top would pass its stretch plane (Stretch Top). Nothing
// was changed."
QString regenerationSentence(const QJsonObject &error, const ParameterMessageContext &context,
                             Id definition, Id driver) {
    const auto reason = error["message"].toString();
    QString lead;
    if (context.place == Place::Value || context.place == Place::Strip) {
        const auto value = shownValue(context);
        lead = !value.isEmpty()                ? "At " + value
               : context.place == Place::Strip ? QString("With these values")
                                               : QString("With this value");
    } else if (context.place == Place::Formula) {
        lead = "With this formula";
    } else {
        lead = "With this change";
    }
    const auto *record =
        context.document ? driverRecord(*context.document, definition, driver) : nullptr;
    if (!record)
        return lead + " the shape can't be regenerated. Nothing was changed.";
    const auto member = memberName(*context.document, definition, record->member);
    const auto name = kindTitle(record->kind) + (member.isEmpty() ? QString{} : " " + member);
    if (reason.contains("glue face"))
        return lead + " the glue face would move off its wall (" + name + "). Nothing was changed.";
    if (reason.contains("across the plane"))
        return lead + " " + (member.isEmpty() ? QString("a part") : member) +
               " would cross the stretch plane (" + name + "). Nothing was changed.";
    if (record->kind == ComponentDriverKind::Stretch)
        return lead + " " + (member.isEmpty() ? QString("the part") : member) +
               " would pass its stretch plane (" + name + "). Nothing was changed.";
    if (record->kind == ComponentDriverKind::Repeat)
        return lead + " the copies of " + (member.isEmpty() ? QString("the part") : member) +
               " can't be placed (" + name + "). Nothing was changed.";
    return lead + " " + (member.isEmpty() ? QString("the part") : member) +
           " can't be regenerated (" + name + "). Nothing was changed.";
}
} // namespace

QJsonObject parameterRefusal(const std::exception &error) {
    if (const auto *coded = dynamic_cast<const InspectionError *>(&error))
        return {{"code", QString::fromStdString(coded->code())},
                {"message", QString::fromUtf8(coded->what())},
                {"details", coded->details()}};
    const auto message = QString::fromUtf8(error.what());
    // executeBatch refuses a batch built at an older revision with a plain message.
    if (message == "STALE_REVISION")
        return {{"code", "STALE_REVISION"}, {"message", message}};
    return {{"message", message}};
}
QString parameterDriverName(const Document &doc, Id definition, Id driver) {
    const auto *record = driverRecord(doc, definition, driver);
    if (!record)
        return {};
    const auto member = memberName(doc, definition, record->member);
    return kindTitle(record->kind) + (member.isEmpty() ? QString{} : " " + member);
}
ParameterMessage parameterMessage(const QJsonObject &error,
                                  const ParameterMessageContext &context) {
    ParameterMessage result;
    if (error.isEmpty())
        return result;
    auto code = error["code"].toString();
    if (code.isEmpty() && error["message"].toString() == "STALE_REVISION")
        code = "STALE_REVISION";
    const auto details = detailsOf(error);
    result.detail = error["message"].toString();
    const Id definition = idOf(details["definition"]);
    const Id driver = idOf(details["driver"]);
    const Id body = idOf(details["body"]);
    const bool valueField = context.place == Place::Value || context.place == Place::Strip;
    if (code == "PARAMETER_DOMAIN" && details.contains("bound")) {
        // The refused value and the bound in the document's units (ADR 0166 §5: "2.6 m is above
        // the maximum, 2.4 m."). The bound's parameter names the type when the context has none.
        auto parameter = context.parameter;
        if (parameter["type"].toString().isEmpty() && context.document && definition) {
            if (const auto found = context.document->definitions().find(definition);
                found != context.document->definitions().end() && found->second->parameters)
                for (const auto &entry : found->second->parameters->parameters)
                    if (QString::fromStdString(entry.name) == details["parameter"].toString())
                        parameter["type"] =
                            QString::fromStdString(std::string(parameterTypeName(entry.type)));
        }
        const auto type = parameter["type"].toString();
        const auto shown = [&](double si) {
            return type.isEmpty() ? QString::number(si)
                                  : parameterValueText(si, type, context.units, context.precision);
        };
        const auto bound = details["bound"].toString();
        const auto limit = shown(details["limit"].toDouble());
        const auto given = details["value"].isDouble() ? shown(details["value"].toDouble())
                                                       : context.typed.trimmed();
        if (bound == "max")
            result.sentence = given + " is above the maximum, " + limit + ". Nothing was changed.";
        else if (bound == "min")
            result.sentence = given + " is below the minimum, " + limit + ". Nothing was changed.";
        else
            result.sentence = given + " is not a step of " + limit + ". Nothing was changed.";
        return result;
    }
    if (code == "PARAMETER_REGENERATION") {
        result.sentence = regenerationSentence(error, context, definition, driver);
        if (driver && definition) {
            result.action = ParameterMessageAction::ShowDriver;
            result.actionText = "Show the driver";
            result.target = driver;
            result.definition = definition;
        }
        return result;
    }
    if (code == "PARAMETER_NAME" && details.contains("referrers")) {
        result.sentence = referrersSentence(details["referrers"].toArray(), context, definition);
        return result;
    }
    if (code == "PARAMETER_NAME") {
        const auto reason = reasonOf(error);
        static const QRegularExpression unknown(R"(unknown (parameter|function) '([^']*)')");
        if (const auto match = unknown.match(reason); match.hasMatch())
            result.sentence = "No " + match.captured(1) + " is called " + match.captured(2) + ".";
        else
            result.sentence = sentence(reason);
        return result;
    }
    if (code == "PARAMETER_TEMPLATE_SCOPE") {
        result.sentence = bodyName(context.document, body ? body : context.placement) +
                          " now has its own values. Close the definition and open it again.";
        result.action = ParameterMessageAction::CloseContext;
        result.target = body ? body : context.placement;
        return result;
    }
    if (code == "LOCKED_ENTITY") {
        result.tone = ParameterMessageTone::Warning;
        const auto name = bodyName(context.document, body ? body : context.placement);
        const auto label = context.parameter["label"].toString();
        if (valueField)
            result.sentence = name + " is locked. Unlock it to change its values.";
        else if (context.place == Place::Definition && !label.isEmpty())
            result.sentence = "Changing the default " + label + " would change " + name +
                              ", which is locked. Unlock it or keep the default.";
        else
            result.sentence = "Changing the definition would change " + name +
                              ", which is locked. Unlock it or keep the definition as it is.";
        if (body && !(valueField && body == context.placement)) {
            result.action = ParameterMessageAction::SelectLocked;
            result.actionText = selectText(name);
            result.target = body;
        }
        return result;
    }
    if (code == "STALE_REVISION") {
        result.sentence = "The model changed. Try again.";
        return result;
    }
    if (code == "NOT_FOUND" && valueField) {
        result.sentence = "The placement was removed.";
        return result;
    }
    if (code == "INVALID_TARGET" && valueField) {
        result.sentence = "This placement can't hold values.";
        return result;
    }
    if (!valueField || code == "PARAMETER_TYPE" || code == "PARAMETER_CYCLE" ||
        code == "PARAMETER_SYNTAX" || code == "PARAMETER_DRIVER_TARGET" ||
        (code == "PARAMETER_DOMAIN" && !details.contains("bound"))) {
        const auto formula = formulaSentence(error, context);
        if (!formula.isEmpty()) {
            result.sentence = formula;
            if (code == "PARAMETER_TYPE" && !context.fix.isEmpty()) {
                result.action = ParameterMessageAction::UseUnit;
                result.actionText = "Use " + context.fix;
            }
            return result;
        }
    }
    // LIMIT_EXCEEDED quotes the core's D06 limit message; anything else falls back to the
    // core message. A value field adds that the document is unchanged.
    auto message = error["message"].toString().trimmed();
    if (!message.isEmpty() && !message.endsWith('.'))
        message += '.';
    if (!message.isEmpty())
        message[0] = message[0].toUpper();
    result.sentence = valueField || code == "LIMIT_EXCEEDED"
                          ? (message.isEmpty() ? QString{} : message + " ") + "Nothing was changed."
                          : message;
    return result;
}
QString parameterErrorSentence(const QJsonObject &error, const QString &typed,
                               const QJsonObject &parameter, DisplayUnit units, int precision,
                               const QJsonValue &value) {
    ParameterMessageContext context;
    context.place = Place::Value;
    context.units = units;
    context.precision = precision;
    context.parameter = parameter;
    context.typed = typed;
    context.value = value;
    return parameterMessage(error, context).sentence;
}
QString formulaErrorSentence(const QJsonObject &error, const QString &text,
                             const std::function<QString(const QString &)> &label,
                             const QString &fix) {
    ParameterMessageContext context;
    context.place = Place::Formula;
    context.formula = text;
    context.label = label;
    context.fix = fix;
    return parameterMessage(error, context).sentence;
}
void paintParameterMark(QPainter &painter, const QRectF &rect, ParameterMessageTone tone) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const double side = std::min(rect.width(), rect.height());
    const QPointF o = rect.topLeft();
    painter.setPen(QPen(painter.pen().color(), std::max(1.2, side / 9)));
    painter.setBrush(Qt::NoBrush);
    if (tone == ParameterMessageTone::Warning) {
        painter.drawRoundedRect(
            QRectF(o.x() + side * .2, o.y() + side * .46, side * .6, side * .44), side * .07,
            side * .07);
        painter.drawArc(QRectF(o.x() + side * .33, o.y() + side * .12, side * .34, side * .5), 0,
                        180 * 16);
        painter.drawLine(QPointF(o.x() + side * .33, o.y() + side * .37),
                         QPointF(o.x() + side * .33, o.y() + side * .46));
        painter.drawLine(QPointF(o.x() + side * .67, o.y() + side * .37),
                         QPointF(o.x() + side * .67, o.y() + side * .46));
    } else {
        const QRectF circle(o.x() + side * .1, o.y() + side * .1, side * .8, side * .8);
        painter.drawEllipse(circle);
        const auto c = circle.center();
        const double arm = side * .18;
        painter.drawLine(QPointF(c.x() - arm, c.y() - arm), QPointF(c.x() + arm, c.y() + arm));
        painter.drawLine(QPointF(c.x() + arm, c.y() - arm), QPointF(c.x() - arm, c.y() + arm));
    }
    painter.restore();
}
ParameterMessageLabel::ParameterMessageLabel(QWidget *parent) : QLabel(parent) {
    setWordWrap(true);
    setTextFormat(Qt::PlainText);
    setProperty("tone", "failure");
    indent();
}
void ParameterMessageLabel::indent() {
    const int side = fontMetrics().height();
    setContentsMargins(side + 4, 0, 0, 0);
}
void ParameterMessageLabel::setTone(ParameterMessageTone tone) {
    tone_ = tone;
    const auto name = tone == ParameterMessageTone::Warning ? "warning" : "failure";
    if (property("tone").toString() != name) {
        setProperty("tone", name);
        style()->unpolish(this);
        style()->polish(this);
        update();
    }
}
void ParameterMessageLabel::changeEvent(QEvent *event) {
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        indent();
}
void ParameterMessageLabel::paintEvent(QPaintEvent *event) {
    QLabel::paintEvent(event);
    if (text().isEmpty())
        return;
    QPainter painter(this);
    painter.setPen(palette().color(foregroundRole()));
    const int side = fontMetrics().height();
    paintParameterMark(
        painter, QRectF(contentsRect().left() - side - 4, contentsRect().top(), side, side), tone_);
}
} // namespace sketchy
