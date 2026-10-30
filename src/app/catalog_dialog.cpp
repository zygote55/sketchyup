#include "app/catalog_dialog.hpp"

#include "app/catalog_report.hpp"
#include "app/component_definition_section.hpp"
#include "app/exchange_progress_sheet.hpp"
#include "app/interface_preferences.hpp"
#include "io/catalog_insert.hpp"
#include "io/library_bundle.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>

namespace sketchy {
namespace {

// The Kind filter's positions (UX_DESIGN 18.5): All kinds, then the four kinds.
constexpr int kindAllPosition = 0;

int kindPosition(CatalogEntryKind kind) {
    switch (kind) {
    case CatalogEntryKind::Component:
        return 1;
    case CatalogEntryKind::Material:
        return 2;
    case CatalogEntryKind::Style:
        return 3;
    case CatalogEntryKind::Template:
        return 4;
    }
    return kindAllPosition;
}

QString quoted(const QString &text) { return QStringLiteral("“%1”").arg(text); }

QString capitalised(const QString &text) {
    return text.isEmpty() ? text : text.left(1).toUpper() + text.mid(1);
}

QString countWord(qsizetype count) {
    return count == 1 ? QStringLiteral("1 entry") : QStringLiteral("%1 entries").arg(count);
}

// A reader's reason as a sentence: no trailing full stop, so the message adds one.
QString sentence(const QString &reason) {
    QString text = reason.trimmed();
    while (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text.isEmpty() ? QString() : capitalised(text) + QLatin1Char('.');
}

// The reader's failure for a file, as the dialog lists it. What did not happen comes first, then the
// reason (UX_DESIGN 18.6).
QString unreadable(const QString &reason) {
    return QStringLiteral("Nothing is listed. This file is not a catalog SketchyUp can read: %1.")
        .arg(sentence(reason).chopped(1));
}

// A catalog in the library folder can be named to the batch (catalog.import's `library`, R100.d)
// only when it sits in the default folder, which the batch reads. Any other catalog is passed as
// the package the dialog listed when it was opened, so its bytes are not encoded into the batch.
bool namedInBatch(const CatalogSource &source) {
    return source.fromLibrary && !source.name.isEmpty() && isCatalogLibraryName(source.name) &&
           QDir(source.folder).absolutePath() == QDir(catalogLibraryFolder()).absolutePath();
}

// The state of a dependency as the reader found it (ADR 0174 section 5).
DependencyState stateOf(const CatalogPackage &package, unsigned owner,
                        const CatalogDependency &dependency) {
    if (dependency.kind == CatalogDependency::Kind::Asset) {
        if (dependency.id == 0 || dependency.id > package.assets.size())
            return DependencyState::Missing;
        return package.assets[dependency.id - 1].verified ? DependencyState::Present
                                                          : DependencyState::Tampered;
    }
    if (dependency.id == 0 || dependency.id > package.entries.size())
        return DependencyState::Missing;
    // An entry depends only on an earlier entry, so the walk ends (ADR 0174 section 4).
    if (dependency.id >= owner)
        return DependencyState::NotEarlier;
    return package.entries[dependency.id - 1].importable ? DependencyState::Present
                                                         : DependencyState::NotImportable;
}

QString stateWord(DependencyState state) {
    switch (state) {
    case DependencyState::Present:
        return QStringLiteral("present");
    case DependencyState::Missing:
        return QStringLiteral("missing");
    case DependencyState::Tampered:
        return QStringLiteral("tampered");
    case DependencyState::NotImportable:
        return QStringLiteral("not importable");
    case DependencyState::NotEarlier:
        return QStringLiteral("not earlier");
    }
    return QString();
}

// The reason word of a refused row (UX_DESIGN 18.5): "missing texture", "not importable" and so on.
QString reasonWord(CatalogDependency::Kind kind, DependencyState state) {
    const bool asset = kind == CatalogDependency::Kind::Asset;
    switch (state) {
    case DependencyState::Missing:
        return asset ? QStringLiteral("missing texture") : QStringLiteral("missing entry");
    case DependencyState::Tampered:
        return QStringLiteral("tampered texture");
    case DependencyState::NotImportable:
        return QStringLiteral("not importable");
    case DependencyState::NotEarlier:
        return QStringLiteral("not earlier");
    case DependencyState::Present:
        break;
    }
    return QStringLiteral("refused");
}

QString assetName(const CatalogPackage &package, std::uint32_t id) {
    return id >= 1 && id <= package.assets.size() ? package.assets[id - 1].name : QString();
}

QString entryName(const CatalogPackage &package, std::uint32_t id) {
    return id >= 1 && id <= package.entries.size() ? package.entries[id - 1].name : QString();
}

// "Asset 7 “Chair fabric” · missing", the dependency row of the report and the details pane.
QString dependencyRow(const CatalogPackage &package, const CatalogDependency &dependency,
                      DependencyState state) {
    const bool asset = dependency.kind == CatalogDependency::Kind::Asset;
    const auto name = asset ? assetName(package, dependency.id) : entryName(package, dependency.id);
    const auto label =
        name.isEmpty() ? QStringLiteral("%1 %2").arg(asset ? "Asset" : "Entry").arg(dependency.id)
                       : QStringLiteral("%1 %2 %3")
                             .arg(asset ? "Asset" : "Entry")
                             .arg(dependency.id)
                             .arg(quoted(name));
    return QStringLiteral("%1 · %2").arg(label, stateWord(state));
}

// The sentence that says why one dependency is not present (UX_DESIGN 18.5, 18.6).
QString dependencySentence(const CatalogPackage &package, const CatalogDependency &dependency,
                           DependencyState state) {
    const bool asset = dependency.kind == CatalogDependency::Kind::Asset;
    const auto id = QString::number(dependency.id);
    switch (state) {
    case DependencyState::Missing:
        if (asset) {
            const auto name = assetName(package, dependency.id);
            return name.isEmpty() ? QStringLiteral("Asset %1 is not in this catalog.").arg(id)
                                  : QStringLiteral("Asset %1 %2 is not in this catalog.")
                                        .arg(id, quoted(name));
        }
        return QStringLiteral("Entry %1 is not in this catalog.").arg(id);
    case DependencyState::Tampered:
        return QStringLiteral("Asset %1 does not match its recorded SHA-256.").arg(id);
    case DependencyState::NotImportable:
        return QStringLiteral("Entry %1 %2 is not importable.")
            .arg(id, quoted(entryName(package, dependency.id)));
    case DependencyState::NotEarlier:
        return QStringLiteral("Entry %1 is not earlier in the catalog.").arg(id);
    case DependencyState::Present:
        break;
    }
    return {};
}

// Why an entry cannot be imported by its own record: its first dependency that is not present, or
// the reader's reason for its own hash or payload. Empty when it can be imported.
QString importRefusal(const CatalogPackage &package, const CatalogEntryReport &entry) {
    if (entry.importable)
        return {};
    for (const auto &dependency : entry.dependencies) {
        const auto state = stateOf(package, entry.id, dependency);
        if (state != DependencyState::Present)
            return dependencySentence(package, dependency, state);
    }
    return sentence(entry.reason);
}

// The reason word of a refused entry's row: its first failing dependency, or its own kind.
QString refusalWord(const CatalogPackage &package, const CatalogEntryReport &entry) {
    if (entry.kind == CatalogEntryKind::Template && entry.importable)
        return QStringLiteral("not insertable");
    for (const auto &dependency : entry.dependencies) {
        const auto state = stateOf(package, entry.id, dependency);
        if (state != DependencyState::Present)
            return reasonWord(dependency.kind, state);
    }
    return QStringLiteral("refused");
}

// Why a listed entry cannot be inserted into a model, or empty when it can. A template opens as a
// new model, and no command does that yet, so it is never inserted (UX_DESIGN 18.5).
QString insertBlock(const CatalogPackage &package, const CatalogEntryReport &entry) {
    const auto refusal = importRefusal(package, entry);
    if (!refusal.isEmpty())
        return refusal;
    if (entry.kind == CatalogEntryKind::Template)
        return QStringLiteral("Templates open as a new model, and no command does that yet.");
    return {};
}

QString placementLine(const std::vector<const CatalogEntryReport *> &entries) {
    if (entries.size() == 1) {
        const auto *entry = entries.front();
        if (entry->kind == CatalogEntryKind::Style)
            return QStringLiteral("Applies the style to this model. It is one undo step.");
        if (entry->kind == CatalogEntryKind::Material)
            return QStringLiteral("Adds the material %1 to this model.").arg(quoted(entry->name));
        return QStringLiteral("Inserts %1 at the model’s origin.").arg(quoted(entry->name));
    }
    return QStringLiteral("Inserts %1 entries. Components are placed at the model’s origin.")
        .arg(qsizetype(entries.size()));
}

QString insertButtonText(const std::vector<const CatalogEntryReport *> &entries) {
    if (entries.size() != 1)
        return QStringLiteral("Insert %1 entries").arg(qsizetype(entries.size()));
    switch (entries.front()->kind) {
    case CatalogEntryKind::Component:
        return QStringLiteral("Insert component");
    case CatalogEntryKind::Material:
        return QStringLiteral("Insert material");
    case CatalogEntryKind::Style:
        return QStringLiteral("Apply style");
    case CatalogEntryKind::Template:
        break;
    }
    return QStringLiteral("Insert");
}

QString kindTitle(CatalogEntryKind kind) { return capitalised(catalogKindName(kind)); }

} // namespace

DependencyState dependencyState(const CatalogPackage &package, unsigned owner,
                                const CatalogDependency &dependency) {
    return stateOf(package, owner, dependency);
}

QString entryRefusal(const CatalogPackage &package, const CatalogEntryReport &entry) {
    return importRefusal(package, entry);
}

QString CatalogSource::title() const {
    if (package && !package->metadata.name.isEmpty())
        return package->metadata.name;
    return QFileInfo(path).fileName();
}

// What an insert's worker hands back to the dialog (R100.i). The worker fills it in, and the dialog
// reads it once the worker has finished.
struct CatalogInsertOutcome {
    QString title;   // the catalog, or the catalogs, the progress sheet names
    QString label;   // the undo step's label
    QString summary; // the first part of the status line, for a commit
    bool style{};    // a single style, whose neutral result is worded for a style
    std::map<QString, CatalogPackage> read;     // each catalog as the worker read it, by path
    std::map<QString, QString> unreadable;      // why a catalog could not be read, by path
    std::optional<Document::PreparedEdit> edit; // the composed insert, committed on the UI thread
};

namespace {

// One catalog an insert reads: its key in the listing, and where it is read from.
struct CatalogRead {
    QString path;
    bool fromLibrary{};
    QString folder, name;
};

// A selected entry: its catalog's path and its index in that catalog.
using EntryRef = std::pair<QString, size_t>;

// The batch an insert sends (R100.i): one catalog.import per catalog the selection uses, each naming
// its entries by the package the insert read. Every catalog is passed under a key of `opened`, so the
// engine reads no file of its own and no bytes are encoded into the batch.
QJsonArray insertBatch(const std::vector<EntryRef> &refs,
                       const std::map<QString, CatalogPackage> &read, OpenedCatalogs &opened) {
    std::map<QString, QStringList> byPath;
    std::vector<QString> order;
    for (const auto &[path, index] : refs) {
        const auto found = read.find(path);
        if (found == read.end())
            throw std::runtime_error("Nothing was inserted. A catalog the selection uses was not read.");
        if (index >= found->second.entries.size())
            throw std::runtime_error(
                QStringLiteral("Nothing was inserted. %1 changed since it was listed, and the entry "
                               "is no longer there. Choose the entries again.")
                    .arg(quoted(found->second.metadata.name))
                    .toStdString());
        if (!byPath.contains(path))
            order.push_back(path);
        byPath[path].append(found->second.entries[index].name);
    }
    QJsonArray result;
    for (const auto &path : order) {
        const auto key = QString::number(opened.size());
        opened.emplace(key, read.at(path));
        QJsonArray names;
        for (const auto &name : byPath[path])
            names.append(name);
        result.append(QJsonObject{{"command", "catalog.import"}, {"opened", key}, {"entries", names}});
    }
    return result;
}

// The insert, on the worker thread (R100.i). It reads each catalog the selection uses and verifies it,
// then composes the insert against the snapshot of the model. Nothing is changed here: the UI thread
// commits the composed edit. A cancel is checked between the steps, between the entries and inside the
// reader, and a cancelled insert leaves no partial result.
ExchangeResult runCatalogInsert(const ExchangeWorkerHooks &hooks,
                                const std::shared_ptr<const Document> &snapshot,
                                const std::vector<CatalogRead> &reads,
                                const std::vector<EntryRef> &refs,
                                const std::function<void(const std::function<bool()> &)> &gate,
                                const std::shared_ptr<CatalogInsertOutcome> &outcome) {
    const CatalogCancelCheck cancelled = hooks.cancelled;
    ExchangeResult result;
    try {
        if (gate)
            gate(cancelled);
        hooks.progress(QStringLiteral("read"), 0);
        std::vector<std::pair<QString, QByteArray>> bytes;
        for (const auto &catalog : reads) {
            try {
                const auto file = catalog.fromLibrary
                                      ? catalogLibraryPath(catalog.folder, catalog.name)
                                      : catalog.path;
                bytes.emplace_back(catalog.path, loadCatalogBytes(file, cancelled));
            } catch (const CatalogCancelled &) {
                throw;
            } catch (const std::exception &failure) {
                outcome->unreadable[catalog.path] = unreadable(QString::fromUtf8(failure.what()));
            }
        }
        hooks.progress(QStringLiteral("verify"), 0);
        for (const auto &[path, data] : bytes) {
            try {
                outcome->read[path] = decodeCatalog(data, cancelled);
            } catch (const CatalogCancelled &) {
                throw;
            } catch (const std::exception &failure) {
                outcome->unreadable[path] = unreadable(QString::fromUtf8(failure.what()));
            }
        }
        if (!outcome->unreadable.empty()) {
            result.code = "unreadable";
            return result;
        }
        hooks.progress(QStringLiteral("insert"), 0);
        OpenedCatalogs opened;
        const auto commands = insertBatch(refs, outcome->read, opened);
        const QJsonObject request{
            {"apiVersion", 1},
            {"documentId", QString::fromStdString(snapshot->identity())},
            {"expectedRevision", QString::number(snapshot->revision())},
            {"commands", commands},
            {"history", QJsonObject{{"label", outcome->label}}}};
        outcome->edit = snapshot->prepareEdit([&](Document &candidate) {
            executeBatch(candidate, request, BatchResponse::Changes, &opened, cancelled);
        });
        // A Cancel that arrived while the edit was composed still wins: nothing is committed.
        if (cancelled())
            throw CatalogCancelled();
        result.done = true;
    } catch (const CatalogCancelled &) {
        result.cancelled = true;
        result.code = "cancelled";
    }
    return result;
}

} // namespace


CatalogDialog::CatalogDialog(Document &doc, Viewport &view, QWidget *parent)
    : QDialog(parent), doc_(doc), view_(view),
      folder_(QSettings("SketchyUp", "SketchyUp")
                  .value("catalogFolder", catalogLibraryFolder())
                  .toString()) {
    setObjectName("catalogDialog");
    setWindowTitle("Asset catalogs");
    resize(1040, 700);
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        "Inserts components, materials and styles from a catalog file. The catalog file is never "
        "changed.");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // The library folder, with Choose folder… (remembered in preferences) and Refresh library.
    auto *folderRow = new QHBoxLayout;
    folderLabel_ = new QLabel(folder_);
    folderLabel_->setObjectName("catalogFolder");
    folderLabel_->setAccessibleName("Library folder");
    folderLabel_->setTextFormat(Qt::PlainText);
    folderLabel_->setWordWrap(true);
    // No button here is the dialog's default, so Enter in the search field does nothing.
    auto *choose = new QPushButton("Choose folder…");
    choose->setObjectName("catalogChooseFolder");
    choose->setAccessibleName("Choose library folder");
    choose->setToolTip("The MCP server and the command line read only the default catalogs folder.");
    choose->setAutoDefault(false);
    auto *refresh = new QPushButton("Refresh library");
    refresh->setObjectName("catalogRefresh");
    refresh->setAutoDefault(false);
    folderRow->addWidget(new QLabel("Library folder"));
    folderRow->addWidget(folderLabel_, 1);
    folderRow->addWidget(choose);
    folderRow->addWidget(refresh);
    layout->addLayout(folderRow);

