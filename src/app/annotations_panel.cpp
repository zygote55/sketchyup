#include "app/annotations_panel.hpp"
#include "app/annotation_display.hpp"
#include "automation/measurements.hpp"
#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace sketchy {
namespace {
QJsonObject fixed(Vec3 p) {
    return {{"kind", "point"}, {"space", "world"}, {"point", QJsonArray{p.x, p.y, p.z}}};
}
QJsonObject selectedAnchor(const Document &doc, SelectedEntity item) {
    const auto body = QString::number(item.body);
    if (item.kind == SelectionKind::Edge)
        return {{"kind", "edge"},
                {"body", body},
                {"edge", QString::number(item.entity)},
                {"fraction", .5}};
    if (item.kind == SelectionKind::Face) {
        const auto triangles = doc.bodies().at(item.body)->surface.triangulate(item.entity);
        if (triangles.empty())
            throw std::runtime_error("Selected face has no valid attachment surface");
        const auto &t = triangles.front();
        const auto p = (t.a + t.b + t.c) / 3.;
        return {{"kind", "face"},
                {"body", body},
                {"face", QString::number(item.entity)},
                {"space", "local"},
                {"point", QJsonArray{p.x, p.y, p.z}}};
    }
    throw std::runtime_error("Select an edge or face inside its editing context");
}
QJsonArray selectedAnchors(const Document &doc, const Selection &selection, AnnotationKind kind) {
    const auto &items = selection.entities();
    if (kind == AnnotationKind::Label && items.size() == 1)
        return {selectedAnchor(doc, *items.begin())};
    if (kind == AnnotationKind::Distance && items.size() == 1 &&
        items.begin()->kind == SelectionKind::Edge) {
        const auto item = *items.begin();
        const auto &edge = doc.bodies().at(item.body)->topology.edges.at(item.entity);
        auto vertex = [&](Id id) {
            return QJsonObject{{"kind", "vertex"},
                               {"body", QString::number(item.body)},
                               {"vertex", QString::number(id)}};
        };
        return {vertex(edge.a), vertex(edge.b)};
    }
    if (kind == AnnotationKind::Distance && items.size() == 2)
        return {selectedAnchor(doc, *items.begin()),
                selectedAnchor(doc, *std::next(items.begin()))};
    throw std::runtime_error(kind == AnnotationKind::Distance
                                 ? "Select one edge, or two edges/faces, for a dimension"
                                 : "Select one edge or face for a label");
}
} // namespace
AnnotationsPanel::AnnotationsPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    setObjectName("annotationsPanel");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel(
        "Select an edge for its length, or an edge/face for a label. Two selected "
        "edges/faces measure between attachment points. Fixed world points are also available.");
    note->setWordWrap(true);
    layout->addWidget(note);
    list_ = new QListWidget;
    list_->setObjectName("annotationsList");
    list_->setAccessibleName("Dimensions, labels and reference status");
    setFocusProxy(list_);
    layout->addWidget(list_, 1);
    auto *buttons = new QGridLayout;
    layout->addLayout(buttons);
    auto button = [&](const QString &id, const QString &text, int row, int column,
                      const QKeySequence &shortcut, const std::function<void()> &operation) {
        auto *action = new QAction(text, this);
        action->setObjectName(id);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(action);
        connect(action, &QAction::triggered, this, [this, operation] { attempt(operation); });
        auto *control = new QPushButton(text);
        control->setObjectName(id + "Button");
        buttons->addWidget(control, row, column);
        connect(control, &QPushButton::clicked, action, &QAction::trigger);
    };
    button("annotationDistance", "Dimension…", 0, 0, QKeySequence("Ctrl+Shift+D"),
           [this] { edit(AnnotationKind::Distance); });
    button("annotationLabel", "Label…", 0, 1, QKeySequence("Ctrl+Shift+L"),
           [this] { edit(AnnotationKind::Label); });
    button("annotationEdit", "Edit / rebind…", 1, 0, QKeySequence("F2"),
           [this] { edit(std::nullopt); });
    button("annotationDelete", "Delete", 1, 1, QKeySequence("Delete"), [this] {
        if (selected())
            view_.editAnnotations({QJsonObject{{"command", "annotation.delete"},
                                               {"annotation", QString::number(selected())}}});
    });
    button("annotationFrame", "Frame", 2, 0, {}, [this] {
        if (!selected())
            return;
        const auto m = measureAnnotation(doc_, *doc_.annotations().at(selected()));
        auto low = m.textPoint, high = low;
        for (const auto &a : m.anchors) {
            low = {std::min(low.x, a.point.x), std::min(low.y, a.point.y),
                   std::min(low.z, a.point.z)};
            high = {std::max(high.x, a.point.x), std::max(high.y, a.point.y),
                    std::max(high.z, a.point.z)};
        }
        view_.frameBounds(low - Vec3{.1, .1, .1}, high + Vec3{.1, .1, .1});
    });
    details_ = new QLabel;
    details_->setWordWrap(true);
    details_->setObjectName("annotationDetails");
    layout->addWidget(details_);
    error_ = new QLabel;
    error_->setWordWrap(true);
    error_->setObjectName("annotationError");
    layout->addWidget(error_);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { describe(); });
    connect(&view_, &Viewport::changed, this, [this] { refresh(); });
    refresh();
}
Id AnnotationsPanel::selected() const {
    return list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toULongLong() : 0;
}
void AnnotationsPanel::choose(Id id) {
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toULongLong() == id)
            list_->setCurrentRow(i);
    describe();
}
void AnnotationsPanel::refresh() {
    const auto previous = doc_.owns(session_) ? selected() : 0;
    session_ = doc_.saveStamp();
    {
        const QSignalBlocker blocked(list_);
        list_->clear();
        for (const auto &[id, record] : doc_.annotations()) {
            const auto m = measureAnnotation(doc_, *record);
            const auto prefix = m.state == AnchorState::Missing     ? "[Missing] "
                                : m.state == AnchorState::Ambiguous ? "[Ambiguous] "
                                                                    : "";
            auto *item = new QListWidgetItem(prefix + QString::fromStdString(record->name), list_);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
        }
        if (list_->count())
            list_->setCurrentRow(0);
    }
    choose(previous);
}
void AnnotationsPanel::describe() {
    const bool exists = selected() && doc_.annotations().contains(selected());
    for (auto *action : actions()) {
        const auto enabled = exists || action->objectName() == "annotationDistance" ||
                             action->objectName() == "annotationLabel";
        action->setEnabled(enabled);
        if (auto *button = findChild<QPushButton *>(action->objectName() + "Button"))
            button->setEnabled(enabled);
    }
    if (!exists) {
        details_->setText("No dimensions or labels yet.");
        return;
    }
    const auto &record = *doc_.annotations().at(selected());
    const auto m = measureAnnotation(doc_, record);
    details_->setText(annotationText(record, m, doc_.displayUnits()) +
                      "\nEdit / rebind keeps existing attachments unless you choose new ones. "
                      "Changes are undoable.");
}
void AnnotationsPanel::attempt(const std::function<void()> &operation) {
    try {
        operation();
        error_->clear();
    } catch (const std::exception &e) {
        error_->setText(e.what());
    }
}
void AnnotationsPanel::edit(std::optional<AnnotationKind> create) {
    const auto id = create ? 0 : selected();
    if (!create && !id)
        return;
    AnnotationRecord original = create ? AnnotationRecord{} : *doc_.annotations().at(id);
    if (create) {
        original.kind = *create;
        const auto prefix = *create == AnnotationKind::Distance ? "Dimension " : "Label ";
        std::set<std::string> names;
        for (const auto &[key, record] : doc_.annotations())
            names.insert(record->name);
        size_t number = 1;
        while (names.contains(prefix + std::to_string(number)))
            ++number;
        original.name = prefix + std::to_string(number);
        original.text = *create == AnnotationKind::Label ? "Label" : "";
        original.offset = {0, .5, 0};
    }
    QJsonArray selection;
    QString selectionHint;
    try {
        selection = selectedAnchors(doc_, view_.selectionState(), original.kind);
    } catch (const std::exception &e) {
        selectionHint = e.what();
    }
    auto candidate = original;
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    QDialog dialog(this);
    dialog.setObjectName("annotationDialog");
    dialog.setWindowTitle(
        create ? (*create == AnnotationKind::Distance ? "New dimension" : "New label")
               : "Edit annotation");
    dialog.setMinimumWidth(500);
    auto *form = new QFormLayout(&dialog);
    auto *name = new QLineEdit(QString::fromStdString(original.name));
    name->setObjectName("annotationName");
    name->setMaxLength(1024);
    form->addRow("Name", name);
    auto *text = new QPlainTextEdit(QString::fromStdString(original.text));
    text->setObjectName("annotationText");
    text->setMaximumHeight(75);
    form->addRow(original.kind == AnnotationKind::Distance ? "Text prefix" : "Label text", text);
    auto *binding = new QComboBox;
    binding->setObjectName("annotationBinding");
    if (!create)
        binding->addItem("Keep current attachments", "keep");
    if (!selection.empty())
        binding->addItem("Attach to selected geometry", "selection");
    binding->addItem("Fixed world points", "fixed");
    form->addRow("Attachment", binding);
    std::array<Vec3, 2> points{Vec3{}, Vec3{1, 0, 0}};
    if (!create) {
        const auto m = measureAnnotation(doc_, original);
        for (size_t i = 0; i < m.anchors.size(); ++i)
            points[i] = m.anchors[i].point;
    }
    std::array<std::array<QLineEdit *, 3>, 2> pointFields{};
    const auto count = original.kind == AnnotationKind::Distance ? 2 : 1;
    auto vectorFields = [&](const QString &prefix, Vec3 value) {
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        std::array<QLineEdit *, 3> fields{};
        const std::array<double, 3> xyz{value.x, value.y, value.z};
        for (size_t i = 0; i < 3; ++i) {
            layout->addWidget(new QLabel(QString(QChar("XYZ"[i]))));
            fields[i] = new QLineEdit(displayLength(xyz[i], doc_.displayUnits()));
            fields[i]->setObjectName(prefix + QString::number(i));
            layout->addWidget(fields[i]);
        }
        return std::pair{row, fields};
    };
    for (int i = 0; i < count; ++i) {
        auto [row, fields] = vectorFields("annotationPoint" + QString::number(i), points[i]);
        pointFields[i] = fields;
        form->addRow("World point " + QString::number(i + 1), row);
        auto enabled = [=] { row->setEnabled(binding->currentData() == "fixed"); };
        connect(binding, &QComboBox::currentIndexChanged, &dialog, enabled);
        enabled();
    }
    auto [offsetRow, offset] = vectorFields("annotationOffset", original.offset);
    form->addRow("Text offset (world)", offsetRow);
    auto *leader = new QCheckBox("Show leader / extension lines");
    leader->setObjectName("annotationLeader");
    leader->setChecked(original.leader);
    form->addRow(leader);
    auto *size = new QDoubleSpinBox;
    size->setObjectName("annotationTextSize");
    size->setRange(8, 48);
    size->setDecimals(1);
    size->setValue(original.textSize);
    form->addRow("Text size (pixels)", size);
    auto *color = new QPushButton;
    color->setObjectName("annotationColor");
    auto showColor = [&] {
        color->setText(
            QColor::fromRgbF(candidate.color[0], candidate.color[1], candidate.color[2]).name());
    };
    showColor();
    form->addRow("Color", color);
    connect(color, &QPushButton::clicked, &dialog, [&] {
        QColorDialog picker(
            QColor::fromRgbF(candidate.color[0], candidate.color[1], candidate.color[2]), &dialog);
        picker.setOption(QColorDialog::DontUseNativeDialog);
        const auto baseline = picker.currentColor();
        if (picker.exec() == QDialog::Accepted && picker.currentColor() != baseline) {
            const auto chosen = picker.currentColor();
            candidate.color = {float(chosen.redF()), float(chosen.greenF()), float(chosen.blueF())};
            showColor();
        }
    });
    auto *hint = new QLabel(
        selectionHint.isEmpty()
            ? "An edge dimension uses its endpoints. Other edge attachments use their midpoint; "
              "face attachments use a point inside the face. Fixed points do not follow geometry."
            : selectionHint + ". Fixed world points do not follow geometry.");
    hint->setWordWrap(true);
    form->addRow(hint);
    auto *error = new QLabel;
    error->setObjectName("annotationDialogError");
    error->setWordWrap(true);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto parseVector = [&](const std::array<QLineEdit *, 3> &fields, Vec3 baseline) {
        std::array<double, 3> values{baseline.x, baseline.y, baseline.z};
        for (size_t i = 0; i < 3; ++i)
            if (fields[i]->isModified())
                values[i] =
                    parseLength(fields[i]->text(), inputUnit(doc_.displayUnits()), QLocale());
        return Vec3{values[0], values[1], values[2]};
    };
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp))
                throw std::runtime_error("Document changed; reopen the annotation editor");
            candidate.name = name->text().toStdString();
            candidate.text = text->toPlainText().toStdString();
            candidate.offset = parseVector(offset, original.offset);
            candidate.leader = leader->isChecked();
            // Preserve untouched stored precision; the spin box is only a presentation control.
            if (size->value() != std::round(original.textSize * 10) / 10)
                candidate.textSize = size->value();
            QJsonObject command{
                {"command", create ? "annotation.create" : "annotation.update"},
                {"name", QString::fromStdString(candidate.name)},
                {"text", QString::fromStdString(candidate.text)},
                {"offset", QJsonArray{candidate.offset.x, candidate.offset.y, candidate.offset.z}},
                {"leader", candidate.leader},
                {"textSize", candidate.textSize},
                {"color", QJsonArray{candidate.color[0], candidate.color[1], candidate.color[2]}}};
            if (create)
                command["kind"] = original.kind == AnnotationKind::Distance ? "distance" : "label";
            else
                command["annotation"] = QString::number(id);
            if (binding->currentData() == "selection")
                command["anchors"] = selection;
            else if (binding->currentData() == "fixed") {
                QJsonArray anchors;
                for (int i = 0; i < count; ++i)
                    anchors.append(fixed(parseVector(pointFields[i], points[i])));
                command["anchors"] = anchors;
            }
            const auto chosen = create ? doc_.nextAnnotationId() : id;
            if (create || candidate != original || command.contains("anchors"))
                view_.editAnnotations({command});
            dialog.accept();
            refresh();
            choose(chosen);
        } catch (const std::exception &e) {
            error->setText(e.what());
        }
    });
    name->selectAll();
    dialog.exec();
}
} // namespace sketchy
