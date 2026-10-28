#pragma once
#include "app/collapsible_section.hpp"
#include "app/parameter_messages.hpp"
#include "app/viewport.hpp"
#include "automation/catalog_commands.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
class QComboBox;
class QLineEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
namespace sketchy {
class FormulaField;
// ADR 0166 §3 (R094.d): what the definition sections share. Queries run in process through
// inspectDocument; commands are one labelled batch each (one History entry per user action);
// dry runs are previewBatch, which never changes the revision, History or saved state.
namespace authoring {
// The definition whose context is open: the component scope placement and its definition.
struct Scope {
    Id placement{}, definition{};
    explicit operator bool() const { return placement != 0; }
};
Scope scopeOf(const Document &doc, const Viewport &view);
// A shared query's data object; throws InspectionError.
QJsonObject query(const Document &doc, const QString &name, QJsonObject fields);
// {code, message, details} of a refusal (a plain exception has only a message).
QJsonObject failure(const std::exception &error);
// The refusal of a dry run of `commands`, or an empty object when they would commit. `opened` is
// the catalogs the commands name by key (the catalog dialog's), if any.
QJsonObject dryRun(const Document &doc, const QJsonArray &commands,
                   const OpenedCatalogs *opened = nullptr);
// Commits `commands` as one entry labelled `label`, then refreshes the viewport and reports
// the change. Throws on refusal.
QJsonObject publish(Document &doc, Viewport &view, const QJsonArray &commands, const QString &label,
                    const OpenedCatalogs *opened = nullptr);
// "Add parameter Width": the label cut to 64 UTF-8 bytes with "…" (ADR 0166 §1.4).
QString historyLabel(const QString &verb, const QString &label);
// The definition member a scene record of the scope placement shows, or 0.
Id memberOf(const Document &doc, Id placement, Id scene);
// The scene record that shows a definition member in the scope placement, or 0.
Id sceneOf(const Document &doc, Id placement, Id member);
} // namespace authoring

// "Parameters · definition" (ADR 0166 §3.2): the open definition's parameters in a tree (Name
// with the label over the name, Type with bounds or "derived", Default or formula) and the
// inline editor of the selected row (Label, Name, Default or Formula, Bounds, Step, Options,
// Used by). "+ Parameter" opens the editor in create mode, where Type and Mode are editable.
// Keys on the tree: F2 renames (focuses Name), Delete removes, Alt+Up and Alt+Down move, Enter
// moves into the editor. In the editor Enter commits and Esc cancels. Each commit is one
// labelled entry: "Add parameter Width", "Edit parameter Width", "Rename parameter width",
// "Move parameter Width", "Remove parameter Width".
class ComponentDefinitionSection : public CollapsibleSection {
    Q_OBJECT
  public:
    ComponentDefinitionSection(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    // Rereads the open context and its definition; cached by document, revision and scope.
    void refresh();
    // Stops every check and commit; the window calls it before its document is destroyed.
    void detach();
    authoring::Scope scope() const { return scope_; }
    const QJsonObject &description() const { return description_; }
    QTreeWidget *tree() const { return tree_; }
    QPushButton *addButton() const { return add_; }
    QWidget *editor() const { return editor_; }
    QLineEdit *labelEdit() const { return label_; }
    QLineEdit *nameEdit() const { return name_; }
    QComboBox *typeChoice() const { return type_; }
    QComboBox *modeChoice() const { return mode_; }
    FormulaField *expressionField() const { return expression_; }
    QLineEdit *minimumEdit() const { return min_; }
    QLineEdit *maximumEdit() const { return max_; }
    QLineEdit *stepEdit() const { return step_; }
    QLineEdit *optionsEdit() const { return options_; }
    QLabel *usedByLabel() const { return usedBy_; }
    QLabel *editorError() const { return error_; }
    QPushButton *editorAction() const { return action_; }
    // The parameter the editor shows ("" in create mode or when closed).
    QString current() const { return current_; }
    bool creating() const { return creating_; }
    // + Parameter: the editor in create mode, focus on Label.
    void beginCreate();
    // Selects a parameter's row and shows it in the editor.
    void select(const QString &name);
    // Commits the editor (Enter); returns the refusal's sentence, empty on success.
    QString commit();
    // Closes create mode or restores the edited fields (Esc).
    void cancel();
    // Removes the selected parameter (Delete); returns the refusal's sentence.
    QString removeCurrent();
    // Moves the selected parameter up (-1) or down (+1) (Alt+Up, Alt+Down).
    QString move(int delta);
    // The parameter's label by name (for sentences).
    QString labelOf(const QString &name) const;

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    Document &doc_;
    Viewport &view_;
    authoring::Scope scope_;
    QJsonObject description_;
    QString cacheKey_, signature_, current_;
    bool creating_{}, detached_{}, busy_{}, syncing_{};
    QPushButton *add_{};
    QTreeWidget *tree_{};
    QWidget *editor_{};
    QLabel *editorTitle_{}, *usedBy_{}, *error_{}, *note_{}, *empty_{};
    QLineEdit *label_{}, *name_{}, *min_{}, *max_{}, *step_{}, *options_{};
    QComboBox *type_{}, *mode_{};
    FormulaField *expression_{};
    QWidget *typeRow_{}, *modeRow_{}, *boundsRow_{}, *stepRow_{}, *optionsRow_{}, *usedRow_{};
    QLabel *expressionCaption_{};
    void rebuild();
    // Forgets the editor's unsaved edits, so it shows the stored values again.
    void resetEdits();
    void showEditor();
    void syncEditorRows();
    QJsonObject parameterNamed(const QString &name) const;
    QString editorType() const;
    bool editorDerived() const;
    // The command the editor would commit (define, or update and rename), or a sentence
    // saying why the form cannot be read.
    QJsonArray editorCommands(QString &label, QString &problem) const;
    QJsonObject checkExpression(const QString &text) const;
    QJsonObject dryRunExpression(const QString &text) const;
    ParameterMessage message(const QJsonObject &error) const;
    QString sentence(const QJsonObject &error) const;
    QString present(const QJsonObject &error);
    // R094.e: the editor message's action ("Select #1") and the message it acts on.
    QPushButton *action_{};
    ParameterMessage message_;
};
} // namespace sketchy
