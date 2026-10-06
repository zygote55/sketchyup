#include "app/texture_editor.hpp"
#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include "automation/measurements.hpp"
#include "core/face_textures.hpp"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <numbers>

namespace sketchy {
namespace {
QJsonArray point(Vec3 value) { return {value.x, value.y, value.z}; }
double scalar(QLineEdit *field, double original) {
    if (!field->isModified())
        return original;
    bool ok = false;
    const auto result = QLocale().toDouble(field->text(), &ok);
    if (!ok || !std::isfinite(result))
        throw std::runtime_error("Enter a finite number for rotation and UV offsets");
    return result;
}
} // namespace
void editTextureMapping(Document &doc, Viewport &view, QWidget *parent, int side) {
    const auto faces = view.selectedTextureFaces();
    const std::vector<SelectedEntity> sources(faces.begin(), faces.end());
    const auto stamp = doc.saveStamp();
    const auto revision = doc.revision(), context = view.selectionState().context();
    QDialog dialog(parent);
    dialog.setObjectName("textureDialog");
    dialog.setWindowTitle("Texture mapping");
    auto *form = new QFormLayout(&dialog);
    auto *note = new QLabel(QString("Apply one projection to %1 selected face(s). "
                                    "Opened components update their shared definition.")
                                .arg(faces.size()));
    note->setWordWrap(true);
    form->addRow(note);
    auto *source = new QComboBox;
    source->setObjectName("textureSource");
    for (const auto face : sources)
        source->addItem(QString("Body %1 · Face %2").arg(face.body).arg(face.entity));
    form->addRow("Projection from", source);
    auto *sourceSide = new QComboBox;
    sourceSide->setObjectName("textureSourceSide");
    sourceSide->addItems({"Front", "Back"});
    sourceSide->setCurrentIndex(side == 1 ? 1 : 0);
    form->addRow("Source side", sourceSide);
    auto *target = new QComboBox;
    target->setObjectName("textureTargetSide");
    target->addItems({"Front", "Back", "Both sides"});
    target->setCurrentIndex(side);
    form->addRow("Apply to", target);
    auto *space = new QComboBox;
    space->setObjectName("textureSpace");
    space->addItems({"World", "Each body's local coordinates"});
    form->addRow("Coordinates", space);
    auto field = [&](const char *id, const char *label) {
        auto *input = new QLineEdit;
        input->setObjectName(id);
        input->setAccessibleName(label);
        form->addRow(label, input);
        return input;
    };
    auto *x = field("textureOriginX", "Origin X");
    auto *y = field("textureOriginY", "Origin Y");
    auto *z = field("textureOriginZ", "Origin Z");
    auto *width = field("textureWidth", "Repeat width");
    auto *height = field("textureHeight", "Repeat height");
    auto *rotation = field("textureRotation", "Rotate by (degrees)");
    auto *u = field("textureOffsetU", "U offset (repeats)");
    auto *v = field("textureOffsetV", "V offset (repeats)");
    auto *hint = new QLabel("Lengths accept document units or explicit units, such as 250 mm. "
                            "Negative repeat sizes mirror that axis. Existing shear is retained. "
                            "Changing source or coordinates reloads its saved projection.");
    hint->setWordWrap(true);
    form->addRow(hint);
    auto *error = new QLabel;
    error->setObjectName("textureError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Apply");
    form->addRow(buttons);
    TextureMapping base;
    double initialWidth{}, initialHeight{};
    bool loaded = false;
    auto current = [&] {
        if (doc.revision() != revision || !doc.isCurrentSnapshot(stamp) ||
            view.selectionState().context() != context || view.selectedTextureFaces() != faces)
            throw std::runtime_error(
                "The model or selection changed; cancel and reopen this editor");
    };
    auto load = [&] {
        try {
            current();
            const auto face = sources.at(size_t(source->currentIndex()));
            base = effectiveFaceTextureMapping(*doc.bodies().at(face.body), face.entity,
                                               sourceSide->currentIndex() == 1);
            if (space->currentIndex() == 0)
                base = transformTextureMapping(base, doc.worldTransform(face.body));
            const auto area = length(cross(base.uGradient, base.vGradient));
            initialWidth = length(base.vGradient) / area;
            initialHeight = length(base.uGradient) / area;
            auto lengthText = [&](double value) {
                return std::abs(value) > coordinateLimit || (value != 0 && std::abs(value) < 1e-7)
                           ? QLocale().toString(value, 'g', 12) + " m"
                           : displayLength(value, doc.displayUnits());
            };
            x->setText(lengthText(base.origin.x));
            y->setText(lengthText(base.origin.y));
            z->setText(lengthText(base.origin.z));
            width->setText(lengthText(initialWidth));
            height->setText(lengthText(initialHeight));
            rotation->setText("0");
            u->setText(QLocale().toString(base.offset.u, 'g', 12));
            v->setText(QLocale().toString(base.offset.v, 'g', 12));
            loaded = true;
            error->clear();
        } catch (const std::exception &failure) {
            loaded = false;
            error->setText(failure.what());
        }
        buttons->button(QDialogButtonBox::Ok)->setEnabled(loaded);
    };
    for (auto *choice : {source, sourceSide, space})
        QObject::connect(choice, &QComboBox::currentIndexChanged, &dialog, load);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            current();
            if (!loaded)
                throw std::runtime_error("Reload a valid source projection");
            bool changed = faces.size() > 1 || target->currentIndex() != side ||
                           sourceSide->currentIndex() != (side == 1 ? 1 : 0);
            for (auto *input : {x, y, z, width, height, rotation, u, v})
                changed |= input->isModified();
            if (!changed) {
                dialog.accept();
                return;
            }
            auto distance = [&](QLineEdit *input, double original) {
                return input->isModified()
                           ? parseLength(input->text(), inputUnit(doc.displayUnits()), QLocale())
                           : original;
            };
            const auto w = distance(width, initialWidth), h = distance(height, initialHeight);
            if (!std::isfinite(w) || !std::isfinite(h) || w == 0 || h == 0)
                throw std::runtime_error("Repeat width and height must be nonzero finite lengths");
            const auto normalVector = cross(base.uGradient, base.vGradient);
            const auto normal = normalVector * (1 / length(normalVector));
            const auto turn = Transform::rotation(
                normal, std::remainder(scalar(rotation, 0), 360.) * std::numbers::pi / 180.);
            auto mapping = base;
            mapping.origin = {distance(x, base.origin.x), distance(y, base.origin.y),
                              distance(z, base.origin.z)};
            mapping.uGradient = turn.vector(base.uGradient * (initialWidth / w));
            mapping.vGradient = turn.vector(base.vGradient * (initialHeight / h));
            mapping.offset = {scalar(u, base.offset.u), scalar(v, base.offset.v)};
            mapping.validate();
            view.mapSelectedTextures(
                QJsonObject{{"type", "affine"},
                            {"origin", point(mapping.origin)},
                            {"uGradient", point(mapping.uGradient)},
                            {"vGradient", point(mapping.vGradient)},
                            {"offset", QJsonArray{mapping.offset.u, mapping.offset.v}}},
                space->currentIndex() == 0 ? "world" : "local", target->currentIndex());
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(failure.what());
        }
    });
    load();
    dialog.exec();
}
} // namespace sketchy