    // Search over names, descriptions and labels, and the Kind filter.
    auto *filters = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setObjectName("catalogSearch");
    search_->setAccessibleName("Search names, descriptions and labels");
    search_->setPlaceholderText("Search names, descriptions and labels");
    search_->setMaxLength(1024);
    kind_ = new QComboBox;
    kind_->setObjectName("catalogKind");
    kind_->setAccessibleName("Kind");
    kind_->addItems({"All kinds", "Component", "Material", "Style", "Template"});
    filters->addWidget(new QLabel("Search"));
    filters->addWidget(search_, 1);
    filters->addWidget(kind_);
    layout->addLayout(filters);

    // The grouped entry list, one Tab stop, and the details of the current row.
    split_ = new QSplitter(Qt::Horizontal);
    split_->setObjectName("catalogSplit");
    list_ = new QTreeWidget;
    list_->setObjectName("catalogEntries");
    list_->setAccessibleName("Catalog entries");
    list_->setColumnCount(3);
    list_->setHeaderLabels({"Entry", "State", "Reason"});
    list_->setSelectionMode(QAbstractItemView::MultiSelection);
    list_->setWordWrap(true);
    list_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->setColumnWidth(0, 300);
    list_->installEventFilter(this);
    details_ = new QPlainTextEdit;
    details_->setObjectName("catalogDetails");
    details_->setAccessibleName("Entry details");
    details_->setReadOnly(true);
    split_->addWidget(list_);
    split_->addWidget(details_);
    split_->setStretchFactor(0, 3);
    split_->setStretchFactor(1, 2);
    layout->addWidget(split_, 1);

