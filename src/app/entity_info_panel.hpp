#pragma once
#include "app/viewport.hpp"
#include "core/entity_measure.hpp"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
namespace sketchy {
class EntityInfoPanel : public QWidget {
  public:
    EntityInfoPanel(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    void refresh();

  protected:
    void showEvent(QShowEvent *event) override;

  private:
    Document &doc_;
    Viewport &view_;
    QComboBox *frame_{};
    QLabel *name_{}, *type_{}, *position_{}, *dimensions_{}, *length_{}, *area_{}, *volume_{};
    QLabel *tag_{}, *color_{}, *properties_{}, *error_{};
    QPushButton *edit_{}, *problems_{};
    QAction *editAction_{};
    std::optional<SelectedEntity> entity_;
    std::optional<EntityMeasures> measured_;
    Document::SaveStamp stamp_;
    void edit();
    void inspectProblems();
};
} // namespace sketchy
