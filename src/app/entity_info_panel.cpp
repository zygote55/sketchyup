#include "app/entity_info_panel.hpp"
#include "app/component_definition_section.hpp"
#include "app/component_drivers_section.hpp"
#include "app/component_parameters_section.hpp"
#include "app/keypad_decimal.hpp"
#include "app/terrain_edits_section.hpp"
#include "app/terrain_section.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include "core/groups.hpp"
#include "core/solid_words.hpp"
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
// The Info tab's stack of sections (ADR 0166 §1.1). Its height for a width counts each
// section as the box layout places it: at no less than its minimum height, and at the height
// for its width bounded by its minimum width (a section wider than a narrow Tray). So the
// scroll area never squeezes a section below the height its wrapped text needs.
class SectionStack final : public QWidget {
  public:
    int heightForWidth(int width) const override {
        auto *box = static_cast<QBoxLayout *>(layout());
        const auto margins = box->contentsMargins();
        const int inner = width - margins.left() - margins.right();
        int height = margins.top() + margins.bottom(), shown = 0;
        for (int i = 0; i < box->count(); ++i) {
            auto *item = box->itemAt(i);
            if (item->isEmpty())
                continue;
            const int bounded =
                std::clamp(inner, item->minimumSize().width(), item->maximumSize().width());
            height += std::max(item->minimumSize().height(), item->hasHeightForWidth()
                                                                 ? item->heightForWidth(bounded)
                                                                 : item->sizeHint().height());
            ++shown;
        }
        return height + std::max(0, shown - 1) * box->spacing();
    }
};
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
    // One scroll area holds the stack of sections; rows wrap rather than scroll sideways.
    auto *scroll = new QScrollArea;
    scroll->setObjectName("entityInfoScroll");
    scroll->setAccessibleName("Selected entity properties");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *sections = new SectionStack;
    sections->setObjectName("entityInfoSections");
    // The stack takes the Tray's width: a wide control clips itself instead of widening
    // every section past the Tray (ADR 0166 §8).
    sections->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *stack = new QVBoxLayout(sections);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(4);
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    frame_ = new QComboBox;
    frame_->setObjectName("entityInfoFrame");
    frame_->setAccessibleName("Measurement coordinate frame");
    frame_->addItems({"World coordinates", "Parent coordinates", "Intrinsic entity coordinates"});
    bodyLayout->addWidget(frame_);
    setFocusProxy(frame_);
    auto *content = new QWidget;
    content->setObjectName("entityInfoForm");
    auto *form = new QFormLayout(content);
    form->setContentsMargins(2, 2, 2, 2);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    name_ = value(form, "Name", "entityInfoName");
    type_ = value(form, "Selection", "entityInfoType");
    position_ = value(form, "Origin", "entityInfoPosition");
    site_ = value(form, "Site", "entityInfoSite");
    site_->setAccessibleName("Site coordinates");
    site_->setToolTip("Site coordinates (E · N · Z) of the entity origin, or of an edge's two "
                      "endpoints with their model coordinates: site = model + site origin");
    form_ = form;
    dimensions_ = value(form, "Bounds", "entityInfoDimensions");
    length_ = value(form, "Edge length", "entityInfoLength");
    area_ = value(form, "Area", "entityInfoArea");
    volume_ = value(form, "Volume", "entityInfoVolume");
    position_->setToolTip("Entity origin: x · y · z in the selected frame");
    dimensions_->setToolTip("Axis-aligned bounds: x · y · z in the selected frame");
    tag_ = value(form, "Tag", "entityInfoTag");
    color_ = value(form, "Materials", "entityInfoColor");
    properties_ = value(form, "Semantic properties", "entityInfoProperties");
    bodyLayout->addWidget(content);
    edit_ = new QPushButton("Edit entity…");
    edit_->setObjectName("entityInfoEdit");
    edit_->setToolTip("Edit name, tag, origin and dimensions (F2)");
    bodyLayout->addWidget(edit_);
    editAction_ = new QAction(this);
    editAction_->setShortcut(Qt::Key_F2);
    editAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(editAction_);
    problems_ = new QPushButton("Inspect problem geometry");
    problems_->setObjectName("entityInfoProblems");
    bodyLayout->addWidget(problems_);
    error_ = new QLabel;
    error_->setObjectName("entityInfoError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    bodyLayout->addWidget(error_);
    info_ = new CollapsibleSection("Entity info", body);
    info_->setObjectName("entityInfoSection");
    stack->addWidget(info_);
    // R095.e1: the Terrain section, under Entity info while a terrain is selected.
    terrain_ = new TerrainSection(doc_, view_);
    stack->addWidget(terrain_);
    // R096.f2 (UX §15.6): Terrain edits follows the Terrain section.
    edits_ = new TerrainEditsSection(doc_, view_);
    stack->addWidget(edits_);
    parameters_ = new ComponentParametersSection(doc_, view_);
    stack->addWidget(parameters_);
    definition_ = new ComponentDefinitionSection(doc_, view_);
    stack->addWidget(definition_);
    drivers_ = new ComponentDriversSection(doc_, view_);
    stack->addWidget(drivers_);
    stack->addStretch(1);
    scroll->setWidget(sections);
    layout->addWidget(scroll, 1);
    // A collapsed Entity info section hides the frame; Tab into the tab then lands on its header.
    connect(info_->header(), &QPushButton::toggled, this, [this](bool open) {
        setFocusProxy(open ? static_cast<QWidget *>(frame_) : info_->header());
    });
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
    // The parameters also answer the Measurements box while this tab is hidden.
    parameters_->refresh();
    definition_->refresh();
    drivers_->refresh();
    if (!isVisible())
        return;
    terrain_->refresh();
    edits_->refresh();
    const auto &selected = view_.selectionState().entities();
    if (selected.size() != 1) {
        entity_.reset();
        measured_.reset();
        name_->setText(selected.empty() ? "Select an entity" : "Select one entity for details");
        for (auto *label : {type_, tag_, color_, position_, dimensions_, length_, area_, volume_,
                            properties_, site_})
            label->clear();
        form_->setRowVisible(site_, false);
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
        // R095.e2 (ADR 0168, Site): with a site origin, the same point (world origin) in site
        // coordinates; an edge shows both endpoints in model and site coordinates.
        if (const auto &site = doc_.site()) {
            const auto units = doc_.displayUnits();
            const auto precision = doc_.displayPrecision();
            auto siteText = [&](Vec3 model) {
                const auto at = modelToSite(*site, model);
                return QString("E %1 · N %2 · Z %3")
                    .arg(displayLength(at.x, units, precision),
                         displayLength(at.y, units, precision),
                         displayLength(at.z, units, precision));
            };
            QString text;
            if (entity.kind == SelectionKind::Edge && body.topology.edges.contains(entity.entity)) {
                const auto &edge = body.topology.edges.at(entity.entity);
                const auto world = doc_.worldTransform(entity.body);
                const auto a = world.point(body.surface.vertices.at(edge.a)),
                           b = world.point(body.surface.vertices.at(edge.b));
                text = "Start " + siteText(a) + "\n  model " + vector(a, units, precision) +
                       "\nEnd " + siteText(b) + "\n  model " + vector(b, units, precision);
            } else
                text = siteText(measured.worldOrigin);
            site_->setText(text);
            site_->setAccessibleDescription(text);
            form_->setRowVisible(site_, true);
        } else {
            site_->clear();
            form_->setRowVisible(site_, false);
        }
        dimensions_->setText(frame.bounds ? vector(frame.bounds->dimensions(), doc_.displayUnits(),
                                                 doc_.displayPrecision())
                                          : "No finite bounds");
        length_->setText(frame.infiniteLength ? "Infinite guide"
                                              : displayLength(frame.length, doc_.displayUnits(),
                                                              doc_.displayPrecision()));
        area_->setText(displayMeasure(frame.area, 2, doc_.displayUnits(), doc_.displayPrecision()));
        volume_->setText(
            frame.volume
                ? displayMeasure(*frame.volume, 3, doc_.displayUnits(), doc_.displayPrecision())
                : QString::fromStdString(solidStatusWords(measured.solid.status)));
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
        form_->setRowVisible(site_, false);
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
            acceptKeypadDecimal(field);
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
