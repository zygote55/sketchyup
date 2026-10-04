#include "app/organization_panel.hpp"
#include "core/groups.hpp"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDropEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QMimeData>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QUuid>
#include <QVBoxLayout>
namespace sketchy {
namespace {
Id identity(QTreeWidgetItem *item) { return item ? item->data(0, Qt::UserRole).toULongLong() : 0; }
QString sid(Id id) { return QString::number(id); }
class HierarchyTree : public QTreeWidget {
  public:
    std::function<bool(const std::set<Id> &, Id)> reparent;
    explicit HierarchyTree(Document &doc) : doc_(doc) {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
    }

  protected:
    QStringList mimeTypes() const override { return {format}; }
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &rows) const override {
        dragged_.clear();
        for (auto *item : rows)
            dragged_.insert(identity(item));
        stamp_ = doc_.saveStamp();
        revision_ = doc_.revision();
        token_ = QUuid::createUuid().toByteArray();
        auto *data = new QMimeData;
        data->setData(format, token_);
        return data;
    }
    void startDrag(Qt::DropActions) override {
        // Qt's default move removes source rows after exec(). Only the document
        // command may change ownership; refresh renders the resulting records.
        auto *drag = new QDrag(this);
        drag->setMimeData(mimeData(selectedItems()));
        drag->exec(Qt::MoveAction, Qt::MoveAction);
        drag->deleteLater();
        dragged_.clear();
        token_.clear();
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (valid(event->mimeData()))
            event->acceptProposedAction();
        else
            event->ignore();
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if (valid(event->mimeData()))
            QTreeWidget::dragMoveEvent(event);
        else
            event->ignore();
    }
    void dropEvent(QDropEvent *event) override {
        if (!valid(event->mimeData())) {
            event->ignore();
            return;
        }
        auto *target = itemAt(event->position().toPoint());
        const auto ids = dragged_;
        const auto parent = identity(target);
        // A row is a destination parent; empty space means the model/tag root.
        if (reparent && reparent(ids, parent))
            event->acceptProposedAction();
        else
            event->ignore();
        token_.clear();
    }

