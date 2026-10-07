#include "app/window.hpp"
#include "automation/extension_store.hpp"
#include "automation/extension_worker.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QThread>
#include <QVBoxLayout>
namespace sketchy {
namespace {
class ExtensionNumber final : public QDoubleSpinBox {
    QString textFromValue(double value) const override {
        auto text = QDoubleSpinBox::textFromValue(value);
        const auto point = locale().decimalPoint();
        const auto zero = locale().zeroDigit();
        if (text.contains(point)) {
            while (text.endsWith(zero))
                text.chop(zero.size());
            if (text.endsWith(point))
                text.chop(point.size());
        }
        return text;
    }
};
} // namespace
void Window::extensionsDialog() {
    const auto directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/extensions";
    ExtensionStore store(directory);
    QDialog dialog(this);
    dialog.setObjectName("extensionsDialog");
    dialog.setWindowTitle("Extensions");
    dialog.resize(740, 700);
    QVBoxLayout layout(&dialog);
    auto *note = new QLabel("Install a command package, enable it, then choose an action to run. "
                            "Actions create one undoable model edit.");
    note->setWordWrap(true);
    layout.addWidget(note);
    auto *list = new QListWidget;
    list->setObjectName("extensionsList");
    list->setAccessibleName("Installed extensions");
    list->setMaximumHeight(160);
    layout.addWidget(list);
    QHBoxLayout controls;
    auto *install = new QPushButton("&Install…");
    install->setObjectName("extensionInstall");
    auto *enable = new QPushButton("&Enable");
    enable->setObjectName("extensionEnable");
    auto *remove = new QPushButton("&Remove");
    remove->setObjectName("extensionRemove");
    controls.addWidget(install);
    controls.addWidget(enable);
    controls.addWidget(remove);
    layout.addLayout(&controls);
    auto *details = new QPlainTextEdit;
    details->setReadOnly(true);
    details->setObjectName("extensionDetails");
    details->setAccessibleName("Extension description and declared commands");
    details->setMaximumHeight(120);
    layout.addWidget(details);
    QFormLayout form;
    auto *actions = new QComboBox;
    actions->setObjectName("extensionAction");
    actions->setAccessibleName("Extension action");
    form.addRow("&Action", actions);
    layout.addLayout(&form);
    auto *parameterBox = new QWidget;
    auto *parameters = new QFormLayout(parameterBox);
    auto *parameterScroll = new QScrollArea;
    parameterScroll->setWidgetResizable(true);
    parameterScroll->setWidget(parameterBox);
    parameterScroll->setMinimumHeight(80);
    parameterScroll->setMaximumHeight(240);
    layout.addWidget(parameterScroll, 1);
    auto *status = new QLabel;
    status->setObjectName("extensionStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout.addWidget(status);
    QDialogButtonBox buttons(QDialogButtonBox::Close);
    auto *run = buttons.addButton("&Run action", QDialogButtonBox::ActionRole);
    run->setObjectName("extensionRun");
    auto *cancel = buttons.addButton("&Cancel action", QDialogButtonBox::ActionRole);
    cancel->setObjectName("extensionCancel");
    layout.addWidget(&buttons);
    QThread *worker{};
    bool busy{}, cancelRequested{}, closing{};
    std::map<QString, QWidget *> editors;
    auto selectedId = [&] {
        return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString{};
    };
    auto state = [&] {
        const auto id = selectedId();
        const bool present = store.entries().contains(id);
        const bool compatible = present && store.entries().at(id).manifest.has_value();
        const bool enabled = present && store.entries().at(id).enabled;
        install->setEnabled(!busy);
        list->setEnabled(!busy);
        remove->setEnabled(!busy && present);
        enable->setEnabled(!busy && compatible);
        enable->setText(enabled ? "&Disable" : "&Enable");
        actions->setEnabled(!busy && compatible);
        parameterBox->setEnabled(!busy && enabled);
        run->setEnabled(!busy && enabled && actions->currentIndex() >= 0);
        cancel->setEnabled(busy && !cancelRequested);
    };
    auto parameterForm = [&] {
        while (parameters->rowCount())
            parameters->removeRow(0);
        editors.clear();
        const auto id = selectedId();
        if (store.entries().contains(id) && store.entries().at(id).manifest &&
            actions->currentIndex() >= 0) {
            const auto action =
                store.entries().at(id).manifest->actions[actions->currentIndex()].toObject();
            for (const auto &value : action["parameters"].toArray()) {
                const auto spec = value.toObject();
                const auto name = spec["name"].toString();
                QWidget *editor{};
                if (spec["type"] == "number") {
                    auto *number = new ExtensionNumber;
                    number->setDecimals(9);
                    number->setRange(spec["minimum"].toDouble(), spec["maximum"].toDouble());
                    number->setValue(spec["default"].toDouble());
                    editor = number;
                } else if (spec["type"] == "boolean") {
                    auto *flag = new QCheckBox;
                    flag->setChecked(spec["default"].toBool());
                    editor = flag;
                } else {
                    auto *text = new QLineEdit(spec["default"].toString());
                    text->setMaxLength(256);
                    editor = text;
                }
                editor->setObjectName("extensionParameter-" + name);
                editor->setAccessibleName(spec["label"].toString());
                auto *label = new QLabel(spec["label"].toString());
                label->setTextFormat(Qt::PlainText);
                label->setWordWrap(true);
                label->setMinimumWidth(140);
                label->setMaximumWidth(220);
                label->setBuddy(editor);
                parameters->addRow(label, editor);
                editors[name] = editor;
            }
        }
        state();
    };
    auto selection = [&] {
        actions->clear();
        details->clear();
        const auto id = selectedId();
        if (store.entries().contains(id)) {
            const auto &entry = store.entries().at(id);
            if (entry.manifest) {
                const auto &manifest = *entry.manifest;
                details->setPlainText(manifest.name + " " + manifest.version + "\n" +
                                      manifest.description +
                                      "\nCommands: " + manifest.commands.join(", ") +
                                      (entry.error.isEmpty() ? QString{} : "\n" + entry.error));
                for (const auto &action : manifest.actions)
                    actions->addItem(action.toObject()["name"].toString(),
                                     action.toObject()["id"].toString());
            } else
                details->setPlainText(id + "\n" + entry.error);
        }
        parameterForm();
    };
    auto refresh = [&](QString preferred = {}) {
        if (preferred.isEmpty())
            preferred = selectedId();
        list->clear();
        for (const auto &[id, entry] : store.entries()) {
            const auto name = entry.manifest ? entry.manifest->name : id;
            const auto label = !entry.manifest          ? "Unavailable"
                               : !entry.error.isEmpty() ? "Error"
                               : entry.enabled          ? "Enabled"
                                                        : "Disabled";
            auto *item = new QListWidgetItem(name + " — " + label, list);
            item->setData(Qt::UserRole, id);
            if (id == preferred)
                list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count())
            list->setCurrentRow(0);
        selection();
    };
    auto guarded = [&](const std::function<void()> &operation) {
        try {
            operation();
        } catch (const std::exception &error) {
            status->setText(error.what());
        }
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, selection);
    connect(actions, &QComboBox::currentIndexChanged, &dialog, parameterForm);
    connect(install, &QPushButton::clicked, &dialog, [&] {
        guarded([&] {
            const auto filePath = QFileDialog::getOpenFileName(
                &dialog, "Install command package", {}, "SketchyUp extensions (*.sketchyext)");
            if (filePath.isEmpty())
                return;
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly) || file.size() > extensionManifestLimit)
                throw std::runtime_error("Cannot read bounded extension package");
            const auto source = file.read(extensionManifestLimit + 1);
            if (file.error() != QFileDevice::NoError)
                throw std::runtime_error("Extension package read failed");
            const auto manifest = parseExtensionManifest(source);
            store = ExtensionStore(directory);
            store.install(source);
            refresh(manifest.id);
            status->setText(
                "Installed disabled · Review its commands, then enable to run an action");
        });
    });
    connect(enable, &QPushButton::clicked, &dialog, [&] {
        guarded([&] {
            const auto id = selectedId();
            store = ExtensionStore(directory);
            const auto enabled = store.entries().at(id).enabled;
            store.setEnabled(id, !enabled);
            refresh(id);
            status->setText(enabled ? "Extension disabled"
                                    : "Extension enabled · Choose an action to run");
        });
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        guarded([&] {
            const auto id = selectedId();
            store = ExtensionStore(directory);
            store.remove(id);
            refresh();
            status->setText("Installed copy removed · Original package unchanged");
        });
    });
    connect(cancel, &QPushButton::clicked, &dialog, [&] {
        if (worker) {
            cancelRequested = true;
            worker->requestInterruption();
            status->setText("Canceling action…");
            state();
        }
    });
    connect(run, &QPushButton::clicked, &dialog, [&] {
        guarded([&] {
            if (busy)
                return;
            if (viewport_->selectionState().context())
                throw std::runtime_error(
                    "Return to model context before running an extension action");
            const auto id = selectedId();
            store = ExtensionStore(directory);
            const auto manifest = store.enabledManifest(id);
            const auto action = actions->currentData().toString();
            QJsonObject input;
            for (const auto &[name, editor] : editors) {
                if (const auto *number = qobject_cast<QDoubleSpinBox *>(editor))
                    input[name] = number->value();
                else if (const auto *flag = qobject_cast<QCheckBox *>(editor))
                    input[name] = flag->isChecked();
                else
                    input[name] = qobject_cast<QLineEdit *>(editor)->text();
            }
            const auto stamp = doc_.saveStamp();
            const auto revision = doc_.revision();
            busy = true;
            cancelRequested = false;
            status->setText("Preparing action…");
            state();
            worker = QThread::create([&, manifest, action, input, id, stamp, revision] {
                QJsonArray commands;
                QString failure;
                try {
                    commands = runExtensionWorker(manifest, action, input);
                } catch (const std::exception &error) {
                    failure = QString::fromUtf8(error.what()).left(256);
                }
                QMetaObject::invokeMethod(
                    &dialog,
                    [&, manifest, id, stamp, revision, commands, failure] {
                        if (closing)
                            return;
                        worker->wait();
                        delete worker;
                        worker = nullptr;
                        busy = false;
                        if (cancelRequested) {
                            status->setText("Action canceled · Model unchanged by the action");
                            state();
                            return;
                        }
                        try {
                            if (!failure.isEmpty())
                                throw std::runtime_error(failure.toStdString());
                            store = ExtensionStore(directory);
                            if (store.enabledManifest(id).source != manifest.source)
                                throw std::runtime_error(
                                    "Installed extension changed during the action");
                            if (!doc_.isCurrentSnapshot(stamp) || doc_.revision() != revision)
                                throw std::runtime_error(
                                    "The model changed. The extension action was not applied");
                            const auto edit = doc_.prepareEdit([&](Document &draft) {
                                executeBatch(
                                    draft,
                                    {{"apiVersion", 1},
                                     {"documentId", QString::fromStdString(draft.identity())},
                                     {"expectedRevision", QString::number(draft.revision())},
                                     {"commands", commands},
                                     {"history",
                                      QJsonObject{{"label", "Extension: " + manifest.name}}}},
                                    BatchResponse::Changes);
                            });
                            viewport_->applyPreparedEdit(edit);
                            status->setText("Action applied · Undo reverses this edit");
                        } catch (const std::exception &error) {
                            const auto message = QString::fromUtf8(error.what()).left(256);
                            try {
                                store = ExtensionStore(directory);
                                if (store.entries().contains(id))
                                    store.recordFailure(id, message);
                            } catch (const std::exception &persistence) {
                                status->setText(message + "\n" + persistence.what());
                                refresh(id);
                                return;
                            }
                            status->setText(message);
                            refresh(id);
                        }
                        state();
                    },
                    Qt::QueuedConnection);
            });
            worker->start();
        });
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    dialog.exec();
    closing = true;
    if (worker) {
        worker->requestInterruption();
        worker->wait();
        delete worker;
    }
}
} // namespace sketchy
