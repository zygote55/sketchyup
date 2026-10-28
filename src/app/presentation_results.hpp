#pragma once
// R099.d3 (UX_DESIGN §17.7, §17.10): the results sheet of a presentation batch. It opens from the
// Jobs row or the status chip, and never by itself, so it cannot take focus from a batch in
// progress. A video is listed only once it is written; the stills grid labels every cell in text.
#include "app/presentation_batches.hpp"
#include <QDialog>

class QLabel;

namespace sketchy {

class PresentationResultsDialog : public QDialog {
    Q_OBJECT
  public:
    PresentationResultsDialog(PresentationBatches &batches, const QString &id,
                              QWidget *parent = nullptr);

  private:
    PresentationBatches &batches_;
    QString id_;
    QLabel *banner_{};
};

} // namespace sketchy
