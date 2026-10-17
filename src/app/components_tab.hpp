#pragma once
#include "app/theme.hpp"
#include "app/viewport.hpp"
#include <QJsonObject>
#include <QPointer>
#include <QWidget>
#include <memory>
#include <vector>
class QComboBox;
class QLabel;
class QListView;
class QPushButton;
class QStandardItemModel;
class QTimer;
class QVBoxLayout;
namespace sketchy {
class CollapsibleSection;
class ParameterValueField;
// The Tray's Components tab (ADR 0166 §6.3, R094.f), the design's Assemblies artboard:
// - "Components": Show (Configurable assemblies | All components) and the cards, a list view
//   in icon mode with one Tab stop, arrows to move and two columns. Each card reads its name
//   and "Cuts opening · 4 parameters" (and "In this model" when the model already has the
//   assembly). Activating a card (Enter, click) starts placing it.
// - "<Name> · values for this placement": one ParameterValueField per input parameter of the
//   bundle's definition, checked through component_parameters.evaluate on the bundle's
//   document; Use defaults resets them and Cancel (Esc) ends placement. Values equal to the
//   defaults are not stored as overrides.
// - "About these assemblies": the note that Dynamic Components are not imported.
// The configurable assemblies are the bundles in share/sketchyup/assemblies beside the
// executable; All components adds the user library folder's component bundles.
class ComponentsTab : public QWidget {
    Q_OBJECT
  public:
    ComponentsTab(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    // applicationDirPath()/../share/sketchyup/assemblies (the installed package), else
    // applicationDirPath()/share/sketchyup/assemblies (the build tree).
    static QString assemblyFolder();
    // Rescans the folders when the filter changes or on first show, and updates the cards'
    // "In this model" from the document.
    void refresh();
    void setTheme(const ThemeColors &colors);
    QComboBox *filter() const { return filter_; }
    QListView *cards() const { return cards_; }
    QWidget *valuesSection() const { return values_; }
    QLabel *valuesTitle() const { return valuesTitle_; }
    QPushButton *useDefaultsButton() const { return useDefaults_; }
    QPushButton *cancelButton() const { return cancel_; }
    const std::vector<ParameterValueField *> &fields() const { return fields_; }
    ParameterValueField *field(const QString &name) const;
    // The override literals the form holds: only values that differ from the defaults.
    QJsonObject overrides() const;
    // Ends placement and hides the form (Cancel, Esc).
    void cancelPlacement();
    // Stops every callback; the window calls it before its document is destroyed.
    void detach();

  signals:
    // A placement was made; the window shows its parameters in Entity info.
    void placed(qulonglong placement);

  protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    struct Card {
        QString path, name;
        std::shared_ptr<const ComponentBundle> bundle; // loaded when the card is activated
        bool assembly{}, cuts{};
        int parameters{};
        std::optional<ComponentAssembly> identity;
    };
    Document &doc_;
    Viewport &view_;
    ThemeColors colors_;
    CollapsibleSection *components_{};
    QComboBox *filter_{};
    QListView *cards_{};
    QStandardItemModel *model_{};
    QLabel *message_{}, *valuesTitle_{}, *valuesNote_{};
    QWidget *values_{}, *rows_{};
    QVBoxLayout *rowsLayout_{};
    QPushButton *useDefaults_{}, *cancel_{};
    QTimer *validation_{};
    QPointer<ParameterValueField> pending_;
    std::vector<ParameterValueField *> fields_;
    std::vector<Card> entries_;
    QString notice_;
    int scanned_{-1}, active_{-1};
    bool detached_{}, starting_{};
    QJsonObject validate(ParameterValueField *field);
    void scan();
    void showCards();
    void layoutCards();
    void start(int card);
    void buildForm(const Card &card);
    void valuesChanged();
    void useDefaults();
    void ended(bool placed, qulonglong placement);
};
} // namespace sketchy
