#include "app/terrain_edits_section.hpp"
#include "app/accessible_names.hpp"
#include "app/terrain_actions.hpp"
#include "app/terrain_replay_job.hpp"
#include "app/theme.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include "core/document_limits.hpp"
#include "core/material_records.hpp"
#include "core/terrain_drape.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_sources.hpp"
#include "core/terrain_stamp.hpp"
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
namespace sketchy {
namespace {
enum Role { GlyphRole = Qt::UserRole + 1, TitleRole, DetailRole, IdRole, SkippedRole };
// Counts grouped by thousands ("4,096"), as the terrain reports write them.
QString count(std::size_t value) { return QString::fromStdString(groupedCount(value)); }
// The name a pad's source body goes by: its own, or the group or component holding it.
QString bodyLabel(const Document &doc, Id id) {
    for (Id at = id; at;) {
        const auto found = doc.bodies().find(at);
        if (found == doc.bodies().end())
            return {};
        const auto &body = *found->second;
        if (!body.name.empty() && (body.kind == BodyKind::Group || !body.parent))
            return QString::fromStdString(body.name);
        at = body.parent;
    }
    return {};
}
// The text as a screen reader should say it: "1:2" as "1 to 2", lengths in words, "·" a pause.
QString spoken(QString text) {
    static const QRegularExpression slope("(\\d+(?:[.,]\\d+)?):(\\d+(?:[.,]\\d+)?)");
    text.replace(slope, "\\1 to \\2");
    static const QRegularExpression metres("(\\d) m\\b");
    text.replace(metres, "\\1 metres");
    text.replace(" · ", ", ");
    text.replace("⚠ ", "");
    return text;
}
} // namespace

std::vector<TerrainEditRow> terrainEditRows(const Document &doc, Id terrain,
                                            const std::map<std::uint32_t, QString> &skips) {
    std::vector<TerrainEditRow> rows;
    if (!isTerrain(doc, terrain))
        return rows;
    const auto &record = *doc.bodies().at(terrain)->terrain;
    const auto world = doc.worldTransform(terrain);
    const auto units = doc.displayUnits();
    const int precision = doc.displayPrecision() == fullDisplayPrecision ? 2 : doc.displayPrecision();
    auto length = [&](double metres) { return displayLength(metres, units, precision); };
    auto height = [&](double local) { return length(world.point({0, 0, local}).z); };
    // Source edits name what they changed: one forward pass over the current sources.
    const bool sources = std::any_of(record.edits.begin(), record.edits.end(), [](const auto &e) {
        return e->kind == terrainEditSourceKind || e->kind == terrainRemoveSourceKind;
    });
    std::optional<TerrainSourceState> state;
    if (sources)
        state.emplace(record.sources);
    const auto n = record.edits.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto &entry = *record.edits[i];
        TerrainEditRow row;
        row.edit = entry.id;
        row.kind = entry.kind;
        const auto *data = entry.data.get();
        if (const auto *sculpt = dynamic_cast<const TerrainSculptData *>(data)) {
            const bool smooth = entry.kind == terrainSmoothKind;
            row.glyph = smooth ? "≈" : "⌒";
            row.title = smooth ? "Smooth · strength " + QLocale().toString(sculpt->strength, 'f', 2)
                               : QString("Sculpt · %1 %2")
                                     .arg(sculpt->amount < 0 ? "lower" : "raise",
                                          length(std::abs(sculpt->amount)));
            const QString falloff =
                QString::fromStdString(terrainFalloffName(sculpt->falloff)) + " falloff";
            const QString scope = sculpt->brush() ? "radius " + length(sculpt->radius)
                                                  : "region · falloff width " + length(sculpt->width);
            row.detail = smooth ? QString("%1 %2 · %3")
                                      .arg(sculpt->iterations)
                                      .arg(sculpt->iterations == 1 ? "pass" : "passes")
                                      .arg(scope)
                                : scope.left(1).toUpper() + scope.mid(1) + " · " + falloff;
        } else if (const auto *pad = dynamic_cast<const TerrainStampData *>(data)) {
            row.glyph = "▭";
            const auto name = pad->sourceBody ? bodyLabel(doc, pad->sourceBody) : QString();
            row.title = name.isEmpty() ? QString("Pad") : "Pad · " + name;
            row.detail = height(pad->elevation) + " · " +
                         (pad->mode == TerrainStampMode::Width
                              ? "fixed width " + length(pad->width)
                              : QString("daylight, cut %1, fill %2")
                                    .arg(terrainSlopeText(pad->cutSlope),
                                         terrainSlopeText(pad->fillSlope)));
            if (!pad->resolved()) {
                row.skipped = true;
                row.reason = "the pad no longer applies here";
            }
        } else if (const auto *drape = dynamic_cast<const TerrainDrapeData *>(data)) {
            row.glyph = "⤓";
            QString material;
            if (!drape->paint.empty())
                if (const auto found = doc.materials().find(drape->paint.front().material);
                    found != doc.materials().end())
                    material = QString::fromStdString(found->second->name);
            const auto lines = drape->lines.size();
            row.title = material.isEmpty()
                            ? QString("Drape · %1 %2").arg(count(lines)).arg(lines == 1 ? "line" : "lines")
                            : "Drape · " + material;
            row.detail = QString("Imprint · %1 %2").arg(count(lines)).arg(lines == 1 ? "line" : "lines");
            if (!drape->paint.empty())
                row.detail += QString(", %1 painted %2")
                                  .arg(count(drape->paint.size()))
                                  .arg(drape->paint.size() == 1 ? "region" : "regions");
            if (!drape->resolved()) {
                row.skipped = true;
                row.reason = "the drape no longer applies here";
            }
        } else if (const auto *source = dynamic_cast<const TerrainSourceEditData *>(data)) {
            row.glyph = "∿";
            auto roleTitle = [&](TerrainSourceRole role, const std::vector<Vec3> &points) {
                if (role == TerrainSourceRole::Contour && !points.empty())
                    return "Contour " + height(points.front().z);
                return role == TerrainSourceRole::Breakline ? QString("Breakline") : QString("Spot points");
            };
            const TerrainSource *before = state ? state->find(source->source) : nullptr;
            if (entry.kind == terrainAddSourceKind) {
                row.title = "Added " + roleTitle(source->role, source->points).toLower();
                if (source->role == TerrainSourceRole::Contour)
                    row.title = "Added contour " + height(source->points.front().z);
                row.detail = QString("%1 %2 · source %3")
                                 .arg(count(source->points.size()))
                                 .arg(source->points.size() == 1 ? "point" : "points")
                                 .arg(source->source);
            } else if (entry.kind == terrainRemoveSourceKind) {
                row.title = "Removed " + (before ? roleTitle(before->role, before->points).toLower()
                                                 : QString("source"));
                if (before && before->role == TerrainSourceRole::Contour)
                    row.title = "Removed contour " + height(before->points.front().z);
                row.detail = QString("source %1").arg(source->source);
            } else {
                row.title = roleTitle(source->role, source->points);
                QString what = "Edited";
                if (before) {
                    if (before->role != source->role)
                        what = source->role == TerrainSourceRole::Contour ? "Now a contour"
                                                                          : "Now a breakline";
                    else if (before->points.size() == source->points.size()) {
                        std::size_t moved = 0, lifted = 0;
                        for (std::size_t k = 0; k < source->points.size(); ++k) {
                            const auto &a = before->points[k], &b = source->points[k];
                            moved += a.x != b.x || a.y != b.y;
                            lifted += a.z != b.z;
                        }
                        if (moved)
                            what = QString("Reshaped · %1 %2 moved")
                                       .arg(count(moved))
                                       .arg(moved == 1 ? "vertex" : "vertices");
                        else if (lifted && !source->points.empty())
                            what = "Elevation from " + height(before->points.front().z);
                    } else {
                        const auto a = before->points.size(), b = source->points.size();
                        what = b > a ? QString("Reshaped · %1 %2 added")
                                           .arg(count(b - a))
                                           .arg(b - a == 1 ? "vertex" : "vertices")
                                     : QString("Reshaped · %1 %2 removed")
                                           .arg(count(a - b))
                                           .arg(a - b == 1 ? "vertex" : "vertices");
                    }
                }
                row.detail = what + QString(" · source %1").arg(source->source);
            }
        } else {
            row.glyph = "•";
            row.title = QString::fromStdString(entry.kind);
        }
        if (const auto found = skips.find(entry.id); found != skips.end()) {
            row.skipped = true;
            row.reason = found->second;
        }
        if (state)
            if (const auto *kind = findTerrainEditKind(entry.kind))
                applyTerrainSourceEffect(*state, *kind, entry);
        const QString position = QString("Change %1 of %2, ").arg(i + 1).arg(n);
        row.accessible = position + spoken(row.title + " · " + row.detail);
        if (row.skipped)
            row.accessible += ". Warning: skipped. " + row.reason;
        rows.push_back(std::move(row));
    }
    return rows;
}

