#include "app/keypad_decimal.hpp"
#include "app/component_definition_section.hpp"
#include "app/component_drivers_section.hpp"
#include "app/context_label.hpp"
#include "app/formula_field.hpp"
#include "app/parameter_value_field.hpp"
#include "app/unit_display.hpp"
#include "app/window.hpp"
#include "automation/measurements.hpp"
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
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
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
        setContextLabelText(
            componentBanner_,
            QString("Editing the shared definition at its <b>default values</b>. Changes apply "
                    "to all %1 placement%2; their overrides are kept. "
                    "<a href=\"close\">Close definition</a> (Esc)")
                .arg(count)
                .arg(count == 1 ? "" : "s"));
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
    previewError_ = new QLabel;
    previewError_->setObjectName("parameterPreviewError");
    previewError_->setWordWrap(true);
    previewError_->setTextFormat(Qt::PlainText);
    previewError_->hide();
    column->addWidget(previewError_);
    strip->hide();
    previewStrip_ = strip;
    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(150);
    connect(previewTimer_, &QTimer::timeout, this, [this] { preview(); });
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
                previewError_->setText(parameter["label"].toString() + ": " +
                                       QString::fromUtf8(error.what()));
                previewError_->show();
                syncComponentActions();
                return;
            }
        }
    }
    const auto error = viewport_->previewParameters(previewScope_, values);
    if (error.isEmpty()) {
        previewError_->hide();
    } else {
        const auto details = error["details"].toObject();
        QJsonObject parameter;
        QString typed;
        for (const auto &[candidate, control] : previewFields_)
            if (candidate["name"].toString() == details["parameter"].toString()) {
                parameter = candidate;
                if (auto *edit = qobject_cast<QLineEdit *>(control))
                    typed = edit->text();
            }
        previewError_->setText(parameterErrorSentence(error, typed, parameter, doc_.displayUnits(),
                                                      doc_.displayPrecision(),
                                                      values[parameter["name"].toString()]));
        previewError_->setToolTip(error["message"].toString());
        previewError_->show();
    }
    syncComponentActions();
}
} // namespace sketchy