  private:
    static constexpr auto format = "application/x-sketchyup-hierarchy";
    Document &doc_;
    mutable Document::SaveStamp stamp_;
    mutable std::uint64_t revision_{};
    mutable QByteArray token_;
    mutable std::set<Id> dragged_;
    bool valid(const QMimeData *data) const {
        return data && !token_.isEmpty() && data->data(format) == token_ && !dragged_.empty() &&
               doc_.revision() == revision_ && doc_.isCurrentSnapshot(stamp_);
    }
};
QAction *control(QWidget *page, QGridLayout *buttons, int position, const QString &id,
                 const QString &text, const QKeySequence &shortcut,
                 const std::function<void()> &fn) {
    auto *action = new QAction(text, page);
    action->setObjectName(id);
    action->setShortcut(shortcut);
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    page->addAction(action);
    QObject::connect(action, &QAction::triggered, page, fn);
    auto *button = new QPushButton(text, page);
    button->setObjectName(id + "Button");
    button->setToolTip(shortcut.isEmpty() ? text : text + " (" + shortcut.toString() + ")");
    QObject::connect(button, &QPushButton::clicked, action, &QAction::trigger);
    buttons->addWidget(button, position / 3, position % 3);
    return action;
}
std::map<Id, QTreeWidgetItem *> items(QTreeWidget *tree) {
    std::map<Id, QTreeWidgetItem *> result;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        result[identity(*it)] = *it;
    return result;
}
// Keep the editor open after a rejected transaction, with its inputs intact.
void form(QWidget *parent, const QString &title, const std::function<void(QFormLayout *)> &fields,
          const std::function<void()> &accept) {
    QDialog dialog(parent);
    dialog.setObjectName("organizationDialog");
    dialog.setWindowTitle(title);
    auto *layout = new QFormLayout(&dialog);
    fields(layout);
    auto *error = new QLabel;
    error->setObjectName("organizationDialogError");
    error->setWordWrap(true);
    layout->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            accept();
            dialog.accept();
        } catch (const std::exception &e) {
            error->setText(e.what());
        }
    });
    dialog.exec();
}
} // namespace
OrganizationPanel::OrganizationPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    setObjectName("organizationPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget;
    tabs_->setObjectName("organizationTabs");
    layout->addWidget(tabs_, 1);
    error_ = new QLabel;
    error_->setObjectName("organizationError");
    error_->setWordWrap(true);
    layout->addWidget(error_);
    auto *entities = new QWidget;
    auto *entityLayout = new QVBoxLayout(entities);
    entityLayout->setContentsMargins(2, 2, 2, 2);
    search_ = new QLineEdit;
    search_->setObjectName("outlinerSearch");
    search_->setPlaceholderText("Find entities…");
    search_->setAccessibleName("Search model hierarchy");
    search_->setClearButtonEnabled(true);
    entityLayout->addWidget(search_);
    auto *outliner = new HierarchyTree(doc_);
    outliner_ = outliner;
    outliner_->setObjectName("outlinerTree");
    outliner_->setAccessibleName("Model hierarchy");
    outliner_->setHeaderLabels({"Entity", "State"});
    outliner_->header()->setStretchLastSection(false);
    outliner_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    outliner_->setExpandsOnDoubleClick(false);
    outliner_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    outliner_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    entityLayout->addWidget(outliner_, 1);
    auto *buttons = new QGridLayout;
    entityLayout->addLayout(buttons);
    control(entities, buttons, 0, "outliner.rename", "Rename", Qt::Key_F2,
            [this] { attempt([&] { rename(false); }); });
    control(entities, buttons, 1, "outliner.move", "Move to", QKeySequence("Ctrl+Shift+M"),
            [this] { attempt([&] { moveDialog(false); }); });
    control(entities, buttons, 2, "outliner.tag", "Tag", QKeySequence("Ctrl+Alt+T"),
            [this] { attempt([&] { assignTag(); }); });
    control(entities, buttons, 3, "outliner.hide", "Hide/show", Qt::Key_Space,
            [this] { attempt([&] { state(false); }); });
    control(entities, buttons, 4, "outliner.lock", "Lock", QKeySequence("Ctrl+Shift+L"),
            [this] { attempt([&] { state(true); }); });
    control(entities, buttons, 5, "outliner.open", "Open", {}, [this] { enter(); });
    auto *close = new QAction(outliner_);
    close->setShortcut(Qt::Key_Escape);
    close->setShortcutContext(Qt::WidgetShortcut);
    outliner_->addAction(close);
    connect(close, &QAction::triggered, this, [this] { attempt([&] { view_.leaveContext(); }); });
    auto *erase = new QAction(outliner_);
    erase->setShortcut(Qt::Key_Delete);
    erase->setShortcutContext(Qt::WidgetShortcut);
    outliner_->addAction(erase);
    connect(erase, &QAction::triggered, this,
            [this] { attempt([&] { view_.deleteSelection(); }); });
    tabs_->addTab(entities, "Outliner");
    auto *tagPage = new QWidget;
    auto *tagLayout = new QVBoxLayout(tagPage);
    tagLayout->setContentsMargins(2, 2, 2, 2);
    auto *tags = new HierarchyTree(doc_);
    tags_ = tags;
    tags_->setObjectName("tagTree");
    tags_->setAccessibleName("Tags and folders");
    tags_->setHeaderLabels({"Tag / folder", "Visible"});
    tags_->header()->setStretchLastSection(false);
    auto *visibility = new QAction(tags_);
    visibility->setObjectName("tags.visibility");
    visibility->setShortcut(Qt::Key_Space);
    visibility->setShortcutContext(Qt::WidgetShortcut);
    tags_->addAction(visibility);
    connect(visibility, &QAction::triggered, this, [this] {
        const auto id = identity(tags_->currentItem());
        if (id)
            attempt([&] {
                view_.organize({QJsonObject{{"command", "tag.edit"},
                                            {"tag", sid(id)},
                                            {"visible", !doc_.tags().at(id)->visible}}});
            });
    });
    tags_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tags_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tagLayout->addWidget(tags_, 1);
    auto *tagButtons = new QGridLayout;
    tagLayout->addLayout(tagButtons);
    control(tagPage, tagButtons, 0, "tags.new", "New tag", QKeySequence("Ctrl+Alt+N"),
            [this] { attempt([&] { createTag(false); }); });
    control(tagPage, tagButtons, 1, "tags.folder", "Folder", QKeySequence("Ctrl+Alt+F"),
            [this] { attempt([&] { createTag(true); }); });
    control(tagPage, tagButtons, 2, "tags.rename", "Rename", Qt::Key_F2,
            [this] { attempt([&] { rename(true); }); });
    control(tagPage, tagButtons, 3, "tags.move", "Move to", QKeySequence("Ctrl+Shift+M"),
            [this] { attempt([&] { moveDialog(true); }); });
    control(tagPage, tagButtons, 4, "tags.assign", "Assign", QKeySequence("Ctrl+Alt+T"),
            [this] { attempt([&] { assignTag(); }); });
    control(tagPage, tagButtons, 5, "tags.delete", "Delete", Qt::Key_Delete,
            [this] { attempt([&] { deleteTag(); }); });
    tabs_->addTab(tagPage, "Tags");
    setFocusProxy(outliner_);
    connect(tabs_, &QTabWidget::currentChanged, this,
            [this](int index) { setFocusProxy(index ? tags_ : outliner_); });
    connect(search_, &QLineEdit::textChanged, this, [this] { filter(); });
    connect(outliner_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (syncing_)
            return;
        SelectionSet entities;
        for (auto *item : outliner_->selectedItems())
            entities.insert({identity(item), SelectionKind::Body, 0});
        // Selection signals fire before Qt finishes updating its current index.
        // Do not rebuild the emitting tree while Qt still owns its row pointers.
        syncing_ = true;
        view_.selectEntities(entities);
        syncing_ = false;
    });
    connect(outliner_, &QTreeWidget::itemActivated, this, [this] { enter(); });
    connect(tags_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (syncing_ || column != 1)
            return;
        const auto id = identity(item);
        const bool visible = item->checkState(1) == Qt::Checked;
        syncing_ = true;
        attempt([&] {
            view_.organize(
                {QJsonObject{{"command", "tag.edit"}, {"tag", sid(id)}, {"visible", visible}}});
        });
        syncing_ = false;
        QTimer::singleShot(0, this, [this] { refresh(); });
    });
    outliner->reparent = [this](const auto &ids, Id parent) {
        return attempt([&] { move(ids, parent, false); });
    };
    tags->reparent = [this](const auto &ids, Id parent) {
        return attempt([&] { move(ids, parent, true); });
    };
}
bool OrganizationPanel::attempt(const std::function<void()> &operation) {
    error_->clear();
    try {
        operation();
        refresh();
        return true;
    } catch (const std::exception &e) {
        error_->setText(e.what());
        refresh();
        return false;
    }
}
std::set<Id> OrganizationPanel::selectedBodies() const {
    std::set<Id> result;
    for (auto *item : outliner_->selectedItems())
        result.insert(identity(item));
    if (result.empty() && outliner_->currentItem())
        result.insert(identity(outliner_->currentItem()));
    if (result.empty())
        throw std::runtime_error("Choose an entity in the Outliner");
    return result;
}
void OrganizationPanel::enter() {
    const auto id = identity(outliner_->currentItem());
    if (!id)
        return;
    syncing_ = true;
    attempt([&] {
        view_.enterContext(id);
        view_.setFocus();
    });
    syncing_ = false;
    QTimer::singleShot(0, this, [this] { refresh(); });
}
void OrganizationPanel::rename(bool tag) {
    const auto ids = tag ? std::set<Id>{identity(tags_->currentItem())} : selectedBodies();
    if (ids.size() != 1 || !*ids.begin())
        throw std::runtime_error("Choose one entity, tag or folder to rename");
    const auto id = *ids.begin();
    QLineEdit *name{};
    form(
        this, "Rename",
        [&](QFormLayout *layout) {
            name = new QLineEdit(QString::fromStdString(tag ? doc_.tags().at(id)->name
                                                            : doc_.bodies().at(id)->name));
            name->setObjectName("organizationName");
            name->selectAll();
            layout->addRow("Name", name);
        },
        [&] {
            const auto old =
                QString::fromStdString(tag ? doc_.tags().at(id)->name : doc_.bodies().at(id)->name);
            if (name->text() != old)
                view_.organize({QJsonObject{{"command", tag ? "tag.edit" : "scene.rename"},
                                            {tag ? "tag" : "body", sid(id)},
                                            {"name", name->text()}}});
        });
}
void OrganizationPanel::move(const std::set<Id> &ids, Id parent, bool tag) {
    QJsonArray commands;
    for (auto id : ids) {
        if (!id)
            throw std::runtime_error("Untagged cannot be moved");
        // Moving both an ancestor and its child should preserve their relation.
        bool covered = false;
        for (auto ancestor = tag ? doc_.tags().at(id)->parent : doc_.bodies().at(id)->parent;
             ancestor;
             ancestor = tag ? doc_.tags().at(ancestor)->parent : doc_.bodies().at(ancestor)->parent)
            covered |= ids.contains(ancestor);
        const auto oldParent = tag ? doc_.tags().at(id)->parent : doc_.bodies().at(id)->parent;
        if (!covered && oldParent != parent)
            commands.append(QJsonObject{{"command", tag ? "tag.edit" : "scene.reparent"},
                                        {tag ? "tag" : "body", sid(id)},
                                        {"parent", sid(parent)}});
    }
    if (!commands.empty())
        view_.organize(commands);
}
void OrganizationPanel::moveDialog(bool tag) {
    const auto ids = tag ? std::set<Id>{identity(tags_->currentItem())} : selectedBodies();
    QComboBox *parent{};
    form(
        this, "Move to parent",
        [&](QFormLayout *layout) {
            parent = new QComboBox;
            parent->setObjectName("organizationParent");
            parent->addItem(tag ? "Top level" : "Model", sid(0));
            if (tag) {
                for (const auto &[id, record] : doc_.tags())
                    if (record->folder && !ids.contains(id))
                        parent->addItem(QString::fromStdString(record->name) + " #" + sid(id),
                                        sid(id));
            } else {
                for (const auto &[id, body] : doc_.bodies())
                    if (body->kind == BodyKind::Group && !ids.contains(id))
                        parent->addItem(QString::fromStdString(body->name) + " #" + sid(id),
                                        sid(id));
            }
            layout->addRow("Parent", parent);
        },
        [&] { move(ids, parent->currentData().toULongLong(), tag); });
}
void OrganizationPanel::state(bool lock) {
    const auto ids = selectedBodies();
    const bool enabled = std::all_of(ids.begin(), ids.end(), [&](Id id) {
        return lock ? doc_.bodies().at(id)->locked : doc_.bodies().at(id)->hidden;
    });
    QJsonArray commands;
    for (auto id : ids)
        if ((lock ? doc_.bodies().at(id)->locked : doc_.bodies().at(id)->hidden) == enabled)
            commands.append(QJsonObject{{"command", "scene.state"},
                                        {"body", sid(id)},
                                        {lock ? "locked" : "hidden", !enabled}});
    if (!commands.empty())
        view_.organize(commands);
}
void OrganizationPanel::createTag(bool folder) {
    auto parent = identity(tags_->currentItem());
    if (parent && !doc_.tags().at(parent)->folder)
        parent = doc_.tags().at(parent)->parent;
    QLineEdit *name{};
    form(
        this, folder ? "New tag folder" : "New tag",
        [&](QFormLayout *layout) {
            name = new QLineEdit;
            name->setObjectName("organizationName");
            layout->addRow("Name", name);
        },
        [&] {
            view_.organize({QJsonObject{{"command", "tag.create"},
                                        {"name", name->text()},
                                        {"parent", sid(parent)},
                                        {"folder", folder}}});
        });
}
void OrganizationPanel::assignTag() {
    const auto ids = selectedBodies();
    QComboBox *tag{};
    form(
        this, "Assign tag",
        [&](QFormLayout *layout) {
            tag = new QComboBox;
            tag->setObjectName("organizationTag");
            tag->addItem("Untagged", sid(0));
            for (const auto &[id, record] : doc_.tags())
                if (!record->folder)
                    tag->addItem(QString::fromStdString(record->name) + " #" + sid(id), sid(id));
            const auto selected = tabs_->currentIndex() == 1 ? identity(tags_->currentItem())
                                                             : doc_.bodies().at(*ids.begin())->tag;
            tag->setCurrentIndex(std::max(0, tag->findData(sid(selected))));
            layout->addRow("Tag", tag);
        },
        [&] {
            QJsonArray commands;
            const auto selected = tag->currentData().toULongLong();
            for (auto id : ids)
                if (doc_.bodies().at(id)->tag != selected)
                    commands.append(QJsonObject{
                        {"command", "tag.assign"}, {"body", sid(id)}, {"tag", sid(selected)}});
            if (!commands.empty())
                view_.organize(commands);
        });
}
void OrganizationPanel::deleteTag() {
    const auto id = identity(tags_->currentItem());
    if (!id)
        throw std::runtime_error("Untagged cannot be deleted");
    view_.organize({QJsonObject{{"command", "tag.delete"}, {"tag", sid(id)}}});
}
void OrganizationPanel::filter() {
    const auto query = search_->text().trimmed();
    const auto rows = items(outliner_);
    std::set<Id> visible;
    for (const auto &[id, item] : rows)
        if (query.isEmpty() || item->text(0).contains(query, Qt::CaseInsensitive)) {
            visible.insert(id);
            for (auto *parent = item->parent(); parent; parent = parent->parent())
                visible.insert(identity(parent));
        }
    for (const auto &[id, item] : rows) {
        item->setHidden(!visible.contains(id));
        if (!query.isEmpty() && visible.contains(id))
            item->setExpanded(true);
    }
}
void OrganizationPanel::refresh() {
    if (syncing_)
        return;
    syncing_ = true;
    const QSignalBlocker block(outliner_), tagBlock(tags_);
    const bool newDocument = !doc_.owns(session_);
    if (newDocument) {
        knownBodies_.clear();
        knownTags_.clear();
        session_ = doc_.saveStamp();
    }
    auto rebuild = [&](QTreeWidget *tree, bool tag) {
        const auto previous = items(tree);
        const auto scroll = newDocument ? 0 : tree->verticalScrollBar()->value();
        std::set<Id> expanded;
        for (const auto &[id, item] : previous)
            if (item->isExpanded())
                expanded.insert(id);
        const auto current = newDocument ? Id{} : identity(tree->currentItem());
        tree->clear();
        std::map<Id, QTreeWidgetItem *> rows;
        std::map<Id, Id> parents;
        std::set<Id> selected;
        for (auto entity : view_.selectionState().entities())
            selected.insert(entity.body);
        auto &known = tag ? knownTags_ : knownBodies_;
        auto add = [&](Id id, Id parent, const QString &name, const QString &state) {
            auto *item = new QTreeWidgetItem(QStringList{name, state});
            item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(id));
            item->setToolTip(0, name);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            rows[id] = item;
            parents[id] = parent;
            return item;
        };
        if (tag) {
            auto *untagged = add(0, 0, "Untagged", "Always");
            untagged->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            for (const auto &[id, record] : doc_.tags()) {
                auto *item =
                    add(id, record->parent,
                        QString::fromStdString(record->name) + (record->folder ? " /" : ""), {});
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(1, record->visible ? Qt::Checked : Qt::Unchecked);
                item->setToolTip(1, tagVisible(doc_.tags(), id) ? "Visible"
                                                                : "Hidden by tag or folder");
            }
        } else {
            for (const auto &[id, body] : doc_.bodies()) {
                const auto &selection = view_.selectionState();
                QString state;
                if (selection.hidden(doc_, {id, SelectionKind::Body, 0}))
                    state += "H";
                if (selection.locked(doc_, id))
                    state += "L";
                if (selection.context() == id)
                    state += " •";
                const auto kind = doc_.instances().contains(id)   ? "Component"
                                  : body->kind == BodyKind::Group ? "Group"
                                                                  : "Geometry";
                auto *item = add(id, body->parent,
                                 QString::fromStdString(body->name) + " #" + sid(id), state);
                item->setToolTip(
                    0, QString(kind) + " · " +
                           (body->tag ? QString::fromStdString(doc_.tags().at(body->tag)->name)
                                      : "Untagged") +
                           "\nEnter/double-click to edit this context. H: hidden, L: locked.");
            }
        }
        for (const auto &[id, item] : rows) {
            if (parents[id])
                rows.at(parents[id])->addChild(item);
            else
                tree->addTopLevelItem(item);
        }
        for (const auto &[id, item] : rows) {
            item->setExpanded(expanded.contains(id) || !known.contains(id));
            if (!tag && selected.contains(id))
                item->setSelected(true);
            if (id == current)
                tree->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
        }
        if (!tag && !selected.empty()) {
            for (auto id : selected)
                if (rows.contains(id)) {
                    if (!tree->currentItem())
                        tree->setCurrentItem(rows.at(id), 0, QItemSelectionModel::NoUpdate);
                    for (auto *p = rows.at(id)->parent(); p; p = p->parent())
                        p->setExpanded(true);
                }
        }
        tree->verticalScrollBar()->setValue(scroll);
        known.clear();
        for (const auto &[id, item] : rows)
            known.insert(id);
    };
    rebuild(outliner_, false);
    rebuild(tags_, true);
    filter();
    syncing_ = false;
}
} // namespace sketchy
