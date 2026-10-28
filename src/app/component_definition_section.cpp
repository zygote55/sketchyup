#include "app/component_definition_section.hpp"
#include "app/formula_field.hpp"
#include "app/parameter_value_field.hpp"
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "core/parameter_expression.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariant>
namespace sketchy {
namespace authoring {
Scope scopeOf(const Document &doc, const Viewport &view) {
    const auto placement = view.componentScope();
    if (!placement || !doc.instances().contains(placement))
        return {};
    return {placement, doc.instances().at(placement)->definition};
}
QJsonObject query(const Document &doc, const QString &name, QJsonObject fields) {
    fields["apiVersion"] = 1;
    fields["documentId"] = QString::fromStdString(doc.identity());
    fields["expectedRevision"] = QString::number(doc.revision());
    fields["query"] = name;
    return inspectDocument(doc, fields)["data"].toObject();
}
QJsonObject failure(const std::exception &error) { return parameterRefusal(error); }
namespace {
QJsonObject batch(const Document &doc, const QJsonArray &commands, const QString &label) {
    QJsonObject request{{"apiVersion", 1},
                        {"documentId", QString::fromStdString(doc.identity())},
                        {"expectedRevision", QString::number(doc.revision())},
                        {"commands", commands}};
    if (!label.isEmpty())
        request["history"] = QJsonObject{{"label", label}};
    return request;
}
} // namespace
QJsonObject dryRun(const Document &doc, const QJsonArray &commands, const OpenedCatalogs *opened) {
    try {
        previewBatch(doc, batch(doc, commands, {}), opened);
        return {};
    } catch (const std::exception &error) {
        auto result = failure(error);
        // A batch that changes nothing would commit nothing: not a refusal of the form.
        if (result["message"].toString() == "Batch has no committed changes")
            return {};
        return result;
    }
}
QJsonObject publish(Document &doc, Viewport &view, const QJsonArray &commands, const QString &label,
                    const OpenedCatalogs *opened) {
    view.cancel();
    auto result = executeBatch(doc, batch(doc, commands, label), BatchResponse::Full, opened);
    view.refresh();
    emit view.changed();
    return result;
}
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
Id memberOf(const Document &doc, Id placement, Id scene) {
    const auto found = doc.instances().find(placement);
    if (found == doc.instances().end())
        return 0;
    for (const auto &[member, target] : found->second->members)
        if (target == scene)
            return member;
    return 0;
}
Id sceneOf(const Document &doc, Id placement, Id member) {
    const auto found = doc.instances().find(placement);
    if (found == doc.instances().end())
        return 0;
    const auto target = found->second->members.find(member);
    return target == found->second->members.end() ? 0 : target->second;
}
} // namespace authoring

namespace {
QString typeTitle(const QString &type) {
    if (type.isEmpty())
        return type;
    auto text = type;
    text[0] = text[0].toUpper();
    return text;
}
bool quantityType(const QString &type) {
    return type == "length" || type == "area" || type == "angle" || type == "number" ||
           type == "count";
}
QString boundsText(const QJsonObject &parameter, DisplayUnit units, int precision) {
    const auto type = parameter["type"].toString();
    if (!quantityType(type))
        return {};
    const auto min = parameter["min"], max = parameter["max"];
    auto shown = [&](const QJsonValue &value) {
        return parameterValueText(value.toDouble(), type, units, precision);
    };
    if (min.isDouble() && max.isDouble()) {
        auto low = shown(min);
        const auto high = shown(max);
        const auto space = high.lastIndexOf(' ');
        if (units != DisplayUnit::FeetInches && space > 0 && low.endsWith(high.mid(space)))
            low.chop(high.size() - space);
        return low + "–" + high;
    }
    if (min.isDouble())
        return "at least " + shown(min);
    if (max.isDouble())
        return "at most " + shown(max);
    return {};
}
// "oak = Oak; ash = Ash" ⇄ the options array.
QString optionsText(const QJsonValue &options) {
    QStringList parts;
    for (const auto &entry : options.toArray()) {
        const auto option = entry.toObject();
        parts << option["value"].toString() + " = " + option["label"].toString();
    }
    return parts.join("; ");
}
QJsonArray parseOptions(const QString &text) {
    QJsonArray result;
    for (const auto &part : text.split(';', Qt::SkipEmptyParts)) {
        const auto equals = part.indexOf('=');
        const auto value = (equals < 0 ? part : part.left(equals)).trimmed();
        const auto label = (equals < 0 ? part : part.mid(equals + 1)).trimmed();
        if (value.isEmpty())
            continue;
        result.append(QJsonObject{{"value", value}, {"label", label.isEmpty() ? value : label}});
    }
    if (result.isEmpty())
        throw std::runtime_error("List the choices as value = Label, separated by semicolons.");
    return result;
}
QLineEdit *line(const QString &objectName, const QString &accessible, bool mono = false) {
    auto *edit = new QLineEdit;
    edit->setObjectName(objectName);
    edit->setAccessibleName(accessible);
    edit->setMaxLength(1024);
    if (mono)
        edit->setProperty("parameterField", true);
    return edit;
}
} // namespace

ComponentDefinitionSection::ComponentDefinitionSection(Document &doc, Viewport &view,
                                                       QWidget *parent)
    : CollapsibleSection("Parameters · definition", new QWidget, true, parent), doc_(doc),
      view_(view) {
    setObjectName("componentDefinitionSection");
    add_ = new QPushButton("+ Parameter");
    add_->setObjectName("definitionAddParameter");
    add_->setAccessibleName("Add parameter");
    add_->setAutoDefault(false);
    add_->setToolTip("Add a parameter to this definition");
    addHeaderWidget(add_);
    auto *layout = new QVBoxLayout(content());
    layout->setContentsMargins(8, 4, 2, 8);
    layout->setSpacing(6);
    empty_ = new QLabel("This definition has no parameters yet. + Parameter adds one.");
    empty_->setObjectName("definitionParametersEmpty");
    empty_->setWordWrap(true);
    layout->addWidget(empty_);
    tree_ = new QTreeWidget;
    tree_->setObjectName("definitionParameters");
    tree_->setAccessibleName("Definition parameters");
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({"Name", "Type", "Default or formula"});
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(false);
    // Rows hold two lines at most; a narrow Tray elides a cell and its tooltip has the text.
    tree_->setWordWrap(false);
    tree_->setTextElideMode(Qt::ElideRight);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    tree_->header()->setStretchLastSection(true);
    tree_->header()->setSectionResizeMode(QHeaderView::Stretch);
    tree_->installEventFilter(this);
    layout->addWidget(tree_);

    editor_ = new QFrame;
    editor_->setObjectName("parameterEditor");
    auto *editorLayout = new QVBoxLayout(editor_);
    editorLayout->setContentsMargins(6, 6, 6, 6);
    editorLayout->setSpacing(6);
    editorTitle_ = new QLabel;
    editorTitle_->setObjectName("parameterEditorTitle");
    editorTitle_->setTextFormat(Qt::PlainText);
    editorLayout->addWidget(editorTitle_);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setContentsMargins(0, 0, 0, 0);
    label_ = line("parameterEditorLabel", "Label");
    form->addRow("Label", label_);
    name_ = line("parameterEditorName", "Name", true);
    name_->setMaxLength(32);
    form->addRow("Name", name_);
    type_ = new QComboBox;
    type_->setObjectName("parameterEditorType");
    type_->setAccessibleName("Type");
    for (const auto *type : {"length", "area", "angle", "number", "count", "boolean", "choice"})
        type_->addItem(typeTitle(type), QString(type));
    form->addRow("Type", type_);
    typeRow_ = type_;
    mode_ = new QComboBox;
    mode_->setObjectName("parameterEditorMode");
    mode_->setAccessibleName("Mode");
    mode_->addItem("Input", "input");
    mode_->addItem("Derived", "derived");
    form->addRow("Mode", mode_);
    modeRow_ = mode_;
    expression_ = new FormulaField;
    expression_->setObjectName("parameterEditorExpression");
    expression_->edit()->setObjectName("parameterEditorExpressionEdit");
    expressionCaption_ = new QLabel("Default");
    form->addRow(expressionCaption_, expression_);
    auto *bounds = new QWidget;
    auto *boundsLayout = new QHBoxLayout(bounds);
    boundsLayout->setContentsMargins(0, 0, 0, 0);
    min_ = line("parameterEditorMin", "Minimum", true);
    max_ = line("parameterEditorMax", "Maximum", true);
    min_->setPlaceholderText("none");
    max_->setPlaceholderText("none");
    boundsLayout->addWidget(min_);
    boundsLayout->addWidget(max_);
    form->addRow("Bounds", bounds);
    boundsRow_ = bounds;
    step_ = line("parameterEditorStep", "Step", true);
    step_->setPlaceholderText("none");
    form->addRow("Step", step_);
    stepRow_ = step_;
    options_ = line("parameterEditorOptions", "Options");
    options_->setPlaceholderText("oak = Oak; ash = Ash");
    form->addRow("Options", options_);
    optionsRow_ = options_;
    usedBy_ = new QLabel;
    usedBy_->setObjectName("parameterEditorUsedBy");
    usedBy_->setWordWrap(true);
    usedBy_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    usedBy_->setFocusPolicy(Qt::TabFocus);
    usedBy_->setAccessibleName("Used by");
    form->addRow("Used by", usedBy_);
    usedRow_ = usedBy_;
    editorLayout->addLayout(form);
    error_ = new ParameterMessageLabel;
    error_->setObjectName("parameterEditorError");
    error_->hide();
    error_->installEventFilter(this);
    editorLayout->addWidget(error_);
    // The message's action (ADR 0166 §5): "Select #1" for a locked placement, "Show the driver".
    action_ = new QPushButton;
    action_->setObjectName("parameterEditorAction");
    action_->setAutoDefault(false);
    action_->hide();
    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->addWidget(action_);
    actionRow->addStretch();
    editorLayout->addLayout(actionRow);
    connect(action_, &QPushButton::clicked, this, [this] {
        emit view_.parameterAction(int(message_.action), message_.target, message_.definition);
    });
    note_ = new QLabel(
        "Changing the default re-baselines the shape and keeps each placement's own values.");
    note_->setObjectName("parameterEditorNote");
    note_->setWordWrap(true);
    editorLayout->addWidget(note_);
    editor_->hide();
    layout->addWidget(editor_);

    for (QWidget *input : std::initializer_list<QWidget *>{label_, name_, type_, mode_, min_, max_,
                                                           step_, options_})
        input->installEventFilter(this);
    expression_->setLabels([this](const QString &name) { return labelOf(name); });
    expression_->setDocument(&doc_);
    connect(expression_, &FormulaField::actionRequested, this,
            [this](int action, qulonglong target, qulonglong definition) {
                emit view_.parameterAction(action, target, definition);
            });
    expression_->setChecks([this](const QString &text) { return checkExpression(text); },
                           [this](const QString &text) { return dryRunExpression(text); });
    expression_->setSentence([this](const QJsonObject &error, const QString &) -> QString {
        const auto details = error.contains("details") ? error["details"].toObject() : error;
        return details.contains("bound") ? sentence(error) : QString{};
    });
    connect(expression_, &FormulaField::commitRequested, this, [this] { commit(); });
    connect(expression_, &FormulaField::escaped, this, [this] { cancel(); });
    connect(add_, &QPushButton::clicked, this, [this] { beginCreate(); });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (syncing_ || !item)
            return;
        creating_ = false;
        current_ = item->data(0, Qt::UserRole).toString();
        resetEdits();
        showEditor();
    });
    connect(type_, &QComboBox::currentIndexChanged, this, [this] {
        if (!syncing_)
            syncEditorRows();
    });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
        if (!syncing_)
            syncEditorRows();
    });
    // Tab: header → + Parameter → tree → the editor's fields in order.
    QWidget::setTabOrder(add_, tree_);
    QWidget *previous = tree_;
    for (QWidget *input : std::initializer_list<QWidget *>{label_, name_, type_, mode_,
                                                           expression_->edit(),
                                                           expression_->fixButton(), min_, max_,
                                                           step_, options_, usedBy_}) {
        QWidget::setTabOrder(previous, input);
        previous = input;
    }
    hide();
}
void ComponentDefinitionSection::detach() {
    detached_ = true;
    scope_ = {};
    hide();
}
QString ComponentDefinitionSection::labelOf(const QString &name) const {
    const auto parameter = parameterNamed(name);
    const auto label = parameter["label"].toString();
    return label.isEmpty() ? name : label;
}
QJsonObject ComponentDefinitionSection::parameterNamed(const QString &name) const {
    for (const auto &entry : description_["parameters"].toArray())
        if (entry.toObject()["name"].toString() == name)
            return entry.toObject();
    return {};
}
void ComponentDefinitionSection::refresh() {
    if (detached_)
        return;
    const auto scope = authoring::scopeOf(doc_, view_);
    if (!scope) {
        if (scope_) {
            scope_ = {};
            description_ = {};
            cacheKey_.clear();
            signature_.clear();
            current_.clear();
            creating_ = false;
            editor_->hide();
        }
        hide();
        return;
    }
    const auto key = QString::fromStdString(doc_.identity()) + "/" +
                     QString::number(doc_.revision()) + "/" + QString::number(scope.placement) +
                     "/" + QString::number(int(doc_.displayUnits())) + "/" +
                     QString::number(doc_.displayPrecision());
    if (key == cacheKey_) {
        show();
        return;
    }
    if (scope.definition != scope_.definition) {
        current_.clear();
        creating_ = false;
        editor_->hide();
    }
    try {
        description_ = authoring::query(doc_, "component_parameters.describe",
                                        {{"definition", QString::number(scope.definition)}});
    } catch (const std::exception &) {
        scope_ = {};
        cacheKey_.clear();
        hide();
        return;
    }
    scope_ = scope;
    cacheKey_ = key;
    const auto signature =
        QString::fromUtf8(QJsonDocument(description_["parameters"].toArray()).toJson()) +
        QString::number(int(doc_.displayUnits())) + "/" + QString::number(doc_.displayPrecision());
    if (signature != signature_) {
        signature_ = signature;
        rebuild();
    }
    if (!creating_ && !current_.isEmpty() && parameterNamed(current_).isEmpty()) {
        current_.clear();
        editor_->hide();
    }
    if (!creating_ && !current_.isEmpty())
        showEditor();
    show();
    relayoutAncestors(this);
}
void ComponentDefinitionSection::rebuild() {
    QScopedValueRollback syncing(syncing_, true);
    tree_->clear();
    auto parameters = description_["parameters"].toArray().toVariantList();
    std::stable_sort(parameters.begin(), parameters.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["position"].toInt() < b.toMap()["position"].toInt();
    });
    QFont mono = font();
    mono.setFamily("DejaVu Sans Mono");
    QTreeWidgetItem *selected = nullptr;
    for (const auto &value : parameters) {
        const auto parameter = QJsonObject::fromVariantMap(value.toMap());
        const auto name = parameter["name"].toString();
        const auto label = parameter["label"].toString();
        const bool derived = parameter["mode"].toString() == "derived";
        const auto bounds = boundsText(parameter, doc_.displayUnits(), doc_.displayPrecision());
        const auto type = typeTitle(parameter["type"].toString());
        const auto detail = derived ? QString("derived") : bounds;
        const auto shown = derived ? "= " + parameter["expression"].toString()
                                   : parameter["display"].toString();
        auto *item = new QTreeWidgetItem(
            tree_, {label + "\n" + name, detail.isEmpty() ? type : type + "\n" + detail, shown});
        item->setData(0, Qt::UserRole, name);
        item->setFont(2, mono);
        // A row reads its label, name, type, bounds and default or formula (ADR 0166 §8).
        const auto spoken = QStringList{label, name, type, detail, shown}.filter(QRegularExpression(".")).join(", ");
        for (int column = 0; column < 3; ++column) {
            item->setData(column, Qt::AccessibleTextRole,
                          column == 0 ? spoken : item->text(column).replace('\n', ", "));
            item->setToolTip(column, item->text(column));
        }
        if (name == current_)
            selected = item;
    }
    if (selected)
        tree_->setCurrentItem(selected);
    empty_->setVisible(parameters.isEmpty());
    tree_->setVisible(!parameters.isEmpty());
    tree_->updateGeometry();
}
void ComponentDefinitionSection::resetEdits() {
    for (auto *edit : {label_, name_, min_, max_, step_, options_})
        edit->setModified(false);
    expression_->setStored(expression_->stored(), true);
}
void ComponentDefinitionSection::select(const QString &name) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        if (tree_->topLevelItem(i)->data(0, Qt::UserRole).toString() == name) {
            creating_ = false;
            current_ = name;
            resetEdits();
            {
                QScopedValueRollback syncing(syncing_, true);
                tree_->setCurrentItem(tree_->topLevelItem(i));
            }
            showEditor();
            return;
        }
}
void ComponentDefinitionSection::beginCreate() {
    if (!scope_)
        return;
    setExpanded(true);
    creating_ = true;
    current_.clear();
    {
        QScopedValueRollback syncing(syncing_, true);
        tree_->setCurrentItem(nullptr);
        tree_->clearSelection();
    }
    showEditor();
    label_->setFocus(Qt::OtherFocusReason);
}
QString ComponentDefinitionSection::editorType() const { return type_->currentData().toString(); }
bool ComponentDefinitionSection::editorDerived() const {
    return mode_->currentData().toString() == "derived";
}
void ComponentDefinitionSection::showEditor() {
    QScopedValueRollback syncing(syncing_, true);
    error_->hide();
    error_->clear();
    const auto units = doc_.displayUnits();
    const auto precision = doc_.displayPrecision();
    if (creating_) {
        editorTitle_->setText("New parameter");
        // setText, not clear(): clear() is an edit and would mark the fields modified.
        for (auto *edit : {label_, name_, min_, max_, step_, options_}) {
            edit->setText({});
            edit->setModified(false);
        }
        type_->setCurrentIndex(0);
        mode_->setCurrentIndex(0);
        expression_->setStored({}, true);
        usedBy_->clear();
    } else {
        const auto parameter = parameterNamed(current_);
        if (parameter.isEmpty()) {
            editor_->hide();
            return;
        }
        editorTitle_->setText(labelOf(current_));
        const auto type = parameter["type"].toString();
        if (!label_->isModified())
            label_->setText(parameter["label"].toString());
        if (!name_->isModified())
            name_->setText(current_);
        type_->setCurrentIndex(type_->findData(type));
        mode_->setCurrentIndex(mode_->findData(parameter["mode"].toString()));
        expression_->setStored(parameter["expression"].toString());
        const auto bound = [&](QLineEdit *edit, const QJsonValue &value) {
            if (edit->isModified())
                return;
            edit->setText(value.isDouble() ? parameterValueText(value.toDouble(), type, units,
                                                                precision)
                                           : QString{});
            edit->setCursorPosition(0);
        };
        bound(min_, parameter["min"]);
        bound(max_, parameter["max"]);
        bound(step_, parameter["step"]);
        if (!options_->isModified())
            options_->setText(optionsText(parameter["options"]));
        QStringList users;
        for (const auto &entry : parameter["usedBy"].toArray()) {
            const auto user = entry.toString();
            if (user.startsWith("parameter "))
                users << labelOf(user.mid(10));
            else if (user.startsWith("driver ")) {
                const auto id = user.mid(7);
                QString text = "Driver #" + id;
                for (const auto &value : description_["drivers"].toArray()) {
                    const auto driver = value.toObject();
                    if (driver["id"].toString() != id)
                        continue;
                    auto kind = driver["kind"].toString();
                    kind = kind == "childParameter" ? "Child parameter" : typeTitle(kind);
                    const auto member = driver["member"].toString().toULongLong();
                    const auto &definition = doc_.definitions().at(scope_.definition);
                    const auto found = definition->members.find(member);
                    text = kind + " " + (found != definition->members.end()
                                             ? QString::fromStdString(found->second->name)
                                             : "#" + QString::number(member));
                }
                users << text;
            }
        }
        usedBy_->setText(users.isEmpty() ? QString("Nothing yet") : users.join(" · "));
    }
    syncEditorRows();
    editor_->show();
    relayoutAncestors(this);
}
void ComponentDefinitionSection::syncEditorRows() {
    auto *form = editor_->findChild<QFormLayout *>();
    const auto type = editorType();
    const bool derived = editorDerived();
    form->setRowVisible(typeRow_, creating_);
    form->setRowVisible(modeRow_, creating_);
    form->setRowVisible(boundsRow_, quantityType(type) && !derived);
    form->setRowVisible(stepRow_, quantityType(type) && !derived);
    form->setRowVisible(optionsRow_, type == "choice");
    form->setRowVisible(usedRow_, !creating_);
    expressionCaption_->setText(derived ? "Formula" : "Default");
    expression_->setAccessibleName(derived ? "Formula" : "Default");
    note_->setVisible(!creating_ && !derived);
    // The unit fix appends the document's length unit, or degrees for an angle.
    const auto length = doc_.displayUnits() == DisplayUnit::Millimeters  ? "mm"
                        : doc_.displayUnits() == DisplayUnit::FeetInches ? "in"
                                                                         : "m";
    expression_->setFixUnits(type == "angle" ? QStringList{"deg", length}
                                             : QStringList{length, "deg"});
}
QJsonObject ComponentDefinitionSection::checkExpression(const QString &text) const {
    if (!scope_)
        return {};
    const auto type = editorType();
    // A new choice's options exist only once it is defined: the dry run checks its default.
    if (type == "choice")
        return {};
    try {
        const auto data =
            authoring::query(doc_, "component_parameters.evaluate",
                             {{"definition", QString::number(scope_.definition)},
                              {"expression", text},
                              {"type", type}});
        return data["status"].toString() == "error" ? data["error"].toObject() : QJsonObject{};
    } catch (const std::exception &error) {
        return authoring::failure(error);
    }
}
QJsonObject ComponentDefinitionSection::dryRunExpression(const QString &) const {
    QString label, problem;
    const auto commands = editorCommands(label, problem);
    if (!problem.isEmpty() || commands.isEmpty())
        return {};
    return authoring::dryRun(doc_, commands);
}
QJsonArray ComponentDefinitionSection::editorCommands(QString &historyLabel,
                                                      QString &problem) const {
    const auto definition = QString::number(scope_.definition);
    const auto type = editorType();
    const bool derived = editorDerived();
    const auto units = doc_.displayUnits();
    const auto label = label_->text().trimmed();
    const auto name = name_->text().trimmed();
    if (label.isEmpty()) {
        problem = "Give the parameter a label.";
        return {};
    }
    if (!isValidParameterName(name.toStdString())) {
        problem = isReservedParameterWord(name.toStdString())
                      ? name + " is a reserved word."
                      : "Use lowercase letters, digits and _, starting with a letter.";
        return {};
    }
    if ((creating_ || name != current_) && !parameterNamed(name).isEmpty()) {
        problem = name + " is already a parameter.";
        return {};
    }
    if (expression_->text().trimmed().isEmpty()) {
        problem = derived ? "Type a formula." : "Type a default value.";
        return {};
    }
    QJsonObject fields;
    auto bound = [&](QLineEdit *edit, const char *key, const QJsonValue &stored) {
        const auto text = edit->text().trimmed();
        if (text.isEmpty()) {
            if (!creating_ && stored.isDouble())
                fields[key] = QJsonValue(QJsonValue::Null);
            return;
        }
        const auto value = parseParameterText(text, type, units);
        if (creating_ || !stored.isDouble() ||
            std::abs(value.toDouble() - stored.toDouble()) >
                1e-12 * std::max(1., std::abs(stored.toDouble())))
            fields[key] = value;
    };
    const auto stored = creating_ ? QJsonObject{} : parameterNamed(current_);
    try {
        if (quantityType(type) && !derived) {
            bound(min_, "min", stored["min"]);
            bound(max_, "max", stored["max"]);
            bound(step_, "step", stored["step"]);
        }
        if (type == "choice") {
            const auto options = parseOptions(options_->text());
            if (creating_ || options != stored["options"].toArray())
                fields["options"] = options;
        }
    } catch (const std::exception &error) {
        problem = QString::fromUtf8(error.what());
        return {};
    }
    if (creating_) {
        QJsonObject define{{"command", "component_parameter.define"},
                           {"definition", definition},
                           {"name", name},
                           {"label", label},
                           {"type", type},
                           {"mode", derived ? "derived" : "input"},
                           {"expression", expression_->text().trimmed()}};
        for (auto it = fields.begin(); it != fields.end(); ++it)
            define[it.key()] = it.value();
        historyLabel = authoring::historyLabel("Add parameter", label);
        return {define};
    }
    QJsonObject update{{"command", "component_parameter.update"},
                       {"definition", definition},
                       {"name", current_}};
    if (label != stored["label"].toString())
        update["label"] = label;
    if (expression_->text().trimmed() != stored["expression"].toString())
        update["expression"] = expression_->text().trimmed();
    for (auto it = fields.begin(); it != fields.end(); ++it)
        update[it.key()] = it.value();
    QJsonArray commands;
    const bool edits = update.size() > 3;
    if (edits)
        commands.append(update);
    if (name != current_)
        commands.append(QJsonObject{{"command", "component_parameter.rename"},
                                    {"definition", definition},
                                    {"name", current_},
                                    {"newName", name}});
    historyLabel = edits ? authoring::historyLabel("Edit parameter", stored["label"].toString())
                         : authoring::historyLabel("Rename parameter", current_);
    return commands;
}
ParameterMessage ComponentDefinitionSection::message(const QJsonObject &error) const {
    const auto details = error.contains("details") ? error["details"].toObject() : error;
    ParameterMessageContext context;
    context.place = ParameterMessagePlace::Definition;
    context.document = &doc_;
    context.units = doc_.displayUnits();
    context.precision = doc_.displayPrecision();
    context.formula = expression_->text();
    context.placement = scope_.placement;
    context.label = [this](const QString &name) { return labelOf(name); };
    // The parameter the error names, else the one the editor shows (bounds and locked
    // placements name it).
    auto parameter = parameterNamed(details["parameter"].toString());
    if (parameter.isEmpty() && !current_.isEmpty())
        parameter = parameterNamed(current_);
    if (parameter.isEmpty())
        parameter = {{"type", editorType()}};
    context.parameter = parameter;
    return parameterMessage(error, context);
}
QString ComponentDefinitionSection::sentence(const QJsonObject &error) const {
    return message(error).sentence;
}
QString ComponentDefinitionSection::present(const QJsonObject &error) {
    const auto details = error.contains("details") ? error["details"].toObject() : error;
    // An expression error belongs to the formula field; the rest to the editor's line.
    if (details.contains("offset") || error["code"].toString() == "PARAMETER_CYCLE") {
        expression_->showError(error);
        error_->hide();
        return expression_->error();
    }
    message_ = message(error);
    const auto &text = message_.sentence;
    static_cast<ParameterMessageLabel *>(error_)->setTone(message_.tone);
    error_->setText(text);
    error_->setToolTip(error["message"].toString());
    error_->setVisible(!text.isEmpty());
    action_->setText(message_.actionText);
    action_->setVisible(!text.isEmpty() &&
                        (message_.action == ParameterMessageAction::SelectLocked ||
                         message_.action == ParameterMessageAction::ShowDriver));
    return text;
}
QString ComponentDefinitionSection::commit() {
    if (busy_ || !scope_ || (!creating_ && current_.isEmpty()))
        return {};
    QString label, problem;
    const auto commands = editorCommands(label, problem);
    if (!problem.isEmpty()) {
        static_cast<ParameterMessageLabel *>(error_)->setTone(ParameterMessageTone::Failure);
        action_->hide();
        error_->setText(problem);
        error_->setToolTip({});
        error_->show();
        return problem;
    }
    error_->hide();
    if (commands.isEmpty()) {
        cancel();
        return {};
    }
    if (const auto refusal = authoring::dryRun(doc_, commands); !refusal.isEmpty())
        return present(refusal);
    const auto name = name_->text().trimmed();
    QScopedValueRollback busy(busy_, true);
    try {
        authoring::publish(doc_, view_, commands, label);
    } catch (const std::exception &error) {
        return present(authoring::failure(error));
    }
    creating_ = false;
    current_ = name;
    resetEdits();
    refresh();
    select(name);
    // After commit the field shows the engine's canonical text (ADR 0166 §3.5).
    expression_->setStored(parameterNamed(name)["expression"].toString(), true);
    return {};
}
void ComponentDefinitionSection::cancel() {
    error_->hide();
    if (creating_) {
        creating_ = false;
        editor_->hide();
        add_->setFocus(Qt::OtherFocusReason);
        relayoutAncestors(this);
        return;
    }
    resetEdits();
    expression_->restore();
    showEditor();
}
QString ComponentDefinitionSection::removeCurrent() {
    if (busy_ || !scope_ || current_.isEmpty())
        return {};
    const QJsonArray commands{QJsonObject{{"command", "component_parameter.remove"},
                                          {"definition", QString::number(scope_.definition)},
                                          {"name", current_}}};
    if (const auto refusal = authoring::dryRun(doc_, commands); !refusal.isEmpty()) {
        editor_->show();
        return present(refusal);
    }
    QScopedValueRollback busy(busy_, true);
    const auto label = labelOf(current_);
    try {
        authoring::publish(doc_, view_, commands, authoring::historyLabel("Remove parameter", label));
    } catch (const std::exception &error) {
        return present(authoring::failure(error));
    }
    current_.clear();
    editor_->hide();
    refresh();
    tree_->setFocus(Qt::OtherFocusReason);
    return {};
}
QString ComponentDefinitionSection::move(int delta) {
    if (busy_ || !scope_ || current_.isEmpty())
        return {};
    const auto parameter = parameterNamed(current_);
    const int to = parameter["position"].toInt() + delta;
    if (to < 0 || to >= int(description_["parameters"].toArray().size()))
        return {};
    const QJsonArray commands{QJsonObject{{"command", "component_parameter.update"},
                                          {"definition", QString::number(scope_.definition)},
                                          {"name", current_},
                                          {"position", to}}};
    QScopedValueRollback busy(busy_, true);
    try {
        authoring::publish(doc_, view_, commands,
                           authoring::historyLabel("Move parameter", labelOf(current_)));
    } catch (const std::exception &error) {
        return present(authoring::failure(error));
    }
    refresh();
    select(current_);
    tree_->setFocus(Qt::OtherFocusReason);
    return {};
}
bool ComponentDefinitionSection::eventFilter(QObject *object, QEvent *event) {
    // The action belongs to the message: it goes when the line goes.
    if (object == error_ && event->type() == QEvent::Hide)
        action_->hide();
    const bool press = event->type() == QEvent::KeyPress;
    if (!press && event->type() != QEvent::ShortcutOverride)
        return CollapsibleSection::eventFilter(object, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
    if (object == tree_) {
        const bool alt = modifiers == Qt::AltModifier;
        const bool plain = modifiers == Qt::NoModifier;
        const bool ours = (plain && (key->key() == Qt::Key_F2 || key->key() == Qt::Key_Delete ||
                                     key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) ||
                          (alt && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down));
        if (!ours || current_.isEmpty())
            return CollapsibleSection::eventFilter(object, event);
        // The tree's keys win over the panel's F2 (Edit entity) and window shortcuts.
        event->accept();
        if (!press)
            return true;
        if (key->key() == Qt::Key_F2) {
            name_->setFocus(Qt::ShortcutFocusReason);
            name_->selectAll();
        } else if (key->key() == Qt::Key_Delete)
            removeCurrent();
        else if (key->key() == Qt::Key_Up)
            move(-1);
        else if (key->key() == Qt::Key_Down)
            move(1);
        else
            label_->setFocus(Qt::OtherFocusReason);
        return true;
    }
    if (modifiers == Qt::NoModifier &&
        (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
         key->key() == Qt::Key_Escape)) {
        // A combo box's open list keeps its own keys.
        if (auto *combo = qobject_cast<QComboBox *>(object); combo && combo->view()->isVisible())
            return CollapsibleSection::eventFilter(object, event);
        event->accept();
        if (press) {
            if (key->key() == Qt::Key_Escape)
                cancel();
            else
                commit();
        }
        return true;
    }
    return CollapsibleSection::eventFilter(object, event);
}
} // namespace sketchy