    notice_ = new QLabel;
    notice_->setObjectName("catalogNotice");
    notice_->setTextFormat(Qt::PlainText);
    notice_->setWordWrap(true);
    layout->addWidget(notice_);
    reason_ = new QLabel;
    reason_->setObjectName("catalogInsertReason");
    reason_->setTextFormat(Qt::PlainText);
    reason_->setWordWrap(true);
    layout->addWidget(reason_);

    // The footer: Open catalog file… on the left; Cancel, then Insert. Neither is the default,
    // so Enter in a field does nothing and Enter on an entry inserts it (the list's activation).
    auto *footer = new QHBoxLayout;
    open_ = new QPushButton("Open catalog file…");
    open_->setObjectName("catalogOpen");
    open_->setAutoDefault(false);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("catalogCancel");
    cancel->setAutoDefault(false);
    insert_ = new QPushButton("Insert");
    insert_->setObjectName("catalogInsert");
    insert_->setAutoDefault(false);
    insert_->setDefault(false);
    footer->addWidget(open_);
    footer->addStretch();
    footer->addWidget(cancel);
    footer->addWidget(insert_);
    layout->addLayout(footer);

    connect(choose, &QPushButton::clicked, this, &CatalogDialog::chooseFolder);
    connect(refresh, &QPushButton::clicked, this, &CatalogDialog::refreshLibrary);
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuildTree(); });
    connect(kind_, &QComboBox::currentIndexChanged, this, [this] { rebuildTree(); });
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &CatalogDialog::selectionChanged);
    connect(list_, &QTreeWidget::currentItemChanged, this, [this] { showDetails(); });
    connect(list_, &QTreeWidget::itemActivated, this, [this] {
        if (insertEnabled_)
            insertSelected();
    });
    connect(open_, &QPushButton::clicked, this, &CatalogDialog::openFile);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(insert_, &QPushButton::clicked, this, &CatalogDialog::insertSelected);

    refreshLibrary();
    search_->setFocus();
}

void CatalogDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    // The large text size (above 125 %, as the command search reads it) and a narrow dialog stack
    // the list above its details (UX_DESIGN 18.7).
    if (split_) {
        const bool stacked = event->size().width() < 900 || interfaceTextPercent() > 125;
        split_->setOrientation(stacked ? Qt::Vertical : Qt::Horizontal);
    }
}

bool CatalogDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == list_ && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        // Space toggles the current entry's selection, and a group row takes no selection (18.7).
        if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier) {
            auto *current = list_->currentItem();
            if (current && current->data(0, Qt::UserRole).isValid())
                current->setSelected(!current->isSelected());
            return true;
        }
        // Ctrl+A selects every insertable entry, not every row (UX_DESIGN 18.7).
        if (key->key() == Qt::Key_A && key->modifiers() == Qt::ControlModifier) {
            rebuilding_ = true;
            list_->clearSelection();
            for (int g = 0; g < list_->topLevelItemCount(); ++g) {
                auto *group = list_->topLevelItem(g);
                for (int c = 0; c < group->childCount(); ++c) {
                    auto *item = group->child(c);
                    const auto *source = sourceAt(item->data(0, Qt::UserRole + 1).toString());
                    if (item->data(0, Qt::UserRole).isValid() && source && source->package &&
                        insertBlock(*source->package,
                                    source->package->entries[size_t(
                                        item->data(0, Qt::UserRole).toULongLong())])
                            .isEmpty())
                        item->setSelected(true);
                }
            }
            rebuilding_ = false;
            selectionChanged();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

const CatalogSource *CatalogDialog::sourceAt(const QString &path) const {
    for (const auto &source : sources_)
        if (source.path == path)
            return &source;
    return nullptr;
}

bool CatalogDialog::matches(const CatalogSource &source, const CatalogEntryReport &entry) const {
    const int kind = kind_->currentIndex();
    if (kind != kindAllPosition && kindPosition(entry.kind) != kind)
        return false;
    const auto needle = search_->text().trimmed();
    if (needle.isEmpty())
        return true;
    const auto hit = [&](const QString &text) {
        return text.contains(needle, Qt::CaseInsensitive);
    };
    if (hit(entry.name) || hit(entry.description) || hit(source.package->metadata.name))
        return true;
    for (const auto &label : source.package->metadata.labels)
        if (hit(label))
            return true;
    return false;
}

std::vector<CatalogDialog::Ref> CatalogDialog::chosen() const {
    return {chosen_.begin(), chosen_.end()};
}

void CatalogDialog::refreshLibrary() {
    std::erase_if(sources_, [](const CatalogSource &source) { return source.fromLibrary; });
    try {
        const auto library = scanCatalogLibrary(folder_);
        std::vector<CatalogSource> listed;
        for (const auto &entry : library.entries) {
            CatalogSource source;
            source.fromLibrary = true;
            source.folder = folder_;
            source.name = entry.name;
            source.path = QDir(folder_).filePath(entry.name + QStringLiteral(".sketchycat"));
            // A file opened by hand is listed once, under its own entry.
            if (sourceAt(source.path))
                continue;
            if (!entry.error.isEmpty()) {
                source.error = unreadable(entry.error);
            } else {
                try {
                    source.package = loadCatalogLibraryEntry(folder_, entry.name);
                } catch (const std::exception &failure) {
                    source.error = unreadable(QString::fromUtf8(failure.what()));
                }
            }
            listed.push_back(std::move(source));
        }
        sources_.insert(sources_.begin(), std::make_move_iterator(listed.begin()),
                        std::make_move_iterator(listed.end()));
        notice_->setText(library.notices.join(QLatin1Char('\n')));
    } catch (const std::exception &failure) {
        notice_->setText(QString::fromUtf8(failure.what()));
    }
    rebuildTree();
    refreshInsert();
    showDetails();
}

void CatalogDialog::rebuildTree() {
    rebuilding_ = true;
    list_->clear();
    const bool filtered = !search_->text().trimmed().isEmpty() || kind_->currentIndex() != 0;
    for (const auto &source : sources_) {
        if (!source.package) {
            auto *group = new QTreeWidgetItem(
                list_, {QStringLiteral("%1 · could not be read").arg(source.title())});
            group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
            group->setData(0, Qt::UserRole + 1, source.path);
            group->setExpanded(true);
            auto *row = new QTreeWidgetItem(
                group, {source.title(), QStringLiteral("✕ Refused"), source.error});
            row->setFlags(row->flags() & ~Qt::ItemIsSelectable);
            continue;
        }
        const auto &package = *source.package;
        std::vector<size_t> shown;
        for (size_t index = 0; index < package.entries.size(); ++index)
            if (matches(source, package.entries[index]))
                shown.push_back(index);
        if (filtered && shown.empty())
            continue;
        auto *group =
            new QTreeWidgetItem(list_, {QStringLiteral("%1 · %2 · %3")
                                            .arg(source.title(), package.metadata.version)
                                            .arg(countWord(qsizetype(package.entries.size())))});
        group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
        group->setData(0, Qt::UserRole + 1, source.path);
        group->setExpanded(true);
        for (const auto index : shown) {
            const auto &entry = package.entries[index];
            const auto block = insertBlock(package, entry);
            auto *item = new QTreeWidgetItem(
                group, {QStringLiteral("%1 · %2").arg(entry.name, catalogKindName(entry.kind)),
                        block.isEmpty() ? QStringLiteral("✓ Insertable")
                                        : QStringLiteral("✕ Not insertable"),
                        block});
            item->setData(0, Qt::UserRole, qulonglong(index));
            item->setData(0, Qt::UserRole + 1, source.path);
            item->setSelected(chosen_.contains({source.path, index}));
        }
    }
    rebuilding_ = false;
}

void CatalogDialog::chooseFolder() {
    QFileDialog dialog(this, "Choose catalog folder", folder_);
    dialog.setObjectName("catalogFolderDialog");
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly);
    if (dialog.exec() != QDialog::Accepted)
        return;
    // A folder chooser with nothing selected chooses the folder it shows.
    const auto picked = dialog.selectedFiles().isEmpty() ? dialog.directory().absolutePath()
                                                         : dialog.selectedFiles().constFirst();
    // The choice is remembered in preferences, as the component library's is (UX_DESIGN 18.1).
    folder_ = QDir(picked).absolutePath();
    QSettings("SketchyUp", "SketchyUp").setValue("catalogFolder", folder_);
    folderLabel_->setText(folder_);
    refreshLibrary();
}

