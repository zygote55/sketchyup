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
// R107.a (ADR 0181 §2.1, §2.6): Site settings also shows the optional geographic location of the
// origin (WGS84 latitude and longitude, zone, datum, vertical reference and the grid convergence,
// read only), with Set location… and Clear. Set location… runs site.set_geographic; the sun
// study's warning shows here when its location differs from the site's by more than 1 m.
// While Site settings is open the viewport shows the site origin marker.
#include "app/settled_dialog.hpp"
#include "core/model.hpp"
#include <QDialog>
#include <QJsonObject>
#include <QTimer>
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
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

// R107.a: the site's geographic location (ADR 0181 §2.1). The origin keeps its projected position;
// this location must be its UTM position in the chosen zone, to within 1 m.
class SiteGeographicDialog final : public Settled<QDialog> {
  public:
    SiteGeographicDialog(Document &doc, Viewport &view, QWidget *parent);
    // The site.set_geographic command the fields stand for.
    QJsonObject command() const;
    bool canApply() const { return problem_.isEmpty(); }

  private:
    void check();
    void apply();
    void useOrigin();
    Document &doc_;
    Viewport &view_;
    QString problem_;
    QTimer recheck_;
    QDoubleSpinBox *latitude_{}, *longitude_{};
    QSpinBox *zone_{};
    QComboBox *hemisphere_{}, *datum_{}, *vertical_{};
    QLineEdit *source_{};
    QLabel *bannerText_{};
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
    void openGeographic();
    Document &doc_;
    Viewport &view_;
    QFrame *none_{}, *origin_{};
    QLabel *easting_{}, *northing_{}, *elevation_{}, *crs_{}, *source_{}, *sha256_{};
    QFrame *geoNone_{}, *geo_{};
    QLabel *geoLatitude_{}, *geoLongitude_{}, *geoZone_{}, *geoDatum_{}, *geoVertical_{},
        *geoConvergence_{}, *geoSource_{}, *geoWarning_{};
    QPushButton *reanchor_{}, *set_{}, *close_{}, *geoSet_{}, *geoChange_{}, *geoClear_{};
};
} // namespace sketchy
