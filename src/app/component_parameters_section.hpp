#pragma once
#include "app/collapsible_section.hpp"
#include "app/viewport.hpp"
#include <QJsonObject>
#include <QPointer>
#include <vector>
class QVBoxLayout;
class QTimer;
namespace sketchy {
class ParameterValueField;
// Entity info's "Parameters · this placement" (ADR 0166 §1): one row per parameter of the
// selected top-level placement, from component_parameters.values and .describe. Values are
// typed in the Measurements box syntax, checked as you type through
// component_parameters.evaluate and committed on Enter or focus loss as one labelled History
// entry each ("Change Width", "Reset Width", "Reset all parameters"). A nested placement shows
// its values read-only. With several placements selected the section asks for one.
//
// It also answers the Measurements box while the Select tool and one parametric placement are
// active (§1.3): the box names the parameter last focused here, else the first length, angle,
// number or count input, and re-entry before another edit amends the entry.
class ComponentParametersSection : public CollapsibleSection {
    Q_OBJECT
  public:
    ComponentParametersSection(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    // Rereads the selection and the document; cached by document, revision, placement and
    // units. Runs whether or not the Info tab is shown, for the Measurements box.
    void refresh();
    // The placement shown, or 0.
    Id placement() const { return placement_; }
    bool nested() const { return nested_; }
    const std::vector<ParameterValueField *> &fields() const { return fields_; }
    ParameterValueField *field(const QString &name) const;
    QPushButton *resetAllButton() const { return resetAll_; }
    QPushButton *editDefinitionButton() const { return editDefinition_; }
    // The Measurements box routing: the parameter's label and stored display, empty when the
    // box is unchanged. `measurements` sets that parameter; on failure `error` holds the
    // sentence.
    QString measurementLabel() const;
    QString measurementValue() const;
    bool measurements(const QString &text, QString &error);
    // The status bar text while focus is in the section (ADR 0166 §1.2).
    static QString hint();
    // Stops every check and commit; the window calls it before its document is destroyed.
    void detach();

  protected:
    void resizeEvent(QResizeEvent *event) override;

  signals:
    // The parameter the Measurements box names changed.
    void measurementChanged();

  private:
    Document &doc_;
    Viewport &view_;
    QWidget *body_{}, *rows_{};
    QVBoxLayout *rowsLayout_{};
    QLabel *message_{}, *note_{};
    QPushButton *resetAll_{}, *editDefinition_{};
    QBoxLayout *buttons_{};
    QTimer *validation_{};
    QPointer<ParameterValueField> pending_;
    std::vector<ParameterValueField *> fields_;
    QJsonObject values_;
    Id placement_{}, definition_{};
    bool nested_{}, busy_{}, detached_{};
    QString parent_, focused_, signature_, cacheKey_;
    // The Measurements entry that re-entry amends (ADR 0166 §1.3).
    QString amendKey_;
    Document::AmendStamp amendStamp_;
    void rebuild(const QJsonArray &parameters);
    void validate();
    QJsonObject overrides() const;
    QJsonObject evaluate(const QString &name, const QJsonValue &value) const;
    // Checks and commits one value; returns the refusal's sentence (and the core message in
    // `detail`), empty on success.
    QString apply(ParameterValueField *field, const QJsonValue &value, const QString &typed,
                  bool amend, QString &detail);
    void commit(ParameterValueField *field);
    void reset(ParameterValueField *field);
    void resetAll();
    void editDefinition();
    // Publishes one labelled batch, amending the previous entry when `amend` is set.
    void publish(const QJsonObject &command, const QString &label, bool amend);
    ParameterValueField *routed() const;
};
} // namespace sketchy