void CatalogDialog::openFile() {
    QFileDialog dialog(this, "Open catalog file", folder_);
    dialog.setObjectName("catalogOpenDialog");
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter("SketchyUp catalogs (*.sketchycat);;All files (*)");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    CatalogSource source;
    source.path = QFileInfo(dialog.selectedFiles().constFirst()).absoluteFilePath();
    const QFileInfo info(source.path);
    try {
        // The bounded loader refuses a larger file, but the size is named here, in the words
        // of 18.6.
        if (info.size() > qint64(libraryBundleLimit))
            throw std::runtime_error(QStringLiteral("the file is %1 MiB, and the limit is 130 MiB")
                                         .arg(double(info.size()) / (1024.0 * 1024.0), 0, 'f', 1)
                                         .toStdString());
        source.package = loadCatalog(source.path);
    } catch (const std::exception &failure) {
        source.error = unreadable(QString::fromUtf8(failure.what()));
    }
    // Opening a file again replaces its listing; the file is never written.
    std::erase_if(sources_,
                  [&](const CatalogSource &listed) { return listed.path == source.path; });
    sources_.push_back(std::move(source));
    rebuildTree();
    refreshInsert();
    showDetails();
}

void CatalogDialog::selectionChanged() {
    if (rebuilding_)
        return;
    // Only the listed rows change the selection; a row that the filter hides keeps its state.
    for (int g = 0; g < list_->topLevelItemCount(); ++g) {
        auto *group = list_->topLevelItem(g);
        for (int c = 0; c < group->childCount(); ++c) {
            auto *item = group->child(c);
            if (!item->data(0, Qt::UserRole).isValid())
                continue;
            const Ref ref{item->data(0, Qt::UserRole + 1).toString(),
                          size_t(item->data(0, Qt::UserRole).toULongLong())};
            if (item->isSelected())
                chosen_.insert(ref);
            else
                chosen_.erase(ref);
        }
    }
    refreshInsert();
    showDetails();
}

