#include "app/terrain_role_table.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace sketchy {
namespace {
// Enter in the table moves to the Role combo (it never reaches the dialog's default button).
class EnterChooses final : public QObject {
  public:
    EnterChooses(QObject *parent, std::function<void()> choose)
        : QObject(parent), choose_(std::move(choose)) {}
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                choose_();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    std::function<void()> choose_;
};
} // namespace

TerrainRoleTable::TerrainRoleTable(std::vector<Choice> choices, const Names &names, QWidget *parent)
    : QWidget(parent), choices_(std::move(choices)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    table_ = new QTableWidget(0, 3);
    table_->setObjectName(names.table);
    table_->setHorizontalHeaderLabels({"Name", "Contents", "Role"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setTabKeyNavigation(false);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setTextElideMode(Qt::ElideRight);
    table_->setWordWrap(false);
    table_->setMinimumHeight(110);
    table_->setMaximumHeight(200);
    layout->addWidget(table_);
    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    auto *caption = new QLabel("Role");
    caption->setObjectName(names.caption);
    row->addWidget(caption);
    combo_ = new QComboBox;
    combo_->setObjectName(names.combo);
    combo_->setAccessibleName("Role for the selected rows");
    for (const auto &choice : choices_)
        combo_->addItem(choice.label, choice.code);
    caption->setBuddy(combo_);
    row->addWidget(combo_, 1);
    layout->addLayout(row);
    setFocusProxy(table_);

    connect(combo_, &QComboBox::activated, this, [this] {
        const auto code = combo_->currentData().toString();
        QList<int> chosen;
        for (const auto &index : table_->selectionModel()->selectedRows())
            chosen << index.row();
        if (chosen.isEmpty() && table_->currentRow() >= 0)
            chosen << table_->currentRow();
        for (const int r : chosen)
            if (r >= 0 && std::size_t(r) < rows_.size()) {
                if (code == "auto")
                    roles_.erase(rows_[std::size_t(r)].key);
                else
                    roles_[rows_[std::size_t(r)].key] = code;
            }
        refreshRows();
        if (changed)
            changed();
    });
    connect(table_, &QTableWidget::currentCellChanged, this, [this] { refreshCombo(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { refreshCombo(); });
    table_->installEventFilter(new EnterChooses(this, [this] { combo_->setFocus(); }));
}
void TerrainRoleTable::setHeadings(const QString &column, const QString &accessibleName) {
    table_->setHorizontalHeaderLabels({column, "Contents", "Role"});
    table_->setAccessibleName(accessibleName);
}
QString TerrainRoleTable::label(const QString &code) const {
    for (const auto &choice : choices_)
        if (choice.code == code)
            return choice.label;
    return code;
}
void TerrainRoleTable::setRows(std::vector<Row> rows) {
    const bool sameKeys = std::equal(rows.begin(), rows.end(), rows_.begin(), rows_.end(),
                                     [](const Row &a, const Row &b) { return a.key == b.key; });
    rows_ = std::move(rows);
    if (!sameKeys)
        table_->setRowCount(int(rows_.size()));
    refreshRows();
    if (table_->currentRow() < 0 && table_->rowCount() > 0)
        table_->setCurrentCell(0, 0);
    combo_->setEnabled(table_->rowCount() > 0);
    refreshCombo();
}
void TerrainRoleTable::clearRoles() {
    roles_.clear();
    refreshRows();
    refreshCombo();
}
QString TerrainRoleTable::role(const QString &key) const {
    const auto found = roles_.find(key);
    return found == roles_.end() ? QString("auto") : found->second;
}
void TerrainRoleTable::setRole(const QString &key, const QString &code) {
    if (code == "auto")
        roles_.erase(key);
    else
        roles_[key] = code;
    refreshRows();
    refreshCombo();
}
std::map<QString, QString> TerrainRoleTable::roles() const {
    std::map<QString, QString> current;
    for (const auto &row : rows_)
        if (const auto found = roles_.find(row.key); found != roles_.end())
            current[row.key] = found->second;
    return current;
}
QString TerrainRoleTable::currentKey() const {
    const auto r = table_->currentRow();
    return r >= 0 && std::size_t(r) < rows_.size() ? rows_[std::size_t(r)].key : QString();
}
void TerrainRoleTable::refreshRows() {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto &row = rows_[i];
        const auto found = roles_.find(row.key);
        const auto shown =
            found == roles_.end() ? label("auto") + ": " + row.automatic : label(found->second);
        auto set = [&](int c, const QString &text) {
            auto *item = table_->item(int(i), c);
            if (!item)
                table_->setItem(int(i), c, item = new QTableWidgetItem);
            item->setText(text);
        };
        set(0, row.label);
        set(1, row.contents);
        set(2, shown);
    }
}
void TerrainRoleTable::refreshCombo() {
    const auto r = table_->currentRow();
    if (r < 0 || std::size_t(r) >= rows_.size())
        return;
    const auto &row = rows_[std::size_t(r)];
    const QSignalBlocker block(combo_);
    combo_->setCurrentIndex(combo_->findData(role(row.key)));
    combo_->setAccessibleName("Role for " + row.spoken);
}
} // namespace sketchy
