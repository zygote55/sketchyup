#include "app/unit_display.hpp"
#include "app/window.hpp"
#include "automation/measurements.hpp"
#include "io/library_catalog.hpp"
#include <QBuffer>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
namespace sketchy {
namespace {
QString libraryFolder() {
    return QSettings("SketchyUp", "SketchyUp")
        .value("libraryFolder",
               QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/library")
        .toString();
}
QByteArray thumbnail(Viewport &view) {
    const auto image = view.renderRaster({320, 240});
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        throw std::runtime_error("Could not capture a library thumbnail");
    return bytes;
}
} // namespace
void Window::openTemplatePath(const QString &path) {
    const auto bundle = loadTemplateBundle(path);
    auto fresh = instantiateTemplate(bundle);
    if (!canReplace())
        return;
    doc_ = std::move(fresh);
    resetRecoveryContext();
    path_.clear();
    viewport_->cancel();
    viewport_->refresh();
    viewport_->setSelection(0);
    viewport_->fit();
    if (bundle.metadata.defaultScene)
        viewport_->recallSavedScene(bundle.metadata.defaultScene, false);
    sync();
    status_->setText("Created an unsaved model from template · Library source unchanged");
}
void Window::libraryDialog(bool templates) {
    QDialog dialog(this);
    dialog.setObjectName("libraryDialog");
    dialog.setWindowTitle(templates ? "New from template" : "Insert library component");
    dialog.resize(720, 600);
    QVBoxLayout layout(&dialog);
    QFormLayout form;
    auto *folder = new QLineEdit(libraryFolder());
    folder->setObjectName("libraryFolder");
    folder->setAccessibleName("Library folder");
    form.addRow("&Library folder", folder);
    auto *choose = new QPushButton("Choose &folder…");
    choose->setObjectName("libraryChooseFolder");
    auto *refresh = new QPushButton("&Refresh library");
    refresh->setObjectName("libraryRefresh");
    QHBoxLayout folderButtons;
    folderButtons.addWidget(choose);
    folderButtons.addWidget(refresh);
    form.addRow(&folderButtons);
    auto *search = new QLineEdit;
    search->setObjectName("librarySearch");
    search->setAccessibleName("Search library names, descriptions and labels");
    search->setMaxLength(1024);
    form.addRow("&Search", search);
    auto *position = new QLineEdit("0, 0, 0");
    if (QLocale().decimalPoint() == ",")
        position->setText("0; 0; 0");
    position->setObjectName("libraryPosition");
    position->setAccessibleName("World placement position");
    if (!templates)
        form.addRow("&World position (x, y, z)", position);
    else {
        position->setParent(&dialog);
        position->hide();
    }
    layout.addLayout(&form);
    auto *list = new QListWidget;
    list->setObjectName("libraryEntries");
    list->setAccessibleName(templates ? "Template library" : "Component library");
    list->setIconSize({120, 90});
    layout.addWidget(list, 1);
    auto *details = new QLabel;
    details->setObjectName("libraryDetails");
    details->setTextFormat(Qt::PlainText);
    details->setWordWrap(true);
    layout.addWidget(details);
    auto *error = new QLabel;
    error->setObjectName("libraryError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    layout.addWidget(error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *accept = buttons.button(QDialogButtonBox::Ok);
    accept->setText(templates ? "&Create model" : "&Insert component");
    accept->setEnabled(false);
    layout.addWidget(&buttons);
    LibraryCatalog catalog;
    auto selection = [&]() -> const LibraryEntry * {
        const auto *item = list->currentItem();
        if (!item)
            return nullptr;
        const auto index = item->data(Qt::UserRole).toULongLong();
        return index < catalog.entries.size() ? &catalog.entries[size_t(index)] : nullptr;
    };
    auto selected = [&] {
        const auto *entry = selection();
        accept->setEnabled(entry && entry->error.isEmpty());
        details->setText(!entry                    ? QString{}
                         : !entry->error.isEmpty() ? entry->error
                                                   : entry->metadata.description + "\n" +
                                                         entry->metadata.labels.join(", "));
    };
    auto filter = [&] {
        list->clear();
        for (size_t i = 0; i < catalog.entries.size(); ++i) {
            const auto &entry = catalog.entries[i];
            if (entry.error.isEmpty() && (entry.kind == LibraryKind::Template) != templates)
                continue;
            if (!matchesLibrarySearch(entry, search->text()))
                continue;
            auto *item = new QListWidgetItem(
                entry.metadata.name + (entry.error.isEmpty() ? QString{} : " — Unavailable"), list);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(i));
            if (!entry.thumbnailPng.isEmpty()) {
                const auto image = QImage::fromData(entry.thumbnailPng, "PNG");
                item->setIcon(QPixmap::fromImage(
                    image.scaled(120, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
        }
        if (list->count())
            list->setCurrentRow(0);
        selected();
    };
    auto scan = [&] {
        try {
            catalog = scanLibraryDirectory(folder->text());
            QSettings("SketchyUp", "SketchyUp")
                .setValue("libraryFolder", QDir(folder->text()).absolutePath());
            error->setText(catalog.notices.join("\n"));
            filter();
        } catch (const std::exception &failure) {
            catalog = {};
            list->clear();
            selected();
            error->setText(failure.what());
        }
    };
    connect(refresh, &QPushButton::clicked, &dialog, scan);
    connect(folder, &QLineEdit::returnPressed, &dialog, scan);
    connect(choose, &QPushButton::clicked, &dialog, [&] {
        const auto path =
            QFileDialog::getExistingDirectory(&dialog, "Choose library folder", folder->text());
        if (!path.isEmpty()) {
            folder->setText(path);
            scan();
        }
    });
    connect(search, &QLineEdit::textChanged, &dialog, filter);
    connect(list, &QListWidget::currentRowChanged, &dialog, selected);
    connect(list, &QListWidget::itemActivated, &dialog, [accept] {
        if (accept->isEnabled())
            QMetaObject::invokeMethod(accept, "click", Qt::QueuedConnection);
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            const auto *entry = selection();
            if (!entry || !entry->error.isEmpty())
                return;
            // Reload and validate; the catalog deliberately does not retain decoded models.
            if (templates)
                openTemplatePath(entry->path);
            else {
                const auto values =
                    parseMeasurements(position->text(), inputUnit(doc_.displayUnits()), QLocale());
                if (values.kind != MeasurementKind::Values || values.values.size() != 3)
                    throw std::runtime_error("Enter three world position values in document units");
                const auto bundle = loadComponentBundle(entry->path);
                viewport_->insertLibrary(bundle,
                                         {values.values[0], values.values[1], values.values[2]});
            }
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(failure.what());
        }
    });
    scan();
    search->setFocus();
    dialog.exec();
}
void Window::saveLibraryDialog(bool component) {
    if (component && doc_.definitions().empty())
        throw std::runtime_error("Make a component before saving it to the library");
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    QDialog dialog(this);
    dialog.setObjectName("saveLibraryDialog");
    dialog.setWindowTitle(component ? "Save component to library" : "Save template to library");
    QFormLayout form(&dialog);
    auto *name = new QLineEdit(component ? "Component" : "Template");
    name->setObjectName("libraryName");
    name->setMaxLength(256);
    form.addRow("&Name", name);
    auto *description = new QPlainTextEdit;
    description->setObjectName("libraryDescription");
    description->setAccessibleName("Description");
    description->setMaximumHeight(100);
    form.addRow("&Description", description);
    auto *labels = new QLineEdit;
    labels->setObjectName("libraryLabels");
    labels->setMaxLength(2048);
    form.addRow("&Labels (comma separated)", labels);
    auto *choice = new QComboBox;
    choice->setObjectName("libraryRecord");
    if (component) {
        for (const auto &[id, definition] : doc_.definitions())
            choice->addItem(QString::fromStdString(definition->name),
                            QVariant::fromValue<qulonglong>(id));
        form.addRow("&Component", choice);
    } else {
        choice->addItem("Fit model", QVariant::fromValue<qulonglong>(0));
        for (const auto &[id, scene] : doc_.scenes())
            choice->addItem(QString::fromStdString(scene->name),
                            QVariant::fromValue<qulonglong>(id));
        form.addRow("&Default view", choice);
    }
    auto *note = new QLabel(component ? "Includes the selected component and its dependencies. The "
                                        "thumbnail uses the current model view."
                                      : "Includes the complete model, defaults and embedded "
                                        "resources. The thumbnail uses the current model view.");
    note->setWordWrap(true);
    form.addRow(note);
    auto *error = new QLabel;
    error->setObjectName("saveLibraryError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    form.addRow(error);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (!doc_.isCurrentSnapshot(stamp) || doc_.revision() != revision)
                throw std::runtime_error("The model changed. Reopen the library save dialog.");
            TemplateMetadata metadata{name->text().trimmed(),
                                      description->toPlainText(),
                                      {},
                                      component ? 0 : choice->currentData().toULongLong()};
            for (const auto &label : labels->text().split(',', Qt::SkipEmptyParts))
                metadata.labels.append(label.trimmed());
            const auto png = thumbnail(*viewport_);
            const auto bytes =
                component ? encodeComponentBundle(doc_, choice->currentData().toULongLong(),
                                                  metadata, png)
                          : encodeTemplateBundle(doc_, metadata, png);
            QFileDialog file(&dialog, "Save new library bundle", libraryFolder());
            file.setObjectName("librarySaveFileDialog");
            file.setAcceptMode(QFileDialog::AcceptSave);
            file.setFileMode(QFileDialog::AnyFile);
            file.setNameFilter("SketchyUp library bundles (*.sketchylib)");
            file.setDefaultSuffix("sketchylib");
            file.setOption(QFileDialog::DontConfirmOverwrite);
            if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
                return;
            const auto path = file.selectedFiles().front();
            if (component)
                writeComponentBundle(bytes, path);
            else
                writeTemplateBundle(bytes, path);
            QSettings("SketchyUp", "SketchyUp")
                .setValue("libraryFolder", QFileInfo(path).absolutePath());
            status_->setText("Saved library bundle · Model unchanged");
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(failure.what());
        }
    });
    dialog.exec();
}
} // namespace sketchy
