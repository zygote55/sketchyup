#include "app/keypad_decimal.hpp"
#include "app/component_definition_section.hpp"
#include "app/component_drivers_section.hpp"
#include "app/component_parameters_section.hpp"
#include "app/context_label.hpp"
#include "app/formula_field.hpp"
#include "app/parameter_messages.hpp"
#include "app/parameter_value_field.hpp"
#include "app/parameters_scope_dialog.hpp"
#include "app/unit_display.hpp"
#include "app/window.hpp"
#include "automation/measurements.hpp"
#include "core/component_parameters.hpp"
#include "core/parameter_expression.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>
namespace sketchy {
namespace {
// Lays its items out left to right and wraps them onto further rows (the Preview strip's
// fields wrap, ADR 0166 §3.1).
class FlowLayout final : public QLayout {
  public:
    explicit FlowLayout(QWidget *parent = nullptr) : QLayout(parent) {
        setContentsMargins(0, 0, 0, 0);
    }
    ~FlowLayout() override {
        while (auto *item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem *item) override { items_.push_back(item); }
    int count() const override { return int(items_.size()); }
    QLayoutItem *itemAt(int index) const override {
        return index >= 0 && index < count() ? items_[std::size_t(index)] : nullptr;
    }
    QLayoutItem *takeAt(int index) override {
        if (index < 0 || index >= count())
            return nullptr;
        auto *item = items_[std::size_t(index)];
        items_.erase(items_.begin() + index);
        return item;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return place(QRect(0, 0, width, 0), false); }
    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        place(rect, true);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize size;
        for (auto *item : items_)
            size = size.expandedTo(item->minimumSize());
        const auto margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }

