#include "app/text_panel.hpp"
#include "app/unit_display.hpp"
#include "automation/commands.hpp"
#include "automation/measurements.hpp"
#include "automation/text_commands.hpp"
#include "core/entity_measure.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
namespace sketchy {
namespace {
Id owner(const Document &doc, Id body) {
    for (auto id = body; id; id = doc.bodies().at(id)->parent)
        if (doc.instances().contains(id))
            return id;
    return 0;
}
QJsonObject batch(const Document &doc, QJsonObject command, Id scope) {
    QJsonArray commands{command};
    if (scope)
        commands = {QJsonObject{{"command", "component.edit_instance"},
                                {"body", QString::number(scope)},
                                {"commands", commands}}};
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QString availability(const TextSource &source) {
    const auto families = QFontDatabase::families();
    QStringList missing;
    const auto family = QString::fromStdString(source.family);
    if (!families.contains(family, Qt::CaseInsensitive))
        missing << family;
    for (const auto &font : source.fonts) {
        const auto used = QString::fromStdString(font.family);
        if (!families.contains(used, Qt::CaseInsensitive) && !missing.contains(used))
            missing << used;
    }
    if (!missing.empty())
        return "Missing local fonts: " + missing.join(", ") +
               ". Cached geometry remains available. Choose an installed font or explicitly allow "
               "substitution to edit.";
    if (!source.style.empty() && !QFontDatabase::styles(family).contains(
                                     QString::fromStdString(source.style), Qt::CaseInsensitive))
        return "The requested font style is unavailable. Cached geometry remains available.";
    return "Font families are available locally. Regeneration checks their fingerprints.";
}
} // namespace
TextPanel::TextPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    setObjectName("textPanel");
    auto *layout = new QVBoxLayout(this);
    auto *note =
        new QLabel("Create editable text from local fonts. Height is nominal font em size; depth "
                   "extrudes along local Z. Native files retain source and geometry.");
    note->setWordWrap(true);
    layout->addWidget(note);
    list_ = new QListWidget;
    list_->setObjectName("textList");
    list_->setAccessibleName("Editable text objects");
    setFocusProxy(list_);
    layout->addWidget(list_, 1);
    auto *buttons = new QGridLayout;
    layout->addLayout(buttons);
    auto button = [&](QString id, QString label, int row, int column, QKeySequence shortcut,
                      std::function<void()> operation) {
        auto *action = new QAction(label, this);
        action->setObjectName(id);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(action);
        connect(action, &QAction::triggered, this, [this, operation] { attempt(operation); });
        auto *button = new QPushButton(label);
        button->setObjectName(id + "Button");
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        buttons->addWidget(button, row, column);
    };
    button("textCreate", "Create 3D text…", 0, 0, QKeySequence("Ctrl+Shift+T"),
           [this] { edit(true); });
    button("textEdit", "Edit text…", 0, 1, QKeySequence(Qt::Key_F2), [this] { edit(false); });
    button("textBake", "Bake geometry", 1, 0, {}, [this] { bake(); });
    button("textFrame", "Frame", 1, 1, {}, [this] {
        if (!selected())
            return;
        const auto bounds = measureEntity(doc_, {selected(), SelectionKind::Body, 0}).world.bounds;
        if (bounds)
            view_.frameBounds(bounds->low, bounds->high);
    });
    details_ = new QLabel;
    details_->setObjectName("textDetails");
    details_->setWordWrap(true);
    details_->setTextFormat(Qt::PlainText);
    layout->addWidget(details_);
    error_ = new QLabel;
    error_->setObjectName("textError");
    error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText);
    layout->addWidget(error_);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { describe(); });
    connect(&view_, &Viewport::changed, this, [this] { refresh(); });
    refresh();
}
TextPanel::~TextPanel() {
    for (const auto &worker : workers_)
        if (worker)
            worker->requestInterruption();
    for (const auto &worker : workers_)
        if (worker)
            worker->wait();
}
Id TextPanel::selected() const {
    return list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toULongLong() : 0;
}
void TextPanel::choose(Id id) {
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toULongLong() == id)
            list_->setCurrentRow(i);
    describe();
}
void TextPanel::refresh() {
    const auto previous = doc_.owns(session_) ? selected() : 0;
    session_ = doc_.saveStamp();
    {
        QSignalBlocker blocked(list_);
        list_->clear();
        for (const auto &[id, body] : doc_.bodies())
            if (body->textSource) {
                auto *item = new QListWidgetItem(QString::fromStdString(body->name), list_);
                item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
            }
    }
    choose(previous);
    if (!selected() && list_->count())
        list_->setCurrentRow(0);
    describe();
}
void TextPanel::describe() {
    if (!selected() || !doc_.bodies().contains(selected()) ||
        !doc_.bodies().at(selected())->textSource) {
        details_->setText("Select editable text to inspect its source. Baking keeps geometry and "
                          "removes source; Undo restores it.");
        return;
    }
    const auto &body = *doc_.bodies().at(selected());
    const auto &source = *body.textSource;
    details_->setText(QString::fromStdString(source.text) + "\n" +
                      QString::fromStdString(source.actualFamily + " " + source.actualStyle) +
                      " · " + displayLength(source.height, doc_.displayUnits()) + " high, " +
                      displayLength(source.depth, doc_.displayUnits()) + " deep\n" +
                      availability(source) +
                      (textGeometryDigest(body) == source.geometryDigest
                           ? QString{}
                           : "\nGeometry was edited independently. Restore it before regeneration, "
                             "or bake it to keep those edits."));
}
void TextPanel::attempt(const std::function<void()> &operation) {
    try {
        error_->clear();
        operation();
    } catch (const std::exception &error) {
        error_->setText(QString::fromUtf8(error.what()));
    }
}
void TextPanel::bake() {
    if (!selected())
        return;
    const auto id = selected();
    const auto request =
        batch(doc_, {{"command", "text.bake"}, {"body", QString::number(id)}}, owner(doc_, id));
    const auto prepared = doc_.prepareEdit([&](Document &draft) { executeBatch(draft, request); });
    view_.applyTextEdit(prepared);
}
void TextPanel::edit(bool creating) {
    const auto target = creating ? 0 : selected();
    if (!creating && !target)
        return;
    const auto baseline = doc_.readSnapshot();
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    const auto parent = creating ? view_.selectionState().context() : 0;
    const auto scope = owner(doc_, creating ? parent : target);
    TextSource original;
    if (!creating)
        original = *doc_.bodies().at(target)->textSource;
    else {
        original.text = "SketchyUp";
        const auto fonts = QFontDatabase::families();
        original.family = (fonts.contains("DejaVu Sans") ? QString("DejaVu Sans")
                           : fonts.contains("Noto Sans") ? QString("Noto Sans")
                                                         : fonts.value(0))
                              .toStdString();
        original.height = .1;
        original.depth = .01;
    }
    QDialog dialog(this);
    dialog.setObjectName("textDialog");
    dialog.setWindowTitle(creating ? "Create 3D text" : "Edit 3D text");
    dialog.resize(550, 650);
    auto *form = new QFormLayout(&dialog);
    auto *name =
        new QLineEdit(creating ? "Text" : QString::fromStdString(doc_.bodies().at(target)->name));
    name->setObjectName("textName");
    name->setMaxLength(1024);
    form->addRow("Name", name);
    const auto originalName = name->text();
    auto *text = new QPlainTextEdit(QString::fromStdString(original.text));
    text->setObjectName("textContent");
    text->setMaximumHeight(110);
    form->addRow("Text", text);
    auto *family = new QComboBox;
    family->setObjectName("textFamily");
    family->setEditable(true);
    family->addItems(QFontDatabase::families());
    family->setCurrentText(QString::fromStdString(original.family));
    form->addRow("Font family", family);
    auto *style = new QComboBox;
    style->setObjectName("textStyle");
    style->setEditable(true);
    auto styles = [&] {
        const auto keep = style->currentText();
        QSignalBlocker blocked(style);
        style->clear();
        style->addItem("");
        style->addItems(QFontDatabase::styles(family->currentText()));
        style->setCurrentText(keep);
    };
    styles();
    style->setCurrentText(QString::fromStdString(original.style));
    connect(family, &QComboBox::currentTextChanged, &dialog, styles);
    form->addRow("Font style (blank uses default)", style);
    auto *height = new QLineEdit(displayLength(original.height, doc_.displayUnits()));
    height->setObjectName("textHeight");
    auto *depth = new QLineEdit(displayLength(original.depth, doc_.displayUnits()));
    depth->setObjectName("textDepth");
    auto *spacing = new QLineEdit(QLocale().toString(original.lineSpacing, 'g', 15));
    spacing->setObjectName("textSpacing");
    const auto originalHeight = height->text(), originalDepth = depth->text(),
               originalSpacing = spacing->text();
    form->addRow("Em height", height);
    form->addRow("Depth (0 makes flat faces)", depth);
    form->addRow("Line spacing (em heights)", spacing);
    std::array<QLineEdit *, 3> position{};
    if (creating) {
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        for (size_t i = 0; i < 3; ++i) {
            layout->addWidget(new QLabel(QString(QChar("XYZ"[i]))));
            position[i] = new QLineEdit("0");
            position[i]->setObjectName("textPosition" + QString::number(i));
            layout->addWidget(position[i]);
        }
        form->addRow("Position in editing context", row);
    }
    auto *substitute =
        new QCheckBox("Allow substitution if the requested font or style is unavailable");
    substitute->setObjectName("textSubstitution");
    substitute->setChecked(original.allowSubstitution);
    form->addRow(substitute);
    auto *fontChange = new QCheckBox("Accept changes to previously used font files");
    fontChange->setObjectName("textAcceptFontChange");
    auto *regenerate = new QCheckBox("Regenerate even when source settings are unchanged");
    regenerate->setObjectName("textRegenerate");
    if (!creating) {
        form->addRow(fontChange);
        form->addRow(regenerate);
    } else {
        fontChange->setParent(&dialog);
        fontChange->hide();
        regenerate->setParent(&dialog);
        regenerate->hide();
    }
    auto *note = new QLabel;
    note->setTextFormat(Qt::PlainText);
    note->setWordWrap(true);
    auto fontNote = [&] {
        const auto fonts = QFontDatabase::families();
        note->setText(
            (fonts.contains(family->currentText(), Qt::CaseInsensitive)
                 ? QString("Fonts remain local; native files keep generated geometry and source.")
                 : QString("The requested family is missing. Choose an installed font or allow "
                           "substitution; existing geometry stays intact.")) +
            (scope ? "\nThis edit makes only this component instance unique." : ""));
    };
    connect(family, &QComboBox::currentTextChanged, &dialog, fontNote);
    fontNote();
    form->addRow(note);
    auto *error = new QLabel;
    error->setObjectName("textDialogError");
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    std::vector<QWidget *> controls{name,  text,    family,     style,      height,
                                    depth, spacing, substitute, fontChange, regenerate};
    for (auto *field : position)
        if (field)
            controls.push_back(field);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp))
                throw std::runtime_error(
                    "The model changed while this editor was open. Reopen the editor to continue.");
            QJsonObject command{{"command", creating ? "text.create" : "text.update"}};
            if (!creating)
                command["body"] = QString::number(target);
            if (creating || name->text() != originalName)
                command["name"] = name->text();
            if (creating || text->toPlainText() != QString::fromStdString(original.text))
                command["text"] = text->toPlainText();
            if (creating || family->currentText() != QString::fromStdString(original.family))
                command["family"] = family->currentText();
            if (creating || style->currentText() != QString::fromStdString(original.style))
                command["style"] = style->currentText();
            const auto unit = inputUnit(doc_.displayUnits());
            if (creating || height->text() != originalHeight)
                command["height"] = height->text() == originalHeight
                                        ? original.height
                                        : parseLength(height->text(), unit, QLocale());
            if (creating || depth->text() != originalDepth)
                command["depth"] = depth->text() == originalDepth
                                       ? original.depth
                                       : parseLength(depth->text(), unit, QLocale());
            if (creating || spacing->text() != originalSpacing) {
                bool valid{};
                const auto value = QLocale().toDouble(spacing->text(), &valid);
                if (!valid)
                    throw std::runtime_error("Line spacing requires a number");
                command["lineSpacing"] =
                    spacing->text() == originalSpacing ? original.lineSpacing : value;
            }
            if (creating || substitute->isChecked() != original.allowSubstitution)
                command["allowSubstitution"] = substitute->isChecked();
            if (creating) {
                command["parent"] = QString::number(parent);
                QJsonArray values;
                for (auto *field : position)
                    values.append(parseLength(field->text(), unit, QLocale()));
                command["position"] = values;
            } else {
                if (regenerate->isChecked())
                    command["regenerate"] = true;
                if (command.size() == 2) {
                    dialog.accept();
                    return;
                }
                if (fontChange->isChecked())
                    command["acceptFontChange"] = true;
            }
            const auto request = batch(baseline, command, scope);
            struct Result {
                std::optional<Document::PreparedEdit> edit;
                QJsonObject outcome;
                QString error;
            };
            auto result = std::make_shared<Result>();
            auto *worker =
                QThread::create([snapshot = baseline.readSnapshot(), request, result]() mutable {
                    try {
                        result->edit = snapshot.prepareEdit([&](Document &candidate) {
                            result->outcome = executeBatch(candidate, request);
                        });
                    } catch (const std::exception &failure) {
                        result->error = QString::fromUtf8(failure.what());
                    }
                });
            std::erase_if(workers_, [](const auto &p) { return p.isNull(); });
            workers_.push_back(worker);
            connect(worker, &QThread::finished, worker, &QObject::deleteLater);
            connect(&dialog, &QDialog::rejected, worker, &QThread::requestInterruption);
            connect(worker, &QThread::finished, &dialog, [&, result] {
                for (auto *control : controls)
                    control->setEnabled(true);
                buttons->button(QDialogButtonBox::Save)->setEnabled(true);
                if (!result->error.isEmpty()) {
                    error->setText(result->error);
                    return;
                }
                try {
                    if (!result->edit)
                        throw std::runtime_error("Text generation returned no edit");
                    view_.applyTextEdit(*result->edit);
                    Id focus = target;
                    if (creating)
                        for (const auto &[id, body] : doc_.bodies())
                            if (!baseline.bodies().contains(id) && body->textSource) {
                                focus = id;
                                break;
                            }
                    refresh();
                    choose(focus);
                    dialog.accept();
                } catch (const std::exception &failure) {
                    error->setText(QString::fromUtf8(failure.what()));
                }
            });
            for (auto *control : controls)
                control->setEnabled(false);
            buttons->button(QDialogButtonBox::Save)->setEnabled(false);
            error->setText("Generating text…");
            worker->start();
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    });
    dialog.exec();
}
} // namespace sketchy
