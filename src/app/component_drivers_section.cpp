#include "app/component_drivers_section.hpp"
#include "app/formula_field.hpp"
#include "app/parameter_value_field.hpp"
#include "core/component_parameter_records.hpp"
#include "geometry/surface.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>
namespace sketchy {
namespace {
// The kind's expected value type for component_parameters.evaluate (ADR 0166 §3.3).
QString expectedType(const QString &kind) {
    if (kind == "rotation")
        return "angle";
    if (kind == "visibility")
        return "boolean";
    if (kind == "repeat")
        return "count";
    return "length";
}
// The axis colours (x red, y green, z blue) as swatches beside the letter, so colour is never
// the only carrier. Each has at least 3:1 contrast on both themes' fields.
QIcon axisSwatch(int axis) {
    static const QColor colors[]{QColor("#d23c3c"), QColor("#2e8b4e"), QColor("#3f74d8")};
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors[axis]);
    painter.drawRoundedRect(QRectF(1, 1, 10, 10), 2, 2);
    return QIcon(pixmap);
}
QString axisName(int axis) { return axis == 0 ? "x" : axis == 1 ? "y" : "z"; }
int axisIndex(const QString &name) { return name == "y" ? 1 : name == "z" ? 2 : 0; }
// Applies the inverse transpose of `transform` to `normal` (how a normal changes frame).
Vec3 normalThrough(const Transform &transform, Vec3 normal) {
    const auto inverse = transform.inverse();
    const auto &m = inverse.m;
    return normalized({m[0] * normal.x + m[1] * normal.y + m[2] * normal.z,
                       m[4] * normal.x + m[5] * normal.y + m[6] * normal.z,
                       m[8] * normal.x + m[9] * normal.y + m[10] * normal.z});
}
QLabel *formLabel(const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    return label;
}
} // namespace

QString ComponentDriversSection::kindTitle(const QString &kind) {
    if (kind == "childParameter")
        return "Child parameter";
    if (kind.isEmpty())
        return kind;
    auto text = kind;
    text[0] = text[0].toUpper();
    return text;
}
QStringList ComponentDriversSection::kindsFor(const Document &doc, Id id, Id member, QString *why) {
    const auto found = doc.definitions().find(id);
    auto refuse = [&](const QString &reason) {
        if (why)
            *why = reason;
        return QStringList{};
    };
    if (found == doc.definitions().end() || !member)
        return refuse("Select a part inside the definition, then Shift+D");
    const auto &definition = *found->second;
    if (member == definition.root)
        return refuse("The definition itself can't be driven; select a part inside it, then "
                      "Shift+D");
    // Repeat copies are generated: they are placement members, not definition members.
    if (!definition.members.contains(member))
        return refuse("A repeat copy can't be driven; select the part it copies, then Shift+D");
    const auto &body = *definition.members.at(member);
    QStringList kinds;
    const auto reference = definition.references.find(member);
    if (reference != definition.references.end()) {
        kinds = {"position", "rotation", "visibility", "repeat"};
        const auto child = doc.definitions().find(reference->second);
        if (child != doc.definitions().end() && child->second->parameters &&
            std::any_of(child->second->parameters->parameters.begin(),
                        child->second->parameters->parameters.end(), [](const auto &parameter) {
                            return parameter.mode == ComponentParameterMode::Input;
                        }))
            kinds << "childParameter";
    } else if (body.kind != BodyKind::Geometry || body.textSource || body.referenceImage) {
        kinds = {"position", "rotation", "visibility", "repeat"};
    } else {
        kinds = {"stretch", "position", "rotation", "visibility", "repeat"};
    }
    // The glue member and its ancestors keep the glue face on its wall: no rotation.
    if (definition.glue)
        for (auto chain = definition.glue->member;
             chain && chain != definition.root && definition.members.contains(chain);
             chain = definition.members.at(chain)->parent)
            if (chain == member) {
                kinds.removeAll("rotation");
                break;
            }
    if (kinds.isEmpty())
        return refuse("This part can't be driven");
    if (why)
        why->clear();
    return kinds;
}