namespace {
// Draws a row: glyph, title and ID on the first line; the values (or the skip) on the second.
class EditRowDelegate final : public QStyledItemDelegate {
  public:
    explicit EditRowDelegate(QListWidget *list) : QStyledItemDelegate(list), list_(list) {}
    void paint(QPainter *p, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        // The chrome's theme: the style sheet sets the text colour (ink), light on a dark theme.
        const auto colors = themeColors(option.palette.color(QPalette::Text).lightness() > 128);
        p->save();
        const QRect r = option.rect;
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        if (selected) {
            p->fillRect(r, colors.selected);
            p->fillRect(QRect(r.left(), r.top(), 3, r.height()), colors.selectionStroke);
        }
        if (option.state.testFlag(QStyle::State_HasFocus) && list_->hasFocus()) {
            p->setPen(QPen(colors.accent, 1, Qt::DotLine));
            p->drawRect(r.adjusted(1, 1, -2, -2));
        }
        p->setPen(QPen(colors.border, 1));
        p->drawLine(r.bottomLeft(), r.bottomRight());
        const auto metrics = option.fontMetrics;
        const int pad = 8, glyph = metrics.horizontalAdvance("⌒") + 10;
        const auto id = "#" + index.data(IdRole).toString();
        const int idWidth = metrics.horizontalAdvance(id) + 6;
        QRect line(r.left() + pad, r.top() + 4, r.width() - 2 * pad, metrics.lineSpacing());
        p->setPen(colors.muted);
        p->drawText(QRect(line.left(), line.top(), glyph, line.height()), Qt::AlignLeft,
                    index.data(GlyphRole).toString());
        p->drawText(QRect(line.right() - idWidth, line.top(), idWidth, line.height()),
                    Qt::AlignRight, id);
        p->setPen(colors.ink);
        p->drawText(QRect(line.left() + glyph, line.top(), line.width() - glyph - idWidth,
                          line.height()),
                    Qt::AlignLeft, metrics.elidedText(index.data(TitleRole).toString(),
                                                      Qt::ElideRight, line.width() - glyph - idWidth));
        auto small = option.font;
        if (small.pointSizeF() > 0)
            small.setPointSizeF(small.pointSizeF() * 0.9);
        else
            small.setPixelSize(std::max(8, int(small.pixelSize() * 0.9)));
        const bool skipped = index.data(SkippedRole).toBool();
        if (skipped)
            small.setBold(true);
        p->setFont(small);
        p->setPen(skipped ? colors.warning : colors.muted);
        const QRect detail(line.left() + glyph, line.bottom() + 2, line.width() - glyph,
                           r.bottom() - line.bottom() - 4);
        p->drawText(detail, Qt::AlignLeft | Qt::TextWordWrap, index.data(DetailRole).toString());
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const auto metrics = option.fontMetrics;
        auto small = option.font;
        if (small.pointSizeF() > 0)
            small.setPointSizeF(small.pointSizeF() * 0.9);
        else
            small.setPixelSize(std::max(8, int(small.pixelSize() * 0.9)));
        small.setBold(index.data(SkippedRole).toBool());
        const QFontMetrics smallMetrics(small);
        const int width = std::max(120, list_->viewport()->width()) - 16 -
                          (metrics.horizontalAdvance("⌒") + 10);
        const auto detail = smallMetrics.boundingRect(QRect(0, 0, width, 1000),
                                                      Qt::AlignLeft | Qt::TextWordWrap,
                                                      index.data(DetailRole).toString());
        return {width, metrics.lineSpacing() + detail.height() + 12};
    }

