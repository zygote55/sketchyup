#pragma once
#include "app/viewport.hpp"
#include "io/style_bundle.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

namespace sketchy {

// The Styles tab (R062, UX_DESIGN §18.1 to §18.3): the display controls, the Edge stroke section
// (each control one labelled `document.style` edit) and the Style library section.
class StylesPanel : public QWidget {
  public:
    StylesPanel(Document &doc, Viewport &view, QWidget *parent = nullptr);
    void refresh();

  protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    Document &doc_;
    Viewport &view_;
    QComboBox *mode_{};
    QLabel *error_{};
    std::vector<std::pair<QCheckBox *, bool ModelStyle::*>> flags_;
    bool syncing_{};

    // Edge stroke (UX_DESIGN §18.3)
    QCheckBox *stroke_{};
    QLabel *strokeState_{}, *strokeError_{}, *strokeSchema_{}, *textureNote_{};
    QLineEdit *width_{}, *opacity_{};
    QComboBox *dash_{}, *texture_{};

    // Style library (UX_DESIGN §18.1, §18.2)
    QLineEdit *folder_{}, *search_{};
    QLabel *libraryCount_{}, *libraryNotice_{}, *libraryError_{};
    QListWidget *library_{};
    QPushButton *apply_{}, *saveLibrary_{};
    StyleLibrary scan_;
    QString scannedPath_;
    QDateTime scannedModified_;
    bool scanned_{};

    void change(const std::function<void(ModelStyle &)> &operation);
    void labelled(const StyleEdit &edit);
    void customize();
    void editStroke(const std::function<StyleEdit(const ModelStyle &)> &operation);
    void setWidth(const QString &text);
    void setOpacity(const QString &text);
    void refreshStroke();
    void setTextureChoices();

    void scanLibrary();
    void fillLibrary();
    void updateLibraryButtons();
    const StyleLibraryEntry *selectedEntry() const;
    bool applicable(const StyleLibraryEntry *entry) const;
    void chooseFolder();
    void applySelected();
    void saveToLibrary();
    void showLibraryMenu(const QPoint &point);
};

} // namespace sketchy
