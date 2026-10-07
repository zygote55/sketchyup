#pragma once
#include "integrations/render_queue.hpp"
#include <QDialog>
#include <functional>
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
namespace sketchy {
class RenderJobsDialog : public QDialog {
  public:
    RenderJobsDialog(RenderQueue &queue, std::function<QString(const StoredRenderJob &)> provenance,
                     std::function<void(const QString &)> open, QWidget *parent);
    void refresh();

  private:
    RenderQueue &queue_;
    std::function<QString(const StoredRenderJob &)> provenance_;
    std::function<void(const QString &)> open_;
    QTreeWidget *list_;
    QPlainTextEdit *details_;
    QLabel *status_;
    QPushButton *cancel_, *retry_, *openButton_, *remove_, *clear_;
    QString selected() const;
    void selection();
    void perform(const std::function<void()> &action);
};
} // namespace sketchy