  private:
    QListWidget *list_;
};
} // namespace

TerrainEditsSection::TerrainEditsSection(Document &doc, Viewport &view, QWidget *parent)
    : CollapsibleSection("Terrain edits", new QWidget, true, parent), doc_(doc), view_(view) {
    setObjectName("terrainEditsSection");
    auto *layout = new QVBoxLayout(content());
    layout->setContentsMargins(8, 4, 2, 8);
    layout->setSpacing(6);
    list_ = new QListWidget;
    list_->setObjectName("terrainEditsList");
    list_->setAccessibleName("Terrain edits, in the order they are applied");
    list_->setItemDelegate(new EditRowDelegate(list_));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setUniformItemSizes(false);
    list_->setResizeMode(QListView::Adjust);
    list_->installEventFilter(this);
    layout->addWidget(list_);
    caption_ = new QLabel;
    caption_->setObjectName("terrainEditsCaption");
    caption_->setWordWrap(true);
    layout->addWidget(caption_);
    auto button = [&](const QString &text, const QString &name) {
        auto *b = new QPushButton(text);
        b->setObjectName(name);
        b->setAutoDefault(false);
        return b;
    };
    editPad_ = button("Edit pad…", "terrainEditPad");
    restamp_ = button("Restamp", "terrainRestamp");
    remove_ = button("Remove", "terrainRemoveEdit");
    reset_ = button("Reset edits…", "terrainEditsReset");
    auto *first = new QHBoxLayout;
    first->addWidget(editPad_);
    first->addWidget(restamp_);
    first->addStretch();
    auto *second = new QHBoxLayout;
    second->addWidget(remove_);
    second->addStretch();
    second->addWidget(reset_);
    layout->addLayout(first);
    layout->addLayout(second);
    notice_ = new QFrame;
    notice_->setObjectName("terrainEditsNotice");
    auto *noticeRow = new QVBoxLayout(notice_);
    noticeRow->setContentsMargins(8, 6, 8, 6);
    noticeRow->setSpacing(2);
    noticeText_ = new QLabel;
    noticeText_->setObjectName("terrainEditsNoticeText");
    noticeText_->setWordWrap(true);
    noticeText_->setTextFormat(Qt::RichText);
    noticeRow->addWidget(noticeText_);
    showReport_ = button("Show report", "terrainEditsShowReport");
    showReport_->setFlat(true);
    showReport_->setAccessibleName("Show the report of the last terrain change");
    noticeRow->addWidget(showReport_, 0, Qt::AlignLeft);
    layout->addWidget(notice_);
    notice_->hide();
    nameInternalEditors(this);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { selectionChanged(true); });
    list_->viewport()->installEventFilter(this);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { frameSelected(); });
    connect(remove_, &QPushButton::clicked, this, [this] { removeSelected(); });
    connect(restamp_, &QPushButton::clicked, this, [this] { restampSelected(); });
    connect(reset_, &QPushButton::clicked, this, [this] { resetEdits(); });
    connect(editPad_, &QPushButton::clicked, this, [this] {
        const auto edit = selectedEdit();
        if (!edit || !isTerrain(doc_, terrain_))
            return;
        try {
            TerrainEditPadDialog dialog(doc_, terrain_, edit, window());
            if (dialog.exec() != QDialog::Accepted || !isTerrain(doc_, terrain_))
                return;
            const auto command = dialog.command();
            if (!command.isEmpty())
                startReplay(command, "Edit terrain change");
        } catch (const std::exception &error) {
            emit view_.message(QString("Edit pad: %1").arg(QString::fromUtf8(error.what())));
        }
    });
    connect(showReport_, &QPushButton::clicked, this, [this] {
        const auto report = view_.lastTerrainReport(terrain_);
        if (report.isEmpty())
            return;
        auto *dialog = new TerrainReportDialog(view_.lastTerrainReportTitle(terrain_),
                                               view_.lastTerrainReportTitle(terrain_) + " · notices",
                                               false, report, window());
        dialog->setObjectName("terrainEditsReportDialog");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    });
    hide();
}
std::uint32_t TerrainEditsSection::selectedEdit() const {
    const auto *item = list_->currentItem();
    return item && item->isSelected() ? std::uint32_t(item->data(IdRole).toULongLong()) : 0;
}
void TerrainEditsSection::hideEvent(QHideEvent *event) {
    CollapsibleSection::hideEvent(event);
    view_.setTerrainEditHighlight(0, 0, {}); // the outline goes with the section
}
// The list fits its rows up to about eight of them, then scrolls; it follows the panel's width.
void TerrainEditsSection::fitList() {
    int height = 2 * list_->frameWidth();
    for (int i = 0; i < list_->count() && i < 8; ++i)
        height += list_->sizeHintForRow(i);
    list_->setFixedHeight(std::max(height, 24));
}
bool TerrainEditsSection::eventFilter(QObject *object, QEvent *event) {
    if (object == list_->viewport() && event->type() == QEvent::Resize) {
        list_->doItemsLayout();
        fitList();
    }
    if (object == list_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Delete && selectedEdit()) {
            removeSelected();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && selectedEdit()) {
            frameSelected();
            return true;
        }
    }
    return CollapsibleSection::eventFilter(object, event);
}
void TerrainEditsSection::refresh() {
    const auto terrain = selectedTerrain(doc_, view_.selectionState());
    if (!terrain) {
        if (terrain_)
            view_.setTerrainEditHighlight(0, 0, {});
        terrain_ = 0;
        cacheKey_.clear();
        rows_.clear();
        hide();
        return;
    }
    // Rebuilt when the terrain's body (its record) changes, not on every document change; the
    // buttons follow the rest (a source body deleted, a lock).
    const auto key = QString::fromStdString(doc_.identity()) + "/" +
                     QString::number(quintptr(doc_.bodies().at(terrain).get())) + "/" +
                     QString::number(terrain) + "/" + QString::number(int(doc_.displayUnits())) +
                     "/" + QString::number(doc_.displayPrecision()) + "/" +
                     QString::number(view_.terrainReportGeneration());
    show();
    if (key == cacheKey_ && terrain == terrain_) {
        updateButtons();
        return;
    }
    const auto keep = terrain == terrain_ ? selectedEdit() : 0;
    if (terrain != terrain_)
        view_.setTerrainEditHighlight(0, 0, {});
    terrain_ = terrain;
    cacheKey_ = key;
    rows_ = terrainEditRows(doc_, terrain, view_.terrainSkips(terrain));
    filling_ = true;
    list_->clear();
    int current = -1;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto &row = rows_[i];
        auto *item = new QListWidgetItem(row.title);
        item->setData(GlyphRole, row.glyph);
        item->setData(TitleRole, row.title);
        item->setData(DetailRole, row.skipped ? "⚠ Skipped: " + row.reason : row.detail);
        item->setData(IdRole, qulonglong(row.edit));
        item->setData(SkippedRole, row.skipped);
        item->setData(Qt::AccessibleTextRole, row.accessible);
        item->setData(Qt::AccessibleDescriptionRole,
                      row.skipped ? "Warning: skipped. " + row.reason : row.detail);
        item->setToolTip(row.skipped ? row.reason : row.detail);
        list_->addItem(item);
        if (row.edit == keep)
            current = int(i);
    }
    fitList();
    list_->setVisible(!rows_.empty());
    if (current >= 0)
        list_->setCurrentRow(current);
    filling_ = false;
    const auto n = rows_.size();
    setDetail(n ? QString("%1 %2").arg(count(n)).arg(n == 1 ? "change" : "changes") : QString());
    caption_->setText(n ? QString("Applied top to bottom · %1 of %2 changes")
                              .arg(count(n), count(TerrainLimits::edits))
                        : QString("No changes yet. Sculpt, Edit contours, Stamp pad and Drape add "
                                  "them."));
    const bool locked = view_.selectionState().locked(doc_, terrain);
    reset_->setEnabled(n && !locked);
    // Notices of the last change or replay (UX §15.6).
    const auto report = view_.lastTerrainReport(terrain);
    QStringList notices;
    for (const auto &value : report["terrain"].toObject()["notices"].toArray()) {
        const auto notice = value.toObject();
        const auto kind = notice["kind"].toString();
        const auto many = std::size_t(notice["count"].toDouble());
        if (kind == "edit-skipped")
            notices << QString("%1 %2 skipped").arg(count(many)).arg(many == 1 ? "change" : "changes");
        else if (kind == "face-paint-dropped")
            notices << QString("paint removed from %1 %2 of rebuilt tiles (use Drape with Paint "
                               "regions to keep paint)")
                           .arg(count(many))
                           .arg(many == 1 ? "face" : "faces");
        else if (kind == "sources-clipped")
            notices << QString("%1 source %2 clipped").arg(count(many)).arg(many == 1 ? "vertex" : "vertices");
        else if (kind == "drape-clipped")
            notices << QString("%1 %2 outside the terrain clipped").arg(count(many)).arg(many == 1 ? "part" : "parts");
        else
            notices << notice["message"].toString();
    }
    notice_->setVisible(!notices.isEmpty());
    if (!notices.isEmpty()) {
        const auto title = view_.lastTerrainReportTitle(terrain);
        const bool replay = title.startsWith("Remove") || title.startsWith("Edit terrain") ||
                            title.startsWith("Reset") || title.startsWith("Restamp");
        const auto text = QString("<b>⚠ Last %1</b> (%2): %3.")
                              .arg(replay ? "replay" : "change", title.toHtmlEscaped(),
                                   notices.join("; ").toHtmlEscaped());
        noticeText_->setText(text);
        noticeText_->setAccessibleName(QString("Warning: last %1, %2: %3")
                                           .arg(replay ? "replay" : "change", title,
                                                notices.join("; ")));
    }
    selectionChanged(false);
}
void TerrainEditsSection::updateButtons() {
    const auto edit = selectedEdit();
    const TerrainEditRow *row = nullptr;
    for (const auto &candidate : rows_)
        if (candidate.edit == edit)
            row = &candidate;
    const bool locked = !terrain_ || view_.selectionState().locked(doc_, terrain_);
    const TerrainStampData *pad = nullptr;
    if (row && isTerrain(doc_, terrain_))
        for (const auto &entry : doc_.bodies().at(terrain_)->terrain->edits)
            if (entry->id == edit)
                pad = dynamic_cast<const TerrainStampData *>(entry->data.get());
    remove_->setEnabled(row && !locked);
    editPad_->setEnabled(pad && !locked);
    QString why;
    if (!pad)
        why = "Restamp re-reads a pad's footprint from its source body; select a pad";
    else if (!pad->sourceBody)
        why = "This pad was stamped from edges or a polygon; it has no source body to restamp";
    else if (!doc_.bodies().contains(pad->sourceBody))
        why = "The pad's source body no longer exists";
    else if (locked)
        why = "The terrain is locked; unlock it to change its edits";
    restamp_->setEnabled(why.isEmpty());
    restamp_->setToolTip(why.isEmpty() ? QString("Re-read the footprint from its source body")
                                       : why);
    restamp_->setAccessibleDescription(restamp_->toolTip());
    editPad_->setToolTip(pad ? QString("Change the pad's elevation, grading and slopes")
                             : QString("Edit pad… changes a pad; select a pad"));
    reset_->setEnabled(!rows_.empty() && !locked);
}
void TerrainEditsSection::selectionChanged(bool announce) {
    if (filling_)
        return;
    updateButtons();
    const auto edit = selectedEdit();
    const TerrainEditRow *row = nullptr;
    std::size_t index = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].edit == edit) {
            row = &rows_[i];
            index = i;
        }
    if (!row) {
        view_.setTerrainEditHighlight(0, 0, {});
        return;
    }
    view_.setTerrainEditHighlight(terrain_, edit,
                                  {QString("Change %1 · %2").arg(edit).arg(row->title), row->detail});
    const auto after = rows_.size() - index - 1;
    if (announce) // the user chose it (a refresh says nothing)
        emit view_.message(QString("Change %1 selected · Double-click frames it · Delete removes it and "
                               "replays the %2 %3 after it")
                           .arg(edit)
                           .arg(after)
                           .arg(after == 1 ? "change" : "changes"));
}
void TerrainEditsSection::startReplay(const QJsonObject &command, const QString &title) {
    try {
        TerrainReplayJob::start(window(), doc_, view_, command, title);
    } catch (const std::exception &error) {
        emit view_.message(QString("%1 refused; nothing changed: %2")
                               .arg(title, QString::fromUtf8(error.what())));
    }
}
void TerrainEditsSection::removeSelected() {
    const auto edit = selectedEdit();
    if (!edit || !terrain_)
        return;
    startReplay({{"command", "terrain.remove_edit"},
                 {"terrain", QString::number(terrain_)},
                 {"edit", double(edit)}},
                QString("Remove change %1").arg(edit));
}
void TerrainEditsSection::restampSelected() {
    const auto edit = selectedEdit();
    if (!edit || !terrain_ || !restamp_->isEnabled())
        return;
    startReplay({{"command", "terrain.update_edit"},
                 {"terrain", QString::number(terrain_)},
                 {"edit", double(edit)},
                 {"restamp", true}},
                QString("Restamp change %1").arg(edit));
}
void TerrainEditsSection::resetEdits() {
    QTimer::singleShot(0, this, [this] {
        try {
            if (auto *action = window()->findChild<QAction *>("terrain.resetEdits");
                action && action->isEnabled())
                action->trigger();
            else if (terrain_ && isTerrain(doc_, terrain_))
                resetTerrainEditsInteractive(window(), doc_, view_, terrain_);
        } catch (const std::exception &error) {
            emit view_.message(QString("Reset edits: %1").arg(QString::fromUtf8(error.what())));
        }
    });
}
void TerrainEditsSection::frameSelected() {
    if (const auto edit = selectedEdit(); edit && terrain_)
        view_.frameTerrainEdit(terrain_, edit);
}

