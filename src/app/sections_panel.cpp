#include "app/sections_panel.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include "core/sections.hpp"
#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace sketchy {
namespace {
QJsonObject updateCommand(const SectionRecord &record) {
    return {
        {"command", "section.update"},
        {"section", QString::number(record.id)},
        {"name", QString::fromStdString(record.name)},
        {"context", QString::number(record.context)},
        {"space", "local"},
        {"plane", QJsonObject{{"normal", QJsonArray{record.plane.normal.x, record.plane.normal.y,
                                                    record.plane.normal.z}},
                              {"offset", record.plane.offset}}},
        {"fill", record.fill},
        {"edges", record.edges},
        {"color", QJsonArray{record.color[0], record.color[1], record.color[2]}}};
}
QString decimal(double value) {
    auto locale = QLocale();
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    return locale.toString(value, 'g', 15);
}
} // namespace
SectionsPanel::SectionsPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    setObjectName("sectionsPanel");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("Section planes cut the view without changing model geometry. Each "
                            "editing context can have one active plane.");
    note->setWordWrap(true);
    layout->addWidget(note);
    list_ = new QListWidget;
    list_->setObjectName("sectionPlanesList");
    list_->setAccessibleName("Section planes and editing contexts");
    layout->addWidget(list_, 1);
    setFocusProxy(list_);
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
        if (id != "sectionNew") {
            auto enabled = [this, action, control] {
                action->setEnabled(selected() != 0);
                control->setEnabled(selected() != 0);
            };
            connect(list_, &QListWidget::currentRowChanged, action, enabled);
            enabled();
        }
    };
    button("sectionNew", "New…", 0, 0, QKeySequence("Ctrl+Shift+N"), [this] { edit(true); });
    button("sectionEdit", "Edit…", 0, 1, QKeySequence("F2"), [this] { edit(false); });
    button("sectionActivate", "Activate", 1, 0, QKeySequence("Return"), [this] {
        if (selected()) {
            const auto &record = *doc_.sections().at(selected());
            if (doc_.activeSections().contains(record.context) &&
                doc_.activeSections().at(record.context) == record.id)
                return;
            view_.editSections({QJsonObject{{"command", "section.activate"},
                                            {"context", QString::number(record.context)},
                                            {"section", QString::number(record.id)}}});
        }
    });
    button("sectionOff", "Turn context off", 1, 1, {}, [this] {
        if (selected()) {
            const auto context = doc_.sections().at(selected())->context;
            if (doc_.activeSections().contains(context))
                view_.editSections({QJsonObject{{"command", "section.activate"},
                                                {"context", QString::number(context)},
                                                {"section", QJsonValue::Null}}});
        }
    });
    button("sectionFlip", "Reverse cut", 2, 0, {}, [this] {
        if (selected()) {
            auto record = *doc_.sections().at(selected());
            record.plane.normal = record.plane.normal * -1;
            record.plane.offset = -record.plane.offset;
            view_.editSections({updateCommand(record)});
        }
    });
    button("sectionDelete", "Delete", 2, 1, QKeySequence("Delete"), [this] {
        if (selected())
            view_.editSections({QJsonObject{{"command", "section.delete"},
                                            {"section", QString::number(selected())}}});
    });
    details_ = new QLabel;
    details_->setWordWrap(true);
    details_->setObjectName("sectionDetails");
    layout->addWidget(details_);
    error_ = new QLabel;
    error_->setWordWrap(true);
    error_->setObjectName("sectionError");
    layout->addWidget(error_);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { describe(); });
    connect(&view_, &Viewport::changed, this, [this] { refresh(); });
    refresh();
}
Id SectionsPanel::selected() const {
    return list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toULongLong() : 0;
}
void SectionsPanel::choose(Id id) {
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toULongLong() == id)
            list_->setCurrentRow(i);
    describe();
}
void SectionsPanel::refresh() {
    const auto previous = doc_.owns(session_) ? selected() : 0;
    session_ = doc_.saveStamp();
    {
        const QSignalBlocker blocked(list_);
        list_->clear();
        for (const auto &[id, record] : doc_.sections()) {
            const bool active = doc_.activeSections().contains(record->context) &&
                                doc_.activeSections().at(record->context) == id;
            auto *item = new QListWidgetItem(
                (active ? "● " : "") + QString::fromStdString(record->name), list_);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
        }
        if (list_->count())
            list_->setCurrentRow(0);
    }
    choose(previous);
    for (auto *action : actions()) {
        const auto enabled = action->objectName() == "sectionNew" || selected() != 0;
        action->setEnabled(enabled);
        if (auto *control = findChild<QPushButton *>(action->objectName() + "Button"))
            control->setEnabled(enabled);
    }
}
void SectionsPanel::describe() {
    if (!selected() || !doc_.sections().contains(selected())) {
        details_->setText("No section planes yet.");
        return;
    }
    const auto &record = *doc_.sections().at(selected());
    const auto context = record.context == 0 ? QString("Model")
                         : doc_.bodies().contains(record.context)
                             ? QString::fromStdString(doc_.bodies().at(record.context)->name)
                             : QString("Missing context");
    details_->setText(QString("Context: %1\nDistance along normal: %2\n%3 · %4\nPlane edits and "
                              "activation are undoable.")
                          .arg(context, displayLength(-record.plane.offset, doc_.displayUnits()),
                               record.fill ? "Fill on" : "Fill off",
                               record.edges ? "Cut edges on" : "Cut edges off"));
}
void SectionsPanel::attempt(const std::function<void()> &operation) {
    try {
        operation();
        error_->clear();
    } catch (const std::exception &error) {
        error_->setText(error.what());
    }
}
void SectionsPanel::edit(bool create) {
    const auto id = create ? 0 : selected();
    if (!create && !id)
        return;
    SectionRecord original = create ? SectionRecord{} : *doc_.sections().at(id);
    if (create) {
        original.context = view_.selectionState().context();
        size_t number = 1;
        std::set<std::string> names;
        for (const auto &[key, record] : doc_.sections())
            if (record->context == original.context)
                names.insert(record->name);
        while (names.contains("Section " + std::to_string(number)))
            ++number;
        original.name = "Section " + std::to_string(number);
    }
    auto candidate = original;
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    QDialog dialog(this);
    dialog.setObjectName("sectionPlaneDialog");
    dialog.setWindowTitle(create ? "New section plane" : "Edit section plane");
    auto *form = new QFormLayout(&dialog);
    auto *name = new QLineEdit(QString::fromStdString(original.name));
    name->setObjectName("sectionName");
    name->setMaxLength(1024);
    form->addRow("Name", name);
    auto *context = new QComboBox;
    context->setObjectName("sectionContext");
    context->addItem("Model", QVariant::fromValue<qulonglong>(0));
    for (const auto &[body, record] : doc_.bodies())
        context->addItem(QString::fromStdString(record->name) + " (#" + QString::number(body) + ")",
                         QVariant::fromValue<qulonglong>(body));
    if (original.context && !doc_.bodies().contains(original.context))
        context->addItem("Missing context #" + QString::number(original.context),
                         QVariant::fromValue<qulonglong>(original.context));
    context->setCurrentIndex(context->findData(QVariant::fromValue<qulonglong>(original.context)));
    form->addRow("Editing context", context);
    std::array<QLineEdit *, 3> normal{};
    const std::array<double, 3> n{original.plane.normal.x, original.plane.normal.y,
                                  original.plane.normal.z};
    for (size_t i = 0; i < 3; ++i) {
        normal[i] = new QLineEdit(decimal(n[i]));
        normal[i]->setObjectName("sectionNormal" + QString::number(i));
        form->addRow(QString("Local normal %1").arg(QChar("XYZ"[i])), normal[i]);
    }
    auto *distance = new QLineEdit(displayLength(-original.plane.offset, doc_.displayUnits()));
    distance->setObjectName("sectionDistance");
    form->addRow("Distance along normal", distance);
    auto *fill = new QCheckBox("Show section fill");
    fill->setObjectName("sectionFill");
    fill->setChecked(original.fill);
    form->addRow(fill);
    auto *edges = new QCheckBox("Show cut edges");
    edges->setObjectName("sectionEdges");
    edges->setChecked(original.edges);
    form->addRow(edges);
    auto *color = new QPushButton;
    color->setObjectName("sectionColor");
    auto showColor = [&] {
        color->setText(
            QColor::fromRgbF(candidate.color[0], candidate.color[1], candidate.color[2]).name());
    };
    showColor();
    form->addRow("Fill color", color);
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
    auto *activate = new QCheckBox("Activate this plane after saving");
    activate->setObjectName("sectionActivateAfterSave");
    activate->setChecked(create);
    form->addRow(activate);
    auto *hint = new QLabel("The normal points toward the side that stays visible. Values use the "
                            "selected context's axes. Changing context keeps these local values.");
    hint->setWordWrap(true);
    form->addRow(hint);
    auto *error = new QLabel;
    error->setObjectName("sectionDialogError");
    error->setWordWrap(true);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp))
                throw std::runtime_error("Document changed; reopen the section editor");
            candidate.name = name->text().toStdString();
            candidate.context = context->currentData().toULongLong();
            std::array<double, 3> values = n;
            bool normalChanged{};
            for (size_t i = 0; i < 3; ++i)
                if (normal[i]->isModified()) {
                    bool ok{};
                    values[i] = QLocale().toDouble(normal[i]->text(), &ok);
                    if (!ok || !std::isfinite(values[i]))
                        throw std::runtime_error("Enter a finite normal direction");
                    normalChanged = true;
                }
            if (normalChanged)
                candidate.plane.normal = normalized({values[0], values[1], values[2]});
            if (distance->isModified())
                candidate.plane.offset =
                    -parseLength(distance->text(), inputUnit(doc_.displayUnits()), QLocale());
            candidate.fill = fill->isChecked();
            candidate.edges = edges->isChecked();
            candidate.id = create ? doc_.nextSectionId() : id;
            QJsonArray commands;
            if (create || candidate != original) {
                auto command = updateCommand(candidate);
                if (create) {
                    command["command"] = "section.create";
                    command.remove("section");
                }
                commands.append(command);
            }
            if (activate->isChecked() &&
                (!doc_.activeSections().contains(candidate.context) ||
                 doc_.activeSections().at(candidate.context) != candidate.id))
                commands.append(QJsonObject{{"command", "section.activate"},
                                            {"context", QString::number(candidate.context)},
                                            {"section", QString::number(candidate.id)}});
            if (!commands.empty())
                view_.editSections(commands);
            dialog.accept();
            refresh();
            choose(candidate.id);
        } catch (const std::exception &failure) {
            error->setText(failure.what());
        }
    });
    name->selectAll();
    dialog.exec();
}
} // namespace sketchy
