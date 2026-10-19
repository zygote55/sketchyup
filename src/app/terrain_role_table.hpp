#pragma once
// R095.h (ADR 0168, "Implementation notes (R095.h)"): the source → role table, factored out of
// the R095.d import dialog (layer or description-code roles) so Create terrain from selection's
// roles step (tag roles) shares it. A three-column table (name, contents, role) with Extended
// row selection and one Role combo below it that sets the role of the selected rows: one Tab
// stop for many rows, and the table follows the list keyboard pattern (Enter in the table
// moves to the combo). A row without a chosen role shows "Automatic: <inferred role>".
#include <QString>
#include <QWidget>
#include <functional>
#include <map>
#include <vector>
class QComboBox;
class QLabel;
class QTableWidget;
namespace sketchy {
class TerrainRoleTable final : public QWidget {
  public:
    struct Choice {
        QString code, label; // "auto" is the automatic choice
    };
    struct Row {
        QString key;       // the identity the role is kept under
        QString label;     // the first column
        QString contents;  // the second column
        QString automatic; // the inferred role, shown as "Automatic: <automatic>"
        QString spoken;    // the combo's accessible name is "Role for <spoken>"
    };
    struct Names {
        QString table, combo, caption; // object names (tests and style sheets find them)
    };
    TerrainRoleTable(std::vector<Choice> choices, const Names &names, QWidget *parent = nullptr);

    // The first column's heading and the table's accessible name.
    void setHeadings(const QString &column, const QString &accessibleName);
    // Replaces the rows; roles chosen for keys that stay are kept.
    void setRows(std::vector<Row> rows);
    // Forgets every chosen role (the rows stay, as Automatic).
    void clearRoles();
    // The chosen role of `key` ("auto" when none was chosen).
    QString role(const QString &key) const;
    // Sets a role, as choosing it for that row does (without calling `changed`).
    void setRole(const QString &key, const QString &code);
    // The chosen (non-automatic) roles of the current rows.
    std::map<QString, QString> roles() const;
    // The key of the current row (empty without rows).
    QString currentKey() const;

    QTableWidget *table() const { return table_; }
    QComboBox *combo() const { return combo_; }
    // Called after the user chooses a role.
    std::function<void()> changed;

  private:
    void refreshRows();
    void refreshCombo();
    QString label(const QString &code) const;

    std::vector<Choice> choices_;
    std::vector<Row> rows_;
    std::map<QString, QString> roles_;
    QTableWidget *table_{};
    QComboBox *combo_{};
};
} // namespace sketchy