void CatalogDialog::showDetails() {
    auto *item = list_->currentItem();
    if (!item) {
        details_->setPlainText("Choose a catalog or an entry to see its details.");
        return;
    }
    const auto *source = sourceAt(item->data(0, Qt::UserRole + 1).toString());
    if (!source) {
        details_->clear();
        return;
    }
    if (!source->package) {
        details_->setPlainText(QStringLiteral("%1\n%2").arg(source->title(), source->error));
        return;
    }
    const auto &package = *source->package;
    QStringList lines;
    if (!item->data(0, Qt::UserRole).isValid()) {
        // A catalog's group row: the metadata of the file.
        lines << QStringLiteral("%1 %2").arg(package.metadata.name, package.metadata.version);
        lines << (package.metadata.description.isEmpty() ? QStringLiteral("No description.")
                                                         : package.metadata.description);
        lines << QStringLiteral("Author: %1")
                     .arg(package.metadata.author.isEmpty() ? QStringLiteral("none")
                                                            : package.metadata.author);
        lines << QStringLiteral("Licence: %1").arg(package.metadata.licenseIdentifier);
        lines << QStringLiteral("Labels: %1")
                     .arg(package.metadata.labels.isEmpty() ? QStringLiteral("none")
                                                            : package.metadata.labels.join(", "));
        lines << QStringLiteral("Entries: %1").arg(countWord(qsizetype(package.entries.size())));
        lines << QStringLiteral("File: %1").arg(source->path);
        details_->setPlainText(lines.join(QLatin1Char('\n')));
        return;
    }
    const auto index = size_t(item->data(0, Qt::UserRole).toULongLong());
    const auto &entry = package.entries[index];
    const auto block = insertBlock(package, entry);
    lines << QStringLiteral("%1 · %2 · %3")
                 .arg(entry.name, kindTitle(entry.kind),
                      block.isEmpty() ? QStringLiteral("Insertable")
                                      : QStringLiteral("Not insertable"));
    if (!block.isEmpty())
        lines << QStringLiteral("✕ %1").arg(block);
    lines << (entry.description.isEmpty() ? QStringLiteral("No description.") : entry.description);
    lines << QStringLiteral("From %1 %2 · licence %3 · author %4")
                 .arg(package.metadata.name, package.metadata.version,
                      package.metadata.licenseIdentifier,
                      package.metadata.author.isEmpty() ? QStringLiteral("none")
                                                        : package.metadata.author);
    lines << QStringLiteral("Dependencies:");
    if (entry.dependencies.empty())
        lines << QStringLiteral("  None.");
    for (const auto &dependency : entry.dependencies) {
        const auto state = stateOf(package, entry.id, dependency);
        lines << QStringLiteral("  %1 %2")
                     .arg(state == DependencyState::Present ? QStringLiteral("✓")
                                                            : QStringLiteral("✕"))
                     .arg(dependencyRow(package, dependency, state));
    }
    switch (entry.kind) {
    case CatalogEntryKind::Component:
        lines << QStringLiteral(
            "Placement: inserted at the model’s origin. A position field follows "
            "when the engine takes one.");
        break;
    case CatalogEntryKind::Material:
        lines << QStringLiteral("Placement: added to the model’s materials.");
        break;
    case CatalogEntryKind::Style:
        lines << QStringLiteral("Placement: applied to the model’s style; nothing is placed.");
        break;
    case CatalogEntryKind::Template:
        lines << QStringLiteral("Templates open as a new model, and no command does that yet.");
        break;
    }
    details_->setPlainText(lines.join(QLatin1Char('\n')));
}

void CatalogDialog::setInsertState(bool enabled, const QString &text, const QString &reason) {
    insertEnabled_ = enabled;
    insert_->setText(text);
    insert_->setEnabled(enabled);
    insert_->setAccessibleDescription(reason);
    reason_->setText(reason);
}

void CatalogDialog::refreshInsert() {
    refusal_.clear();
    const auto refs = chosen();
    if (refs.empty()) {
        setInsertState(
            false, QStringLiteral("Insert"),
            QStringLiteral(
                "Select an entry to insert. Entries are inserted at the model’s origin."));
        return;
    }
    std::vector<const CatalogEntryReport *> entries;
    for (const auto &ref : refs) {
        const auto *source = sourceAt(ref.first);
        if (!source || !source->package || ref.second >= source->package->entries.size()) {
            const auto name = source ? source->title() : ref.first;
            const auto why = source && !source->error.isEmpty()
                                 ? source->error
                                 : QStringLiteral("It is no longer in the catalog.");
            setInsertState(false, QStringLiteral("Insert"),
                           QStringLiteral("Insert is off: %1 cannot be inserted. %2 %3")
                               .arg(quoted(name), why,
                                    refs.size() == 1
                                        ? QStringLiteral("Select another entry to insert.")
                                        : QStringLiteral("Deselect it to insert the other %1.")
                                              .arg(qsizetype(refs.size() - 1))));
            return;
        }
        const auto &package = *source->package;
        const auto &entry = package.entries[ref.second];
        entries.push_back(&entry);
    }
    // The first selected entry that cannot be inserted is named, with the reason, as 18.5 says.
    for (size_t i = 0; i < refs.size(); ++i) {
        const auto &package = *sourceAt(refs[i].first)->package;
        const auto why = insertBlock(package, *entries[i]);
        if (why.isEmpty())
            continue;
        setInsertState(false, QStringLiteral("Insert"),
                       QStringLiteral("Insert is off: %1 cannot be inserted. %2 %3")
                           .arg(quoted(entries[i]->name), why,
                                refs.size() == 1
                                    ? QStringLiteral("Select another entry to insert.")
                                    : QStringLiteral("Deselect it to insert the other %1.")
                                          .arg(qsizetype(refs.size() - 1))));
        return;
    }
    // The engine's shared dry run (previewBatch): the insert is checked before the button is on,
    // so the dialog never waits for a refusal.
    OpenedCatalogs opened;
    const auto batch = commands(refs, opened);
    const auto refusal = authoring::dryRun(doc_, batch, &opened);
    if (!refusal.isEmpty()) {
        refusal_ = refusal["message"].toString();
        setInsertState(false, QStringLiteral("Insert"),
                       QStringLiteral("Insert is off: %1").arg(refusal_));
        return;
    }
    setInsertState(true, insertButtonText(entries), placementLine(entries));
}

