#include "app/unit_display.hpp"
#include "app/window.hpp"
#include "automation/measurements.hpp"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
namespace sketchy {
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
    origin->setObjectName("componentOrigin");
    origin->setAccessibleName(operation == "axes" ? "Origin in component coordinates"
                                                  : "World placement position");
    auto *normal = new QLineEdit(QStringList{"0", "0", "1"}.join(separator), &dialog);
    normal->setObjectName("componentNormal");
    auto *axis = new QLineEdit(QStringList{"1", "0", "0"}.join(separator), &dialog);
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
    if (!scope)
        return;
    const auto definition = doc_.instances().at(scope)->definition;
    const auto count = uses(scope);
    componentBanner_->setText(
        QString("<b>Editing %1</b> &middot; Changes affect %2 instance%3%4")
            .arg(QString::fromStdString(doc_.definitions().at(definition)->name).toHtmlEscaped())
            .arg(count)
            .arg(count == 1 ? "" : "s")
            .arg(count > 1 ? " &middot; <a href=\"unique\">Make unique</a>" : ""));
    componentBanner_->setToolTip("Geometry edits update every instance of this definition. Make "
                                 "unique isolates the current placement.");
    componentBanner_->setPalette(breadcrumb_->palette());
    componentBanner_->setFixedWidth(std::max(100, std::min(640, viewport_->width() - 32)));
    componentBanner_->move(16, breadcrumb_->geometry().bottom() + 8);
    componentBanner_->adjustSize();
    componentBanner_->raise();
}
} // namespace sketchy
