#pragma once
#include "app/theme.hpp"
#include "core/model.hpp"
#include "io/drawing_items.hpp"
#include <QJsonObject>
#include <QWidget>
#include <optional>
#include <vector>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
namespace sketchy {
// Entity info for a selected page item (R091.g, ADR 0164 §4): every value is a labelled
// field, so a screen-reader user can read and edit what the canvas shows. Each edit is one
// history entry; the window turns it into a drawing_item.update (or, for the sheet number,
// drawing_sheet.update).
// - Text: text, font, size, alignment and frame; Label: text, font, size, leader and what it
//   points to; Dimension: value, view, orientation, offset, prefix, font and size;
// - Line and Rectangle: geometry, stroke and line style;
// - Title block: project, drawing, date and revision, the sheet number, and the derived page
//   and scale;
// - the state, and Reattach for a broken dimension or label.
class SheetItemInfo : public QWidget {
    Q_OBJECT
  public:
    explicit SheetItemInfo(const Document &doc, QWidget *parent = nullptr);
    // Shows an item; zero hides every row. The evaluation gives the value and state.
    void refresh(Id item, const std::optional<DrawingItemEvaluation> &evaluation,
                 const QString &viewName);
    Id item() const { return item_; }
    void setTheme(const ThemeColors &colors);
    QString title() const; // "Entity info · Dimension"

  signals:
    void changeItem(qulonglong item, const QString &kind, const QJsonObject &data,
                    const QString &label);
    void changeSheetNumber(qulonglong sheet, const QString &number);
    void reattachRequested(qulonglong item);

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    const Document &doc_;
    Id item_{};
    bool syncing_{};
    ThemeColors colors_{themeColors(false)};
    QGridLayout *grid_{};
    struct Row {
        QLabel *caption{};
        QWidget *field{};
    };
    std::vector<Row> rows_;
    QPlainTextEdit *text_{};
    QLineEdit *prefix_{}, *project_{}, *drawing_{}, *date_{}, *revision_{}, *number_{};
    QLabel *value_{}, *view_{}, *target_{}, *geometry_{}, *page_{}, *scale_{}, *state_{},
        *stateIcon_{};
    QComboBox *orientation_{}, *font_{}, *align_{}, *dash_{};
    QDoubleSpinBox *offset_{}, *size_{}, *stroke_{};
    QCheckBox *leader_{};
    QPushButton *reattach_{};
    QWidget *stateRow_{};
    QWidget *addRow(const QString &caption, QWidget *field);
    void show(QWidget *field, bool visible);
    const DrawingItemRecord *record() const;
    void emitChange(const QJsonObject &data, const QString &label);
    void commitText();
    void commitFont();
    void commitTitle(QLineEdit *edit, const char *field);
};
} // namespace sketchy
