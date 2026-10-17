#include "app/formula_field.hpp"
#include <QAccessible>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
namespace sketchy {
namespace {
constexpr int formulaLimit = 1024;
// The error mark beside a sentence: a circle with a cross in the text colour (the theme sets
// the failure token), as the placement parameters draw it.
class FormulaErrorMark : public QWidget {
  public:
    FormulaErrorMark() {
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
bool unitWord(const QString &word) {
    static const QStringList units{"mm", "cm", "m", "in", "ft", "deg", "rad"};
    return units.contains(word);
}
struct Literal {
    int begin{}, end{};
};
// Number literals not followed by a unit, in text order.
std::vector<Literal> bareNumbers(const QString &text) {
    std::vector<Literal> result;
    static const QRegularExpression number(R"((?<![A-Za-z0-9_.])(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?)");
    for (auto it = number.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        int after = int(match.capturedEnd());
        while (after < text.size() && text[after] == ' ')
            ++after;
        int word = after;
        while (word < text.size() && text[word].isLetter())
            ++word;
        if (word > after && unitWord(text.mid(after, word - after)))
            continue;
        result.push_back({int(match.capturedStart()), int(match.capturedEnd())});
    }
    return result;
}
QString kindOfUnit(const QString &unit) {
    return unit == "deg" || unit == "rad" ? "an angle" : "a length";
}
} // namespace

QString formulaErrorSentence(const QJsonObject &error, const QString &text,
                             const std::function<QString(const QString &)> &label,
                             const QString &fix) {
    const auto code = error["code"].toString();
    const auto details = detailsOf(error);
    const auto reason = reasonOf(error);
    const int offset = details["offset"].toInt();
    const auto at = [&](int character) {
        return QString(" at character %1.").arg(character);
    };
    const auto named = [&](const QString &name) { return label ? label(name) : name; };
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
    if (code == "PARAMETER_NAME") {
        static const QRegularExpression unknown(R"(unknown (parameter|function) '([^']*)')");
        if (const auto match = unknown.match(reason); match.hasMatch())
            return "No " + match.captured(1) + " is called " + match.captured(2) + ".";
        if (details.contains("referrers")) {
            QStringList referrers;
            for (const auto &entry : details["referrers"].toArray())
                referrers << entry.toString();
            return "Still used by " + referrers.join(" · ") + ".";
        }
        return sentence(reason);
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
        auto text = sentence(reason);
        text.chop(1);
        return text + (offset > 0 ? at(offset) : QString("."));
    }
    if (code == "PARAMETER_CYCLE") {
        QStringList chain;
        for (const auto &entry : details["cycle"].toArray())
            chain << named(entry.toString());
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
        if (reason.startsWith("count is not a whole number"))
            return "The value must be a whole number; use round().";
        auto text = sentence(reason);
        if (offset > 0) {
            text.chop(1);
            text += at(offset);
        }
        return text;
    }
    if (code == "PARAMETER_DRIVER_TARGET") {
        if (reason.contains("crosses the stretch plane"))
            return "The arc crosses the stretch plane; move the plane.";
        if (reason.contains("glue face off"))
            return "This would move the glue face off its wall.";
        if (reason.contains("stretch drives Geometry members only"))
            return "Only geometry can be stretched.";
        static const QRegularExpression driver(R"(driver #\d+: (.*)$)");
        const auto match = driver.match(reason);
        return sentence(match.hasMatch() ? match.captured(1) : reason);
    }
    if (code == "PARAMETER_REGENERATION")
        return "The shape can't be regenerated with this formula. Nothing was changed.";
    if (code == "STALE_REVISION")
        return "The model changed. Try again.";
    return sentence(error["message"].toString());
}

FormulaField::FormulaField(QWidget *parent) : QWidget(parent) {
    setObjectName("formulaField");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    edit_ = new QLineEdit;
    edit_->setObjectName("formulaEdit");
    edit_->setProperty("parameterField", true);
    // One character past the limit, so a longer paste is refused with the sentence rather
    // than cut silently.
    edit_->setMaxLength(formulaLimit + 1);
    edit_->installEventFilter(this);
    layout->addWidget(edit_);
    caretRow_ = new QWidget;
    caretRow_->setObjectName("formulaCaretRow");
    caretRow_->setFixedHeight(fontMetrics().height());
    caret_ = new QLabel("^", caretRow_);
    caret_->setObjectName("formulaCaret");
    caret_->setAccessibleName("Error position");
    caretRow_->hide();
    layout->addWidget(caretRow_);
    errorRow_ = new QWidget;
    errorRow_->setObjectName("parameterErrorRow");
    auto *line = new QHBoxLayout(errorRow_);
    line->setContentsMargins(0, 2, 0, 0);
    line->setSpacing(4);
    line->addWidget(new FormulaErrorMark, 0, Qt::AlignTop);
    errorText_ = new QLabel;
    errorText_->setObjectName("parameterError");
    errorText_->setTextFormat(Qt::PlainText);
    errorText_->setWordWrap(true);
    line->addWidget(errorText_, 1);
    errorRow_->hide();
    layout->addWidget(errorRow_);
    fix_ = new QPushButton;
    fix_->setObjectName("formulaUnitFix");
    fix_->setAutoDefault(false);
    fix_->hide();
    auto *fixRow = new QHBoxLayout;
    fixRow->setContentsMargins(0, 4, 0, 0);
    fixRow->addWidget(fix_);
    fixRow->addStretch();
    layout->addLayout(fixRow);
    dryRunTimer_ = new QTimer(this);
    dryRunTimer_->setSingleShot(true);
    dryRunTimer_->setInterval(300);
    connect(dryRunTimer_, &QTimer::timeout, this, [this] { runDryRun(); });
    connect(edit_, &QLineEdit::textEdited, this, [this] { typed(); });
    connect(edit_, &QLineEdit::cursorPositionChanged, this, [this] { placeCaret(); });
    connect(edit_, &QLineEdit::returnPressed, this, [this] {
        if (!syncing_)
            emit commitRequested();
    });
    connect(fix_, &QPushButton::clicked, this, [this] {
        if (fixed_.isEmpty())
            return;
        edit_->setText(fixed_);
        accepted_ = fixed_;
        edited_ = true;
        edit_->setFocus(Qt::OtherFocusReason);
        runEvaluate();
        dryRunTimer_->start();
        emit textEdited();
    });
}
void FormulaField::setAccessibleName(const QString &name) {
    edit_->setAccessibleName(name);
    caret_->setAccessibleDescription(name);
}
void FormulaField::setChecks(Check evaluate, Check dryRun) {
    evaluate_ = std::move(evaluate);
    dryRun_ = std::move(dryRun);
}
void FormulaField::setStored(const QString &text, bool force) {
    stored_ = text;
    if (edited_ && !force)
        return;
    edited_ = false;
    syncing_ = true;
    edit_->setText(text);
    edit_->setCursorPosition(0);
    syncing_ = false;
    accepted_ = text;
    dryRunTimer_->stop();
    clearError();
}
QString FormulaField::text() const { return edit_->text(); }
void FormulaField::typed() {
    if (syncing_)
        return;
    const auto text = edit_->text();
    const bool ascii = std::all_of(text.begin(), text.end(),
                                   [](QChar c) { return c.unicode() >= 0x20 && c.unicode() < 0x7f; });
    if (!ascii || text.size() > formulaLimit) {
        // The field refuses the input: the text stays as it was, and the sentence says why.
        const int cursor = std::min<int>(int(accepted_.size()), edit_->cursorPosition());
        syncing_ = true;
        edit_->setText(accepted_);
        edit_->setCursorPosition(cursor);
        syncing_ = false;
        present({{"code", "LIMIT"},
                 {"message", ascii ? "A formula holds at most 1,024 characters"
                                   : "Formulas use ASCII characters only: letters, digits, "
                                     "operators and units such as mm"}});
        return;
    }
    accepted_ = text;
    edited_ = true;
    runEvaluate();
    dryRunTimer_->start();
    emit textEdited();
}
void FormulaField::runEvaluate() {
    if (!evaluate_) {
        clearError();
        return;
    }
    present(evaluate_(edit_->text()));
}
void FormulaField::runDryRun() {
    // The cheap check failed already: its error stays until the text is fixed.
    if (!error_.isEmpty() || !dryRun_ || !edited_)
        return;
    present(dryRun_(edit_->text()));
}
QString FormulaField::checkNow() {
    dryRunTimer_->stop();
    runEvaluate();
    if (error_.isEmpty() && dryRun_)
        present(dryRun_(edit_->text()));
    return error_;
}
void FormulaField::showError(const QJsonObject &error) { present(error); }
void FormulaField::clearError() { present({}); }
QString FormulaField::unitFix(const QJsonObject &error, QString &literal) const {
    if (error["code"].toString() != "PARAMETER_TYPE" || !evaluate_)
        return {};
    const auto text = edit_->text();
    const int offset = detailsOf(error)["offset"].toInt();
    auto numbers = bareNumbers(text);
    // The literal nearest the character the error names comes first.
    std::stable_sort(numbers.begin(), numbers.end(), [&](const Literal &a, const Literal &b) {
        const auto distance = [&](const Literal &n) {
            return n.begin >= offset - 1 ? n.begin - (offset - 1) : (offset - 1) - n.end + 1;
        };
        return distance(a) < distance(b);
    });
    for (const auto &number : numbers)
        for (const auto &unit : units_) {
            auto candidate = text;
            candidate.insert(number.end, " " + unit);
            if (evaluate_(candidate).isEmpty()) {
                literal = text.mid(number.begin, number.end - number.begin) + " " + unit;
                return candidate;
            }
        }
    return {};
}
void FormulaField::present(const QJsonObject &error) {
    QString literal;
    fixed_ = error.isEmpty() ? QString{} : unitFix(error, literal);
    shownError_ = error;
    QString sentence;
    if (!error.isEmpty() && sentence_)
        sentence = sentence_(error, edit_->text());
    if (!error.isEmpty() && sentence.isEmpty())
        sentence = error["code"].toString() == "LIMIT"
                       ? error["message"].toString() + "."
                       : formulaErrorSentence(error, edit_->text(), label_, literal);
    const bool changed = sentence != error_;
    error_ = sentence;
    errorText_->setText(sentence);
    errorText_->setToolTip(error["message"].toString());
    errorRow_->setVisible(!sentence.isEmpty());
    fix_->setText("Use " + literal);
    fix_->setAccessibleName("Use " + literal);
    fix_->setVisible(!fixed_.isEmpty());
    const int offset = detailsOf(error)["offset"].toInt();
    caretIndex_ = -1;
    if (!sentence.isEmpty() && offset > 0) {
        // A missing closing parenthesis points one past the end, where it belongs.
        const bool end = error["code"].toString() == "PARAMETER_SYNTAX" &&
                         reasonOf(error) == "unexpected end of expression";
        caretIndex_ = end ? int(edit_->text().size()) : offset - 1;
    }
    caretRow_->setVisible(caretIndex_ >= 0);
    placeCaret();
    if (edit_->property("invalid").toBool() != !sentence.isEmpty()) {
        edit_->setProperty("invalid", !sentence.isEmpty());
        repolish(edit_);
    }
    const auto description = sentence.isEmpty() ? QString{} : "Invalid: " + sentence;
    if (edit_->accessibleDescription() != description) {
        edit_->setAccessibleDescription(description);
        QAccessibleEvent event(edit_, QAccessible::DescriptionChanged);
        QAccessible::updateAccessibility(&event);
    }
    if (changed)
        updateGeometry();
    emit checked();
}
void FormulaField::placeCaret() {
    if (caretIndex_ < 0 || !caretRow_->isVisible())
        return;
    // The edit's cursor rectangle gives the scroll offset and the text margins; the caret marks
    // the character by the edit's own font metrics, so it follows text size and scrolling.
    const auto metrics = edit_->fontMetrics();
    const auto text = edit_->text();
    const int cursor = edit_->cursorPosition();
    const int origin = edit_->inputMethodQuery(Qt::ImCursorRectangle).toRect().center().x() -
                       metrics.horizontalAdvance(text.left(cursor));
    const int index = std::min<int>(caretIndex_, int(text.size()));
    const int width = index < text.size() ? metrics.horizontalAdvance(text[index])
                                          : metrics.averageCharWidth();
    const int x = edit_->x() + origin + metrics.horizontalAdvance(text.left(index)) + width / 2 -
                  caret_->sizeHint().width() / 2;
    caret_->adjustSize();
    caret_->move(std::clamp(x, 0, std::max(0, caretRow_->width() - caret_->width())), 0);
    caret_->setToolTip(QString("Character %1").arg(index + 1));
}
void FormulaField::restore() {
    edited_ = false;
    syncing_ = true;
    edit_->setText(stored_);
    syncing_ = false;
    accepted_ = stored_;
    dryRunTimer_->stop();
    clearError();
}
void FormulaField::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    placeCaret();
}
bool FormulaField::eventFilter(QObject *object, QEvent *event) {
    if (object == edit_ &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
            event->accept();
            if (event->type() == QEvent::KeyPress) {
                if (edited_ || !error_.isEmpty())
                    restore();
                else
                    emit escaped();
            }
            return true;
        }
    }
    if (object == edit_ && event->type() == QEvent::Resize)
        placeCaret();
    return QWidget::eventFilter(object, event);
}
} // namespace sketchy