// ---- Edit pad… -----------------------------------------------------------------------------------
TerrainEditPadDialog::TerrainEditPadDialog(const Document &doc, Id terrain, std::uint32_t edit,
                                           QWidget *parent)
    : QDialog(parent), doc_(doc), terrain_(terrain), edit_(edit) {
    setObjectName("terrainEditPadDialog");
    setWindowTitle("Edit pad");
    const TerrainStampData *pad = nullptr;
    for (const auto &entry : doc.bodies().at(terrain)->terrain->edits)
        if (entry->id == edit)
            pad = dynamic_cast<const TerrainStampData *>(entry->data.get());
    if (!pad)
        throw std::runtime_error("Edit pad… changes a pad; this change is not one");
    const auto world = doc.worldTransform(terrain);
    elevationModel_ = world.point({0, 0, pad->elevation}).z;
    if (pad->sourceBody && doc.bodies().contains(pad->sourceBody)) {
        QJsonObject source{{"body", QString::number(pad->sourceBody)}};
        if (pad->sourceFace)
            source["face"] = QString::number(pad->sourceFace);
        try {
            footprintZ_ = terrainPadFootprint(doc, source).z;
        } catch (const std::exception &) {
            try {
                footprintZ_ = terrainPadFootprint(doc, QJsonObject{{"body", QString::number(pad->sourceBody)}}).z;
            } catch (const std::exception &) {
            }
        }
    }
    const auto units = doc.displayUnits();
    precision_ = doc.displayPrecision() == fullDisplayPrecision ? 2 : doc.displayPrecision();
    auto length = [&](double v) { return displayLength(v, units, precision_); };
    auto *form = new QFormLayout(this);
    elevation_ = new QLineEdit(length(elevationModel_));
    elevation_->setObjectName("terrainPadElevation");
    elevation_->setAccessibleName("Pad elevation");
    form->addRow("Elevation", elevation_);
    offset_ = new QLineEdit(footprintZ_ ? length(*footprintZ_ - elevationModel_) : QString());
    offset_->setObjectName("terrainPadOffsetField");
    offset_->setAccessibleName("Pad offset below the footprint");
    offset_->setEnabled(footprintZ_.has_value());
    offset_->setToolTip(footprintZ_ ? QString("The pad's depth below its source footprint")
                                    : QString("The footprint has no source body with a height"));
    form->addRow("Pad offset", offset_);
    mode_ = new QComboBox;
    mode_->setObjectName("terrainPadGrading");
    mode_->setAccessibleName("Grading");
    mode_->addItem("Daylight", "daylight");
    mode_->addItem("Fixed width", "width");
    mode_->setCurrentIndex(pad->mode == TerrainStampMode::Width ? 1 : 0);
    form->addRow("Grading", mode_);
    cut_ = new QLineEdit(terrainSlopeText(pad->mode == TerrainStampMode::Daylight ? pad->cutSlope : 0.5));
    cut_->setObjectName("terrainPadCut");
    cut_->setAccessibleName("Cut slope");
    fill_ = new QLineEdit(terrainSlopeText(pad->mode == TerrainStampMode::Daylight ? pad->fillSlope : 0.5));
    fill_->setObjectName("terrainPadFill");
    fill_->setAccessibleName("Fill slope");
    distance_ = new QLineEdit(length(pad->mode == TerrainStampMode::Daylight ? pad->maxDistance : 100));
    distance_->setObjectName("terrainPadDistance");
    distance_->setAccessibleName("Maximum daylight distance");
    width_ = new QLineEdit(length(pad->mode == TerrainStampMode::Width ? pad->width : 3));
    width_->setObjectName("terrainPadWidthField");
    width_->setAccessibleName("Grading width");
    form->addRow("Cut", cut_);
    form->addRow("Fill", fill_);
    form->addRow("Max distance", distance_);
    form->addRow("Width", width_);
    status_ = new QLabel;
    status_->setObjectName("terrainEditPadStatus");
    status_->setWordWrap(true);
    form->addRow(status_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText("Apply");
    ok_->setObjectName("terrainEditPadApply");
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("terrainEditPadCancel");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (auto *edit : {elevation_, offset_, cut_, fill_, distance_, width_}) {
        initial_[edit] = edit->text();
        connect(edit, &QLineEdit::textEdited, this, [this, edit] {
            userEdited_.insert(edit);
            // Elevation and offset are one value: the last one typed is the one that counts.
            if (edit == elevation_)
                userEdited_.erase(offset_);
            if (edit == offset_)
                userEdited_.erase(elevation_);
            changed(edit);
        });
    }
    initialMode_ = mode_->currentIndex();
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { changed(nullptr); });
    changed(nullptr);
}
void TerrainEditPadDialog::changed(QLineEdit *source) {
    const auto unit = inputUnit(doc_.displayUnits());
    const auto locale = QLocale();
    const auto units = doc_.displayUnits();
    // Elevation and offset follow each other (elevation = footprint height - offset).
    try {
        if (source == offset_ && footprintZ_)
            elevation_->setText(displayLength(
                *footprintZ_ - parseLength(offset_->text(), unit, locale), units, precision_));
        else if (source == elevation_ && footprintZ_)
            offset_->setText(displayLength(
                *footprintZ_ - parseLength(elevation_->text(), unit, locale), units, precision_));
    } catch (const std::exception &) {
    }
    const bool daylight = mode_->currentData() == "daylight";
    for (auto *edit : {cut_, fill_, distance_})
        edit->setEnabled(daylight);
    width_->setEnabled(!daylight);
    problem_.clear();
    try {
        (void)command();
    } catch (const std::exception &error) {
        problem_ = QString::fromUtf8(error.what());
    }
    status_->setText(problem_);
    ok_->setEnabled(problem_.isEmpty() && !command().isEmpty());
}
QJsonObject TerrainEditPadDialog::command() const {
    const auto unit = inputUnit(doc_.displayUnits());
    const auto locale = QLocale();
    const double elevation = parseLength(elevation_->text(), unit, locale);
    QJsonObject command{{"command", "terrain.update_edit"},
                        {"terrain", QString::number(terrain_)},
                        {"edit", double(edit_)}};
    const TerrainStampData *pad = nullptr;
    for (const auto &entry : doc_.bodies().at(terrain_)->terrain->edits)
        if (entry->id == edit_)
            pad = dynamic_cast<const TerrainStampData *>(entry->data.get());
    if (!pad)
        return {};
    // Only what the user changed goes in the command: an untouched field keeps its exact stored
    // value (the shown text is rounded to the display precision).
    auto edited = [&](QLineEdit *field) {
        const auto found = initial_.find(field);
        return userEdited_.contains(field) && found != initial_.end() &&
               found->second != field->text();
    };
    bool changed = false;
    if (edited(offset_) && !edited(elevation_) && footprintZ_) {
        command["elevation"] = *footprintZ_ - parseLength(offset_->text(), unit, locale);
        changed = true;
    } else if (edited(elevation_)) {
        command["elevation"] = elevation;
        changed = true;
    }
    const bool modeChanged = mode_->currentIndex() != initialMode_;
    if (mode_->currentData() == "daylight") {
        const bool was = pad->mode == TerrainStampMode::Daylight;
        const double cut = edited(cut_) || !was ? parseTerrainSlope(cut_->text()) : pad->cutSlope;
        const double fill = edited(fill_) || !was ? parseTerrainSlope(fill_->text()) : pad->fillSlope;
        const double distance = edited(distance_) || !was ? parseLength(distance_->text(), unit, locale)
                                                          : pad->maxDistance;
        if (!(distance > 0) || distance > terrainStampDistanceMax)
            throw std::runtime_error("The maximum distance is above 0 and at most 1,000 m");
        if (modeChanged || edited(cut_) || edited(fill_) || edited(distance_)) {
            command["mode"] = "daylight";
            command["cutSlope"] = cut;
            command["fillSlope"] = fill;
            command["maxDistance"] = distance;
            changed = true;
        }
    } else {
        const bool was = pad->mode == TerrainStampMode::Width;
        const double width = edited(width_) || !was ? parseLength(width_->text(), unit, locale)
                                                    : pad->width;
        if (width < terrainStampWidthMin || width > terrainStampDistanceMax)
            throw std::runtime_error("The grading width is 0.01 to 1,000 m");
        if (modeChanged || edited(width_)) {
            command["mode"] = "width";
            command["width"] = width;
            changed = true;
        }
    }
    return changed ? command : QJsonObject{};
}
} // namespace sketchy
