#pragma once
#include "app/viewport.hpp"
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
namespace sketchy {
class MaterialsPanel : public QWidget {
  public:
    MaterialsPanel(Document &doc, Viewport &view, QWidget *parent = nullptr);
    void refresh();

  protected:
    void showEvent(QShowEvent *) override;

  private:
    Document &doc_;
    Viewport &view_;
    QListWidget *swatches_{};
    QComboBox *side_{};
    QLabel *details_{}, *selection_{}, *error_{};
    Document::SaveStamp stamp_;
    MaterialRecords materials_;
    AssetRecords assets_;
    bool syncing_{};
    void attempt(const std::function<void()> &operation);
    void edit(bool create);
    void attach(bool replace);
    void detach();
    void remove();
};
} // namespace sketchy