  private:
    std::vector<QLayoutItem *> items_;
    int place(const QRect &rect, bool apply) const {
        const int gapX = 12, gapY = 6;
        int x = rect.x(), y = rect.y(), line = 0;
        for (auto *item : items_) {
            if (item->isEmpty())
                continue;
            const auto hint = item->sizeHint();
            const int width = std::min(hint.width(), rect.width());
            if (x > rect.x() && x + width > rect.right() + 1) {
                x = rect.x();
                y += line + gapY;
                line = 0;
            }
            const int height = item->hasHeightForWidth() ? item->heightForWidth(width)
                                                         : hint.height();
            if (apply)
                item->setGeometry(QRect(QPoint(x, y), QSize(width, height)));
            x += width + gapX;
            line = std::max(line, height);
        }
        return y + line - rect.y();
    }
};
// ADR 0165 §4 (R094.e): an edit through the placement would be refused, because it shows its
// own values (overrides, or values from its parent) rather than its template.
bool showsOwnValues(const Document &doc, Id placement) {
    if (!doc.instances().contains(placement))
        return false;
    try {
        requireComponentTemplateScope(doc, placement, {});
    } catch (const ComponentParameterError &error) {
        return error.code() == "PARAMETER_TEMPLATE_SCOPE";
    } catch (const std::exception &) {
    }
    return false;
}
QString bodyName(const Document &doc, Id body) {
    const auto found = doc.bodies().find(body);
    return found == doc.bodies().end() || found->second->name.empty()
               ? "Placement #" + QString::number(body)
               : QString::fromStdString(found->second->name);
}
// The placement whose definition holds a nested placement (it supplies the nested values), or 0.
Id enclosingPlacement(const Document &doc, Id placement) {
    if (!doc.bodies().contains(placement))
        return 0;
    for (auto ancestor = doc.bodies().at(placement)->parent;
         ancestor && doc.bodies().contains(ancestor); ancestor = doc.bodies().at(ancestor)->parent)
        if (doc.instances().contains(ancestor))
            return ancestor;
    return 0;
}
// The context is the placement or lies inside it.
bool within(const Document &doc, Id context, Id placement) {
    for (std::size_t depth = 0; context && doc.bodies().contains(context) && depth <= 256;
         context = doc.bodies().at(context)->parent, ++depth)
        if (context == placement)
            return true;
    return false;
}
bool sameValue(const QJsonValue &a, const QJsonValue &b) {
    if (a.isDouble() && b.isDouble())
        return std::abs(a.toDouble() - b.toDouble()) <=
               1e-9 * std::max({1., std::abs(a.toDouble()), std::abs(b.toDouble())});
    return a == b;
}
// A JSON value of a parameter type as a default expression ("1.5 m", "true", "\"oak\"").
std::string literalOf(const QJsonValue &value, const QString &typeName) {
    for (int index = 0; index <= int(ParameterType::Choice); ++index) {
        const auto type = ParameterType(index);
        if (QString::fromLatin1(parameterTypeName(type).data()) != typeName)
            continue;
        if (type == ParameterType::Boolean)
            return componentParameterLiteral(value.toBool(), type);
        if (type == ParameterType::Choice)
            return componentParameterLiteral(ParameterChoice{value.toString().toStdString()}, type);
        return componentParameterLiteral(
            ParameterQuantity{value.toDouble(), parameterDimension(type)}, type);
    }
    throw std::runtime_error("Unknown parameter type");
}
// The changed-since-bound strip's mark (States artboard): a circular arrow in the text colour.
class RebindMark final : public QWidget {
  public:
    RebindMark() {
        setObjectName("parameterStatusIcon");
        const int side = fontMetrics().height();
        setFixedSize(side, side);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const double side = std::min(width(), height());
        painter.setPen(QPen(palette().color(QPalette::WindowText), std::max(1.2, side / 8),
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QRectF circle(side * .17, side * .17, side * .66, side * .66);
        painter.drawArc(circle, 60 * 16, 270 * 16);
        const QPointF tip(side * .83, side * .17);
        painter.drawLine(tip, QPointF(side * .83, side * .4));
        painter.drawLine(tip, QPointF(side * .6, side * .17));
    }
};
} // namespace
void Window::addComponentActions(QMenu *menu) {
    for (auto [operation, title] :
         {std::pair{"create", "Make component…"}, std::pair{"instance", "Place component…"},
          std::pair{"replace", "Replace component…"},
          std::pair{"axes", "Change component axes…"}}) {
        auto *item = action(QString("component.") + operation, title,
                            QString(operation) == "create" ? QKeySequence("G") : QKeySequence{},
                            [this, operation] { componentDialog(operation); });
        if (QString(operation) == "create") {
            item->setShortcutContext(Qt::WidgetShortcut);
            removeAction(item);
            viewport_->addAction(item);
        }
        item->setProperty("command", QString("component.") + operation);
        menu->addAction(item);
    }
    auto *unique = action("component.make_unique", "Make component unique", {},
                          [this] { viewport_->makeComponentUnique(); });
    unique->setProperty("command", "component.make_unique");
    menu->addAction(unique);
    menu->addAction(action("component.browseLibrary", "Browse component library…", {},
                           [this] { libraryDialog(false); }));
    menu->addAction(action("component.saveLibrary", "Save component to library…", {},
                           [this] { saveLibraryDialog(true); }));
    // ADR 0166 §3.3 (R094.d): Shift+D, model scope, on the viewport like the other
    // Shift-only keys; also in the palette, the Drivers header and the rail.
    menu->addAction(
        action("component.add_driver", "Add driver", QKeySequence("Shift+D"), [this] { addDriver(); }));
    addHostedActions(menu);
}
void Window::componentDialog(const QString &operation) {
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    QDialog dialog(this);
    dialog.setObjectName("componentDialog");
    dialog.setWindowTitle(
        findChild<QAction *>("component." + operation)->text().remove(QChar(0x2026)));
    QFormLayout form(&dialog);
    auto *definitions = new QComboBox(&dialog);
    definitions->setObjectName("componentDefinitionChoice");
    definitions->setAccessibleName("Component definition");
    for (const auto &[id, definition] : doc_.definitions())
        definitions->addItem(QString::fromStdString(definition->name),
                             QVariant::fromValue<qulonglong>(id));
    auto *name = new QLineEdit("Component", &dialog);
    name->setObjectName("componentName");
    name->setAccessibleName("Component name");
    name->setMaxLength(1024);
    const auto separator = QLocale().decimalPoint() == "," ? ";" : ",";
    auto *origin = new QLineEdit(QStringList{"0", "0", "0"}.join(separator), &dialog);
    acceptKeypadDecimal(origin);
    origin->setObjectName("componentOrigin");
    origin->setAccessibleName(operation == "axes" ? "Origin in component coordinates"
                                                  : "World placement position");
    auto *normal = new QLineEdit(QStringList{"0", "0", "1"}.join(separator), &dialog);
    acceptKeypadDecimal(normal);
    normal->setObjectName("componentNormal");
    auto *axis = new QLineEdit(QStringList{"1", "0", "0"}.join(separator), &dialog);
    acceptKeypadDecimal(axis);
    axis->setObjectName("componentXAxis");
    if (operation == "create")
        form.addRow("Name", name);
    else
        name->hide();
    if (operation == "instance" || operation == "replace")
        form.addRow("Definition", definitions);
    else
        definitions->hide();
    if (operation == "axes" || operation == "instance")
        form.addRow(operation == "axes" ? "Origin in component (x, y, z)"
                                        : "World position (x, y, z)",
                    origin);
    else
        origin->hide();
    if (operation == "axes") {
        form.addRow("Normal direction in component", normal);
        form.addRow("X direction in component", axis);
        auto *note = new QLabel("Axes change for every instance. World geometry stays in place.");
        note->setWordWrap(true);
        form.addRow(note);
    } else {
        normal->hide();
        axis->hide();
    }
    auto *error = new QLabel;
    error->setObjectName("componentDialogError");
    error->setWordWrap(true);
    form.addRow(error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (!doc_.isCurrentSnapshot(stamp) || doc_.revision() != revision)
                throw std::runtime_error(
                    "The document changed. Reopen the component dialog to try again.");
            auto vector = [&](const QString &text, bool length = false) {
                const auto value = parseMeasurements(
                    text, length ? inputUnit(doc_.displayUnits()) : "m", QLocale());
                if (value.kind != MeasurementKind::Values || value.values.size() != 3)
                    throw std::runtime_error(
                        "Enter three values separated by commas (semicolons with decimal commas)");
                return Vec3{value.values[0], value.values[1], value.values[2]};
            };
            const auto definition = definitions->currentData().toULongLong();
            if (operation == "create")
                viewport_->makeComponent(name->text());
            else if (operation == "replace")
                viewport_->replaceComponent(definition);
            else if (operation == "instance")
                viewport_->placeComponent(definition, vector(origin->text(), true));
            else {
                const auto plane = DrawingPlane::make(vector(origin->text(), true),
                                                      vector(normal->text()), vector(axis->text()));
                Transform axes;
                axes.m = {plane.xAxis.x,  plane.xAxis.y,  plane.xAxis.z,  0,
                          plane.yAxis.x,  plane.yAxis.y,  plane.yAxis.z,  0,
                          plane.normal.x, plane.normal.y, plane.normal.z, 0,
                          plane.origin.x, plane.origin.y, plane.origin.z, 1};
                viewport_->changeComponentAxes(axes);
            }
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(failure.what());
        }
    });
    dialog.exec();
}
void Window::syncComponentActions() {
    // R094.e (ADR 0166 §4.3, §5): closing a definition edited at its defaults restores the
    // placement's values; values given while a context is open close it.
    syncParameterStates();
    syncHostedActions();
    const auto &selection = viewport_->selectionState().entities();
    const auto scope = viewport_->componentScope();
    const bool one = selection.size() == 1 && selection.begin()->kind == SelectionKind::Body;
    const bool component = one && doc_.instances().contains(selection.begin()->body);
    findChild<QAction *>("component.create")->setEnabled(!selection.empty());
    findChild<QAction *>("component.instance")->setEnabled(!doc_.definitions().empty());
    for (const auto &name : {"component.replace", "component.make_unique", "component.axes"})
        findChild<QAction *>(name)->setEnabled(component || scope);
    auto uses = [&](Id instance) {
        size_t count = 0;
        if (instance)
            for (const auto &[root, record] : doc_.instances())
                count += record->definition == doc_.instances().at(instance)->definition;
        return count;
    };
    findChild<QAction *>("component.make_unique")
        ->setEnabled(uses(component ? selection.begin()->body : scope) > 1);
    componentBanner_->setVisible(scope != 0);
    const bool parametric =
        scope && doc_.definitions().at(doc_.instances().at(scope)->definition)->parameters;
    // Add driver works in any definition context; the rail shows it only there (§3.3).
    findChild<QAction *>("component.add_driver")->setEnabled(scope != 0);
    if (railAddDriver_) {
        railSeparator_->setVisible(scope != 0);
        railAddDriver_->setVisible(scope != 0);
    }
    syncPreviewStrip(scope, parametric);
    syncStatusStrip(scope, parametric);
    if (scope != statusScope_) {
        statusScope_ = scope;
        if (parametric)
            status_->setText("Select a part, then Shift+D adds a driver to it · Formulas use the "
                             "parameters' names");
    }
    if (!scope)
        return;
    const auto definition = doc_.instances().at(scope)->definition;
    const auto count = uses(scope);
    if (componentBanner_->property("parametric").toBool() != parametric) {
        componentBanner_->setProperty("parametric", parametric);
        componentBanner_->style()->unpolish(componentBanner_);
        componentBanner_->style()->polish(componentBanner_);
    }
    // A parametric definition is edited at its default values (ADR 0166 §3.1): the banner is
    // the design's informational strip with Close definition (Esc). Its links take the
    // information colour, which has 4.5:1 contrast on the strip's fill in both themes.
    setContextLabelColor(componentBanner_,
                         parametric ? sheetColors_.information : sheetColors_.accent);
    if (parametric) {
        // §4.3: a placement edited at its defaults gets its own values back on close.
        const auto restores =
            suspension_ && suspension_->placement == scope
                ? suspension_->name.toHtmlEscaped() +
                      "'s own values return when you close the definition. "
                : QString{};
        setContextLabelText(
            componentBanner_,
            QString("Editing the shared definition at its <b>default values</b>. Changes apply "
                    "to all %1 placement%2; their overrides are kept. %3"
                    "<a href=\"close\">Close definition</a> (Esc)")
                .arg(count)
                .arg(count == 1 ? "" : "s")
                .arg(restores));
        componentBanner_->setToolTip("Edits change the shared definition and regenerate every "
                                     "placement. Close definition returns to the model.");
    } else {
        setContextLabelText(
            componentBanner_,
            QString("<b>Editing %1</b> &middot; Changes affect %2 instance%3%4")
                .arg(QString::fromStdString(doc_.definitions().at(definition)->name).toHtmlEscaped())
                .arg(count)
                .arg(count == 1 ? "" : "s")
                .arg(count > 1 ? " &middot; <a href=\"unique\">Make unique</a>" : ""));
        componentBanner_->setToolTip("Geometry edits update every instance of this definition. "
                                     "Make unique isolates the current placement.");
    }
    componentBanner_->setFixedWidth(std::max(100, std::min(640, viewport_->width() - 32)));
    componentBanner_->move(16, breadcrumb_->geometry().bottom() + 8);
    componentBanner_->adjustSize();
    componentBanner_->raise();
    if (previewStrip_ && previewStrip_->isVisible()) {
        previewStrip_->setFixedWidth(componentBanner_->width());
        previewStrip_->move(16, componentBanner_->geometry().bottom() + 8);
        previewStrip_->setFixedHeight(previewStrip_->heightForWidth(previewStrip_->width()));
        previewStrip_->raise();
    }
    placeParameterStrips();
}
void Window::addDriver() {
    if (!driversSection_)
        return;
    const auto status = driversSection_->addDriver();
    if (!status.isEmpty()) {
        status_->setText(status);
        return;
    }
    // The form lives in the Info tab: show it there (an explicit command, not a selection).
    organization_->showInfo();
    QTimer::singleShot(0, driversSection_, [section = driversSection_] {
        section->kindChoice()->setFocus(Qt::ShortcutFocusReason);
    });
}
void Window::setupDefinitionAuthoring() {
    definitionSection_ = organization_->findChild<ComponentDefinitionSection *>();
    driversSection_ = organization_->findChild<ComponentDriversSection *>();
    connect(driversSection_, &ComponentDriversSection::planeChanged, this,
            &Window::syncParameterMeasurement);
    // The rail's Add driver follows a separator and shows only in a definition context; it is
    // not a tool, so it stays out of the exclusive tool group.
    railSeparator_ = modelRail_->addSeparator();
    railAddDriver_ = new QAction("Add driver", this);
    railAddDriver_->setObjectName("railAddDriver");
    railAddDriver_->setToolTip("Add driver (Shift+D)");
    connect(railAddDriver_, &QAction::triggered, this, [this] {
        run([this] { findChild<QAction *>("component.add_driver")->trigger(); });
    });
    modelRail_->addAction(railAddDriver_);
    railSeparator_->setVisible(false);
    railAddDriver_->setVisible(false);
    // The Preview strip (ADR 0166 §3.1): a frame on the viewport under the banner, after it in
    // the Tab chain. It never commits; its fields drive the ghost (§2).
    auto *strip = new QFrame(viewport_);
    strip->setObjectName("parameterPreviewStrip");
    strip->setAccessibleName("Preview");
    strip->setFrameShape(QFrame::NoFrame);
    auto *column = new QVBoxLayout(strip);
    column->setContentsMargins(12, 8, 12, 8);
    column->setSpacing(6);
    auto *fields = new QWidget;
    fields->setObjectName("parameterPreviewFields");
    new FlowLayout(fields);
    column->addWidget(fields);
    // The note and Back to defaults close the strip; the note wraps beside the button.
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(12);
    auto *note =
        new QLabel("Shows the regenerated shape as a ghost. Nothing is changed until you apply.");
    note->setObjectName("parameterPreviewNote");
    note->setWordWrap(true);
    footer->addWidget(note, 1);
    auto *defaults = new QPushButton("Back to defaults");
    defaults->setObjectName("parameterPreviewDefaults");
    defaults->setAutoDefault(false);
    connect(defaults, &QPushButton::clicked, this, [this] { previewDefaults(); });
    defaults->installEventFilter(this);
    footer->addWidget(defaults, 0, Qt::AlignTop);
    column->addLayout(footer);
    previewError_ = new ParameterMessageLabel;
    previewError_->setObjectName("parameterPreviewError");
    previewError_->hide();
    column->addWidget(previewError_);
    strip->hide();
    previewStrip_ = strip;
    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(150);
    connect(previewTimer_, &QTimer::timeout, this, [this] { preview(); });
    setupParameterStates();
}
void Window::syncPreviewStrip(Id scope, bool parametric) {
    if (!previewStrip_)
        return;
    if (!scope || !parametric) {
        if (previewScope_)
            viewport_->clearParameterGhost();
        previewScope_ = 0;
        previewSignature_.clear();
        previewKey_.clear();
        previewStrip_->hide();
        return;
    }
    // Unchanged document, scope and units: the strip keeps its fields and ghost.
    if (scope == previewScope_ && !previewKey_.isEmpty() &&
        previewKey_ == QString::fromStdString(doc_.identity()) + "/" +
                           QString::number(doc_.revision()) + "/" +
                           QString::number(int(doc_.displayUnits())) + "/" +
                           QString::number(doc_.displayPrecision())) {
        previewStrip_->show();
        return;
    }
    QJsonObject description;
    try {
        description = authoring::query(doc_, "component_parameters.describe",
                                       {{"definition", QString::number(
                                                           doc_.instances().at(scope)->definition)}});
    } catch (const std::exception &) {
        previewStrip_->hide();
        return;
    }
    QJsonArray inputs;
    auto parameters = description["parameters"].toArray().toVariantList();
    std::stable_sort(parameters.begin(), parameters.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["position"].toInt() < b.toMap()["position"].toInt();
    });
    for (const auto &value : parameters) {
        const auto parameter = QJsonObject::fromVariantMap(value.toMap());
        if (parameter["mode"].toString() == "input")
            inputs.append(parameter);
    }
    // The fields are rebuilt only when the inputs themselves change; new defaults only refill
    // them, so focus stays in the strip.
    QJsonArray shape;
    for (const auto &entry : inputs) {
        const auto parameter = entry.toObject();
        shape.append(QJsonArray{parameter["name"], parameter["label"], parameter["type"],
                                parameter["options"]});
    }
    const auto signature = QString::number(scope) + "/" +
                           QString::fromUtf8(QJsonDocument(shape).toJson(QJsonDocument::Compact));
    const auto key = QString::fromStdString(doc_.identity()) + "/" +
                     QString::number(doc_.revision()) + "/" +
                     QString::number(int(doc_.displayUnits())) + "/" +
                     QString::number(doc_.displayPrecision());
    previewScope_ = scope;
    if (signature != previewSignature_) {
        previewSignature_ = signature;
        auto *fields = previewStrip_->findChild<QWidget *>("parameterPreviewFields");
        auto *flow = fields->layout();
        while (auto *item = flow->takeAt(0)) {
            if (auto *widget = item->widget()) {
                // Out of the strip at once (and out of findChild), deleted when control returns.
                widget->hide();
                widget->setParent(nullptr);
                widget->deleteLater();
            }
            delete item;
        }
        previewFields_.clear();
        auto *title = new QLabel("Preview");
        title->setObjectName("parameterPreviewTitle");
        flow->addWidget(title);
        QWidget *previous = componentBanner_;
        for (const auto &entry : inputs) {
            const auto parameter = entry.toObject();
            const auto type = parameter["type"].toString();
            auto *pair = new QWidget;
            pair->setObjectName("parameterPreviewPair");
            auto *row = new QHBoxLayout(pair);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(6);
            auto *label = new QLabel(parameter["label"].toString());
            row->addWidget(label);
            QWidget *control{};
            if (type == "boolean") {
                auto *check = new QCheckBox;
                connect(check, &QCheckBox::toggled, this, [this, check] {
                    check->setText(check->isChecked() ? "Shown" : "Hidden");
                    previewTimer_->start();
                });
                control = check;
            } else if (type == "choice") {
                auto *choice = new QComboBox;
                for (const auto &option : parameter["options"].toArray())
                    choice->addItem(option.toObject()["label"].toString(),
                                    option.toObject()["value"].toString());
                connect(choice, &QComboBox::activated, this, [this] { previewTimer_->start(); });
                control = choice;
            } else {
                auto *edit = new QLineEdit;
                edit->setProperty("parameterField", true);
                edit->setMaxLength(1024);
                edit->setMinimumWidth(edit->fontMetrics().averageCharWidth() * 10);
                acceptKeypadDecimal(edit);
                connect(edit, &QLineEdit::textEdited, this, [this] { previewTimer_->start(); });
                control = edit;
            }
            control->setObjectName("parameterPreview_" + parameter["name"].toString());
            control->setAccessibleName(parameter["label"].toString());
            control->installEventFilter(this);
            label->setBuddy(control);
            row->addWidget(control);
            flow->addWidget(pair);
            QWidget::setTabOrder(previous, control);
            previous = control;
            previewFields_.push_back({parameter, control});
        }
        auto *defaults = previewStrip_->findChild<QPushButton *>("parameterPreviewDefaults");
        QWidget::setTabOrder(previous, defaults);
        previewKey_.clear();
    }
    for (std::size_t i = 0; i < previewFields_.size() && i < std::size_t(inputs.size()); ++i)
        previewFields_[i].first = inputs[qsizetype(i)].toObject();
    // Any committed edit clears the ghost: the strip shows the defaults of the new revision.
    if (key != previewKey_) {
        previewKey_ = key;
        previewDefaults();
    }
    previewStrip_->show();
}
void Window::previewDefaults() {
    previewTimer_->stop();
    viewport_->clearParameterGhost();
    previewError_->hide();
    if (previewAction_)
        previewAction_->hide();
    for (auto &[parameter, control] : previewFields_) {
        const auto value = parameter["defaultValue"];
        if (auto *check = qobject_cast<QCheckBox *>(control)) {
            const QSignalBlocker block(check);
            check->setChecked(value.toBool());
            check->setText(value.toBool() ? "Shown" : "Hidden");
        } else if (auto *choice = qobject_cast<QComboBox *>(control)) {
            choice->setCurrentIndex(std::max(0, choice->findData(value.toString())));
        } else if (auto *edit = qobject_cast<QLineEdit *>(control)) {
            edit->setText(parameter["display"].toString());
            edit->setCursorPosition(0);
        }
    }
}
void Window::preview() {
    if (!previewScope_ || !doc_.instances().contains(previewScope_))
        return;
    QJsonObject values;
    for (const auto &[parameter, control] : previewFields_) {
        const auto name = parameter["name"].toString();
        if (auto *check = qobject_cast<QCheckBox *>(control))
            values[name] = check->isChecked();
        else if (auto *choice = qobject_cast<QComboBox *>(control))
            values[name] = choice->currentData().toString();
        else if (auto *edit = qobject_cast<QLineEdit *>(control)) {
            try {
                values[name] = parseParameterText(edit->text(), parameter["type"].toString(),
                                                  doc_.displayUnits());
            } catch (const std::exception &error) {
                viewport_->clearParameterGhost();
                static_cast<ParameterMessageLabel *>(previewError_)
                    ->setTone(ParameterMessageTone::Failure);
                previewError_->setText(parameter["label"].toString() + ": " +
                                       QString::fromUtf8(error.what()));
                previewError_->show();
                previewAction_->hide();
                syncComponentActions();
                return;
            }
        }
    }
    const auto error = viewport_->previewParameters(previewScope_, values);
    if (error.isEmpty()) {
        previewError_->hide();
        previewAction_->hide();
    } else {
        const auto details = error["details"].toObject();
        ParameterMessageContext context;
        context.place = ParameterMessagePlace::Strip;
        context.document = &doc_;
        context.units = doc_.displayUnits();
        context.precision = doc_.displayPrecision();
        context.placement = previewScope_;
        for (const auto &[candidate, control] : previewFields_)
            if (candidate["name"].toString() == details["parameter"].toString()) {
                context.parameter = candidate;
                if (auto *edit = qobject_cast<QLineEdit *>(control))
                    context.typed = edit->text();
            }
        context.value = values[context.parameter["name"].toString()];
        const auto message = parameterMessage(error, context);
        static_cast<ParameterMessageLabel *>(previewError_)->setTone(message.tone);
        previewError_->setText(message.sentence);
        previewError_->setToolTip(error["message"].toString());
        previewError_->show();
        previewActionDriver_ = message.target;
        previewActionDefinition_ = message.definition;
        previewAction_->setVisible(message.action == ParameterMessageAction::ShowDriver);
    }
    syncComponentActions();
}
// ---- R094.e (ADR 0166 §§4, 5): states, refusals and dialogs ------------------------------
void Window::setupParameterStates() {
    // Double-click, Enter, Edit selected context and Edit definition… ask first (§4.1).
    viewport_->setContextOpener([this](Id placement) { return openPlacement(placement); });
    connect(viewport_, &Viewport::parameterAction, this,
            [this](int action, qulonglong target, qulonglong definition) {
                parameterAction(action, target, definition);
            });
    // The Preview strip's refusal names its cause with the §5 mark and offers Show the driver.
    previewAction_ = new QPushButton("Show the driver");
    previewAction_->setObjectName("parameterPreviewAction");
    previewAction_->setAutoDefault(false);
    previewAction_->hide();
    if (auto *column = qobject_cast<QVBoxLayout *>(previewStrip_->layout())) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(previewAction_);
        row->addStretch();
        column->addLayout(row);
    }
    connect(previewAction_, &QPushButton::clicked, this, [this] {
        parameterAction(int(ParameterMessageAction::ShowDriver), previewActionDriver_,
                        previewActionDefinition_);
    });
    // The changed-since-bound strip (§5): amber, a mark and the sentence, then Rebind and Undo.
    auto *strip = new QFrame(viewport_);
    strip->setObjectName("parameterStatusStrip");
    strip->setAccessibleName("Changed since bound");
    strip->setFrameShape(QFrame::NoFrame);
    auto *column = new QVBoxLayout(strip);
    column->setContentsMargins(8, 8, 8, 8);
    column->setSpacing(6);
    auto *line = new QHBoxLayout;
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(8);
    line->addWidget(new RebindMark, 0, Qt::AlignTop);
    statusStripText_ = new QLabel;
    statusStripText_->setObjectName("parameterStatusText");
    statusStripText_->setTextFormat(Qt::RichText);
    statusStripText_->setWordWrap(true);
    line->addWidget(statusStripText_, 1);
    column->addLayout(line);
    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(6);
    statusRebind_ = new QPushButton("Rebind");
    statusRebind_->setObjectName("parameterStatusRebind");
    statusRebind_->setAutoDefault(false);
    // Undo with its key in the design's muted monospace.
    statusUndo_ = new QPushButton;
    statusUndo_->setObjectName("parameterStatusUndo");
    statusUndo_->setAutoDefault(false);
    statusUndo_->setAccessibleName("Undo");
    statusUndo_->setAccessibleDescription("Ctrl+Z");
    auto *undoLine = new QHBoxLayout(statusUndo_);
    undoLine->setContentsMargins(10, 4, 10, 4);
    undoLine->setSpacing(6);
    auto *undoText = new QLabel("Undo");
    auto *undoKey = new QLabel(QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText));
    undoKey->setObjectName("parameterStatusShortcut");
    for (auto *label : {undoText, undoKey}) {
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        undoLine->addWidget(label);
    }
    statusUndo_->setMinimumSize(undoLine->sizeHint());
    buttons->addWidget(statusRebind_);
    buttons->addWidget(statusUndo_);
    buttons->addStretch();
    column->addLayout(buttons);
    strip->hide();
    statusStrip_ = strip;
    connect(statusRebind_, &QPushButton::clicked, this, [this] {
        if (!stripDefinition_ || stripDrivers_.isEmpty())
            return;
        const auto count = stripDrivers_.size();
        run([&] {
            authoring::publish(doc_, *viewport_,
                               {QJsonObject{{"command", "component_parameter.rebind"},
                                            {"definition", QString::number(stripDefinition_)},
                                            {"drivers", stripDrivers_}}},
                               count == 1 ? QString("Rebind driver")
                                          : QString("Rebind %1 drivers").arg(count));
        });
    });
    connect(statusUndo_, &QPushButton::clicked, this,
            [this] { findChild<QAction *>("edit.undo")->trigger(); });
}
void Window::detachParameterStates() {
    // Nothing reaches the window while it is torn down (see ~Window).
    parameterStatesDetached_ = true;
    suspension_.reset();
    if (viewport_) {
        viewport_->setContextOpener({});
        disconnect(viewport_, &Viewport::parameterAction, this, nullptr);
    }
}
bool Window::openPlacement(Id placement) {
    if (parameterStatesDetached_ || parameterStatesBusy_ || !doc_.instances().contains(placement) ||
        within(doc_, viewport_->selectionState().context(), placement) ||
        !showsOwnValues(doc_, placement))
        return false;
    QJsonObject values, description;
    try {
        values = authoring::query(doc_, "component_parameters.values",
                                  {{"body", QString::number(placement)}});
        description = authoring::query(doc_, "component_parameters.describe",
                                       {{"definition", values["definition"]}});
    } catch (const std::exception &) {
        return false;
    }
    std::map<QString, QJsonObject> parameters;
    for (const auto &entry : description["parameters"].toArray())
        parameters[entry.toObject()["name"].toString()] = entry.toObject();
    const auto parent = enclosingPlacement(doc_, placement);
    const bool nested = parent != 0;
    // The values that differ from the defaults, in display order ("Width 1200 mm and Shelves 2").
    std::vector<std::pair<int, QString>> differing;
    QJsonObject overrides, labels;
    QJsonArray promotions; // nested: the copy's new defaults
    const Id copy = doc_.nextDefinitionId();
    for (const auto &entry : values["values"].toArray()) {
        const auto value = entry.toObject();
        const auto name = value["name"].toString();
        const auto source = value["source"].toString();
        const auto &parameter = parameters[name];
        if (source == "override") {
            overrides[name] = value["value"];
            labels[name] = parameter["label"].toString().isEmpty() ? name : parameter["label"].toString();
        }
        if (source != (nested ? "parent" : "override") ||
            sameValue(value["value"], parameter["defaultValue"]))
            continue;
        const auto label = parameter["label"].toString().isEmpty() ? name
                                                                     : parameter["label"].toString();
        differing.push_back({parameter["position"].toInt(), label + " " + value["display"].toString()});
        if (nested)
            try {
                promotions.append(QJsonObject{
                    {"command", "component_parameter.update"},
                    {"definition", QString::number(copy)},
                    {"name", name},
                    {"expression", QString::fromStdString(
                                       literalOf(value["value"], parameter["type"].toString()))}});
            } catch (const std::exception &) {
            }
    }
    std::stable_sort(differing.begin(), differing.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList listed;
    for (const auto &[position, text] : differing)
        listed << text;
    const auto name = bodyName(doc_, placement);
    const auto definitionName =
        QString::fromStdString(doc_.definitions().at(doc_.instances().at(placement)->definition)->name);
    // Make unique and edit: component_parameter.promote (§4.2). A nested placement cannot be
    // promoted (its values come from its parent): it is made unique and the copy's defaults
    // become the values its parent gives it, in the same entry.
    QJsonArray unique;
    if (nested) {
        unique.append(QJsonObject{{"command", "component.make_unique"},
                                  {"body", QString::number(placement)}});
        for (const auto &command : promotions)
            unique.append(command);
    } else {
        unique.append(QJsonObject{{"command", "component_parameter.promote"},
                                  {"body", QString::number(placement)}});
    }
    const QJsonArray reset{QJsonObject{{"command", "component_parameter.reset"},
                                       {"body", QString::number(placement)},
                                       {"all", true}}};
    const auto uniqueLabel = authoring::historyLabel("Make", name) + " unique";
    const auto defaultsLabel = authoring::historyLabel("Edit", definitionName) + " at defaults";
    ParametersScopeDialog dialog(name, ParametersScopeDialog::listed(listed), int(listed.size()),
                                 nested ? bodyName(doc_, parent) : QString{}, this);
    const auto revision = doc_.revision();
    const auto identity = doc_.identity();
    auto refusal = [&](const QJsonObject &error) {
        ParameterMessageContext context;
        context.place = ParameterMessagePlace::Value;
        context.document = &doc_;
        context.placement = placement;
        return parameterMessage(error, context);
    };
    // A refused choice is shown in the dialog, which stays open (§5).
    dialog.setCheck([&](int choice) {
        if (doc_.identity() != identity || doc_.revision() != revision)
            return refusal({{"code", "STALE_REVISION"}, {"message", "STALE_REVISION"}});
        return refusal(authoring::dryRun(
            doc_, choice == ParametersScopeDialog::MakeUnique ? unique : reset));
    });
    const int choice = dialog.exec();
    if (choice != ParametersScopeDialog::MakeUnique &&
        choice != ParametersScopeDialog::EditAtDefaults)
        return true;
    if (doc_.identity() != identity || doc_.revision() != revision) {
        status_->setText("The model changed. Try again.");
        return true;
    }
    {
        QScopedValueRollback busy(parameterStatesBusy_, true);
        try {
            if (choice == ParametersScopeDialog::MakeUnique) {
                authoring::publish(doc_, *viewport_, unique, uniqueLabel);
            } else {
                // Edit definition at defaults (§4.3): the placement shows its template while the
                // context is open; its values return when the context closes.
                authoring::publish(doc_, *viewport_, reset, defaultsLabel);
                suspension_ = ParameterSuspension{doc_.identity(), placement, overrides, labels,
                                                  name, doc_.amendmentStamp()};
            }
        } catch (const std::exception &error) {
            suspension_.reset();
            status_->setText(refusal(parameterRefusal(error)).sentence);
            return true;
        }
        templateScope_ = placement;
        templateScopeOwn_ = false;
        viewport_->enterContext(placement);
    }
    sync();
    viewport_->setFocus(Qt::OtherFocusReason);
    return true;
}
void Window::syncParameterStates() {
    if (parameterStatesDetached_ || parameterStatesBusy_)
        return;
    QScopedValueRollback busy(parameterStatesBusy_, true);
    const auto context = viewport_->selectionState().context();
    auto leave = [&](Id placement, const QString &status) {
        if (within(doc_, viewport_->selectionState().context(), placement))
            viewport_->enterContext(doc_.bodies().contains(placement)
                                        ? doc_.bodies().at(placement)->parent
                                        : 0);
        // After the context changed, and again once the event is done: the selection summary
        // the viewport reports would replace it.
        status_->setText(status);
        QTimer::singleShot(0, this, [this, status] {
            if (!parameterStatesDetached_)
                status_->setText(status);
        });
    };
    if (suspension_) {
        const auto &held = *suspension_;
        if (held.document != doc_.identity() || !doc_.instances().contains(held.placement)) {
            suspension_.reset();
        } else if (showsOwnValues(doc_, held.placement)) {
            // Undoing the reset, or another edit that gives the placement values again, ends
            // the context and forgets the restore (§4.3).
            const auto placement = held.placement;
            const auto name = held.name;
            suspension_.reset();
            leave(placement, name + " has its own values again; the definition was closed");
        } else if (!within(doc_, context, held.placement)) {
            finishSuspension();
        }
    }
    // PARAMETER_TEMPLATE_SCOPE (§5): another client gives the open placement values.
    const auto scope = viewport_->componentScope();
    if (scope != templateScope_) {
        templateScope_ = scope;
        templateScopeOwn_ = scope && showsOwnValues(doc_, scope);
    } else if (scope) {
        const bool own = showsOwnValues(doc_, scope);
        if (own && !templateScopeOwn_ && !suspension_) {
            ParameterMessageContext messageContext;
            messageContext.document = &doc_;
            messageContext.placement = scope;
            const auto message = parameterMessage(
                {{"code", "PARAMETER_TEMPLATE_SCOPE"},
                 {"message", "The placement shows non-default parameter values"},
                 {"details", QJsonObject{{"body", QString::number(scope)}}}},
                messageContext);
            templateScope_ = 0;
            leave(scope, message.sentence);
            templateScope_ = viewport_->componentScope();
            templateScopeOwn_ = templateScope_ && showsOwnValues(doc_, templateScope_);
            return;
        }
        templateScopeOwn_ = own;
    }
}
void Window::finishSuspension() {
    // The context closed: the selection summary the viewport reports next would replace the
    // status text, so it is set once the event is done.
    const auto status = [this](const QString &text) {
        QTimer::singleShot(0, this, [this, text] {
            if (!parameterStatesDetached_)
                status_->setText(text);
        });
    };
    const auto held = *suspension_;
    suspension_.reset();
    // Nothing was edited: the reset is undone, so the records and the saved state return
    // exactly and the entry moves to the redo branch (§4.3).
    if (doc_.canAmend(held.stamp)) {
        viewport_->cancel();
        doc_.undo();
        viewport_->refresh();
        emit viewport_->changed();
        return;
    }
    // Otherwise one entry restores the values the definition still accepts.
    QJsonObject description;
    const auto definition = doc_.instances().at(held.placement)->definition;
    try {
        description = authoring::query(doc_, "component_parameters.describe",
                                       {{"definition", QString::number(definition)}});
    } catch (const std::exception &) {
    }
    std::map<QString, QString> inputs; // name -> label
    for (const auto &entry : description["parameters"].toArray()) {
        const auto parameter = entry.toObject();
        if (parameter["mode"].toString() == "input")
            inputs[parameter["name"].toString()] = parameter["label"].toString().isEmpty()
                                                       ? parameter["name"].toString()
                                                       : parameter["label"].toString();
    }
    auto values = held.values;
    QStringList dropped;
    for (const auto &name : held.values.keys())
        if (!inputs.contains(name)) {
            values.remove(name);
            dropped << held.labels[name].toString(name);
        }
    // Each value the new definition refuses (bounds that now exclude it) is left out.
    for (int attempt = 0; attempt <= int(held.values.size()) && !values.isEmpty(); ++attempt) {
        QJsonObject error;
        try {
            const auto data = authoring::query(doc_, "component_parameters.evaluate",
                                               {{"definition", QString::number(definition)},
                                                {"overrides", values}});
            if (data["status"].toString() == "error")
                error = data["error"].toObject();
        } catch (const std::exception &failure) {
            error = parameterRefusal(failure);
        }
        if (error.isEmpty())
            break;
        const auto details = error.contains("details") ? error["details"].toObject() : error;
        const auto name = details["parameter"].toString();
        if (!values.contains(name)) {
            // A refusal that names no restored value: none can be checked one by one.
            for (const auto &key : values.keys())
                dropped << inputs[key];
            values = {};
            break;
        }
        values.remove(name);
        dropped << inputs[name];
    }
    if (!values.isEmpty())
        try {
            authoring::publish(doc_, *viewport_,
                               {QJsonObject{{"command", "component_parameter.override"},
                                            {"body", QString::number(held.placement)},
                                            {"values", values}}},
                               authoring::historyLabel("Restore", held.name) + " values");
        } catch (const std::exception &error) {
            ParameterMessageContext context;
            context.document = &doc_;
            context.placement = held.placement;
            status(parameterMessage(parameterRefusal(error), context).sentence);
            return;
        }
    if (!dropped.isEmpty())
        status(held.name + "'s values are restored except " +
                         ParametersScopeDialog::listed(dropped) +
                         ", which the definition no longer accepts");
}
void Window::syncStatusStrip(Id scope, bool parametric) {
    if (!statusStrip_)
        return;
    const auto definition = scope && parametric ? doc_.instances().at(scope)->definition : 0;
    if (!definition) {
        stripDefinition_ = 0;
        stripChanged_.clear();
        stripDrivers_ = {};
        statusStrip_->hide();
        return;
    }
    if (definition == stripDefinition_ && stripDocument_ == doc_.identity() &&
        stripRevision_ == doc_.revision())
        return;
    QJsonObject description;
    try {
        description = authoring::query(doc_, "component_parameters.describe",
                                       {{"definition", QString::number(definition)}});
    } catch (const std::exception &) {
        statusStrip_->hide();
        return;
    }
    std::set<Id> changed;
    for (const auto &entry : description["drivers"].toArray())
        if (entry.toObject()["state"].toString() == "changedSinceBound")
            changed.insert(entry.toObject()["id"].toString().toULongLong());
    // Only an edit made while the context is open shows the strip: the drivers it moved from
    // Bound to Changed. Any later edit, Undo or Rebind hides it.
    // A new edit adds an entry and leaves no redo branch; Undo and Redo do not show it.
    const auto position = doc_.history(0, 1).position;
    const bool edited = definition == stripDefinition_ && stripDocument_ == doc_.identity() &&
                        doc_.revision() != stripRevision_ && position > stripPosition_ &&
                        !doc_.canRedo();
    QJsonArray fresh;
    QStringList names;
    if (edited)
        for (const auto driver : changed)
            if (!stripChanged_.contains(driver)) {
                fresh.append(QString::number(driver));
                const auto name = parameterDriverName(doc_, definition, driver);
                names << "<b>" + (name.isEmpty() ? "Driver #" + QString::number(driver) : name)
                                     .toHtmlEscaped() +
                             "</b>";
            }
    stripDefinition_ = definition;
    stripDocument_ = doc_.identity();
    stripRevision_ = doc_.revision();
    stripPosition_ = position;
    stripChanged_ = changed;
    if (fresh.isEmpty()) {
        stripDrivers_ = {};
        statusStrip_->hide();
        return;
    }
    stripDrivers_ = fresh;
    // "Moved the shelf. Repeat shelves now starts from a changed shape. …": the first part is
    // the History label of the edit.
    QString label;
    if (const auto position = doc_.history(0, 1).position; position > 0)
        label = QString::fromStdString(doc_.history(position - 1, 1).entries.front().label);
    if (!label.isEmpty() && !label.endsWith('.'))
        label += '.';
    const auto text =
        (label.isEmpty() ? QString{} : label.toHtmlEscaped() + " ") +
        ParametersScopeDialog::listed(names) +
        (fresh.size() == 1 ? " now starts" : " now start") +
        " from a changed shape. Placements may not look as expected until you rebind.";
    statusStripText_->setText(text);
    auto plain = text;
    plain.remove(QRegularExpression("<[^>]*>"));
    statusStrip_->setAccessibleDescription(plain);
    statusRebind_->setAccessibleDescription(fresh.size() == 1 ? "Rebind the changed driver"
                                                              : "Rebind the changed drivers");
    statusStrip_->show();
    // The status semantics of the design's strip: an accessible alert.
    QAccessibleEvent alert(statusStrip_, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
}
void Window::placeParameterStrips() {
    // The status strip follows the banner and the Preview strip, in the viewport's Tab chain too.
    if (!statusStrip_ || !statusStrip_->isVisible())
        return;
    QWidget *above = previewStrip_ && previewStrip_->isVisible() ? previewStrip_ : componentBanner_;
    statusStrip_->setFixedWidth(componentBanner_->width());
    statusStrip_->move(16, above->geometry().bottom() + 8);
    statusStrip_->setFixedHeight(statusStrip_->layout()->heightForWidth(statusStrip_->width()) > 0
                                     ? statusStrip_->layout()->heightForWidth(statusStrip_->width())
                                     : statusStrip_->sizeHint().height());
    statusStrip_->raise();
    QWidget *previous = componentBanner_;
    if (previewStrip_ && previewStrip_->isVisible())
        previous = previewStrip_->findChild<QPushButton *>("parameterPreviewDefaults");
    QWidget::setTabOrder(previous, statusRebind_);
    QWidget::setTabOrder(statusRebind_, statusUndo_);
}
void Window::parameterAction(int action, Id target, Id definition) {
    if (parameterStatesDetached_)
        return;
    switch (ParameterMessageAction(action)) {
    case ParameterMessageAction::ShowDriver: {
        // Show the driver (§5): the definition opens (through §4 when the placement has its own
        // values) and the Drivers list selects the driver and its member.
        auto scope = viewport_->componentScope();
        if (!scope || doc_.instances().at(scope)->definition != definition) {
            Id placement = 0;
            const auto selected = parameters_ ? parameters_->placement() : 0;
            if (selected && doc_.instances().contains(selected) &&
                doc_.instances().at(selected)->definition == definition)
                placement = selected;
            for (const auto &[root, instance] : doc_.instances())
                if (!placement && instance->definition == definition)
                    placement = root;
            if (!placement)
                return;
            viewport_->openContext(placement);
            scope = viewport_->componentScope();
            if (!scope || doc_.instances().at(scope)->definition != definition)
                return; // Cancelled.
        }
        organization_->showInfo();
        driversSection_->refresh();
        driversSection_->selectDriver(target);
        driversSection_->tree()->setFocus(Qt::OtherFocusReason);
        return;
    }
    case ParameterMessageAction::SelectLocked: {
        // Select #1 (§5): the context closes (restoring a suspended placement) and the locked
        // placement is selected in its parent's context.
        if (!doc_.bodies().contains(target))
            return;
        viewport_->enterContext(doc_.bodies().at(target)->parent);
        viewport_->selectEntities({{target, SelectionKind::Body, 0}});
        const auto &selected = viewport_->selectionState().entities();
        if (selected.size() == 1 && selected.begin()->body == target) {
            viewport_->setFocus(Qt::OtherFocusReason);
            return;
        }
        // A locked placement cannot be selected in the canvas: its Outliner row is made
        // current instead, where it can be unlocked.
        organization_->showOutliner();
        std::function<QTreeWidgetItem *(QTreeWidgetItem *)> find = [&](QTreeWidgetItem *item) {
            if (item->data(0, Qt::UserRole).toULongLong() == target)
                return item;
            for (int i = 0; i < item->childCount(); ++i)
                if (auto *found = find(item->child(i)))
                    return found;
            return static_cast<QTreeWidgetItem *>(nullptr);
        };
        for (int i = 0; i < outliner_->topLevelItemCount(); ++i)
            if (auto *item = find(outliner_->topLevelItem(i))) {
                outliner_->setCurrentItem(item);
                outliner_->scrollToItem(item);
                outliner_->setFocus(Qt::OtherFocusReason);
                break;
            }
        return;
    }
    case ParameterMessageAction::CloseContext: {
        const auto scope = viewport_->componentScope();
        if (scope)
            viewport_->enterContext(doc_.bodies().at(scope)->parent);
        return;
    }
    default:
        return;
    }
}
} // namespace sketchy
