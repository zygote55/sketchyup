#include "app/parameter_value_field.hpp"
#include "app/keypad_decimal.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include <QAccessible>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <numbers>
namespace sketchy {
namespace {
// Steps by the keyboard, the wheel or the − and + buttons commit at once (ADR 0166 §1.3).
class StepSpinBox : public QSpinBox {
  public:
    std::function<void()> stepped;
    void stepBy(int steps) override {
        QSpinBox::stepBy(steps);
        if (stepped)
            stepped();
    }
};
// The error mark beside a field's sentence: a circle with a cross in the text colour, which
// the theme sets to the failure token.
class ErrorMark : public QWidget {
  public:
    ErrorMark() {
        setObjectName("parameterErrorIcon");
        const int side = fontMetrics().height();
        setFixedSize(side, side);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const double side = std::min(width(), height());
        painter.setPen(QPen(palette().color(QPalette::WindowText), std::max(1.2, side / 9)));
        const QRectF circle(side * .1, side * .1, side * .8, side * .8);
        painter.drawEllipse(circle);
        const auto c = circle.center();
        const double arm = side * .18;
        painter.drawLine(QPointF(c.x() - arm, c.y() - arm), QPointF(c.x() + arm, c.y() + arm));
        painter.drawLine(QPointF(c.x() + arm, c.y() - arm), QPointF(c.x() - arm, c.y() + arm));
    }
};
void repolish(QWidget *widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}
double number(const QString &text) {
    bool ok = false;
    auto value = QLocale().toDouble(text.trimmed(), &ok);
    if (!ok)
        value = QLocale::c().toDouble(text.trimmed(), &ok);
    if (!ok || !std::isfinite(value))
        throw std::runtime_error("not a number");
    return value;
}
// An area in the document's units squared unless the text names a unit.
double area(QString text, DisplayUnit units) {
    text = text.trimmed();
    static const std::vector<std::pair<QString, double>> suffixes{
        {"mm²", 1e-6},      {"mm2", 1e-6},      {"cm²", 1e-4},      {"cm2", 1e-4},
        {"ft²", .09290304}, {"ft2", .09290304}, {"in²", 6.4516e-4}, {"in2", 6.4516e-4},
        {"m²", 1},          {"m2", 1}};
    for (const auto &[suffix, factor] : suffixes)
        if (text.endsWith(suffix, Qt::CaseInsensitive))
            return number(text.chopped(suffix.size())) * factor;
    const double factor = units == DisplayUnit::Millimeters  ? 1e-6
                          : units == DisplayUnit::FeetInches ? .09290304
                                                             : 1.;
    return number(text) * factor;
}
QString typeHint(const QString &type) {
    if (type == "length")
        return "Type a length, such as 1200 mm or 3'11\".";
    if (type == "area")
        return "Type an area, such as 1.5 m².";
    if (type == "angle")
        return "Type an angle, such as 15° or 0.2 rad.";
    if (type == "count")
        return "Type a whole number.";
    return "Type a number.";
}
} // namespace

void relayoutAncestors(QWidget *widget) {
    // updateGeometry drops each widget's cached height-for-width in its parent's layout.
    for (auto *parent = widget; parent; parent = parent->parentWidget()) {
        parent->updateGeometry();
        if (parent->layout())
            parent->layout()->invalidate();
        if (auto *area = qobject_cast<QScrollArea *>(parent)) {
            QCoreApplication::postEvent(area, new QEvent(QEvent::LayoutRequest));
            break;
        }
    }
}
QString parameterValueText(double si, const QString &type, DisplayUnit units, int precision) {
    const int decimals = precision == fullDisplayPrecision ? 8 : precision;
    if (type == "length")
        return displayLength(si, units, precision);
    if (type == "area")
        return displayMeasure(si, 2, units, precision);
    if (type == "angle")
        return displayDecimal(si * 180 / std::numbers::pi, decimals) + "°";
    if (type == "count")
        return QString::number(qlonglong(std::llround(si)));
    return displayDecimal(si, 8);
}
QString parameterErrorSentence(const QJsonObject &error, const QString &typed,
                               const QJsonObject &parameter, DisplayUnit units, int precision,
                               const QJsonValue &value) {
    const auto code = error["code"].toString();
    const auto details = error.contains("details") ? error["details"].toObject() : error;
    const auto type = parameter["type"].toString();
    const auto shown = [&](const QJsonValue &value) {
        return typed.trimmed().isEmpty() && value.isDouble()
                   ? parameterValueText(value.toDouble(), type, units, precision)
                   : typed.trimmed();
    };
    if (code == "PARAMETER_DOMAIN" && details.contains("bound")) {
        const auto bound = details["bound"].toString();
        const auto limit = parameterValueText(details["limit"].toDouble(), type, units, precision);
        const auto given = shown(details["value"]);
        if (bound == "max")
            return given + " is above the maximum, " + limit + ". Nothing was changed.";
        if (bound == "min")
            return given + " is below the minimum, " + limit + ". Nothing was changed.";
        return given + " is not a step of " + limit + ". Nothing was changed.";
    }
    if (code == "PARAMETER_REGENERATION")
        return "At " +
               (value.isDouble() ? parameterValueText(value.toDouble(), type, units, precision)
                                 : shown({})) +
               " the shape can't be regenerated. Nothing was changed.";
    if (code == "STALE_REVISION")
        return "The model changed. Try again.";
    if (code == "NOT_FOUND")
        return "The placement was removed.";
    if (code == "INVALID_TARGET")
        return "This placement can't hold values.";
    auto message = error["message"].toString().trimmed();
    if (!message.isEmpty() && !message.endsWith('.'))
        message += '.';
    return (message.isEmpty() ? QString{} : message + " ") + "Nothing was changed.";
}

ParameterValueField::ParameterValueField(const QJsonObject &parameter, QWidget *parent)
    : QWidget(parent), parameter_(parameter) {
    const auto id = name();
    setObjectName("parameterRow_" + id);
    labelColumn_ = new QWidget;
    labelColumn_->setObjectName("parameterLabelColumn_" + id);
    auto *labels = new QVBoxLayout(labelColumn_);
    labels->setContentsMargins(0, 0, 0, 0);
    labels->setSpacing(0);
    auto *title = new QLabel(label());
    title->setObjectName("parameterLabel");
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    labels->addWidget(title);
    caption_ = new QLabel;
    caption_->setObjectName("parameterCaption");
    caption_->setTextFormat(Qt::PlainText);
    caption_->setWordWrap(true);
    labels->addWidget(caption_);

    // The label column beside the value column, or above it in a narrow Tray (resizeEvent).
    line_ = new QBoxLayout(QBoxLayout::LeftToRight, this);
    line_->setContentsMargins(0, 0, 0, 0);
    line_->setSpacing(6);
    line_->addWidget(labelColumn_, 1);
    valueColumn_ = new QWidget;
    valueColumn_->setObjectName("parameterValueColumn");
    line_->addWidget(valueColumn_, 1);
    auto *column = new QVBoxLayout(valueColumn_);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    column->addLayout(row);
    const auto kind = type();
    if (derived() || (quantity() && kind != "count")) {
        edit_ = new QLineEdit;
        acceptKeypadDecimal(edit_);
        edit_->setMaxLength(1024);
        row->addWidget(edit_, 1);
        connect(edit_, &QLineEdit::textEdited, this, [this] {
            markEdited();
            emit textEdited();
        });
        connect(edit_, &QLineEdit::editingFinished, this, [this] {
            if (!syncing_ && edited_ && !readOnly_)
                emit commitRequested();
        });
    } else if (kind == "count") {
        auto *spin = new StepSpinBox;
        spin_ = spin;
        // A wide range: values are refused by their bounds, never clamped (ADR 0166 §1.3).
        spin_->setRange(-999999, 999999);
        spin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin_->setKeyboardTracking(true);
        fewer_ = new QToolButton;
        fewer_->setText("−");
        fewer_->setObjectName("parameterFewer");
        fewer_->setAccessibleName("Decrease " + label());
        fewer_->setFocusPolicy(Qt::NoFocus);
        more_ = new QToolButton;
        more_->setText("+");
        more_->setObjectName("parameterMore");
        more_->setAccessibleName("Increase " + label());
        more_->setFocusPolicy(Qt::NoFocus);
        row->addWidget(fewer_);
        row->addWidget(spin_, 1);
        row->addWidget(more_);
        spin->stepped = [this] {
            if (syncing_ || readOnly_)
                return;
            markEdited();
            emit commitRequested();
        };
        connect(fewer_, &QToolButton::clicked, this, [this] { spin_->stepBy(-1); });
        connect(more_, &QToolButton::clicked, this, [this] { spin_->stepBy(1); });
        connect(spin_->findChild<QLineEdit *>(), &QLineEdit::textEdited, this, [this] {
            markEdited();
            emit textEdited();
        });
        connect(spin_, &QSpinBox::editingFinished, this, [this] {
            if (!syncing_ && edited_ && !readOnly_)
                emit commitRequested();
        });
    } else if (kind == "boolean") {
        check_ = new QCheckBox;
        row->addWidget(check_, 1);
        connect(check_, &QCheckBox::clicked, this, [this] {
            check_->setText(check_->isChecked() ? "Shown" : "Hidden");
            markEdited();
            emit commitRequested();
        });
    } else {
        choice_ = new QComboBox;
        for (const auto &entry : parameter_["options"].toArray()) {
            const auto option = entry.toObject();
            choice_->addItem(option["label"].toString(), option["value"].toString());
        }
        row->addWidget(choice_, 1);
        connect(choice_, &QComboBox::activated, this, [this] {
            if (syncing_ || readOnly_)
                return;
            markEdited();
            emit commitRequested();
        });
    }
    auto *field = control();
    field->setObjectName("parameterValue_" + id);
    field->setProperty("parameterField", true);
    field->setAccessibleName(label());
    field->setMinimumWidth(fontMetrics().averageCharWidth() * 8);
    field->installEventFilter(this);
    reset_ = new QPushButton("↺");
    reset_->setObjectName("parameterReset_" + id);
    reset_->setAutoDefault(false);
    reset_->setProperty("parameterReset", true);
    row->addWidget(reset_);
    connect(reset_, &QPushButton::clicked, this, [this] { emit resetRequested(); });

    errorRow_ = new QWidget;
    errorRow_->setObjectName("parameterErrorRow");
    auto *errorLine = new QHBoxLayout(errorRow_);
    errorLine->setContentsMargins(0, 0, 0, 0);
    errorLine->setSpacing(4);
    errorLine->addWidget(new ErrorMark, 0, Qt::AlignTop);
    errorText_ = new QLabel;
    errorText_->setObjectName("parameterError");
    errorText_->setTextFormat(Qt::PlainText);
    errorText_->setWordWrap(true);
    errorLine->addWidget(errorText_, 1);
    errorRow_->hide();
    column->addWidget(errorRow_);
}
void ParameterValueField::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    // Side by side while the label fits on one line beside a value column of at least 16
    // characters; otherwise the label wraps above the value (ADR 0166 §1.2, §8).
    const int value = std::max(valueColumn_->minimumSizeHint().width(),
                               fontMetrics().averageCharWidth() * 16);
    const int label = labelColumn_->findChild<QLabel *>("parameterLabel")->sizeHint().width();
    const auto direction = width() >= label + value + line_->spacing() ? QBoxLayout::LeftToRight
                                                                        : QBoxLayout::TopToBottom;
    if (line_->direction() != direction) {
        line_->setDirection(direction);
        updateGeometry();
        relayoutAncestors(this);
    }
}
QString ParameterValueField::name() const { return parameter_["name"].toString(); }
QString ParameterValueField::label() const {
    const auto text = parameter_["label"].toString();
    return text.isEmpty() ? name() : text;
}
QString ParameterValueField::type() const { return parameter_["type"].toString(); }
bool ParameterValueField::derived() const { return parameter_["mode"].toString() == "derived"; }
bool ParameterValueField::quantity() const {
    const auto kind = type();
    return kind == "length" || kind == "area" || kind == "angle" || kind == "number" ||
           kind == "count";
}
QWidget *ParameterValueField::control() const {
    return edit_    ? static_cast<QWidget *>(edit_)
           : spin_  ? static_cast<QWidget *>(spin_)
           : check_ ? static_cast<QWidget *>(check_)
                    : static_cast<QWidget *>(choice_);
}
void ParameterValueField::setStored(const QJsonObject &value, const QString &source,
                                    const QString &parent, DisplayUnit units, int precision,
                                    bool readOnly) {
    stored_ = value;
    source_ = source;
    parent_ = parent;
    units_ = units;
    precision_ = precision;
    readOnly_ = readOnly || derived();
    if (!edited_)
        showStored();
    refreshState();
}
void ParameterValueField::showStored() {
    syncing_ = true;
    const auto value = stored_["value"];
    if (edit_) {
        edit_->setText(stored_["display"].toString());
        edit_->setCursorPosition(0);
    } else if (spin_) {
        spin_->setValue(int(std::llround(value.toDouble())));
    } else if (check_) {
        check_->setChecked(value.toBool());
        check_->setText(value.toBool() ? "Shown" : "Hidden");
    } else if (choice_) {
        choice_->setCurrentIndex(choice_->findData(value.toString()));
    }
    syncing_ = false;
}
void ParameterValueField::markEdited() {
    if (!syncing_)
        edited_ = true;
}
void ParameterValueField::restore() {
    edited_ = false;
    showStored();
    clearError();
}
QString ParameterValueField::text() const {
    if (edit_)
        return edit_->text();
    if (spin_)
        return spin_->cleanText();
    if (check_)
        return check_->text();
    return choice_->currentText();
}
QJsonValue ParameterValueField::parse(const QString &text) const {
    const auto kind = type();
    try {
        if (text.trimmed().isEmpty())
            throw std::runtime_error("empty");
        if (kind == "length")
            return parseLength(text, inputUnit(units_), QLocale());
        if (kind == "angle")
            return parseAngle(text, "deg", QLocale());
        if (kind == "area")
            return area(text, units_);
        if (kind == "count") {
            const auto value = number(text);
            if (std::floor(value) != value)
                throw std::runtime_error("not whole");
            return value;
        }
        return number(text);
    } catch (const std::exception &) {
        throw std::runtime_error(typeHint(kind).toStdString());
    }
}
QJsonValue ParameterValueField::value() const {
    if (check_)
        return check_->isChecked();
    if (choice_)
        return choice_->currentData().toString();
    return parse(text());
}
bool ParameterValueField::equalsStored(const QJsonValue &value) const {
    const auto stored = storedValue();
    if (value.isDouble() && stored.isDouble()) {
        const auto a = value.toDouble(), b = stored.toDouble();
        return std::abs(a - b) <= 1e-9 * std::max({1., std::abs(a), std::abs(b)});
    }
    return value == stored;
}
void ParameterValueField::setError(const QString &sentence, const QString &detail) {
    error_ = sentence;
    errorText_->setText(sentence);
    errorText_->setToolTip(detail);
    errorRow_->setVisible(!sentence.isEmpty());
    refreshState();
}
void ParameterValueField::clearError() {
    if (error_.isEmpty())
        return;
    setError({});
}
QString ParameterValueField::captionText(const QString &separator) const {
    QStringList parts;
    if (derived()) {
        parts << "Derived" + separator + parameter_["expression"].toString();
        return parts.join(separator);
    }
    if (source_ == "parent")
        return "From " + parent_;
    if (source_ == "override") {
        parts << "Override"
              << "default " + parameter_["display"].toString();
    } else {
        parts << "Default";
    }
    const auto kind = type();
    if (quantity()) {
        const auto min = parameter_["min"], max = parameter_["max"];
        auto shown = [&](const QJsonValue &value) {
            return parameterValueText(value.toDouble(), kind, units_, precision_);
        };
        if (min.isDouble() && max.isDouble()) {
            auto low = shown(min);
            const auto high = shown(max);
            // "700–780 mm": one unit for the range when both ends share it.
            const auto space = high.lastIndexOf(' ');
            if (units_ != DisplayUnit::FeetInches && space > 0 && low.endsWith(high.mid(space)))
                low.chop(high.size() - space);
            // A zero-width space lets a narrow Tray wrap a long range after its dash.
            parts << low + "–" + QChar(0x200B) + high;
        } else if (min.isDouble()) {
            parts << "at least " + shown(min);
        } else if (max.isDouble()) {
            parts << "at most " + shown(max);
        }
    }
    return parts.join(separator);
}
QString ParameterValueField::caption() const {
    return captionText(" · ").remove(QChar(0x200B));
}
void ParameterValueField::refreshState() {
    const bool override = source_ == "override";
    const bool invalid = !error_.isEmpty();
    caption_->setText(captionText(" · "));
    auto *field = control();
    if (edit_)
        edit_->setReadOnly(readOnly_);
    if (spin_) {
        spin_->setReadOnly(readOnly_);
        fewer_->setEnabled(!readOnly_);
        more_->setEnabled(!readOnly_);
    }
    if (check_)
        check_->setEnabled(!readOnly_);
    if (choice_)
        choice_->setEnabled(!readOnly_);
    reset_->setVisible(override && !readOnly_);
    const auto defaultText = parameter_["display"].toString();
    reset_->setAccessibleName("Reset " + label() + " to the default, " + defaultText);
    reset_->setToolTip("Reset to the default (" + defaultText + ")");
    if (field->property("override").toBool() != override ||
        field->property("invalid").toBool() != invalid) {
        field->setProperty("override", override);
        field->setProperty("invalid", invalid);
        repolish(field);
    }
    if (caption_->property("override").toBool() != override) {
        caption_->setProperty("override", override);
        repolish(caption_);
    }
    const auto description = (invalid ? "Invalid: " + error_ + " " : QString{}) +
                             captionText(", ").remove(QChar(0x200B));
    if (field->accessibleDescription() != description) {
        field->setAccessibleDescription(description);
        QAccessibleEvent event(field, QAccessible::DescriptionChanged);
        QAccessible::updateAccessibility(&event);
    }
}
bool ParameterValueField::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::FocusIn)
        emit focusEntered();
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
        return QWidget::eventFilter(object, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    const bool press = event->type() == QEvent::KeyPress;
    // Ctrl+Backspace resets an override row while its value is unchanged; while the user is
    // editing text it keeps its text meaning (ADR 0166 §1.2).
    if (key->key() == Qt::Key_Backspace && key->modifiers() == Qt::ControlModifier &&
        source_ == "override" && !readOnly_ && !edited_) {
        event->accept();
        if (press)
            emit resetRequested();
        return true;
    }
    if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier &&
        (edited_ || !error_.isEmpty())) {
        event->accept();
        if (press)
            restore();
        return true;
    }
    return QWidget::eventFilter(object, event);
}
} // namespace sketchy
