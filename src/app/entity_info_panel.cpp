#include "app/entity_info_panel.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include "core/groups.hpp"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>
namespace sketchy {
namespace {
QString number(double value) { return QLocale().toString(value, 'g', 8); }
QString vector(Vec3 value, DisplayUnit unit, int precision) {
    return displayLength(value.x, unit, precision) + " · " +
           displayLength(value.y, unit, precision) + " · " + displayLength(value.z, unit, precision);
}
QJsonArray point(Vec3 value) { return {value.x, value.y, value.z}; }
QString unavailable(const std::string &status) {
    if (status == "open_boundary")
        return "Not a solid: open boundary";
    if (status == "non_manifold")
        return "Not a solid: non-manifold boundary";
    if (status == "inconsistent_winding")
        return "Not a solid: inconsistent face winding";
    if (status == "loose_geometry")
        return "Not a solid: loose geometry";
    if (status == "self_intersection")
        return "Not a solid: intersecting faces";
    if (status == "degenerate")
        return "Not a solid: degenerate shell";
    if (status == "multiple_records")
        return "Unavailable: multiple geometry records";
    if (status == "multiple_shells")
        return "Unavailable: multiple shells";
    if (status == "ambiguous_containment")
        return "Unavailable: ambiguous shell containment";
    if (status == "analysis_limit")
        return "Unavailable: analysis limit";
    if (status == "not_a_context")
        return "Select a whole entity for volume";
    return "No closed surface";
}
QLabel *value(QFormLayout *form, const QString &label, const QString &name) {
    auto *result = new QLabel;
    result->setObjectName(name);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    form->addRow(label, result);
    return result;
}
} // namespace
EntityInfoPanel::EntityInfoPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    setObjectName("entityInfoPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    frame_ = new QComboBox;
    frame_->setObjectName("entityInfoFrame");
    frame_->setAccessibleName("Measurement coordinate frame");
    frame_->addItems({"World coordinates", "Parent coordinates", "Intrinsic entity coordinates"});
    layout->addWidget(frame_);
    setFocusProxy(frame_);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("entityInfoScroll");
    scroll->setAccessibleName("Selected entity properties");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *form = new QFormLayout(content);
    form->setContentsMargins(2, 2, 2, 2);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    name_ = value(form, "Name", "entityInfoName");
    type_ = value(form, "Selection", "entityInfoType");
    position_ = value(form, "Origin", "entityInfoPosition");
    dimensions_ = value(form, "Bounds", "entityInfoDimensions");
    length_ = value(form, "Edge length", "entityInfoLength");
    area_ = value(form, "Area", "entityInfoArea");
    volume_ = value(form, "Volume", "entityInfoVolume");
    position_->setToolTip("Entity origin: x · y · z in the selected frame");
    dimensions_->setToolTip("Axis-aligned bounds: x · y · z in the selected frame");
    tag_ = value(form, "Tag", "entityInfoTag");
    color_ = value(form, "Materials", "entityInfoColor");
    properties_ = value(form, "Semantic properties", "entityInfoProperties");
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    edit_ = new QPushButton("Edit entity…");
    edit_->setObjectName("entityInfoEdit");
    edit_->setToolTip("Edit name, tag, origin and dimensions (F2)");
    layout->addWidget(edit_);
    editAction_ = new QAction(this);
    editAction_->setShortcut(Qt::Key_F2);
    editAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(editAction_);
    problems_ = new QPushButton("Inspect problem geometry");
    problems_->setObjectName("entityInfoProblems");
    layout->addWidget(problems_);
    error_ = new QLabel;
    error_->setObjectName("entityInfoError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    layout->addWidget(error_);
    connect(frame_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(edit_, &QPushButton::clicked, this, [this] { edit(); });
    connect(editAction_, &QAction::triggered, this, [this] { edit(); });
    connect(problems_, &QPushButton::clicked, this, [this] { inspectProblems(); });
}
void EntityInfoPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refresh();
}
void EntityInfoPanel::refresh() {
    if (!isVisible())
        return;
    const auto &selected = view_.selectionState().entities();
    if (selected.size() != 1) {
        entity_.reset();
        measured_.reset();
        name_->setText(selected.empty() ? "Select an entity" : "Select one entity for details");
        for (auto *label :
             {type_, tag_, color_, position_, dimensions_, length_, area_, volume_, properties_})
            label->clear();
        edit_->setEnabled(false);
        editAction_->setEnabled(false);
        problems_->setEnabled(false);
        return;
    }
    const auto entity = *selected.begin();
    try {
        if (!entity_ || *entity_ != entity || !doc_.isCurrentSnapshot(stamp_)) {
            measured_ = measureEntity(doc_, entity);
            entity_ = entity;
            stamp_ = doc_.saveStamp();
            error_->clear();
        }
        const auto &body = *doc_.bodies().at(entity.body);
        const auto &measured = *measured_;
        const auto &frame = frame_->currentIndex() == 0   ? measured.world
                            : frame_->currentIndex() == 1 ? measured.parent
                                                          : measured.local;
        name_->setText(QString::fromStdString(body.name) + " #" + QString::number(entity.body));
        const auto kind = entity.kind == SelectionKind::Face       ? "Face"
                          : entity.kind == SelectionKind::Edge     ? "Edge"
                          : entity.kind == SelectionKind::Guide    ? "Guide"
                          : doc_.instances().contains(entity.body) ? "Component"
                          : body.referenceImage                    ? "Reference image"
                          : body.kind == BodyKind::Group           ? "Group"
                                                                   : "Geometry";
        type_->setText(
            QString("%1 · %2 faces · %3 edges").arg(kind).arg(measured.faces).arg(measured.edges));
        tag_->setText(body.tag ? QString::fromStdString(doc_.tags().at(body.tag)->name)
                               : "Untagged");
        auto sideText = [&](bool back) {
            const auto appearance =
                surfaceAppearance(doc_.materials(), body,
                                  entity.kind == SelectionKind::Face ? entity.entity : 0, back);
            const auto &rgb = appearance.color;
            return (appearance.material
                        ? QString::fromStdString(doc_.materials().at(appearance.material)->name) +
                              " · "
                        : QString{}) +
                   QColor::fromRgbF(rgb[0], rgb[1], rgb[2]).name() +
                   QString(" · %1%").arg(qRound(appearance.opacity * 100));
        };
        color_->setText("Front: " + sideText(false) + "\nBack: " + sideText(true) +
                        (entity.kind == SelectionKind::Body && !body.faceMaterials.empty()
                             ? "\nIndividual faces have overrides"
                             : ""));
        Vec3 origin = frame_->currentIndex() == 0   ? measured.worldOrigin
                      : frame_->currentIndex() == 1 ? measured.parentOrigin
                                                    : Vec3{};
        if (frame_->currentIndex() == 2 && entity.kind == SelectionKind::Guide)
            origin = body.guides.at(entity.entity).origin;
        position_->setText(vector(origin, doc_.displayUnits(), doc_.displayPrecision()));
        dimensions_->setText(frame.bounds ? vector(frame.bounds->dimensions(), doc_.displayUnits(),
                                                 doc_.displayPrecision())
                                          : "No finite bounds");
        length_->setText(frame.infiniteLength ? "Infinite guide"
                                              : displayLength(frame.length, doc_.displayUnits(),
                                                              doc_.displayPrecision()));
        area_->setText(displayMeasure(frame.area, 2, doc_.displayUnits(), doc_.displayPrecision()));
        volume_->setText(frame.volume ? displayMeasure(*frame.volume, 3, doc_.displayUnits(),
                                                     doc_.displayPrecision())
                                      : unavailable(measured.solid.status));
        QStringList properties;
        for (const auto &[key, property] : body.properties) {
            if (properties.size() == 4) {
                properties.append(QString("… %1 properties total").arg(body.properties.size()));
                break;
            }
            QString text;
            std::visit(
                [&](const auto &v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::string>)
                        text = QString::fromStdString(v);
                    else if constexpr (std::is_same_v<T, bool>)
                        text = v ? "true" : "false";
                    else
                        text = number(v);
                },
                property);
            properties.append(QString::fromStdString(key) + ": " + text.left(80));
        }
        properties_->setText(properties.empty() ? "None" : properties.join('\n'));
        const bool editable =
            entity.kind == SelectionKind::Body && !view_.selectionState().locked(doc_, entity.body);
        edit_->setEnabled(editable);
        editAction_->setEnabled(editable);
        const auto &report = measured.solid;
        problems_->setEnabled(
            measured.solidBody &&
            (!report.faces.empty() || !report.edges.empty() || !report.vertices.empty()));
    } catch (const std::exception &error) {
        measured_.reset();
        entity_.reset();
        edit_->setEnabled(false);
        editAction_->setEnabled(false);
        problems_->setEnabled(false);
        error_->setText(error.what());
    }
}
void EntityInfoPanel::edit() {
    refresh();
    if (!entity_ || !measured_ || entity_->kind != SelectionKind::Body || !edit_->isEnabled())
        return;
    const auto entity = *entity_;
    const auto measured = *measured_;
    const auto body = doc_.bodies().at(entity.body);
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    const auto context = view_.selectionState().context();
    QDialog dialog(this);
    dialog.setObjectName("entityInfoDialog");
    dialog.setWindowTitle("Edit entity");
    auto *form = new QFormLayout(&dialog);
    auto *name = new QLineEdit(QString::fromStdString(body->name));
    name->setObjectName("entityEditName");
    name->setMaxLength(1024);
    form->addRow("Name", name);
    auto *tag = new QComboBox;
    tag->setObjectName("entityEditTag");
    tag->addItem("Untagged", QString("0"));
    for (const auto &[id, record] : doc_.tags())
        if (!record->folder)
            tag->addItem(QString::fromStdString(record->name) + " #" + QString::number(id),
                         QString::number(id));
    tag->setCurrentIndex(tag->findData(QString::number(body->tag)));
    form->addRow("Tag", tag);
    auto *frame = new QComboBox;
    frame->setObjectName("entityEditFrame");
    frame->addItems({"World coordinates", "Parent coordinates"});
    frame->setCurrentIndex(frame_->currentIndex() == 1 ? 1 : 0);
    form->addRow("Frame", frame);
    std::array<QLineEdit *, 3> position{}, dimensions{};
    auto fields = [&](const QString &label, const QString &prefix,
                      std::array<QLineEdit *, 3> &result) {
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        for (size_t i = 0; i < 3; ++i) {
            auto *field = new QLineEdit;
            field->setObjectName(prefix + QString::number(i));
            field->setAccessibleName(label + " " + QString("xyz")[i]);
            field->setMinimumWidth(85);
            result[i] = field;
            layout->addWidget(field);
        }
        form->addRow(label + " (x, y, z)", row);
    };
    fields("Origin", "entityEditPosition", position);
    fields("Dimensions", "entityEditDimensions", dimensions);
    Vec3 originalPosition{}, originalDimensions{};
    auto load = [&] {
        originalPosition =
            frame->currentIndex() == 0 ? measured.worldOrigin : measured.parentOrigin;
        const auto &bounds =
            frame->currentIndex() == 0 ? measured.world.bounds : measured.parent.bounds;
        originalDimensions = bounds ? bounds->dimensions() : Vec3{};
        const std::array<double, 3> p{originalPosition.x, originalPosition.y, originalPosition.z},
            d{originalDimensions.x, originalDimensions.y, originalDimensions.z};
        for (size_t i = 0; i < 3; ++i) {
            position[i]->setText(displayLength(p[i], doc_.displayUnits(), fullDisplayPrecision));
            dimensions[i]->setText(displayLength(d[i], doc_.displayUnits(), fullDisplayPrecision));
            dimensions[i]->setEnabled(bool(bounds));
        }
    };
    load();
    connect(frame, &QComboBox::currentIndexChanged, &dialog, load);
    auto *note =
        new QLabel("Dimensions scale about the minimum bounds. Use push/pull to add thickness.");
    note->setWordWrap(true);
    form->addRow(note);
    auto *error = new QLabel;
    error->setObjectName("entityEditError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp) ||
                view_.selectionState().context() != context)
                throw std::runtime_error(
                    "The document or editing context changed. Reopen Entity info to edit it.");
            auto read = [&](const std::array<QLineEdit *, 3> &fields, Vec3 original) {
                const std::array<double *, 3> output{&original.x, &original.y, &original.z};
                for (size_t i = 0; i < 3; ++i)
                    if (fields[i]->isEnabled() && fields[i]->isModified())
                        *output[i] = parseLength(fields[i]->text(), inputUnit(doc_.displayUnits()),
                                                 QLocale());
                return original;
            };
            const auto p = read(position, originalPosition),
                       d = read(dimensions, originalDimensions);
            const auto id = QString::number(entity.body);
            const auto frameName = frame->currentIndex() == 0 ? "world" : "parent";
            QJsonArray commands;
            if (length(d - originalDimensions) > tolerance)
                commands.append(QJsonObject{{"command", "entity.dimensions"},
                                            {"body", id},
                                            {"dimensions", point(d)},
                                            {"frame", frameName}});
            // Scaling about the bounds can move the origin. Apply an explicitly
            // edited position last, even when it repeats the original position.
            if (std::any_of(position.begin(), position.end(),
                            [](const auto *field) { return field->isModified(); }))
                commands.append(QJsonObject{{"command", "entity.position"},
                                            {"body", id},
                                            {"position", point(p)},
                                            {"frame", frameName}});
            if (name->text() != QString::fromStdString(body->name))
                commands.append(
                    QJsonObject{{"command", "scene.rename"}, {"body", id}, {"name", name->text()}});
            if (tag->currentData().toString() != QString::number(body->tag))
                commands.append(QJsonObject{{"command", "tag.assign"},
                                            {"body", id},
                                            {"tag", tag->currentData().toString()}});
            if (!commands.empty())
                view_.organize(commands);
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(failure.what());
        }
    });
    dialog.exec();
}
void EntityInfoPanel::inspectProblems() {
    refresh();
    if (!measured_ || !measured_->solidBody)
        return;
    const auto id = measured_->solidBody;
    const auto report = measured_->solid;
    try {
        SelectionSet selected;
        for (auto face : report.faces)
            selected.insert({id, SelectionKind::Face, face});
        for (auto edge : report.edges)
            selected.insert({id, SelectionKind::Edge, edge});
        const auto adjacency =
            doc_.bodies().at(id)->topology.adjacency(doc_.bodies().at(id)->surface);
        for (auto vertex : report.vertices)
            if (adjacency.vertexEdges.contains(vertex))
                for (auto edge : adjacency.vertexEdges.at(vertex))
                    selected.insert({id, SelectionKind::Edge, edge});
        if (selected.empty())
            return;
        view_.enterContext(enclosingGroup(doc_, id));
        view_.selectEntities(selected);
        view_.setFocus();
    } catch (const std::exception &failure) {
        error_->setText(failure.what());
    }
}
} // namespace sketchy