ComponentDriversSection::ComponentDriversSection(Document &doc, Viewport &view, QWidget *parent)
    : CollapsibleSection("Drivers", new QWidget, true, parent), doc_(doc), view_(view) {
    setObjectName("componentDriversSection");
    add_ = new QPushButton("+ Driver");
    add_->setObjectName("driversAddDriver");
    add_->setAccessibleName("Add driver");
    add_->setAutoDefault(false);
    add_->setToolTip("Add a driver to the selected part (Shift+D)");
    addHeaderWidget(add_);
    auto *layout = new QVBoxLayout(content());
    layout->setContentsMargins(8, 4, 2, 8);
    layout->setSpacing(6);
    empty_ = new QLabel("No drivers yet. Select a part, then Shift+D adds a driver to it.");
    empty_->setObjectName("driversEmpty");
    empty_->setWordWrap(true);
    layout->addWidget(empty_);
    tree_ = new QTreeWidget;
    tree_->setObjectName("componentDrivers");
    tree_->setAccessibleName("Drivers");
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({"Kind", "Target", "Formula", "State"});
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(false);
    // Rows hold two lines at most; a narrow Tray elides a cell and its tooltip has the text.
    tree_->setWordWrap(false);
    tree_->setTextElideMode(Qt::ElideRight);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    tree_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    tree_->header()->setSectionResizeMode(QHeaderView::Stretch);
    tree_->installEventFilter(this);
    layout->addWidget(tree_);
    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    rebind_ = new QPushButton("Rebind");
    rebind_->setObjectName("driversRebind");
    rebind_->setAutoDefault(false);
    rebind_->setToolTip("Accept the selected drivers' changed members as their starting point");
    rebindAll_ = new QPushButton("Rebind all");
    rebindAll_->setObjectName("driversRebindAll");
    rebindAll_->setAutoDefault(false);
    rebindAll_->setToolTip("Accept every changed member as its driver's starting point");
    buttons->addWidget(rebind_);
    buttons->addWidget(rebindAll_);
    buttons->addStretch();
    layout->addLayout(buttons);
    changedNote_ = new QLabel;
    changedNote_->setObjectName("driverChangedNote");
    changedNote_->setWordWrap(true);
    changedNote_->hide();
    layout->addWidget(changedNote_);

    editor_ = new QFrame;
    editor_->setObjectName("driverEditor");
    auto *editorLayout = new QVBoxLayout(editor_);
    editorLayout->setContentsMargins(6, 6, 6, 6);
    editorLayout->setSpacing(6);
    editorTitle_ = new QLabel;
    editorTitle_->setObjectName("driverEditorTitle");
    editorTitle_->setTextFormat(Qt::PlainText);
    editorTitle_->setWordWrap(true);
    editorLayout->addWidget(editorTitle_);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setContentsMargins(0, 0, 0, 0);
    kind_ = new QComboBox;
    kind_->setObjectName("driverKind");
    kind_->setAccessibleName("Kind");
    form->addRow(formLabel("Kind"), kind_);
    kindRow_ = kind_;
    axis_ = new QComboBox;
    axis_->setObjectName("driverAxis");
    axis_->setAccessibleName("Axis");
    form->addRow(formLabel("Axis"), axis_);
    axisRow_ = axis_;
    formula_ = new FormulaField;
    formula_->setObjectName("driverFormula");
    formula_->edit()->setObjectName("driverFormulaEdit");
    formula_->setAccessibleName("Formula");
    form->addRow(formLabel("Formula"), formula_);
    auto *planeBox = new QWidget;
    auto *planeLayout = new QVBoxLayout(planeBox);
    planeLayout->setContentsMargins(0, 0, 0, 0);
    planeLayout->setSpacing(2);
    plane_ = new QLineEdit;
    plane_->setObjectName("driverPlane");
    plane_->setAccessibleName("Plane at");
    plane_->setProperty("parameterField", true);
    plane_->setToolTip("The stretch plane's offset along the axis, in the part's own coordinates");
    planeLayout->addWidget(plane_);
    moving_ = new QLabel;
    moving_->setObjectName("driverMoving");
    moving_->setWordWrap(true);
    planeLayout->addWidget(moving_);
    form->addRow(formLabel("Plane at"), planeBox);
    planeRow_ = planeBox;
    spacing_ = new FormulaField;
    spacing_->setObjectName("driverSpacing");
    spacing_->edit()->setObjectName("driverSpacingEdit");
    spacing_->setAccessibleName("Spacing");
    form->addRow(formLabel("Spacing"), spacing_);
    spacingRow_ = spacing_;
    maxCount_ = new QSpinBox;
    maxCount_->setObjectName("driverMaxCount");
    maxCount_->setAccessibleName("Max count");
    maxCount_->setRange(1, 1000);
    form->addRow(formLabel("Max count"), maxCount_);
    maxRow_ = maxCount_;
    child_ = new QComboBox;
    child_->setObjectName("driverChildParameter");
    child_->setAccessibleName("Child parameter");
    form->addRow(formLabel("Child parameter"), child_);
    childRow_ = child_;
    editorLayout->addLayout(form);
    error_ = new ParameterMessageLabel;
    error_->setObjectName("driverEditorError");
    error_->hide();
    error_->installEventFilter(this);
    editorLayout->addWidget(error_);
    auto *commitRow = new QHBoxLayout;
    commit_ = new QPushButton("Add");
    commit_->setObjectName("driverCommit");
    commit_->setAutoDefault(false);
    commitRow->addWidget(commit_);
    // The message's action (ADR 0166 §5): "Select #1" for a locked placement.
    action_ = new QPushButton;
    action_->setObjectName("driverEditorAction");
    action_->setAutoDefault(false);
    action_->hide();
    commitRow->addWidget(action_);
    connect(action_, &QPushButton::clicked, this, [this] {
        emit view_.parameterAction(int(message_.action), message_.target, message_.definition);
    });
    for (auto *field : {formula_, spacing_}) {
        field->setDocument(&doc_);
        connect(field, &FormulaField::actionRequested, this,
                [this](int action, qulonglong target, qulonglong definition) {
                    emit view_.parameterAction(action, target, definition);
                });
    }
    commitRow->addStretch();
    editorLayout->addLayout(commitRow);
    editor_->hide();
    layout->addWidget(editor_);

    check_ = new QTimer(this);
    check_->setSingleShot(true);
    check_->setInterval(150);
    connect(check_, &QTimer::timeout, this, [this] { checkForm(); });
    for (QWidget *input : std::initializer_list<QWidget *>{kind_, axis_, plane_, maxCount_, child_})
        input->installEventFilter(this);
    for (auto *field : {formula_, spacing_}) {
        const bool spacing = field == spacing_;
        field->setLabels([this](const QString &name) { return labelOf(name); });
        field->setChecks(
            [this, spacing](const QString &text) { return checkFormula(text, spacing); },
            [this, spacing](const QString &text) -> QJsonObject {
                QString problem;
                const auto command = driveCommand(spacing ? formula_->text() : text,
                                                  spacing ? text : spacing_->text(), problem);
                if (!problem.isEmpty() || command.isEmpty())
                    return {};
                const auto error = authoring::dryRun(doc_, {command});
                if (error.isEmpty()) {
                    error_->hide();
                    return {};
                }
                // An error in the other formula, or of the form, shows where it belongs.
                const auto details = error["details"].toObject();
                const auto excerpt = details["excerpt"].toString();
                const bool other = details.contains("offset") && !excerpt.isEmpty() &&
                                   !text.contains(excerpt) &&
                                   (spacing ? formula_ : spacing_)->text().contains(excerpt);
                if (details.contains("offset") && !other)
                    return error;
                present(error);
                return {};
            });
        connect(field, &FormulaField::commitRequested, this, [this] { commit(); });
        connect(field, &FormulaField::escaped, this, [this] { cancel(); });
    }
    connect(formula_, &FormulaField::textEdited, this, [this] { syncPlane(); });
    connect(add_, &QPushButton::clicked, this, [this] {
        const auto status = addDriver();
        if (!status.isEmpty())
            emit view_.message(status);
    });
    connect(commit_, &QPushButton::clicked, this, [this] { commit(); });
    connect(rebind_, &QPushButton::clicked, this, [this] { rebind(false); });
    connect(rebindAll_, &QPushButton::clicked, this, [this] { rebind(true); });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (syncing_ || !item)
            return;
        selectDriver(item->data(0, Qt::UserRole).toString().toULongLong());
    });
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (syncing_)
            return;
        bool changed = false;
        for (auto *item : tree_->selectedItems())
            changed |= item->data(0, Qt::UserRole + 1).toBool();
        rebind_->setEnabled(changed);
    });
    connect(kind_, &QComboBox::currentIndexChanged, this, [this] {
        if (syncing_)
            return;
        syncForm();
        check_->start();
    });
    for (auto *combo : {axis_, child_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            if (syncing_)
                return;
            syncPlane();
            check_->start();
        });
    connect(maxCount_, &QSpinBox::valueChanged, this, [this] {
        if (!syncing_)
            check_->start();
    });
    connect(plane_, &QLineEdit::textEdited, this, [this] {
        try {
            at_ = parseParameterText(plane_->text(), "length", doc_.displayUnits()).toDouble();
            error_->hide();
            syncPlane();
            check_->start();
        } catch (const std::exception &error) {
            static_cast<ParameterMessageLabel *>(error_)->setTone(ParameterMessageTone::Failure);
            action_->hide();
            error_->setText(QString::fromUtf8(error.what()));
            error_->show();
        }
    });
    connect(&view_, &Viewport::stretchPlaneMoved, this, [this](double at, bool finished) {
        if (!!editor_->isHidden() || kind() != "stretch")
            return;
        at_ = at;
        syncPlane();
        if (finished)
            checkForm();
        else
            check_->start();
    });
    // Tab: header → + Driver → tree → Rebind → Rebind all → the form.
    QWidget::setTabOrder(add_, tree_);
    QWidget *previous = tree_;
    for (QWidget *input : std::initializer_list<QWidget *>{
             rebind_, rebindAll_, kind_, axis_, formula_->edit(), formula_->fixButton(), plane_,
             spacing_->edit(), spacing_->fixButton(), maxCount_, child_, commit_}) {
        QWidget::setTabOrder(previous, input);
        previous = input;
    }
    hide();
}
void ComponentDriversSection::detach() {
    detached_ = true;
    check_->stop();
    scope_ = {};
    view_.setStretchPlane(std::nullopt);
    hide();
}
QString ComponentDriversSection::labelOf(const QString &name) const {
    if (!scope_ || !doc_.definitions().contains(scope_.definition))
        return name;
    const auto &record = doc_.definitions().at(scope_.definition)->parameters;
    if (record)
        for (const auto &parameter : record->parameters)
            if (QString::fromStdString(parameter.name) == name)
                return QString::fromStdString(parameter.label);
    return name;
}
QJsonObject ComponentDriversSection::driverNamed(Id driver) const {
    for (const auto &entry : description_["drivers"].toArray())
        if (entry.toObject()["id"].toString().toULongLong() == driver)
            return entry.toObject();
    return {};
}
QString ComponentDriversSection::memberName(Id member) const {
    if (!scope_ || !doc_.definitions().contains(scope_.definition))
        return {};
    const auto &definition = *doc_.definitions().at(scope_.definition);
    const auto found = definition.members.find(member);
    return found == definition.members.end() ? "#" + QString::number(member)
                                              : QString::fromStdString(found->second->name);
}
QString ComponentDriversSection::targetText(const QJsonObject &driver) const {
    const auto name = memberName(driver["member"].toString().toULongLong());
    const auto data = driver["data"].toObject();
    const auto kind = driver["kind"].toString();
    const auto axis = data["axis"].toString();
    if (kind == "stretch")
        return name + " · " + axis + " past " +
               parameterValueText(data["at"].toDouble(), "length", doc_.displayUnits(),
                                  doc_.displayPrecision());
    if (kind == "position")
        return name + " · " + axis;
    if (kind == "rotation")
        return name + " · about " + axis;
    if (kind == "repeat")
        return name + " · " + axis + ", " + data["spacing"].toString();
    if (kind == "childParameter")
        return name + " · " + data["parameter"].toString();
    return name;
}
void ComponentDriversSection::refresh() {
    if (detached_)
        return;
    const auto scope = authoring::scopeOf(doc_, view_);
    if (!scope) {
        if (scope_ || !editor_->isHidden()) {
            scope_ = {};
            description_ = {};
            cacheKey_.clear();
            signature_.clear();
            driver_ = member_ = 0;
            creating_ = false;
            editor_->hide();
            check_->stop();
            view_.setStretchPlane(std::nullopt);
            emit planeChanged();
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
        driver_ = member_ = 0;
        creating_ = false;
        editor_->hide();
        view_.setStretchPlane(std::nullopt);
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
        QString::fromUtf8(QJsonDocument(description_["drivers"].toArray()).toJson()) +
        QString::number(int(doc_.displayUnits())) + "/" + QString::number(doc_.displayPrecision());
    if (signature != signature_) {
        signature_ = signature;
        rebuild();
    }
    // A form whose driver or member went away closes.
    const auto &definition = *doc_.definitions().at(scope_.definition);
    if (!editor_->isHidden() && (!definition.members.contains(member_) ||
                                 (!creating_ && driverNamed(driver_).isEmpty()))) {
        driver_ = member_ = 0;
        creating_ = false;
        editor_->hide();
        view_.setStretchPlane(std::nullopt);
        emit planeChanged();
    } else if (!editor_->isHidden() && !creating_) {
        formula_->setStored(driverNamed(driver_)["expression"].toString());
        syncPlane();
    }
    show();
    relayoutAncestors(this);
}
void ComponentDriversSection::rebuild() {
    QScopedValueRollback syncing(syncing_, true);
    tree_->clear();
    QFont mono = font();
    mono.setFamily("DejaVu Sans Mono");
    const auto drivers = description_["drivers"].toArray();
    QStringList changedMembers;
    QTreeWidgetItem *current = nullptr;
    for (const auto &entry : drivers) {
        const auto driver = entry.toObject();
        const auto id = driver["id"].toString().toULongLong();
        const bool changed = driver["state"].toString() == "changedSinceBound";
        const auto kind = kindTitle(driver["kind"].toString());
        const auto target = targetText(driver);
        const auto formula = driver["expression"].toString();
        auto *item = new QTreeWidgetItem(tree_, {kind, target, formula, changed ? "" : "Bound"});
        item->setData(0, Qt::UserRole, QString::number(id));
        item->setData(0, Qt::UserRole + 1, changed);
        item->setFont(2, mono);
        const auto state = changed ? QString("Changed") : QString("Bound");
        // A row reads its kind, target, formula and state (ADR 0166 §8).
        item->setData(0, Qt::AccessibleTextRole,
                      kind + ", " + target + ", " + formula + ", " + state);
        item->setData(3, Qt::AccessibleTextRole, state);
        const auto member = memberName(driver["member"].toString().toULongLong());
        const auto sentence =
            changed ? member + " was edited after this driver was set. Rebind accepts the new "
                               "shape as the driver's starting point."
                    : QString("The driver acts on the shape it was set on.");
        for (int column = 0; column < 4; ++column) {
            item->setData(column, Qt::AccessibleDescriptionRole, sentence);
            item->setToolTip(column, (column < 3 ? item->text(column) + "\n" : QString{}) + sentence);
        }
        if (changed) {
            changedMembers << member;
            // The design's in-row Rebind is for the pointer: the keyboard path is the Rebind
            // button under the list, so this one takes no focus.
            auto *cell = new QWidget;
            auto *box = new QHBoxLayout(cell);
            box->setContentsMargins(2, 0, 2, 0);
            box->setSpacing(4);
            auto *label = new QLabel("⚠ Changed");
            label->setObjectName("driverChangedState");
            box->addWidget(label);
            auto *button = new QPushButton("Rebind");
            button->setObjectName("driverRowRebind");
            button->setFocusPolicy(Qt::NoFocus);
            button->setAutoDefault(false);
            button->setAccessibleName("Rebind " + kind + " " + member);
            // The rebuild after the commit deletes this button: commit once its click returns.
            connect(button, &QPushButton::clicked, this, [this, id] {
                QTimer::singleShot(0, this, [this, id] {
                if (!scope_)
                    return;
                QJsonArray commands{QJsonObject{{"command", "component_parameter.rebind"},
                                                {"definition", QString::number(scope_.definition)},
                                                {"drivers", QJsonArray{QString::number(id)}}}};
                try {
                    authoring::publish(doc_, view_, commands, "Rebind driver");
                } catch (const std::exception &error) {
                    emit view_.message(QString::fromUtf8(error.what()));
                }
                });
            });
            box->addWidget(button);
            box->addStretch();
            tree_->setItemWidget(item, 3, cell);
        }
        if (id == driver_ && !creating_)
            current = item;
    }
    if (current)
        tree_->setCurrentItem(current);
    empty_->setVisible(drivers.isEmpty());
    tree_->setVisible(!drivers.isEmpty());
    rebind_->setVisible(!changedMembers.isEmpty());
    rebind_->setEnabled(current && current->data(0, Qt::UserRole + 1).toBool());
    rebindAll_->setVisible(changedMembers.size() > 1);
    changedNote_->setVisible(!changedMembers.isEmpty());
    changedNote_->setText(
        changedMembers.size() == 1
            ? changedMembers.front() + " was edited by hand after its driver was set. Rebind "
                                       "accepts the new shape as the driver's starting point."
            : "Some parts were edited by hand after their drivers were set. Rebind accepts the "
              "new shapes as the drivers' starting points.");
    tree_->updateGeometry();
}
QString ComponentDriversSection::kind() const { return kind_->currentData().toString(); }
QStringList ComponentDriversSection::axesFor(const QString &kind) const {
    QStringList axes{"x", "y", "z"};
    if (kind != "position" || !scope_)
        return axes;
    // A position along the glue normal would move the glue face off its wall.
    const auto &definition = *doc_.definitions().at(scope_.definition);
    if (!definition.glue || !definition.members.contains(definition.glue->member))
        return axes;
    const auto &glueBody = *definition.members.at(definition.glue->member);
    if (!glueBody.surface.faces.contains(definition.glue->face))
        return axes;
    const auto normal = glueBody.surface.normal(definition.glue->face);
    Transform chain;
    for (auto member = definition.glue->member;
         member && member != definition.root && definition.members.contains(member);
         member = definition.members.at(member)->parent) {
        chain = definition.members.at(member)->transform * chain;
        if (member != member_)
            continue;
        const auto local = normalThrough(chain, normal);
        QStringList allowed;
        for (int axis = 0; axis < 3; ++axis) {
            const double along = axis == 0 ? local.x : axis == 1 ? local.y : local.z;
            if (std::abs(along) <= 1e-9)
                allowed << axisName(axis);
        }
        return allowed;
    }
    return axes;
}
QString ComponentDriversSection::defaultFormula(const QString &kind) const {
    if (!scope_)
        return {};
    const auto &record = doc_.definitions().at(scope_.definition)->parameters;
    if (!record)
        return {};
    QString wanted = expectedType(kind);
    if (kind == "childParameter") {
        const auto name = child_->currentData().toString();
        for (const auto &parameter : record->parameters)
            if (QString::fromStdString(parameter.name) == name)
                return name;
        wanted = child_->currentData(Qt::UserRole + 1).toString();
    }
    for (const auto &parameter : record->parameters) {
        const auto name = QString::fromStdString(parameter.name);
        const auto type = QString::fromLatin1(parameterTypeName(parameter.type).data());
        if (type == wanted)
            return name;
    }
    return {};
}
void ComponentDriversSection::openForm(Id member, Id driver) {
    QJsonObject data;
    {
    QScopedValueRollback syncing(syncing_, true);
    member_ = member;
    driver_ = driver;
    creating_ = driver == 0;
    error_->hide();
    const auto existing = driverNamed(driver);
    const auto kinds = kindsFor(doc_, scope_.definition, member);
    kind_->clear();
    for (const auto &entry : creating_ ? kinds : QStringList{existing["kind"].toString()})
        kind_->addItem(kindTitle(entry), entry);
    // A child parameter's choices come from the referenced definition.
    child_->clear();
    const auto &definition = *doc_.definitions().at(scope_.definition);
    if (const auto reference = definition.references.find(member);
        reference != definition.references.end() &&
        doc_.definitions().contains(reference->second) &&
        doc_.definitions().at(reference->second)->parameters)
        for (const auto &parameter : doc_.definitions().at(reference->second)->parameters->parameters)
            if (parameter.mode == ComponentParameterMode::Input) {
                child_->addItem(QString::fromStdString(parameter.label),
                                QString::fromStdString(parameter.name));
                child_->setItemData(child_->count() - 1,
                                    QString::fromLatin1(parameterTypeName(parameter.type).data()),
                                    Qt::UserRole + 1);
            }
    data = existing["data"].toObject();
    if (!creating_) {
        child_->setCurrentIndex(std::max(0, child_->findData(data["parameter"].toString())));
        maxCount_->setValue(std::max(1, data["maxCount"].toInt()));
        at_ = data["at"].toDouble();
        formula_->setStored(existing["expression"].toString(), true);
        spacing_->setStored(data["spacing"].toString(), true);
    } else {
        // Max count starts at the first count parameter's maximum, else 10.
        int maximum = 10;
        if (const auto &record = definition.parameters)
            for (const auto &parameter : record->parameters)
                if (parameter.type == ParameterType::Count && parameter.max) {
                    maximum = std::clamp(int(*parameter.max), 1, 1000);
                    break;
                }
        maxCount_->setValue(maximum);
        spacing_->setStored({}, true);
    }
    editorTitle_->setText((creating_ ? QString("New driver") : kindTitle(existing["kind"].toString()) +
                                                                    " driver") +
                          " · " + memberName(member));
    commit_->setText(creating_ ? "Add" : "Apply");
    commit_->setAccessibleName(creating_ ? "Add driver" : "Apply driver");
    }
    syncForm();
    if (!creating_) {
        QScopedValueRollback again(syncing_, true);
        axis_->setCurrentIndex(std::max(0, axis_->findData(data["axis"].toString())));
    }
    editor_->show();
    syncPlane();
    relayoutAncestors(this);
}
void ComponentDriversSection::syncForm() {
    QScopedValueRollback syncing(syncing_, true);
    const auto kind = this->kind();
    auto *form = editor_->findChild<QFormLayout *>();
    form->setRowVisible(kindRow_, creating_);
    form->setRowVisible(axisRow_, kind != "visibility" && kind != "childParameter");
    form->setRowVisible(planeRow_, kind == "stretch");
    form->setRowVisible(spacingRow_, kind == "repeat");
    form->setRowVisible(maxRow_, kind == "repeat");
    form->setRowVisible(childRow_, kind == "childParameter");
    formula_->setAccessibleName(kind == "repeat" ? "Count" : "Formula");
    const auto length = doc_.displayUnits() == DisplayUnit::Millimeters  ? "mm"
                        : doc_.displayUnits() == DisplayUnit::FeetInches ? "in"
                                                                         : "m";
    formula_->setFixUnits(kind == "rotation" ? QStringList{"deg"} : QStringList{length, "deg"});
    spacing_->setFixUnits({length});
    // Axes: the member's own for stretch and rotation, the parent frame's for position and
    // repeat (ADR 0166 §3.3). Each shows its colour and its letter.
    const auto previous = axis_->currentData().toString();
    axis_->clear();
    for (const auto &name : axesFor(kind)) {
        const int index = axisIndex(name);
        const auto frame = kind == "stretch" || kind == "rotation" ? "own" : "parent";
        axis_->addItem(axisSwatch(index), name, name);
        axis_->setItemData(axis_->count() - 1,
                           name + " axis (" + (index == 0 ? "red" : index == 1 ? "green" : "blue") +
                               "), the part's " + frame + " frame",
                           Qt::AccessibleTextRole);
    }
    if (creating_) {
        int axis = 0;
        // Stretch starts on the member's longest axis, the others on x; the plane at the
        // middle of the member's bounds on that axis.
        if (const auto &definition = *doc_.definitions().at(scope_.definition);
            definition.members.contains(member_)) {
            const auto &body = *definition.members.at(member_);
            if (!body.surface.vertices.empty()) {
                Vec3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
                for (const auto &[id, point] : body.surface.vertices) {
                    low = {std::min(low.x, point.x), std::min(low.y, point.y),
                           std::min(low.z, point.z)};
                    high = {std::max(high.x, point.x), std::max(high.y, point.y),
                            std::max(high.z, point.z)};
                }
                const double size[]{high.x - low.x, high.y - low.y, high.z - low.z};
                if (kind == "stretch")
                    axis = int(std::max_element(size, size + 3) - size);
                const double lows[]{low.x, low.y, low.z}, highs[]{high.x, high.y, high.z};
                at_ = (lows[axis] + highs[axis]) / 2;
            }
        }
        axis_->setCurrentIndex(std::max(0, axis_->findData(axisName(axis))));
        if (kind == "childParameter" && child_->count())
            child_->setCurrentIndex(0);
        formula_->setStored(defaultFormula(kind), true);
        if (kind == "repeat") {
            const auto spacing = defaultFormula("stretch");
            spacing_->setStored(spacing, true);
        }
    } else if (axis_->findData(previous) >= 0) {
        axis_->setCurrentIndex(axis_->findData(previous));
    }
    syncPlane();
}
void ComponentDriversSection::syncPlane() {
    const bool active = planeActive();
    if (active) {
        const auto scene = authoring::sceneOf(doc_, scope_.placement, member_);
        view_.setStretchPlane(Viewport::StretchPlane{scene, axisIndex(axis_->currentData().toString()),
                                                     at_, "Stretch · " + formula_->text()});
        if (!plane_->hasFocus())
            plane_->setText(parameterValueText(at_, "length", doc_.displayUnits(),
                                               doc_.displayPrecision()));
        const auto [moving, total] = view_.stretchMoving();
        moving_->setText(QString("%1 of %2 points move").arg(moving).arg(total));
    } else if (view_.stretchPlane()) {
        view_.setStretchPlane(std::nullopt);
    }
    emit planeChanged();
}
bool ComponentDriversSection::planeActive() const {
    return scope_ && !editor_->isHidden() && kind() == "stretch" &&
           authoring::sceneOf(doc_, scope_.placement, member_) != 0;
}
QString ComponentDriversSection::planeText() const {
    return parameterValueText(at_, "length", doc_.displayUnits(), doc_.displayPrecision());
}
bool ComponentDriversSection::setPlaneText(const QString &text, QString &error) {
    if (!planeActive()) {
        error = "Open a Stretch driver to set its plane";
        return false;
    }
    try {
        at_ = parseParameterText(text, "length", doc_.displayUnits()).toDouble();
    } catch (const std::exception &failure) {
        error = QString::fromUtf8(failure.what());
        return false;
    }
    syncPlane();
    checkForm();
    return true;
}
void ComponentDriversSection::selectDriver(Id driver) {
    const auto existing = driverNamed(driver);
    if (existing.isEmpty() || !scope_)
        return;
    {
        QScopedValueRollback syncing(syncing_, true);
        for (int i = 0; i < tree_->topLevelItemCount(); ++i)
            if (tree_->topLevelItem(i)->data(0, Qt::UserRole).toString().toULongLong() == driver)
                tree_->setCurrentItem(tree_->topLevelItem(i));
    }
    const auto member = existing["member"].toString().toULongLong();
    openForm(member, driver);
    rebind_->setEnabled(existing["state"].toString() == "changedSinceBound");
    // Selecting a row selects its member in the viewport (ADR 0166 §3.3).
    if (const auto scene = authoring::sceneOf(doc_, scope_.placement, member))
        view_.selectEntities({{scene, SelectionKind::Body, 0}});
}
QString ComponentDriversSection::addDriver() {
    const auto scope = authoring::scopeOf(doc_, view_);
    const QString select = "Select a part inside the definition, then Shift+D";
    if (!scope)
        return select;
    refresh();
    std::set<Id> bodies;
    for (const auto &entity : view_.selectionState().entities())
        bodies.insert(entity.body);
    if (bodies.size() != 1 || *bodies.begin() == scope.placement)
        return select;
    const auto member = authoring::memberOf(doc_, scope.placement, *bodies.begin());
    QString why;
    if (kindsFor(doc_, scope.definition, member, &why).isEmpty())
        return why;
    {
        QScopedValueRollback syncing(syncing_, true);
        tree_->setCurrentItem(nullptr);
        tree_->clearSelection();
    }
    setExpanded(true);
    openForm(member, 0);
    kind_->setFocus(Qt::ShortcutFocusReason);
    return {};
}
QJsonObject ComponentDriversSection::driveCommand(const QString &formula, const QString &spacing,
                                                  QString &problem) const {
    if (!scope_ || !member_)
        return {};
    const auto kind = this->kind();
    if (formula.trimmed().isEmpty()) {
        problem = "Type a formula.";
        return {};
    }
    QJsonObject data;
    const auto axis = axis_->currentData().toString();
    if (kind == "stretch")
        data = {{"axis", axis}, {"at", at_}};
    else if (kind == "position" || kind == "rotation")
        data = {{"axis", axis}};
    else if (kind == "repeat") {
        if (spacing.trimmed().isEmpty()) {
            problem = "Type a spacing.";
            return {};
        }
        data = {{"axis", axis}, {"maxCount", maxCount_->value()}, {"spacing", spacing.trimmed()}};
    } else if (kind == "childParameter") {
        if (child_->currentIndex() < 0) {
            problem = "Choose the child's parameter.";
            return {};
        }
        data = {{"parameter", child_->currentData().toString()}};
    }
    QJsonObject command{{"command", "component_parameter.drive"},
                        {"definition", QString::number(scope_.definition)},
                        {"kind", kind},
                        {"member", QString::number(member_)},
                        {"expression", formula.trimmed()},
                        {"data", data}};
    if (!creating_)
        command["driver"] = QString::number(driver_);
    return command;
}
QJsonObject ComponentDriversSection::checkFormula(const QString &text, bool spacing) const {
    if (!scope_)
        return {};
    auto type = spacing ? QString("length") : expectedType(kind());
    if (!spacing && kind() == "childParameter")
        type = child_->currentData(Qt::UserRole + 1).toString();
    // A child's choice is checked against its options only by the dry run (R094.b notes).
    if (type == "choice" || type.isEmpty())
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
QString ComponentDriversSection::present(const QJsonObject &error) {
    if (error.isEmpty()) {
        error_->hide();
        return {};
    }
    const auto details = error["details"].toObject();
    if (details.contains("offset")) {
        const auto excerpt = details["excerpt"].toString();
        auto *field = !excerpt.isEmpty() && !formula_->text().contains(excerpt) &&
                              spacing_->text().contains(excerpt)
                          ? spacing_
                          : formula_;
        field->showError(error);
        error_->hide();
        return field->error();
    }
    // The §5 message table: the member names what can't be stretched and the child that has no
    // such parameter.
    ParameterMessageContext context;
    context.place = ParameterMessagePlace::Driver;
    context.document = &doc_;
    context.units = doc_.displayUnits();
    context.precision = doc_.displayPrecision();
    context.formula = formula_->text();
    context.placement = scope_.placement;
    context.label = [this](const QString &name) { return labelOf(name); };
    context.member = memberName(member_);
    if (const auto &definition = *doc_.definitions().at(scope_.definition);
        definition.members.contains(member_))
        context.textMember = bool(definition.members.at(member_)->textSource);
    context.childParameter = child_->currentData().toString();
    message_ = parameterMessage(error, context);
    static_cast<ParameterMessageLabel *>(error_)->setTone(message_.tone);
    error_->setText(message_.sentence);
    error_->setToolTip(error["message"].toString());
    error_->show();
    action_->setText(message_.actionText);
    action_->setVisible(message_.action == ParameterMessageAction::SelectLocked ||
                        message_.action == ParameterMessageAction::ShowDriver);
    return message_.sentence;
}
QString ComponentDriversSection::checkForm() {
    check_->stop();
    if (!!editor_->isHidden())
        return {};
    QString problem;
    const auto command = driveCommand(formula_->text(), spacing_->text(), problem);
    if (!problem.isEmpty()) {
        static_cast<ParameterMessageLabel *>(error_)->setTone(ParameterMessageTone::Failure);
        action_->hide();
        error_->setText(problem);
        error_->show();
        return problem;
    }
    if (command.isEmpty())
        return {};
    return present(authoring::dryRun(doc_, {command}));
}
QString ComponentDriversSection::commit() {
    if (busy_ || !scope_ || !!editor_->isHidden())
        return {};
    check_->stop();
    QString problem;
    const auto command = driveCommand(formula_->text(), spacing_->text(), problem);
    if (!problem.isEmpty()) {
        static_cast<ParameterMessageLabel *>(error_)->setTone(ParameterMessageTone::Failure);
        action_->hide();
        error_->setText(problem);
        error_->show();
        return problem;
    }
    if (const auto refusal = authoring::dryRun(doc_, {command}); !refusal.isEmpty())
        return present(refusal);
    const auto label = authoring::historyLabel(creating_ ? "Add" : "Edit",
                                               kindTitle(kind()) + " driver");
    QScopedValueRollback busy(busy_, true);
    QJsonObject result;
    try {
        result = authoring::publish(doc_, view_, {command}, label);
    } catch (const std::exception &error) {
        return present(authoring::failure(error));
    }
    const auto created = result["createdComponentDrivers"].toArray();
    const auto driver = creating_ && !created.isEmpty() ? created[0].toString().toULongLong()
                                                        : driver_;
    creating_ = false;
    driver_ = driver;
    refresh();
    selectDriver(driver);
    return {};
}
void ComponentDriversSection::cancel() {
    check_->stop();
    error_->hide();
    if (creating_ || !driver_) {
        creating_ = false;
        driver_ = member_ = 0;
        editor_->hide();
        syncPlane();
        add_->setFocus(Qt::OtherFocusReason);
        relayoutAncestors(this);
        return;
    }
    openForm(member_, driver_);
}
QString ComponentDriversSection::removeCurrent() {
    if (busy_ || !scope_ || !driver_ || creating_)
        return {};
    const auto existing = driverNamed(driver_);
    const QJsonArray commands{QJsonObject{{"command", "component_parameter.undrive"},
                                          {"definition", QString::number(scope_.definition)},
                                          {"driver", QString::number(driver_)}}};
    QScopedValueRollback busy(busy_, true);
    try {
        authoring::publish(doc_, view_, commands,
                           "Remove " + kindTitle(existing["kind"].toString()) + " driver");
    } catch (const std::exception &error) {
        return present(authoring::failure(error));
    }
    driver_ = member_ = 0;
    editor_->hide();
    syncPlane();
    refresh();
    tree_->setFocus(Qt::OtherFocusReason);
    return {};
}
QString ComponentDriversSection::rebind(bool all) {
    if (busy_ || !scope_)
        return {};
    QJsonArray drivers;
    int changed = 0;
    for (const auto &entry : description_["drivers"].toArray())
        changed += entry.toObject()["state"].toString() == "changedSinceBound";
    for (auto *item : tree_->selectedItems())
        if (item->data(0, Qt::UserRole + 1).toBool())
            drivers.append(item->data(0, Qt::UserRole).toString());
    if (drivers.isEmpty() && driver_ &&
        driverNamed(driver_)["state"].toString() == "changedSinceBound")
        drivers.append(QString::number(driver_));
    if ((!all && drivers.isEmpty()) || (all && !changed))
        return {};
    QJsonObject command{{"command", "component_parameter.rebind"},
                        {"definition", QString::number(scope_.definition)}};
    if (all)
        command["all"] = true;
    else
        command["drivers"] = drivers;
    const int count = all ? changed : int(drivers.size());
    QScopedValueRollback busy(busy_, true);
    try {
        authoring::publish(doc_, view_, {command},
                           count == 1 ? QString("Rebind driver")
                                      : QString("Rebind %1 drivers").arg(count));
    } catch (const std::exception &error) {
        return present(authoring::failure(error));
    }
    refresh();
    return {};
}
bool ComponentDriversSection::eventFilter(QObject *object, QEvent *event) {
    // The action belongs to the message: it goes when the line goes.
    if (object == error_ && event->type() == QEvent::Hide)
        action_->hide();
    const bool press = event->type() == QEvent::KeyPress;
    if (!press && event->type() != QEvent::ShortcutOverride)
        return CollapsibleSection::eventFilter(object, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
    if (object == tree_) {
        const bool ours = modifiers == Qt::NoModifier &&
                          (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Return ||
                           key->key() == Qt::Key_Enter);
        if (!ours || !driver_ || creating_)
            return CollapsibleSection::eventFilter(object, event);
        event->accept();
        if (!press)
            return true;
        if (key->key() == Qt::Key_Delete)
            removeCurrent();
        else if (axis_->isVisible())
            axis_->setFocus(Qt::OtherFocusReason);
        else
            formula_->edit()->setFocus(Qt::OtherFocusReason);
        return true;
    }
    if (modifiers == Qt::NoModifier &&
        (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
         key->key() == Qt::Key_Escape)) {
        if (auto *combo = qobject_cast<QComboBox *>(object); combo && combo->view()->isVisible())
            return CollapsibleSection::eventFilter(object, event);
        event->accept();
        if (press) {
            if (key->key() == Qt::Key_Escape)
                cancel();
            else {
                if (object == plane_) {
                    QString error;
                    if (!plane_->text().isEmpty() && !setPlaneText(plane_->text(), error)) {
                        static_cast<ParameterMessageLabel *>(error_)->setTone(ParameterMessageTone::Failure);
                        action_->hide();
                        error_->setText(error);
                        error_->show();
                        return true;
                    }
                }
                commit();
            }
        }
        return true;
    }
    return CollapsibleSection::eventFilter(object, event);
}
} // namespace sketchy
