#pragma once
#include "app/presentation_batches.hpp"
#include "integrations/render_queue.hpp"
#include <QDialog>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace sketchy {

// The Jobs list (UX_DESIGN §9, §17.6): the render jobs and the presentation batches. A batch is one
// parent row with one child row per preset. Renders read RenderQueue; batches read PresentationBatches.
class RenderJobsDialog : public QDialog {
    Q_OBJECT
  public:
    RenderJobsDialog(RenderQueue &queue, std::function<QString(const StoredRenderJob &)> provenance,
                     std::function<void(const QString &)> open, QWidget *parent,
                     PresentationBatches *batches = nullptr,
                     std::function<QString(const StoredPresentationJob &)> batchProvenance = {});
    void refresh();
    // Makes a batch the current row, so the Jobs list opens with a batch the dialog just started.
    void selectJob(const QString &id);

  signals:
    // The results sheet of a batch was asked for (Show results…).
    void batchResults(const QString &id);

  private:
    RenderQueue &queue_;
    std::function<QString(const StoredRenderJob &)> provenance_;
    std::function<void(const QString &)> open_;
    PresentationBatches *batches_;
    std::function<QString(const StoredPresentationJob &)> batchProvenance_;
    QTreeWidget *list_;
    QPlainTextEdit *details_;
    QLabel *status_;
    QPushButton *cancel_, *retry_, *openButton_, *remove_, *clear_, *folder_;
    QString selected() const;
    QString selectedKind() const;
    void selection();
    void selectionBatch(const QString &id);
    void selectionRender(const QString &id);
    void perform(const std::function<void()> &action);
    void addBatch(const StoredPresentationJob &job);
};

} // namespace sketchy
