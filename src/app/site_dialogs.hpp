#pragma once
// R095.e2 (ADR 0168 §4 and "Decisions > Interaction design", Site): File > Site settings… and
// its Re-anchor… and Set origin… dialogs.
//   Site settings   the origin (E, N, Z) read-only, the CRS label (read-only: the core keeps
//                   it from site.set or the first import, and site.reanchor keeps it) and the
//                   provenance (source file, SHA-256); Re-anchor…. Without an origin it says
//                   how one is established (the first terrain import) and offers Set origin….
//   Re-anchor       new E, N, Z for the model origin; runs site.reanchor as ONE edit that moves
//                   geometry so site coordinates are unchanged. Refused clearly (banner, the
//                   action disabled) when a locked top-level body or the coordinate limit
//                   blocks it, before anything is applied.
//   Set origin      E, N, Z and an optional CRS label; runs site.set (geometry does not move).
// While Site settings is open the viewport shows the site origin marker.
#include "app/settled_dialog.hpp"
#include "core/model.hpp"
#include <QDialog>
#include <QJsonObject>
#include <QTimer>
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
namespace sketchy {
class Viewport;

class SiteOriginDialog final : public Settled<QDialog> {
  public:
    enum class Mode { Reanchor, Set };
    SiteOriginDialog(Mode mode, Document &doc, Viewport &view, QWidget *parent);
    // The site.reanchor or site.set command the fields stand for.
    QJsonObject command() const;
    // Why the action is unavailable (a locked body, the coordinate limit, no change), or empty.
    QString problem() const { return problem_; }
    bool canApply() const;
    QDoubleSpinBox *easting() const { return easting_; }
    QDoubleSpinBox *northing() const { return northing_; }
    QDoubleSpinBox *elevation() const { return elevation_; }

  private:
    void check();
    void apply();
    Mode mode_;
    Document &doc_;
    Viewport &view_;
    QString problem_;
    QTimer recheck_;
    QDoubleSpinBox *easting_{}, *northing_{}, *elevation_{};
    QLineEdit *crs_{};
    QLabel *delta_{}, *bannerText_{};
    QFrame *banner_{};
    QPushButton *apply_{};
};

class SiteSettingsDialog final : public Settled<QDialog> {
  public:
    SiteSettingsDialog(Document &doc, Viewport &view, QWidget *parent);
    ~SiteSettingsDialog() override;
    // Rereads the document's site origin.
    void refresh();

  private:
    void open(SiteOriginDialog::Mode mode);
    Document &doc_;
    Viewport &view_;
    QFrame *none_{}, *origin_{};
    QLabel *easting_{}, *northing_{}, *elevation_{}, *crs_{}, *source_{}, *sha256_{};
    QPushButton *reanchor_{}, *set_{}, *close_{};
};
} // namespace sketchy
