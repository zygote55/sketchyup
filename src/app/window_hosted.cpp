#include "app/unit_display.hpp"
#include "app/window.hpp"
#include "automation/measurements.hpp"
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <numbers>
namespace sketchy {
namespace {
QString vectorText(Vec3 vector, DisplayUnit units, bool lengths) {
    QStringList values;
    for (auto value : {vector.x, vector.y, vector.z})
        values.append(lengths ? displayLength(value, units) : QLocale().toString(value, 'g', 10));
    return values.join(QLocale().decimalPoint() == "," ? "; " : ", ");
}
Vec3 vectorValue(const QString &text, DisplayUnit units, bool lengths) {
    const auto value = parseMeasurements(text, lengths ? inputUnit(units) : "m", QLocale());
    if (value.kind != MeasurementKind::Values || value.values.size() != 3)
        throw std::runtime_error(
            "Enter three values; use semicolons when the decimal separator is a comma");
    return {value.values[0], value.values[1], value.values[2]};
}
QLineEdit *field(QDialog &dialog, QFormLayout &form, const char *name, const QString &label,
                 const QString &text) {
    auto *edit = new QLineEdit(text, &dialog);
    edit->setObjectName(name);
    edit->setAccessibleName(label);
    edit->setMaxLength(1024);
    form.addRow(label, edit);
    return edit;
}
} // namespace
void Window::addHostedActions(QMenu *menu) {
    menu->addSeparator();
    auto add = [&](const char *name, const char *title, const char *command,
                   const std::function<void()> &run) {
        auto *item = action(name, title, {}, run);
        item->setProperty("command", command);
        menu->addAction(item);
    };
    add("component.glue", "Set glue face…", "component.glue", [this] { hostedGlueDialog(); });
    add("component.clear_glue", "Clear shared glue behavior", "component.glue",
        [this] { viewport_->configureComponentGlue(std::nullopt); });
    add("component.attach", "Attach component to selected face", "component.attach", [this] {
        tool(Viewport::Tool::HostedPlacement, "Move on the selected host face to preview");
    });
    add("component.bind", "Bind component at current pose", "component.bind", [this] {
        viewport_->startHostedPlacement(true);
        measurements_->setPlaceholderText("Explicit host-local inset length");
    });
    add("component.placement_options", "Attachment placement options…", "component.attach",
        [this] { hostedOptionsDialog(); });
    add("component.detach", "Detach component and restore opening", "component.detach",
        [this] { viewport_->detachSelectedComponent(); });
    add("component.bake_host", "Bake host openings and release attachments", "component.bake_host",
        [this] { viewport_->bakeSelectedHost(); });
}
void Window::syncHostedActions() {
    const auto &selection = viewport_->selectionState().entities();
    const auto scope = viewport_->componentScope();
    Id root = 0, host = 0;
    int roots = 0, faces = 0;
    for (const auto entity : selection) {
        if (entity.kind == SelectionKind::Body && doc_.instances().contains(entity.body)) {
            root = entity.body;
            ++roots;
        }
        if (entity.kind == SelectionKind::Face) {
            host = entity.body;
            ++faces;
        }
    }
    const bool pair = !scope && selection.size() == 2 && roots == 1 && faces == 1;
    for (const auto *name : {"component.attach", "component.bind"})
        findChild<QAction *>(name)->setEnabled(
            pair && doc_.definitions().at(doc_.instances().at(root)->definition)->glue.has_value());
    findChild<QAction *>("component.glue")
        ->setEnabled(scope && selection.size() == 1 && faces == 1);
    const auto chosen = roots == 1 && selection.size() == 1 ? root : scope;
    findChild<QAction *>("component.clear_glue")
        ->setEnabled(
            chosen &&
            doc_.definitions().at(doc_.instances().at(chosen)->definition)->glue.has_value());
    findChild<QAction *>("component.detach")
        ->setEnabled(chosen && doc_.hostedComponents().attachments.contains(chosen));
    if (selection.size() == 1)
        host = selection.begin()->body;
    findChild<QAction *>("component.bake_host")
        ->setEnabled(selection.size() == 1 && doc_.hostedComponents().hosts.contains(host));
}
void Window::hostedGlueDialog() {
    auto glue = viewport_->selectedGlueFace();
    const auto definition = viewport_->selectedComponentDefinition();
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    const auto selected = viewport_->selectionState().entities();
    const auto context = viewport_->selectionState().context();
    size_t uses = 0;
    for (const auto &[root, instance] : doc_.instances())
        uses += instance->definition == definition;
    QDialog dialog(this);
    dialog.setObjectName("hostedGlueDialog");
    dialog.setWindowTitle("Set component glue face");
    QFormLayout form(&dialog);
    auto *note = new QLabel(QString("The selected face defines alignment. This shared definition "
                                    "has %1 placement(s). Cutting uses this face's outer boundary.")
                                .arg(uses));
    note->setWordWrap(true);
    form.addRow(note);
    auto *anchor = field(dialog, form, "hostedGlueAnchor", "Anchor in selected geometry (x, y, z)",
                         vectorText(glue.anchor, doc_.displayUnits(), true));
    auto *tangent = field(dialog, form, "hostedGlueTangent", "Alignment direction (x, y, z)",
                          vectorText(glue.tangent, doc_.displayUnits(), false));
    auto *cut = new QCheckBox("Cut an opening through the host", &dialog);
    cut->setObjectName("hostedGlueCutsOpening");
    cut->setChecked(glue.cutsOpening);
    form.addRow(cut);
    auto *error = new QLabel(&dialog);
    error->setObjectName("hostedGlueError");
    error->setWordWrap(true);
    form.addRow(error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (!doc_.isCurrentSnapshot(stamp) || doc_.revision() != revision ||
                selected != viewport_->selectionState().entities() ||
                context != viewport_->selectionState().context())
                throw std::runtime_error("The document or selection changed. Reopen glue setup.");
            glue.anchor = vectorValue(anchor->text(), doc_.displayUnits(), true);
            glue.tangent = vectorValue(tangent->text(), doc_.displayUnits(), false);
            glue.cutsOpening = cut->isChecked();
            viewport_->configureComponentGlue(glue);
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(failure.what());
        }
    });
    dialog.resize(540, dialog.sizeHint().height());
    dialog.exec();
}
void Window::hostedOptionsDialog() {
    const auto stamp = doc_.saveStamp();
    const auto options = viewport_->hostedPlacementOptions();
    QDialog dialog(this);
    dialog.setObjectName("hostedPlacementOptions");
    dialog.setWindowTitle("Attachment placement options");
    QFormLayout form(&dialog);
    auto *angle = field(dialog, form, "hostedPlacementAngle", "Rotation on face (degrees)",
                        QLocale().toString(options.angle * 180 / std::numbers::pi, 'g', 10));
    auto *scale = field(dialog, form, "hostedPlacementScale", "Scale (x, y, z; negative mirrors)",
                        vectorText(options.scale, doc_.displayUnits(), false));
    auto *inset = field(dialog, form, "hostedPlacementInset", "Inset along host normal",
                        displayLength(options.inset, doc_.displayUnits()));
    auto *note = new QLabel(
        "Anchor and inset use the host's local coordinates. Rotation follows its first boundary "
        "edge. Scale 1 uses the definition's original size. Bind current pose uses only inset. "
        "After closing, select a component and host face, then Shift+H to preview.");
    note->setWordWrap(true);
    form.addRow(note);
    auto *error = new QLabel(&dialog);
    error->setObjectName("hostedPlacementOptionsError");
    error->setWordWrap(true);
    form.addRow(error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (!doc_.isCurrentSnapshot(stamp))
                throw std::runtime_error("The document changed. Reopen placement options.");
            viewport_->setHostedPlacementOptions(
                {parseAngle(angle->text(), "deg", QLocale()),
                 parseLength(inset->text(), inputUnit(doc_.displayUnits()), QLocale()),
                 vectorValue(scale->text(), doc_.displayUnits(), false)});
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(failure.what());
        }
    });
    dialog.resize(540, dialog.sizeHint().height());
    dialog.exec();
}
} // namespace sketchy