QJsonArray CatalogDialog::commands(const std::vector<Ref> &refs, OpenedCatalogs &opened) const {
    // One catalog.import per catalog the selection uses, so each catalog is named once.
    std::map<QString, QStringList> byPath;
    std::vector<QString> order;
    for (const auto &ref : refs) {
        if (!byPath.contains(ref.first))
            order.push_back(ref.first);
        const auto *source = sourceAt(ref.first);
        if (source && source->package && ref.second < source->package->entries.size())
            byPath[ref.first].append(source->package->entries[ref.second].name);
    }
    QJsonArray result;
    for (const auto &path : order) {
        const auto *source = sourceAt(path);
        if (!source || !source->package)
            continue;
        QJsonObject command{{"command", "catalog.import"}};
        if (namedInBatch(*source)) {
            command["library"] = source->name;
        } else {
            // The key is the catalog's place in `opened`; the package shares its file's bytes.
            const auto key = QString::number(opened.size());
            opened.emplace(key, *source->package);
            command["opened"] = key;
        }
        QJsonArray names;
        for (const auto &name : byPath[path])
            names.append(name);
        command["entries"] = names;
        result.append(command);
    }
    return result;
}

QString CatalogDialog::insertLabel(const std::vector<Ref> &refs) const {
    if (refs.size() == 1) {
        const auto *source = sourceAt(refs.front().first);
        const auto &entry = source->package->entries[refs.front().second];
        switch (entry.kind) {
        case CatalogEntryKind::Component:
            return QStringLiteral("Insert component %1").arg(quoted(entry.name));
        case CatalogEntryKind::Material:
            return QStringLiteral("Insert material %1").arg(quoted(entry.name));
        case CatalogEntryKind::Style:
            return QStringLiteral("Apply style %1").arg(quoted(entry.name));
        case CatalogEntryKind::Template:
            break;
        }
    }
    std::set<QString> paths;
    for (const auto &ref : refs)
        paths.insert(ref.first);
    if (paths.size() == 1) {
        const auto *source = sourceAt(*paths.begin());
        return QStringLiteral("Insert %1 from %2")
            .arg(countWord(qsizetype(refs.size())), quoted(source->title()));
    }
    return QStringLiteral("Insert %1").arg(countWord(qsizetype(refs.size())));
}

void CatalogDialog::insertSelected() {
    if (!insertEnabled_ || insertRunner_)
        return;
    const auto refs = chosen();
    const auto outcome = std::make_shared<CatalogInsertOutcome>();
    outcome->label = insertLabel(refs);
    outcome->style =
        refs.size() == 1 && sourceAt(refs.front().first)->package->entries[refs.front().second].kind ==
                                CatalogEntryKind::Style;
    // Each catalog the selection uses is read again on the worker, so the check and the insert see
    // the file as it is now. The entries are named as the listing has them.
    std::vector<CatalogRead> reads;
    std::vector<EntryRef> entryRefs;
    std::set<QString> paths;
    QStringList titles;
    for (const auto &ref : refs) {
        const auto *source = sourceAt(ref.first);
        entryRefs.push_back(ref);
        if (paths.insert(ref.first).second) {
            reads.push_back({source->path, source->fromLibrary, source->folder, source->name});
            titles << source->title();
        }
    }
    outcome->title = titles.join(QStringLiteral(", "));
    outcome->summary =
        refs.size() == 1
            ? QStringLiteral("Inserted %1 at the model’s origin")
                  .arg(quoted(sourceAt(refs.front().first)->package->entries[refs.front().second].name))
            : QStringLiteral("Inserted %1 at the model’s origin")
                  .arg(countWord(qsizetype(refs.size())));
    // The composed edit is built against this snapshot. The model cannot change under the insert: the
    // progress sheet is modal, and the commit checks the revision again.
    const auto snapshot = std::make_shared<const Document>(doc_.readSnapshot());
    const auto gate = insertGateForTesting;
    insertOutcome_ = outcome;
    insertRunner_ = std::make_unique<ExchangeRunner>();
    insertRunner_->begin(
        [snapshot, reads, entryRefs, gate, outcome](const ExchangeWorkerHooks &hooks) {
            return runCatalogInsert(hooks, snapshot, reads, entryRefs, gate, outcome);
        },
        QStringLiteral("The catalog insert failed."));
    // runFinished, not QThread::finished (D-073): the runner posts runFinished to this thread, so it
    // arrives after this function returns to an event loop, however fast the job is. QThread::finished
    // is emitted by the worker as it exits, and a refused read can exit before this connect is made;
    // then nothing settles the insert and the loop below never quits.
    connect(insertRunner_.get(), &ExchangeRunner::runFinished, this, &CatalogDialog::insertFinished);
    // The sheet opens only if the insert is still running after the delay, so a quick insert shows none.
    insertDelay_ = new QTimer(this);
    insertDelay_->setSingleShot(true);
    connect(insertDelay_, &QTimer::timeout, this, &CatalogDialog::showInsertProgress);
    insertDelay_->start(insertProgressDelayMs);
    insert_->setEnabled(false);
    reason_->setText(QStringLiteral("Inserting…"));
    // The caller sees the settled result, as it did when the insert ran in line. The dialog keeps
    // handling events meanwhile, and the progress sheet, when it opens, is modal.
    QEventLoop loop;
    insertLoop_ = &loop;
    loop.exec();
    insertLoop_ = nullptr;
}

void CatalogDialog::showInsertProgress() {
    if (!insertRunner_ || !insertRunner_->isRunning() || insertSheet_)
        return;
    insertSheet_ = new ExchangeProgressSheet(insertRunner_.get(), catalogInsertProgressConfig(),
                                             insertOutcome_->title, this);
    // Modal, so the dialog behind it takes no input while the insert runs.
    insertSheet_->setModal(true);
    insertSheet_->show();
}

