#include "app/component_parameters_section.hpp"
#include "app/parameter_value_field.hpp"
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopedValueRollback>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
namespace sketchy {
namespace {
// "Change Width": the parameter's label, cut to 64 UTF-8 bytes with "…" (ADR 0166 §1.4).
QString historyLabel(const QString &verb, const QString &label) {
    QString cut;
    qsizetype bytes = 0;
    for (const auto character : label.toUcs4()) {
        const char32_t point = character;
        const auto text = QString::fromUcs4(&point, 1);
        bytes += text.toUtf8().size();
        if (bytes > 64)
            return verb + " " + cut + "…";
        cut += text;
    }
    return verb + " " + cut;
}
QJsonObject refusal(const InspectionError &error) {
    return {{"code", QString::fromStdString(error.code())},
            {"message", QString::fromUtf8(error.what())},
            {"details", error.details()}};
}
bool parametric(const Document &doc, Id body) {
    const auto instance = doc.instances().find(body);
    if (instance == doc.instances().end())
        return false;
    const auto definition = doc.definitions().find(instance->second->definition);
    return definition != doc.definitions().end() && definition->second->parameters;
}
} // namespace
ComponentParametersSection::ComponentParametersSection(Document &doc, Viewport &view,
                                                       QWidget *parent)
    : CollapsibleSection("Parameters · this placement", new QWidget, true, parent), doc_(doc),
      view_(view) {
    setObjectName("componentParametersSection");
    header()->setObjectName("sectionHeader");
    body_ = content();
    auto *layout = new QVBoxLayout(body_);
    layout->setContentsMargins(8, 4, 2, 8);
    layout->setSpacing(8);
    message_ = new QLabel("Select one placement to see its parameters");
    message_->setObjectName("parameterMessage");
    message_->setWordWrap(true);
    layout->addWidget(message_);
    // The row asks for no minimum width, so a narrow Tray can resize the section, which then
    // stacks the buttons (resizeEvent).
    auto *buttonRow = new QWidget;
    buttonRow->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *buttons = buttons_ = new QHBoxLayout(buttonRow);
    buttons->setContentsMargins(0, 0, 0, 0);
    resetAll_ = new QPushButton("Reset all");
    resetAll_->setObjectName("parameterResetAll");
    resetAll_->setAutoDefault(false);
    resetAll_->setToolTip("Reset every parameter of this placement to its default");
    editDefinition_ = new QPushButton("Edit definition…");
    editDefinition_->setObjectName("parameterEditDefinition");
    editDefinition_->setAutoDefault(false);
    editDefinition_->setToolTip("Open the definition, as Enter on the placement does");
    buttons->addWidget(resetAll_);
    buttons->addWidget(editDefinition_);
    buttons->addStretch();
    layout->addWidget(buttonRow);
    note_ = new QLabel("Values accept the same units as the Measurements box. Enter applies, Esc "
                       "restores. Each change is one Undo step.");
    note_->setObjectName("parameterNote");
    note_->setWordWrap(true);
    layout->addWidget(note_);
    validation_ = new QTimer(this);
    validation_->setSingleShot(true);
    validation_->setInterval(150);
    connect(validation_, &QTimer::timeout, this, [this] { validate(); });
    connect(resetAll_, &QPushButton::clicked, this, [this] { resetAll(); });
    connect(editDefinition_, &QPushButton::clicked, this, [this] { editDefinition(); });
    hide();
}
void ComponentParametersSection::resizeEvent(QResizeEvent *event) {
    CollapsibleSection::resizeEvent(event);
    // The buttons stack when the Tray is narrower than both side by side.
    const int needed =
        resetAll_->sizeHint().width() + editDefinition_->sizeHint().width() + buttons_->spacing();
    const auto margins = body_->layout()->contentsMargins();
    const auto direction = needed <= width() - margins.left() - margins.right()
                               ? QBoxLayout::LeftToRight
                               : QBoxLayout::TopToBottom;
    if (buttons_->direction() != direction) {
        buttons_->setDirection(direction);
        updateGeometry();
        relayoutAncestors(this);
    }
}
QString ComponentParametersSection::hint() {
    return "Tab moves to the next parameter · Ctrl+Backspace resets the focused one · Enter edits "
           "the definition";
}
void ComponentParametersSection::detach() {
    detached_ = true;
    validation_->stop();
    placement_ = 0;
}
ParameterValueField *ComponentParametersSection::field(const QString &name) const {
    for (auto *field : fields_)
        if (field->name() == name)
            return field;
    return nullptr;
}
void ComponentParametersSection::refresh() {
    if (detached_)
        return;
    const auto &selected = view_.selectionState().entities();
    Id placement = 0;
    bool several = false;
    if (selected.size() == 1 && selected.begin()->kind == SelectionKind::Body &&
        parametric(doc_, selected.begin()->body))
        placement = selected.begin()->body;
    else if (selected.size() > 1)
        several = std::any_of(selected.begin(), selected.end(), [&](const auto &entity) {
            return entity.kind == SelectionKind::Body && parametric(doc_, entity.body);
        });
    if (!placement) {
        const bool changed = placement_ != 0;
        placement_ = definition_ = 0;
        values_ = {};
        cacheKey_.clear();
        validation_->stop();
        if (rows_)
            rows_->hide();
        message_->show();
        resetAll_->hide();
        editDefinition_->hide();
        note_->hide();
        setDetail({});
        setVisible(several);
        if (changed)
            emit measurementChanged();
        return;
    }
    const auto key = QString::fromStdString(doc_.identity()) + "/" +
                     QString::number(doc_.revision()) + "/" + QString::number(placement) + "/" +
                     QString::number(int(doc_.displayUnits())) + "/" +
                     QString::number(doc_.displayPrecision());
    if (key == cacheKey_) {
        show();
        return;
    }
    // A placement inside another component's definition takes its values from defaults or
    // from its parent and has no overrides of its own (ADR 0165 §1).
    bool nested = false;
    QString parent;
    for (auto ancestor = doc_.bodies().at(placement)->parent;
         ancestor && doc_.bodies().contains(ancestor);
         ancestor = doc_.bodies().at(ancestor)->parent)
        if (doc_.instances().contains(ancestor)) {
            nested = true;
            parent = QString::fromStdString(doc_.bodies().at(ancestor)->name);
            break;
        }
    QJsonObject description;
    try {
        auto query = [&](const char *name, const char *field, Id id) {
            return inspectDocument(doc_, {{"apiVersion", 1},
                                          {"documentId", QString::fromStdString(doc_.identity())},
                                          {"expectedRevision", QString::number(doc_.revision())},
                                          {"query", name},
                                          {field, QString::number(id)}})["data"]
                .toObject();
        };
        values_ = query("component_parameters.values", "body", placement);
        definition_ = values_["definition"].toString().toULongLong();
        description = query("component_parameters.describe", "definition", definition_);
    } catch (const std::exception &) {
        placement_ = 0;
        cacheKey_.clear();
        hide();
        emit measurementChanged();
        return;
    }
    if (placement != placement_)
        amendKey_.clear();
    placement_ = placement;
    nested_ = nested;
    parent_ = parent;
    cacheKey_ = key;
    // describe lists parameters in evaluation order; rows follow the record's display order.
    auto ordered = description["parameters"].toArray().toVariantList();
    std::stable_sort(ordered.begin(), ordered.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["position"].toInt() < b.toMap()["position"].toInt();
    });
    const auto parameters = QJsonArray::fromVariantList(ordered);
    const auto signature =
        QString::number(definition_) + "/" + QString::number(nested) + "/" +
        QString::number(int(doc_.displayUnits())) + "/" + QString::number(doc_.displayPrecision()) +
        "/" + QString::fromUtf8(QJsonDocument(parameters).toJson(QJsonDocument::Compact));
    if (signature != signature_) {
        signature_ = signature;
        rebuild(parameters);
    }
    int overridden = 0;
    for (const auto &entry : values_["values"].toArray()) {
        const auto value = entry.toObject();
        const auto source = value["source"].toString();
        overridden += source == "override";
        if (auto *field = this->field(value["name"].toString()))
            field->setStored(value, source, parent_, doc_.displayUnits(), doc_.displayPrecision(),
                             nested_);
    }
    setDetail(overridden ? QString("%1 overridden").arg(overridden) : QString{});
    message_->hide();
    rows_->show();
    resetAll_->show();
    resetAll_->setEnabled(!nested_ && overridden);
    editDefinition_->show();
    note_->setVisible(!nested_);
    show();
    // The rows' heights changed with their text: the Info tab's stack lays out again.
    relayoutAncestors(this);
    emit measurementChanged();
}
void ComponentParametersSection::rebuild(const QJsonArray &parameters) {
    validation_->stop();
    pending_ = nullptr;
    fields_.clear();
    if (rows_) {
        rows_->hide();
        rows_->deleteLater();
    }
    rows_ = new QWidget;
    rows_->setObjectName("parameterRows");
    rowsLayout_ = new QVBoxLayout(rows_);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(6);
    static_cast<QVBoxLayout *>(body_->layout())->insertWidget(1, rows_);
    QWidget *previous = header();
    for (const auto &entry : parameters) {
        auto *field = new ParameterValueField(entry.toObject());
        rowsLayout_->addWidget(field);
        fields_.push_back(field);
        connect(field, &ParameterValueField::textEdited, this, [this, field] {
            // A parse failure shows at once; a readable value is checked 150 ms after the
            // last edit, so typing never waits on evaluate.
            try {
                field->value();
                field->clearError();
                pending_ = field;
                validation_->start();
            } catch (const std::exception &error) {
                pending_ = nullptr;
                validation_->stop();
                field->setError(QString::fromUtf8(error.what()));
            }
        });
        connect(field, &ParameterValueField::commitRequested, this,
                [this, field] { commit(field); });
        connect(field, &ParameterValueField::resetRequested, this, [this, field] { reset(field); });
        connect(field, &ParameterValueField::focusEntered, this, [this, field] {
            if (!field->quantity() || field->name() == focused_)
                return;
            focused_ = field->name();
            emit measurementChanged();
        });
        // Tab goes field → its reset → next field, after the header.
        connect(field, &ParameterValueField::actionRequested, this,
                [this](int action, qulonglong target, qulonglong definition) {
                    emit view_.parameterAction(action, target, definition);
                });
        QWidget::setTabOrder(previous, field->control());
        QWidget::setTabOrder(field->control(), field->resetButton());
        QWidget::setTabOrder(field->resetButton(), field->actionButton());
        previous = field->actionButton();
    }
    QWidget::setTabOrder(previous, resetAll_);
    QWidget::setTabOrder(resetAll_, editDefinition_);
}
QJsonObject ComponentParametersSection::overrides() const {
    QJsonObject result;
    for (const auto &entry : values_["values"].toArray()) {
        const auto value = entry.toObject();
        if (value["source"].toString() == "override")
            result[value["name"].toString()] = value["value"];
    }
    return result;
}
QJsonObject ComponentParametersSection::evaluate(const QString &name,
                                                 const QJsonValue &value) const {
    auto values = overrides();
    values[name] = value;
    try {
        const auto data =
            inspectDocument(doc_, {{"apiVersion", 1},
                                   {"documentId", QString::fromStdString(doc_.identity())},
                                   {"expectedRevision", QString::number(doc_.revision())},
                                   {"query", "component_parameters.evaluate"},
                                   {"definition", QString::number(definition_)},
                                   {"overrides", values}})["data"]
                .toObject();
        return data["status"].toString() == "error" ? data["error"].toObject() : QJsonObject{};
    } catch (const InspectionError &error) {
        return refusal(error);
    }
}
void ComponentParametersSection::validate() {
    auto *field = pending_.data();
    pending_ = nullptr;
    if (!field || !field->edited() || placement_ == 0)
        return;
    QJsonValue value;
    try {
        value = field->value();
    } catch (const std::exception &error) {
        field->setError(QString::fromUtf8(error.what()));
        return;
    }
    if (field->equalsStored(value)) {
        field->clearError();
        return;
    }
    const auto error = evaluate(field->name(), value);
    if (error.isEmpty())
        field->clearError();
    else
        field->setMessage(message(error, field, field->text(), value));
}
ParameterMessage ComponentParametersSection::message(const QJsonObject &error,
                                                     const ParameterValueField *field,
                                                     const QString &typed,
                                                     const QJsonValue &value) const {
    ParameterMessageContext context;
    context.place = ParameterMessagePlace::Value;
    context.document = &doc_;
    context.units = doc_.displayUnits();
    context.precision = doc_.displayPrecision();
    context.parameter = field->parameter();
    context.typed = typed;
    context.value = value;
    context.placement = placement_;
    context.label = [this](const QString &name) {
        const auto *row = this->field(name);
        return row ? row->label() : name;
    };
    return parameterMessage(error, context);
}
void ComponentParametersSection::publish(const QJsonObject &command, const QString &label,
                                         bool amend) {
    QScopedValueRollback busy(busy_, true);
    const QJsonObject request{{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc_.identity())},
                              {"expectedRevision", QString::number(doc_.revision())},
                              {"commands", QJsonArray{command}},
                              {"history", QJsonObject{{"label", label}}}};
    const auto key =
        QString::number(placement_) + "/" + command["values"].toObject().keys().value(0);
    view_.cancel();
    // The placement is top level, so the batch needs no component scope (§1.1).
    if (amend && amendKey_ == key && doc_.canAmend(amendStamp_))
        executeAmend(doc_, amendStamp_, request);
    else
        executeBatch(doc_, request);
    amendKey_ = amend ? key : QString{};
    amendStamp_ = doc_.amendmentStamp();
    view_.refresh();
    emit view_.changed();
}
ParameterMessage ComponentParametersSection::apply(ParameterValueField *field,
                                                   const QJsonValue &value, const QString &typed,
                                                   bool amend) {
    auto sentence = [&](const QJsonObject &error) { return message(error, field, typed, value); };
    if (const auto error = evaluate(field->name(), value); !error.isEmpty())
        return sentence(error);
    const QJsonObject command{{"command", "component_parameter.override"},
                              {"body", QString::number(placement_)},
                              {"values", QJsonObject{{field->name(), value}}}};
    const bool edited = field->edited();
    QPointer<ParameterValueField> alive(field);
    field->setEdited(false);
    try {
        publish(command, historyLabel("Change", field->label()), amend);
    } catch (const std::exception &error) {
        if (alive)
            alive->setEdited(edited);
        return sentence(parameterRefusal(error));
    }
    if (alive)
        alive->clearError();
    return {};
}
void ComponentParametersSection::commit(ParameterValueField *field) {
    if (busy_ || field->readOnly() || placement_ == 0)
        return;
    validation_->stop();
    pending_ = nullptr;
    QJsonValue value;
    try {
        value = field->value();
    } catch (const std::exception &error) {
        field->setError(QString::fromUtf8(error.what()));
        return;
    }
    // An unchanged value commits nothing and shows the stored text again.
    if (field->equalsStored(value)) {
        field->restore();
        return;
    }
    QPointer<ParameterValueField> alive(field);
    if (const auto refused = apply(field, value, field->text(), false);
        !refused.sentence.isEmpty() && alive)
        alive->setMessage(refused);
}
void ComponentParametersSection::reset(ParameterValueField *field) {
    if (busy_ || field->readOnly() || field->source() != "override" || placement_ == 0)
        return;
    validation_->stop();
    pending_ = nullptr;
    // The reset button leaves with the override; keep focus on the row.
    if (field->resetButton()->hasFocus())
        field->control()->setFocus(Qt::OtherFocusReason);
    QPointer<ParameterValueField> alive(field);
    const bool edited = field->edited();
    field->setEdited(false);
    try {
        publish({{"command", "component_parameter.reset"},
                 {"body", QString::number(placement_)},
                 {"names", QJsonArray{field->name()}}},
                historyLabel("Reset", field->label()), false);
    } catch (const std::exception &error) {
        if (alive) {
            alive->setEdited(edited);
            alive->setMessage(message(parameterRefusal(error), alive, {}, {}));
        }
        return;
    }
    if (alive)
        alive->restore();
}
void ComponentParametersSection::resetAll() {
    if (busy_ || nested_ || placement_ == 0)
        return;
    validation_->stop();
    pending_ = nullptr;
    for (auto *field : fields_)
        field->setEdited(false);
    try {
        publish({{"command", "component_parameter.reset"},
                 {"body", QString::number(placement_)},
                 {"all", true}},
                "Reset all parameters", false);
    } catch (const std::exception &error) {
        ParameterMessageContext context;
        context.document = &doc_;
        context.placement = placement_;
        emit view_.message(parameterMessage(parameterRefusal(error), context).sentence);
        return;
    }
    for (auto *field : fields_)
        field->restore();
}
void ComponentParametersSection::editDefinition() {
    if (placement_ == 0)
        return;
    const auto placement = placement_;
    // A placement with its own values asks first ("… has its own values", ADR 0166 §4.1):
    // openContext offers the request to the window's ParametersScopeDialog.
    view_.openContext(placement);
    view_.setFocus(Qt::OtherFocusReason);
}
ParameterValueField *ComponentParametersSection::routed() const {
    if (placement_ == 0 || nested_)
        return nullptr;
    if (auto *last = field(focused_); last && last->quantity() && !last->readOnly())
        return last;
    for (auto *field : fields_) {
        const auto type = field->type();
        if (!field->derived() &&
            (type == "length" || type == "angle" || type == "number" || type == "count"))
            return field;
    }
    return nullptr;
}
QString ComponentParametersSection::measurementLabel() const {
    const auto *field = routed();
    return field ? field->label() : QString{};
}
QString ComponentParametersSection::measurementValue() const {
    const auto *field = routed();
    return field ? field->storedDisplay() : QString{};
}
bool ComponentParametersSection::measurements(const QString &text, QString &error) {
    auto *field = routed();
    if (!field) {
        error = "Select one placement with parameters";
        return false;
    }
    if (busy_)
        return false;
    QJsonValue value;
    try {
        value = field->parse(text);
    } catch (const std::exception &failure) {
        error = QString::fromUtf8(failure.what());
        return false;
    }
    if (field->equalsStored(value))
        return true;
    error = apply(field, value, text, true).sentence;
    return error.isEmpty();
}
} // namespace sketchy