void CatalogDialog::insertFinished() {
    if (!insertRunner_)
        return;
    insertRunner_->wait();
    if (insertDelay_) {
        insertDelay_->stop();
        insertDelay_->deleteLater();
        insertDelay_ = nullptr;
    }
    // The sheet closes itself when the run ends (its own finished handler, which waits for the
    // platform's focus as every settled prompt does), so the result is settled once it has closed.
    if (insertSheet_ && insertSheet_->isVisible()) {
        connect(insertSheet_, &QDialog::finished, this, &CatalogDialog::settleInsert,
                Qt::SingleShotConnection);
        return;
    }
    settleInsert();
}

void CatalogDialog::settleInsert() {
    // However the insert ends, the loop in insertSelected returns once it has been settled here.
    struct ReturnToCaller {
        QEventLoop *loop;
        ~ReturnToCaller() {
            if (loop)
                loop->quit();
        }
    } returnToCaller{insertLoop_};
    const auto runner = std::move(insertRunner_);
    const auto outcome = std::move(insertOutcome_);
    if (insertSheet_) {
        insertSheet_->deleteLater();
        insertSheet_ = nullptr;
    }
    insert_->setEnabled(insertEnabled_);
    // A cancel changes nothing: no undo step, and no catalog is listed again.
    if (runner->wasCancelled()) {
        reason_->setText(
            QStringLiteral("Insert cancelled. Nothing was inserted, and your model is unchanged."));
        return;
    }
    if (runner->code() == "unreadable") {
        reportRefusal(*outcome, QString());
        return;
    }
    if (!runner->hasResult()) {
        const auto message = runner->message();
        if (message == QStringLiteral("Batch has no committed changes")) {
            // The model already has these entries: a neutral result, not a refusal (UX_DESIGN 18.2).
            inserted_ = outcome->style
                            ? QStringLiteral("This model already has this style. Nothing changed.")
                            : QStringLiteral("This model already has these entries. Nothing changed.");
            accept();
            return;
        }
        reportRefusal(*outcome, message);
        return;
    }
    // The commit: the composed edit, as one labelled undo step.
    try {
        view_.applyPreparedEdit(*outcome->edit);
    } catch (const std::exception &failure) {
        reportRefusal(*outcome, QString::fromUtf8(failure.what()));
        return;
    }
    inserted_ = QStringLiteral("%1. Undo: %2.").arg(outcome->summary, outcome->label);
    accept();
}

// The report of an insert that did not commit. The listing takes what the insert read. The refusal
// line is the engine's own only when the dialog says the insert is on, as refreshInsert decides; when a
// listed entry is blocked by its own reason, the entry's row says why, as it always has.
void CatalogDialog::reportRefusal(const CatalogInsertOutcome &outcome, const QString &refusal) {
    applyReads(outcome);
    refreshInsert();
    showReport(insertEnabled_ ? refusal : refusal_);
}

void CatalogDialog::applyReads(const CatalogInsertOutcome &outcome) {
    for (auto &source : sources_) {
        if (const auto read = outcome.read.find(source.path); read != outcome.read.end()) {
            source.package = read->second;
            source.error.clear();
        }
        if (const auto failed = outcome.unreadable.find(source.path);
            failed != outcome.unreadable.end()) {
            source.package.reset();
            source.error = failed->second;
        }
    }
    rebuildTree();
}

void CatalogDialog::showReport(const QString &refusal) {
    CatalogInsertReport report;
    const auto refs = chosen();
    std::set<QString> paths;
    for (const auto &ref : refs)
        paths.insert(ref.first);
    const auto *first = paths.size() == 1 ? sourceAt(*paths.begin()) : nullptr;
    if (first && first->package)
        report.catalog =
            QStringLiteral("%1 %2").arg(first->title(), first->package->metadata.version);
    else if (first)
        report.catalog = first->title();
    else
        report.catalog = QStringLiteral("%1 catalogs").arg(qsizetype(paths.size()));
    report.refusal = refusal;
    for (const auto &ref : refs) {
        CatalogReportEntry row;
        const auto *source = sourceAt(ref.first);
        if (!source || !source->package || ref.second >= source->package->entries.size()) {
            row.name = source ? source->title() : ref.first;
            row.kind = QStringLiteral("entry");
            row.refused = true;
            row.reason = QStringLiteral("refused");
            row.explanation = source && !source->error.isEmpty()
                                  ? source->error
                                  : QStringLiteral("It is no longer in the catalog.");
            report.entries.push_back(row);
            continue;
        }
        const auto &package = *source->package;
        const auto &entry = package.entries[ref.second];
        row.name = entry.name;
        row.kind = catalogKindName(entry.kind);
        row.number = entry.id;
        const auto block = insertBlock(package, entry);
        row.refused = !block.isEmpty();
        if (row.refused) {
            row.reason = refusalWord(package, entry);
            for (const auto &dependency : entry.dependencies) {
                const auto state = stateOf(package, entry.id, dependency);
                if (state != DependencyState::Present)
                    row.dependencies << dependencyRow(package, dependency, state);
            }
            row.explanation = block;
        }
        report.entries.push_back(row);
    }
    report.notices << QStringLiteral("Components are placed at the model’s origin. Templates are "
                                     "not inserted.");
    const auto result = showCatalogReport(this, report);
    if (result.choice == CatalogReportChoice::SelectOthers) {
        chosen_.clear();
        for (size_t i = 0; i < report.entries.size() && i < refs.size(); ++i)
            if (!report.entries[i].refused)
                chosen_.insert(refs[i]);
    } else if (result.choice == CatalogReportChoice::RemoveEntry && result.entry < refs.size()) {
        chosen_.erase(refs[result.entry]);
    }
    rebuildTree();
    refreshInsert();
    showDetails();
}

} // namespace sketchy
